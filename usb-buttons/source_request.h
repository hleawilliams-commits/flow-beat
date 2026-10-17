/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded native SOURCE request. UI owns init/request/tick/end; the mixer bus
 * owns pulse/knob state. Call only from the named threads. All shared lifecycle
 * state is atomic, and requested_at is immutable until the bus acknowledges end.
 *
 * ARRIVED means SOURCE is actually visible after our pulse. It does not end the
 * request: native navigation/LOAD/BACK/knob input still cancels it until end().
 * The caller must re-check cancellation before any later native activation.
 * end() requests retirement; the next mixer frame resets pulse state and opens
 * the gate for another request. No UI callback writes mixer-only phase fields.
 */
#ifndef USB_BUTTONS_SOURCE_REQUEST_H
#define USB_BUTTONS_SOURCE_REQUEST_H

#include <time.h>
#include "azmod-mod.h"

enum usb_source_status {
    USB_SOURCE_IDLE,
    USB_SOURCE_PENDING,
    USB_SOURCE_SENT,
    USB_SOURCE_CANCELLED,
    USB_SOURCE_ARRIVED
};
enum { USB_SOURCE_RETIRING = 5 };

static const az_core_v1 *usb_source_core;
static int usb_source_ready;
/* Atomic lifecycle gate; only the bus publishes IDLE after retirement. */
static int usb_source_state;
static int usb_source_seen;
/* UI writes before publishing PENDING; never changes until a new IDLE request. */
static long usb_source_requested_at;
/* UI-only debounce. */
static long usb_source_last_request;
static int usb_source_had_request;
/* Mixer-thread-only fields. phase: 0 unstarted, 1 pulse down, 2 released. */
static int usb_source_phase;
static long usb_source_pressed_at;
static int usb_source_knob_known;
static unsigned char usb_source_knob;

static long usb_source_clock_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return -1;
    return (long)t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static int usb_source_live_state(int state)
{
    return state == USB_SOURCE_PENDING || state == USB_SOURCE_SENT;
}

static int usb_source_expired(long now)
{
    return now < usb_source_requested_at || now - usb_source_requested_at >= 1000;
}

/* Either thread may cancel, but never overwrites retirement or a later IDLE. */
static void usb_source_cancel(void)
{
    int state = __atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE);
    while (usb_source_live_state(state) &&
           !__atomic_compare_exchange_n(&usb_source_state, &state, USB_SOURCE_CANCELLED,
                                        0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
}

static int usb_source_cancelled(void)
{
    return __atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE) == USB_SOURCE_CANCELLED;
}

static void usb_source_end(void)
{
    int state = __atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE);
    while (state != USB_SOURCE_IDLE && state != USB_SOURCE_RETIRING &&
           !__atomic_compare_exchange_n(&usb_source_state, &state, USB_SOURCE_RETIRING,
                                        0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
}

static int usb_source_request(long now)
{
    if (!usb_source_ready || now < 0 || usb_source_core->view() != AZ_VIEW_DECK ||
        __atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE) != USB_SOURCE_IDLE) return 0;
    if (usb_source_had_request &&
        (now < usb_source_last_request || now - usb_source_last_request < 500)) return 0;
    usb_source_last_request = now;
    usb_source_had_request = 1;
    usb_source_requested_at = now;
    __atomic_store_n(&usb_source_seen, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&usb_source_state, USB_SOURCE_PENDING, __ATOMIC_RELEASE);
    return 1;
}

