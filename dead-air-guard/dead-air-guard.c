/* SPDX-License-Identifier: GPL-3.0-only
 * Dead-Air Guard: warns before the room goes silent. Thin bars at the top and bottom of
 * the screen plus one small label:
 *   LOAD NEXT  (amber)  everything audible ends within the warning time, nothing cued
 *   ENDING     (red)    the same, within 10 s
 *   QUIET      (amber)  every audible deck has been near-silent for a few seconds
 *   DEAD AIR   (red)    nothing audible, mic off; clears itself after a few seconds
 * "Audible" = playing with its channel fader up. A mic or TALK OVER switch on silences the
 * DEAD/QUIET alarms (speeches). Read-only: watches inputs, never changes them.
 *
 * Threads: a read-only raw mixer filter stores faders and mic switches as words; audio taps
 * keep a near-silence counter per deck; a worker thread reads track lengths from the
 * library (the deck reports none) and publishes each deck's length; the UI thread judges.
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "azmod-mod.h"
#include "guard.h"

#define MOD_ID "dead-air-guard"
static const az_core_v1 *C;

/* Bus thread -> UI: faders 1-4 packed (byte each), and the mic byte (0x18). */
static uint32_t dg_faders = 0xffffffffu;   /* __atomic; 0xff.. = unknown */
static int dg_mic = -1;                     /* __atomic */
/* Tap thread -> UI: near-silence per deck, ms. */
static int dg_quiet[4];                     /* __atomic */
/* Worker -> UI: length of each deck's loaded track in tenths of a second, -1 unknown,
 * plus the content id it belongs to. */
static int dg_len10[4] = {-1, -1, -1, -1}; /* __atomic */
static unsigned dg_len_id[4];               /* __atomic */

static int dg_row_on = -1, dg_row_warn = -1, dg_row_fader = -1, dg_row_quiet = -1;
static az_surface *dg_top, *dg_bot, *dg_lab;
static int dg_shown;
static dg_out dg_cur = {DG_OK, -1, -1, 0};
static int dg_blink, dg_last_paint_key = -1;

/* ---- raw mixer filter (read-only copy) ---- */
static int dg_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (bus != AZ_BUS_MIXER_IN || !f || len < 0x19) return AZ_FRAME_OK;
    uint32_t v = (uint32_t)f[0x0a] | (uint32_t)f[0x0b] << 8 | (uint32_t)f[0x0c] << 16 | (uint32_t)f[0x0d] << 24;
    __atomic_store_n(&dg_faders, v, __ATOMIC_RELAXED);
    __atomic_store_n(&dg_mic, f[0x18] & 0xe0, __ATOMIC_RELAXED);   /* MIC1 0x80, MIC2 0x40, TALK OVER 0x20 */
    return AZ_FRAME_OK;
}

/* ---- audio taps: near-silence counters, no clock (block length is the clock) ---- */
static void dg_tap(const float *lr, int frames, void *user)
{
    int deck = (int)(intptr_t)user;
    double acc = 0;
    for (int i = 0; i < frames * 2; i++) acc += (double)lr[i] * lr[i];
    double rms = frames > 0 ? sqrt(acc / (frames * 2)) : 0;
    int ms = frames * 1000 / 44100;
    int q = __atomic_load_n(&dg_quiet[deck], __ATOMIC_RELAXED);
    q = rms < 0.0032 ? (q < 600000 ? q + ms : q) : 0;               /* below about -50 dBFS */
    __atomic_store_n(&dg_quiet[deck], q, __ATOMIC_RELAXED);
}

