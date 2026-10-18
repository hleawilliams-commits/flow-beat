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

/* Spare lanes. The core switches a filter off for the session after 8 overruns,
 * counted cumulatively, and a preempted call counts. One filter therefore dies
 * eventually on a busy deck. Each lane is its own registration with its own
 * strike count; only the active lane runs the shortcut, the others only beat.
 * One bus = one thread, so lanes never run at the same time. */
#ifndef GP_SHORTCUT_LANES
#define GP_SHORTCUT_LANES 4
#endif
static int gp_shortcut_lanes;                          /* registered, set in init */
static int gp_shortcut_lane;                           /* __atomic: active lane */
static uint32_t gp_shortcut_beat[GP_SHORTCUT_LANES];   /* __atomic: frames seen, per lane */
static int gp_shortcut_all_off;                        /* __atomic: no lane beating */
/* UI-only watch state. */
static uint32_t gp_shortcut_seen[GP_SHORTCUT_LANES];
static int gp_shortcut_off[GP_SHORTCUT_LANES], gp_shortcut_stall[GP_SHORTCUT_LANES];
static long gp_shortcut_watch_at = -1, gp_shortcut_quiet_since = -1;
/* Last cancellation, for the log: f[31], f[32], f[33], knob moved. Mixer writes. */
static uint32_t gp_shortcut_cancel_why;                /* __atomic */
/* Or the screen it left for, + 1 (0 = not a screen change). Mixer writes. */
static uint32_t gp_shortcut_cancel_view;               /* __atomic */

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
    /* Clear old log reasons before publishing: once the token is out, the bus owns them. */
    if (!__atomic_load_n(&gp_shortcut_request_word, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&gp_shortcut_cancel_why, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&gp_shortcut_cancel_view, 0, __ATOMIC_RELAXED);
    }
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
        __atomic_store_n(&gp_shortcut_cancel_why,
                         (uint32_t)f[31] << 24 | (uint32_t)f[32] << 16 |
                         (uint32_t)f[33] << 8 | (uint32_t)knob_moved, __ATOMIC_RELAXED);
        gp_shortcut_finish(token, 0);
        return AZ_FRAME_OK;
    }
    if (gp_shortcut_expired(token, now)) {
        gp_shortcut_finish(token, GP_SHORTCUT_EXPIRED);
        return AZ_FRAME_OK;
    }
    int view = gp_shortcut_core->view();
    if (view != AZ_VIEW_DECK) {
        __atomic_store_n(&gp_shortcut_cancel_view, (uint32_t)view + 1, __ATOMIC_RELAXED);
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

__attribute__((unused)) static int gp_shortcut_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    return gp_shortcut_frame_at(bus, f, len, LONG_MIN);
}

/* The registered callback. user = lane + 1. A spare lane does one relaxed
 * store and returns: the smallest window a preemption can land in. */
static int gp_shortcut_lane_frame(int bus, unsigned char *f, int len, void *user)
{
    int lane = (int)(intptr_t)user - 1;
    if (lane < 0 || lane >= GP_SHORTCUT_LANES) return AZ_FRAME_OK;
    /* Sole writer is this bus thread; the UI only loads. */
    __atomic_store_n(&gp_shortcut_beat[lane],
                     __atomic_load_n(&gp_shortcut_beat[lane], __ATOMIC_RELAXED) + 1,
                     __ATOMIC_RELAXED);
    if (lane != __atomic_load_n(&gp_shortcut_lane, __ATOMIC_RELAXED)) return AZ_FRAME_OK;
    return gp_shortcut_frame_at(bus, f, len, LONG_MIN);
}

#define GP_SHORTCUT_LOG(...) do { \
    if (AZ_HAS(gp_shortcut_core, log)) gp_shortcut_core->log(gp_shortcut_id, __VA_ARGS__); \
} while (0)

/* UI thread, about 4 times a second. A lane that stops beating while another
 * still beats, on two looks in a row (a frame can land between two loads of
 * one look), has been switched off by the core (it never comes back until
 * EP147 restarts). Move the shortcut to the next live lane. If every lane is
 * quiet for 2 s, report all off; any beat afterwards clears that again. */
