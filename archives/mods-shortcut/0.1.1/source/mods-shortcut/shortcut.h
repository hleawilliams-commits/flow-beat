/* SPDX-License-Identifier: GPL-3.0-only
 * SDK-only route to the official Mod Menu: SOURCE, then the DJ taps MOD MENU.
 * The SDK exposes no direct Mod Menu-open operation. This helper does not guess
 * source-row positions, inject pointers, turn the knob, or call core internals.
 *
 * The parent owns the button surface. From its UI thread: init once, request only
 * for AZ_EV_TAP (not also AZ_EV_DOWN), tick each UI tick. The core emits one TAP
 * per contact press. A request cannot repeat while pending or inside its debounce.
 * The frame filter touches only the SOURCE bit and the native panel press latch.
 */
#ifndef GROUPED_PANELS_SHORTCUT_H
#define GROUPED_PANELS_SHORTCUT_H

#include <time.h>
#include "azmod-mod.h"

static const az_core_v1 *gp_shortcut_core;
static const char *gp_shortcut_id;
static int gp_shortcut_ready;

/* UI is the sole request producer; the mixer bus is the sole consumer. The
 * timestamp is published before pending and stays immutable until consumed. */
static int gp_shortcut_pending;
static long gp_shortcut_requested_at;
static int gp_shortcut_sent;

/* UI-only feedback/debounce state. */
static int gp_shortcut_notice;
static long gp_shortcut_notice_until;
static long gp_shortcut_last_request = -10000;

/* Mixer-thread-only pulse and native knob state. */
static int gp_shortcut_phase;
static long gp_shortcut_until;
static int gp_shortcut_knob_known;
static unsigned char gp_shortcut_knob;

static long gp_shortcut_clock_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return -1;
    return (long)t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void gp_shortcut_finish(int delivered)
{
    gp_shortcut_phase = 0;
    __atomic_store_n(&gp_shortcut_sent, delivered, __ATOMIC_RELEASE);
    __atomic_store_n(&gp_shortcut_pending, 0, __ATOMIC_RELEASE);
}

static int gp_shortcut_request(long now)
{
    if (!gp_shortcut_ready || now < 0 ||
        gp_shortcut_core->view() != AZ_VIEW_DECK ||
        __atomic_load_n(&gp_shortcut_pending, __ATOMIC_ACQUIRE)) return 0;
    /* Also suppress a repeated tap immediately after cancellation/release. */
    if (now >= gp_shortcut_last_request && now - gp_shortcut_last_request < 500) return 0;
    gp_shortcut_last_request = now;
    gp_shortcut_requested_at = now;
    gp_shortcut_notice_until = now + 1500;
    gp_shortcut_notice = 1;
    __atomic_store_n(&gp_shortcut_sent, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gp_shortcut_pending, 1, __ATOMIC_RELEASE);
    return 1;
}

static int gp_shortcut_frame_at(int bus, unsigned char *f, int len, long now)
{
    if (bus != AZ_BUS_MIXER_IN || !f || len < 98) return AZ_FRAME_OK;
    int knob_moved = gp_shortcut_knob_known && gp_shortcut_knob != f[34];
    gp_shortcut_knob = f[34];
    gp_shortcut_knob_known = 1;
    if (!__atomic_load_n(&gp_shortcut_pending, __ATOMIC_ACQUIRE)) return AZ_FRAME_OK;

    /* Native screen/LOAD/BACK/knob input always wins, unchanged. Stop injecting
     * immediately; the real incoming frame supplies the synthetic button release. */
    if (f[32] || f[33] || (f[31] & 1) || knob_moved ||
        now < gp_shortcut_requested_at || now - gp_shortcut_requested_at > 1000) {
        gp_shortcut_finish(0);
        return AZ_FRAME_OK;
    }
    int view = gp_shortcut_core->view();
    if (view != AZ_VIEW_DECK) {
        gp_shortcut_finish(gp_shortcut_phase && view == AZ_VIEW_SOURCE);
        return AZ_FRAME_OK;
    }
    if (!gp_shortcut_phase) {
        gp_shortcut_phase = 1;
        gp_shortcut_until = now + 200;
    }
    if (now >= gp_shortcut_until) {
        gp_shortcut_finish(1);
        return AZ_FRAME_OK;
    }
    f[32] |= 0x80;                 /* Native SOURCE; never LOAD, PLAY or MENU. */
    f[6] &= (unsigned char)~0x08;  /* Same panel press latch as a physical press. */
    __atomic_store_n(&gp_shortcut_sent, 1, __ATOMIC_RELEASE);
    return AZ_FRAME_OK;
}

static int gp_shortcut_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    return gp_shortcut_frame_at(bus, f, len, gp_shortcut_clock_ms());
}

static void gp_shortcut_tick_at(long now)
{
    if (!gp_shortcut_ready || !gp_shortcut_notice) return;
    if (now < gp_shortcut_requested_at || now > gp_shortcut_notice_until) {
        gp_shortcut_notice = 0;
        return;
    }
    if (__atomic_load_n(&gp_shortcut_sent, __ATOMIC_ACQUIRE) &&
        gp_shortcut_core->view() == AZ_VIEW_SOURCE) {
        gp_shortcut_notice = 0;
        if (AZ_HAS(gp_shortcut_core, banner))
            gp_shortcut_core->banner(gp_shortcut_id,
                                    "Tap MOD MENU in the source list", 2200);
    }
}

static void gp_shortcut_tick(void)
{
    gp_shortcut_tick_at(gp_shortcut_clock_ms());
}

static int gp_shortcut_init(const az_core_v1 *core, const char *id)
{
    if (gp_shortcut_ready) return 1;
    if (!core || !id || !AZ_HAS(core, frame_filter) || !AZ_HAS(core, view)) return 0;
    gp_shortcut_core = core;
    gp_shortcut_id = id;
    if (core->frame_filter(id, 1u << AZ_BUS_MIXER_IN, gp_shortcut_frame, NULL) != 0) return 0;
    gp_shortcut_ready = 1;
    return 1;
}
#endif
