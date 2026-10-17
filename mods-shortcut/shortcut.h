/* SPDX-License-Identifier: GPL-3.0-only
 * SDK-only SOURCE shortcut. The DJ still opens the official MOD MENU row.
 * UI is the sole producer; the serialized mixer filter is the sole consumer.
 * Neither thread waits for the other. No core/private-address calls are used.
 */
#ifndef GROUPED_PANELS_SHORTCUT_H
#define GROUPED_PANELS_SHORTCUT_H
#include <limits.h>
#include <stdint.h>
#include <time.h>
#include "azmod-mod.h"

#ifndef GP_SHORTCUT_GETTIME
#define GP_SHORTCUT_GETTIME clock_gettime
#endif
/* Deterministic host-test scheduling hooks; compile to nothing in the mod. */
#ifndef GP_SHORTCUT_BEFORE_CLAIM
#define GP_SHORTCUT_BEFORE_CLAIM(token) ((void)(token))
#endif
#ifndef GP_SHORTCUT_AFTER_CLAIM
#define GP_SHORTCUT_AFTER_CLAIM(token) ((void)(token))
#endif

#define GP_SHORTCUT_BUSY UINT64_C(1)
#define GP_SHORTCUT_SENT UINT64_C(1)
#define GP_SHORTCUT_EXPIRED UINT64_C(2)
#define GP_SHORTCUT_TOKEN_MASK (~UINT64_C(3))
_Static_assert(__atomic_always_lock_free(sizeof(uint64_t), 0),
               "shortcut requires lock-free 64-bit atomics");

static const az_core_v1 *gp_shortcut_core;
static const char *gp_shortcut_id;
static int gp_shortcut_ready;
/* Immutable request timestamp + ownership bit. UI may cancel only an unclaimed
 * token. BUSY prevents token replacement while a callback edits a frame. */
static uint64_t gp_shortcut_request_word;
/* Completion belongs to its token, never to whichever request happens to be new. */
static uint64_t gp_shortcut_completion;
/* UI-only feedback and debounce. */
static int gp_shortcut_notice;
static uint64_t gp_shortcut_notice_token;
static uint64_t gp_shortcut_notice_until;
static long gp_shortcut_last_request = -10000;
/* Mixer-thread-only state. UI never clears/reuses these fields. */
static uint64_t gp_shortcut_pulse_token;
static uint64_t gp_shortcut_until;
static int gp_shortcut_knob_known;
static unsigned char gp_shortcut_knob;