static void gp_shortcut_watch_at_time(long now)
{
    if (!gp_shortcut_ready || now < 0) return;
    if (gp_shortcut_watch_at >= 0 && now >= gp_shortcut_watch_at && now - gp_shortcut_watch_at < 250)
        return;
    gp_shortcut_watch_at = now;
    int moved[GP_SHORTCUT_LANES] = {0}, any = 0;
    for (int i = 0; i < gp_shortcut_lanes; i++) {
        uint32_t b = __atomic_load_n(&gp_shortcut_beat[i], __ATOMIC_RELAXED);
        moved[i] = b != gp_shortcut_seen[i];
        gp_shortcut_seen[i] = b;
        any |= moved[i];
    }
    if (!any) {
        if (gp_shortcut_quiet_since < 0 || now < gp_shortcut_quiet_since) gp_shortcut_quiet_since = now;
        if (now - gp_shortcut_quiet_since >= 2000 &&
            !__atomic_load_n(&gp_shortcut_all_off, __ATOMIC_RELAXED)) {
            __atomic_store_n(&gp_shortcut_all_off, 1, __ATOMIC_RELAXED);
            GP_SHORTCUT_LOG("MODS: no input lane is running; button off until restart");
        }
        return;
    }
    gp_shortcut_quiet_since = -1;
    if (__atomic_load_n(&gp_shortcut_all_off, __ATOMIC_RELAXED)) {
        __atomic_store_n(&gp_shortcut_all_off, 0, __ATOMIC_RELAXED);
        GP_SHORTCUT_LOG("MODS: input lane running again");
    }
    for (int i = 0; i < gp_shortcut_lanes; i++) {
        if (moved[i]) {
            gp_shortcut_stall[i] = gp_shortcut_off[i] = 0;
        } else if (!gp_shortcut_off[i] && ++gp_shortcut_stall[i] >= 2) {
            gp_shortcut_off[i] = 1;
            GP_SHORTCUT_LOG("MODS: lane %d switched off by the core", i + 1);
        }
    }
    int active = __atomic_load_n(&gp_shortcut_lane, __ATOMIC_RELAXED);
    if (!gp_shortcut_off[active]) return;
    for (int step = 1; step < gp_shortcut_lanes; step++) {
        int next = (active + step) % gp_shortcut_lanes;
        if (!gp_shortcut_off[next]) {
            __atomic_store_n(&gp_shortcut_lane, next, __ATOMIC_RELAXED);
            GP_SHORTCUT_LOG("MODS: moved to lane %d of %d", next + 1, gp_shortcut_lanes);
            return;
        }
    }
}

__attribute__((unused)) static int gp_shortcut_running(void)
{
    return gp_shortcut_ready && !__atomic_load_n(&gp_shortcut_all_off, __ATOMIC_RELAXED);
}

static void gp_shortcut_fallback(void)
{
    gp_shortcut_notice = 0;
    GP_SHORTCUT_LOG("MODS tap: timed out before SOURCE opened (lane %d of %d)",
                    __atomic_load_n(&gp_shortcut_lane, __ATOMIC_RELAXED) + 1, gp_shortcut_lanes);
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
        if (!status) {
            uint32_t why = __atomic_load_n(&gp_shortcut_cancel_why, __ATOMIC_RELAXED);
            uint32_t left = __atomic_load_n(&gp_shortcut_cancel_view, __ATOMIC_RELAXED);
            if (left)
                GP_SHORTCUT_LOG("MODS tap: screen changed to view %u before the press finished (now view %d)",
                                (unsigned)(left - 1), gp_shortcut_core->view());
            else
                GP_SHORTCUT_LOG("MODS tap: cancelled by panel input (b31=%02x b32=%02x b33=%02x knob=%u)",
                                (unsigned)(why >> 24), (unsigned)(why >> 16 & 0xff),
                                (unsigned)(why >> 8 & 0xff), (unsigned)(why & 0xff));
            __atomic_store_n(&gp_shortcut_cancel_why, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&gp_shortcut_cancel_view, 0, __ATOMIC_RELAXED);
            gp_shortcut_notice = 0;
            return;
        }
        if (now >= 0 && (uint64_t)now >= gp_shortcut_token_time(gp_shortcut_notice_token) &&
            (uint64_t)now <= gp_shortcut_notice_until &&
            gp_shortcut_core->view() == AZ_VIEW_SOURCE) {
            gp_shortcut_notice = 0;
            GP_SHORTCUT_LOG("MODS tap: SOURCE opened");
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
    long now = gp_shortcut_clock_ms();
    gp_shortcut_watch_at_time(now);
    gp_shortcut_tick_at(now);
}

static int gp_shortcut_init(const az_core_v1 *core, const char *id)
{
    if (gp_shortcut_ready) return 1;
    if (!core || !id || !AZ_HAS(core, frame_filter) || !AZ_HAS(core, view)) return 0;
    gp_shortcut_core = core;
    gp_shortcut_id = id;
    /* Lane 1 is required; spares are best effort (the core allows 32 filters in all). */
    gp_shortcut_lanes = 0;
    __atomic_store_n(&gp_shortcut_lane, 0, __ATOMIC_RELAXED);
    for (int i = 0; i < GP_SHORTCUT_LANES; i++) {
        if (core->frame_filter(id, 1u << AZ_BUS_MIXER_IN, gp_shortcut_lane_frame,
                               (void *)(intptr_t)(i + 1)) != 0) break;
        gp_shortcut_lanes = i + 1;
    }
    if (!gp_shortcut_lanes) return 0;
    GP_SHORTCUT_LOG("MODS: %d input lane%s registered", gp_shortcut_lanes,
                    gp_shortcut_lanes == 1 ? "" : "s");
    gp_shortcut_ready = 1;
    return 1;
}
#endif