/* ---- worker: track lengths from the library ---- */
typedef struct { uint32_t id; uint32_t hash; int len10; } dg_tl;
typedef struct { dg_tl *v; int n, cap; } dg_list;
static dg_list dg_lib[2];
static int dg_lib_state[2];                 /* 0 unread, 1 read, -1 no library */
static uint32_t dg_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; s && *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}
static int dg_collect(const az_track *t, void *user)
{
    dg_list *l = user;
    if (l->n == l->cap) {
        int c = l->cap ? l->cap * 2 : 2048;
        dg_tl *n = realloc(l->v, sizeof *n * (size_t)c);
        if (!n) return 1;
        l->v = n; l->cap = c;
    }
    l->v[l->n++] = (dg_tl){t->id, dg_hash(t->title), t->len_s > 0 ? (int)(t->len_s * 10) : -1};
    return 0;
}
static int dg_cmp(const void *a, const void *b)
{
    uint32_t x = ((const dg_tl *)a)->id, y = ((const dg_tl *)b)->id;
    return x < y ? -1 : x > y;
}
static void dg_read_stick(int s)
{
    dg_list l = {0};
    int r = C->lib_tracks(MOD_ID, s, 0, dg_collect, &l);
    free(dg_lib[s].v);
    if (r < 0) { free(l.v); memset(&dg_lib[s], 0, sizeof dg_lib[s]); dg_lib_state[s] = -1; return; }
    qsort(l.v, (size_t)l.n, sizeof *l.v, dg_cmp);
    dg_lib[s] = l; dg_lib_state[s] = 1;
}
/* Length for (id, title): the stick whose entry also matches the title wins (ids repeat
 * across sticks); a single id match is accepted when titles aren't known. */