static long gp_shortcut_clock_ms(void)
{
    struct timespec t;
    if (GP_SHORTCUT_GETTIME(CLOCK_MONOTONIC, &t) != 0 || t.tv_sec < 0 ||
        (uint64_t)t.tv_sec > ((uint64_t)LONG_MAX - 999) / 1000) return -1;
    return (long)t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static uint64_t gp_shortcut_token_time(uint64_t token)
{
    return (token >> 2) - 1;
}

static int gp_shortcut_expired(uint64_t token, long now)
{
    uint64_t requested = gp_shortcut_token_time(token);
    return now < 0 || (uint64_t)now < requested || (uint64_t)now - requested > 1000;
}

static int gp_shortcut_request(long now)
{
    if (!gp_shortcut_ready || now < 0 ||
        (uint64_t)now > (UINT64_MAX >> 2) - 1 ||
        now <= gp_shortcut_last_request ||
        (gp_shortcut_last_request >= 0 && now - gp_shortcut_last_request < 500) ||
        gp_shortcut_core->view() != AZ_VIEW_DECK) return 0;
    uint64_t token = ((uint64_t)now + 1) << 2, expected = 0;
    if (!__atomic_compare_exchange_n(&gp_shortcut_request_word, &expected, token,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    gp_shortcut_last_request = now;
    gp_shortcut_notice_token = token;
    gp_shortcut_notice_until = (uint64_t)now + 1500;
    gp_shortcut_notice = 1;
    return 1;
}

static void gp_shortcut_finish(uint64_t token, unsigned status)
{
    gp_shortcut_until = 0;
    __atomic_store_n(&gp_shortcut_completion, token | status, __ATOMIC_RELEASE);
    /* Last shared write: a producer may publish a new token immediately after. */
    __atomic_store_n(&gp_shortcut_request_word, 0, __ATOMIC_RELEASE);
}

/* LONG_MIN selects a fresh clock read AFTER ownership, for the public callback.
 * Explicit times are used only by focused host tests. */
static int gp_shortcut_frame_at(int bus, unsigned char *f, int len, long now)
{
    if (bus != AZ_BUS_MIXER_IN || !f || len < 98) return AZ_FRAME_OK;
    int knob_moved = gp_shortcut_knob_known && gp_shortcut_knob != f[34];
    gp_shortcut_knob = f[34];
    gp_shortcut_knob_known = 1;
    uint64_t token = __atomic_load_n(&gp_shortcut_request_word, __ATOMIC_ACQUIRE);
    if (!token || (token & GP_SHORTCUT_BUSY)) return AZ_FRAME_OK;
    GP_SHORTCUT_BEFORE_CLAIM(token);
    uint64_t expected = token;
    if (!__atomic_compare_exchange_n(&gp_shortcut_request_word, &expected,
                                    token | GP_SHORTCUT_BUSY, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return AZ_FRAME_OK;
    GP_SHORTCUT_AFTER_CLAIM(token);
    if (now == LONG_MIN) now = gp_shortcut_clock_ms();

    /* Native input wins unchanged, including the release of our synthetic press. */
    if (f[32] || f[33] || (f[31] & 1) || knob_moved) {
        gp_shortcut_finish(token, 0);
        return AZ_FRAME_OK;
    }
    if (gp_shortcut_expired(token, now)) {
        gp_shortcut_finish(token, GP_SHORTCUT_EXPIRED);
        return AZ_FRAME_OK;
    }
    int view = gp_shortcut_core->view();
    if (view != AZ_VIEW_DECK) {
        gp_shortcut_finish(token, gp_shortcut_pulse_token == token &&
                           gp_shortcut_until && view == AZ_VIEW_SOURCE ? GP_SHORTCUT_SENT : 0);
        return AZ_FRAME_OK;
    }
    if (gp_shortcut_pulse_token != token) {
        int release_old_pulse = gp_shortcut_until != 0;
        gp_shortcut_pulse_token = token;
        gp_shortcut_until = 0;
        if (release_old_pulse) {
            /* UI expired an old pulse between frames. Supply one unchanged
             * release frame before a new request may press SOURCE again. */
            __atomic_store_n(&gp_shortcut_request_word, token, __ATOMIC_RELEASE);
            return AZ_FRAME_OK;
        }
    }
    if (!gp_shortcut_until) gp_shortcut_until = (uint64_t)now + 200;
    if ((uint64_t)now >= gp_shortcut_until) {
        gp_shortcut_finish(token, GP_SHORTCUT_SENT);
        return AZ_FRAME_OK;
    }
    f[32] |= 0x80;
    f[6] &= (unsigned char)~0x08;
    __atomic_store_n(&gp_shortcut_completion, token | GP_SHORTCUT_SENT, __ATOMIC_RELEASE);
    __atomic_store_n(&gp_shortcut_request_word, token, __ATOMIC_RELEASE);
    return AZ_FRAME_OK;
}

static int gp_shortcut_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    return gp_shortcut_frame_at(bus, f, len, LONG_MIN);
}

static void gp_shortcut_fallback(void)
{
    gp_shortcut_notice = 0;
    if (AZ_HAS(gp_shortcut_core, banner))
        gp_shortcut_core->banner(gp_shortcut_id, "MODS timed out. Use SOURCE > MOD MENU", 3000);
}

static void gp_shortcut_tick_at(long now)
{
    if (!gp_shortcut_ready) return;
    uint64_t word = __atomic_load_n(&gp_shortcut_request_word, __ATOMIC_ACQUIRE);
    uint64_t token = word & GP_SHORTCUT_TOKEN_MASK;
    int timed_out = token && gp_shortcut_expired(token, now);
    if (timed_out && !(word & GP_SHORTCUT_BUSY)) {
        /* Never steal BUSY: its owner might still be editing a frame. No UI
         * writes to bus pulse state or completion, including after this CAS. */
        __atomic_compare_exchange_n(&gp_shortcut_request_word, &word, 0,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    if (!gp_shortcut_notice) return;
    uint64_t completed = __atomic_load_n(&gp_shortcut_completion, __ATOMIC_ACQUIRE);
    if ((completed & GP_SHORTCUT_TOKEN_MASK) == gp_shortcut_notice_token) {
        unsigned status = (unsigned)(completed & 3);
        if (status == GP_SHORTCUT_EXPIRED) { gp_shortcut_fallback(); return; }
        if (!status) { gp_shortcut_notice = 0; return; }
        if (now >= 0 && (uint64_t)now >= gp_shortcut_token_time(gp_shortcut_notice_token) &&
            (uint64_t)now <= gp_shortcut_notice_until &&
            gp_shortcut_core->view() == AZ_VIEW_SOURCE) {
            gp_shortcut_notice = 0;
            if (AZ_HAS(gp_shortcut_core, banner))
                gp_shortcut_core->banner(gp_shortcut_id, "Tap MOD MENU in the source list", 2200);
            return;
        }
    }
    if (timed_out && token == gp_shortcut_notice_token) {
        gp_shortcut_fallback();
        return;
    }
    if (now < 0 || (uint64_t)now < gp_shortcut_token_time(gp_shortcut_notice_token) ||
        (uint64_t)now > gp_shortcut_notice_until) gp_shortcut_fallback();
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