static int usb_source_frame_at(int bus, unsigned char *f, int len, long now)
{
    if (bus != AZ_BUS_MIXER_IN || !f || len < 98) return AZ_FRAME_OK;
    int knob_moved = usb_source_knob_known && usb_source_knob != f[34];
    usb_source_knob = f[34];
    usb_source_knob_known = 1;
    int state = __atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE);
    if (state == USB_SOURCE_RETIRING) {
        usb_source_phase = 0;
        /* Final operation before returning: a new UI request may now publish. */
        __atomic_store_n(&usb_source_state, USB_SOURCE_IDLE, __ATOMIC_RELEASE);
        return AZ_FRAME_OK;
    }
    if (!usb_source_live_state(state)) {
        if (state == USB_SOURCE_CANCELLED) usb_source_phase = 0;
        return AZ_FRAME_OK;
    }
    /* Pass genuine controls through byte-for-byte, even after our pulse ends. */
    if (f[32] || f[33] || (f[31] & 1) || knob_moved || usb_source_expired(now)) {
        usb_source_cancel();
        usb_source_phase = 0;
        return AZ_FRAME_OK;
    }
    int view = usb_source_core->view();
    if (view == AZ_VIEW_SOURCE && state == USB_SOURCE_SENT) {
        __atomic_store_n(&usb_source_seen, 1, __ATOMIC_RELEASE);
        usb_source_phase = 2;
        return AZ_FRAME_OK;
    }
    if (view != AZ_VIEW_DECK || __atomic_load_n(&usb_source_seen, __ATOMIC_ACQUIRE)) {
        usb_source_cancel();
        usb_source_phase = 0;
        return AZ_FRAME_OK;
    }
    if (usb_source_phase == 2) return AZ_FRAME_OK;
    if (!usb_source_phase) {
        usb_source_phase = 1;
        usb_source_pressed_at = now;
    }
    if (now < usb_source_pressed_at || now - usb_source_pressed_at >= 200) {
        usb_source_phase = 2;
        return AZ_FRAME_OK;
    }
    /* CAS cannot resurrect a request retired/cancelled by the UI meanwhile. */
    state = USB_SOURCE_PENDING;
    if (!__atomic_compare_exchange_n(&usb_source_state, &state, USB_SOURCE_SENT,
                                     0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) &&
        state != USB_SOURCE_SENT) return AZ_FRAME_OK;
    if (__atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE) != USB_SOURCE_SENT)
        return AZ_FRAME_OK;
    f[32] |= 0x80;                /* Native SOURCE only. */
    f[6] &= (unsigned char)~0x08; /* Native panel press latch. */
    return AZ_FRAME_OK;
}

static int usb_source_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    return usb_source_frame_at(bus, f, len, usb_source_clock_ms());
}

static enum usb_source_status usb_source_tick(long now)
{
    if (!usb_source_ready) return USB_SOURCE_IDLE;
    int state = __atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE);
    if (state == USB_SOURCE_IDLE || state == USB_SOURCE_RETIRING) return USB_SOURCE_IDLE;
    if (state == USB_SOURCE_CANCELLED) return USB_SOURCE_CANCELLED;
    if (usb_source_expired(now)) {
        usb_source_cancel();
        return USB_SOURCE_CANCELLED;
    }
    int view = usb_source_core->view();
    if (view == AZ_VIEW_SOURCE && state == USB_SOURCE_SENT) {
        __atomic_store_n(&usb_source_seen, 1, __ATOMIC_RELEASE);
        /* Arrival does not clear the gate or stop physical-input monitoring. */
        return usb_source_cancelled() ? USB_SOURCE_CANCELLED : USB_SOURCE_ARRIVED;
    }
    if (view != AZ_VIEW_DECK || __atomic_load_n(&usb_source_seen, __ATOMIC_ACQUIRE)) {
        usb_source_cancel();
        return USB_SOURCE_CANCELLED;
    }
    return (enum usb_source_status)__atomic_load_n(&usb_source_state, __ATOMIC_ACQUIRE);
}

static int usb_source_init(const az_core_v1 *core, const char *id)
{
    if (usb_source_ready) return 1;
    if (!core || !id || !id[0] || !AZ_HAS(core, frame_filter) || !AZ_HAS(core, view)) return 0;
    usb_source_core = core;
    if (core->frame_filter(id, 1u << AZ_BUS_MIXER_IN, usb_source_frame, NULL) != 0) return 0;
    usb_source_ready = 1;
    return 1;
}
#endif