static int dg_lookup(uint32_t id, const char *title)
{
    int best = -1;
    uint32_t h = dg_hash(title);
    for (int s = 0; s < 2; s++) {
        if (dg_lib_state[s] != 1) continue;
        dg_tl key = {id, 0, 0};
        dg_tl *e = bsearch(&key, dg_lib[s].v, (size_t)dg_lib[s].n, sizeof key, dg_cmp);
        if (!e) continue;
        if (title && title[0] && e->hash == h) return e->len10;
        if (best < 0) best = e->len10;
    }
    return best;
}
static void *dg_worker(void *arg)
{
    (void)arg;
    int miss_rounds = 0;
    for (;;) {
        int missing = 0;
        for (int d = 0; d < 4; d++) {
            az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
            if (!AZ_HAS(C, deck_info) || C->deck_info(d, &di) != 0 || di.loaded != 1 || !di.content_id) {
                __atomic_store_n(&dg_len10[d], -1, __ATOMIC_RELAXED); continue;
            }
            di.title[sizeof di.title - 1] = 0;
            int len10 = di.len_s > 0 ? (int)(di.len_s * 10) : dg_lookup(di.content_id, di.title);
            if (len10 < 0) missing = 1;
            __atomic_store_n(&dg_len_id[d], di.content_id, __ATOMIC_RELAXED);
            __atomic_store_n(&dg_len10[d], len10, __ATOMIC_RELEASE);
        }
        /* Read libraries on first need, and again (at most every ~30 s) when a loaded track
         * isn't found: a USB was swapped or re-exported. */
        if (missing) miss_rounds++; else miss_rounds = 0;
        for (int s = 0; s < 2; s++)
            if (dg_lib_state[s] == 0 || (missing && miss_rounds % 30 == 1)) dg_read_stick(s);
        struct timespec ts = {1, 0};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- UI ---- */
static long dg_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static int dg_row(int h, int def) { return h >= 0 ? C->row_value(h) : def; }
static uint32_t dg_color(void)
{
    int red = dg_cur.state == DG_ENDING || dg_cur.state == DG_DEAD;
    if ((dg_cur.state == DG_ENDING || dg_cur.state == DG_DEAD) && dg_blink) return 0x5A1416;
    return red ? 0xE5484Du : 0xD49A21u;
}
static void dg_paint_bar(az_draw *d, az_rect a, void *user) { (void)user; C->fill_rect(d, a.x, a.y, a.w, a.h, dg_color()); }
static void dg_paint_label(az_draw *d, az_rect a, void *user)
{
    (void)user;
    char t[64];
    int left = dg_cur.left >= 0 ? (int)(dg_cur.left + 0.999) : 0;
    switch (dg_cur.state) {
    case DG_LOAD_NEXT: snprintf(t, sizeof t, "LOAD NEXT  %d:%02d", left / 60, left % 60); break;
    case DG_ENDING: snprintf(t, sizeof t, "ENDING  0:%02d - nothing cued", left); break;
    case DG_QUIET: snprintf(t, sizeof t, "QUIET  %ds", dg_cur.quiet_s); break;
    case DG_DEAD: snprintf(t, sizeof t, "DEAD AIR"); break;
    default: return;
    }
    uint32_t col = dg_color();
    C->fill_rect(d, a.x, a.y, a.w, a.h, 0x0B0912);
    C->fill_rect(d, a.x, a.y, a.w, 4, col);
    int px = 24;
    while (px > 14 && AZ_HAS(C, text_width) && C->text_width(t, px) > a.w - 20) px -= 2;
    int tw = AZ_HAS(C, text_width) ? C->text_width(t, px) : 0;
    C->draw_text(d, a.x + (a.w - tw) / 2, a.y + 5, t, px, dg_cur.state == DG_DEAD || dg_cur.state == DG_ENDING ? 0xFFFFFF : col);
}
static void dg_show(int on)
{
    if (on == dg_shown) return;
    dg_shown = on;
    if (dg_top) C->surface_show(dg_top, on);
    if (dg_bot) C->surface_show(dg_bot, on);
    if (dg_lab) C->surface_show(dg_lab, on);
}
static long dg_last_audible = -100000, dg_play_since[4];
static void dg_tick(void)
{
    long now = dg_ms();
    dg_cfg cfg = {30, 10, 4000, 1, 1};
    static const int warns[] = {20, 30, 45, 60};
    int w = dg_row(dg_row_warn, 1); cfg.warn_s = warns[w >= 0 && w < 4 ? w : 1];
    cfg.use_faders = dg_row(dg_row_fader, 1) == 1;
    cfg.use_quiet = dg_row(dg_row_quiet, 1) == 1;
    if (dg_row(dg_row_on, 1) != 1) { dg_show(0); return; }
    uint32_t fv = __atomic_load_n(&dg_faders, __ATOMIC_RELAXED);
    int mic = __atomic_load_n(&dg_mic, __ATOMIC_RELAXED);
    dg_deck dk[4];
    int any_aud = 0;
    for (int d = 0; d < 4; d++) {
        az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di; di.playing = -1;
        dg_deck *x = &dk[d]; memset(x, 0, sizeof *x); x->remaining = -1; x->fader = -1;
        if (AZ_HAS(C, deck_info) && C->deck_info(d, &di) == 0 && di.loaded == 1) {
            x->loaded = 1; x->playing = di.playing == 1; x->id = di.content_id;
            int l10 = __atomic_load_n(&dg_len10[d], __ATOMIC_ACQUIRE);
            if (l10 > 0 && __atomic_load_n(&dg_len_id[d], __ATOMIC_RELAXED) == di.content_id && di.pos_s >= 0)
                x->remaining = l10 / 10.0 - di.pos_s;
            if (x->remaining < -1) x->remaining = -1;
        }
        if (fv != 0xffffffffu) x->fader = (int)((fv >> (8 * d)) & 0xff);
        if (!x->playing) dg_play_since[d] = 0;
        else if (!dg_play_since[d]) dg_play_since[d] = now;
        /* Just started: ignore the silence counted while it was paused. */
        x->quiet_ms = x->playing && now - dg_play_since[d] > 2500 ? __atomic_load_n(&dg_quiet[d], __ATOMIC_RELAXED) : 0;
        if (dg_audible(x, &cfg)) any_aud = 1;
    }
    if (any_aud) dg_last_audible = now;
    dg_out o = dg_judge(dk, &cfg, mic > 0, now - dg_last_audible);
    int view = AZ_HAS(C, view) ? C->view() : AZ_VIEW_DECK;
    if (view == AZ_VIEW_MENU) o.state = DG_OK;
    int blink = (int)((now / 500) & 1);
    int key = o.state * 100000 + (o.left >= 0 ? (int)(o.left + 0.999) : 0) * 10 + o.quiet_s + blink * 7777777;
    dg_cur = o; dg_blink = blink;
    dg_show(o.state != DG_OK);
    if (o.state != DG_OK && key != dg_last_paint_key) {
        dg_last_paint_key = key;
        if (dg_top) C->surface_redraw(dg_top);
        if (dg_bot) C->surface_redraw(dg_bot);
        if (dg_lab) C->surface_redraw(dg_lab);
    }
}

static int dg_init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    if (__atomic_load_n(&C, __ATOMIC_ACQUIRE) != core) __atomic_store_n(&C, core, __ATOMIC_RELEASE);
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) ||
        !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, fill_rect) || !AZ_HAS(C, draw_text) || !AZ_HAS(C, thread_start)) return 2;
    const unsigned views = AZ_VIEW_BIT(AZ_VIEW_DECK) | AZ_VIEW_BIT(AZ_VIEW_BROWSE) | AZ_VIEW_BIT(AZ_VIEW_SOURCE) | AZ_VIEW_BIT(AZ_VIEW_SEARCH);
    const az_surface_v1 bar = {.size = sizeof bar, .layer = AZ_LAYER_OVER_EP147, .views = views, .on_paint = dg_paint_bar};
    const az_surface_v1 lab = {.size = sizeof lab, .layer = AZ_LAYER_OVER_EP147, .views = views, .on_paint = dg_paint_label};
    dg_top = C->surface_create(MOD_ID, &bar); if (dg_top) { C->surface_place(dg_top, (az_rect){0, 0, 1280, 6}); C->surface_show(dg_top, 0); }
    dg_bot = C->surface_create(MOD_ID, &bar); if (dg_bot) { C->surface_place(dg_bot, (az_rect){0, 794, 1280, 6}); C->surface_show(dg_bot, 0); }
    dg_lab = C->surface_create(MOD_ID, &lab); if (dg_lab) { C->surface_place(dg_lab, (az_rect){440, 508, 400, 46}); C->surface_show(dg_lab, 0); }
    dg_shown = 0; dg_last_paint_key = -1;
    if (!dg_lab) return 3;
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        const az_row_v1 on = {.size = sizeof on, .key = "on", .label = "Dead-air guard", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "Amber LOAD NEXT when everything you can hear ends soon and no other deck has a track ready, red ENDING at 10 s, QUIET for long near-silence, DEAD AIR if the music stops with the mic off."};
        dg_row_on = C->register_row(MOD_ID, &on);
        static const char *const warns[] = {"20 s", "30 s", "45 s", "60 s"};
        const az_row_v1 warn = {.size = sizeof warn, .key = "warn", .label = "Warn before the end", .kind = AZ_ROW_CHOICE,
            .parent = "on", .choices = warns, .n_choices = 4, .default_choice = 1,
            .help = "How long before the last audible track ends LOAD NEXT appears, if nothing is cued."};
        dg_row_warn = C->register_row(MOD_ID, &warn);
        const az_row_v1 fader = {.size = sizeof fader, .key = "faders", .label = "Fader down = not heard", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON, .parent = "on",
            .help = "A deck playing with its channel fader down (cueing in headphones) doesn't count as audible."};
        dg_row_fader = C->register_row(MOD_ID, &fader);
        const az_row_v1 quiet = {.size = sizeof quiet, .key = "quiet", .label = "Quiet alarm", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON, .parent = "on",
            .help = "QUIET when every audible deck has been near-silent for 4 s (a long silent outro). Off while a mic is on."};
        dg_row_quiet = C->register_row(MOD_ID, &quiet);
    }
    static int started;
    if (!started) {
        if (AZ_HAS(C, frame_filter)) C->frame_filter(MOD_ID, (1u << AZ_BUS_MIXER_IN) | AZ_FILTER_RAW, dg_frame, NULL);
        if (AZ_HAS(C, audio_tap))
            for (int d = 0; d < 4; d++) C->audio_tap(MOD_ID, AZ_TAP_DECK(d), 1024, dg_tap, (void *)(intptr_t)d);
        if (AZ_HAS(C, lib_tracks) && C->thread_start(MOD_ID, "dead-air", dg_worker, NULL) != 0) return 4;
        started = 1;
    }
    return 0;
}
static const char *const TAGS[] = {"any", NULL};
static const az_mod_v1 MOD = {.abi = AZ_MOD_ABI, .size = sizeof MOD, .id = MOD_ID, .fw_tags = TAGS,
                              .init = dg_init, .tick = dg_tick};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
