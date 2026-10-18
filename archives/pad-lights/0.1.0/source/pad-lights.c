/* SPDX-License-Identifier: GPL-3.0-only
 * Pad Lights: the pads as a light show when you're not using them, and optionally a beat
 * pulse (key-coloured) or 32-beat phrase countdown on the playing side.
 *   Idle show (default): pads animate only on a side whose selected deck is stopped or
 *     empty; the side you're playing keeps the deck's own hot-cue colours.
 *   Beat pulse / Phrase countdown: the playing side's pads follow the beat too.
 * Read-only apart from pad colours: no controls, audio or library changes.
 *
 * Threads: the UI thread computes colours (gamma-corrected) into words; the ERP LED filter
 * (bus thread) copies them into the frame and records which deck each side shows; a worker
 * reads keys from the library (the deck reports none) for the beat colour.
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "azmod-mod.h"
#include "lights.h"

#define MOD_ID "pad-lights"
static const az_core_v1 *C;

/* UI -> bus: per side, whether to override, and 8 packed colours (already gamma'd). */
static int pl_on_side[2];                 /* __atomic */
static uint32_t pl_cell[2][8];            /* __atomic */
/* bus -> UI: selection bytes (left 0x0c, right 0x44) decoded: 1 = deck 1/2, 2 = deck 3/4. */
static int pl_sel[2];                     /* __atomic */
/* worker -> UI: Camelot number per deck, and the content id it belongs to. */
static int pl_cam[4];                     /* __atomic */
static unsigned pl_cam_id[4];             /* __atomic */
static unsigned char pl_gam[256];
static int pl_row_mode = -1;

static int pl_selection(unsigned char v)
{
    unsigned p = v & 3u, s = (v >> 2) & 3u;
    return p == 2 && s == 1 ? 1 : p == 1 && s == 2 ? 2 : 0;
}
/* ---- ERP LED filter: copy, never compute ---- */
static int pl_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (bus != AZ_BUS_ERP_LED || !f || len < 0x78) return AZ_FRAME_OK;
    __atomic_store_n(&pl_sel[0], pl_selection(f[0x0c]), __ATOMIC_RELAXED);
    __atomic_store_n(&pl_sel[1], pl_selection(f[0x44]), __ATOMIC_RELAXED);
    for (int side = 0; side < 2; side++) {
        if (!__atomic_load_n(&pl_on_side[side], __ATOMIC_ACQUIRE)) continue;
        int base = side * 0x38;
        for (int k = 0; k < 8; k++) {
            uint32_t c = __atomic_load_n(&pl_cell[side][k], __ATOMIC_RELAXED);
            unsigned char *p = f + base + 0x1e + 4 * k;
            p[0] = (unsigned char)(c >> 16); p[1] = (unsigned char)(c >> 8); p[2] = (unsigned char)c;
        }
    }
    return AZ_FRAME_OK;
}

/* ---- worker: track keys from the library ---- */
static void pl_camelot(const char *in, char out[8])
{
    static const char *const pl_minor[12] = {
        "Abm", "Ebm", "Bbm", "Fm", "Cm", "Gm", "Dm", "Am", "Em", "Bm", "F#m", "Dbm"
    };
    static const char *const pl_major[12] = {
        "B", "F#", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E"
    };
    static const char *const pl_minor_alt[12] = {
        "G#m", "D#m", "A#m", "", "", "", "", "", "", "", "Gbm", "C#m"
    };
    static const char *const pl_major_alt[12] = {
        "", "Gb", "C#", "G#", "D#", "A#", "", "", "", "", "", ""
    };
    out[0] = 0;
    if (!in) return;
    char b[24]; size_t n = 0;
    for (; *in; in++) {
        if (*in == ' ') continue;
        if (n == sizeof b - 1) return;
        b[n++] = *in;
    }
    b[n] = 0;
    if (!n) return;
    if (b[0] >= '0' && b[0] <= '9') {
        unsigned num = 0; size_t i = 0;
        while (b[i] >= '0' && b[i] <= '9') {
            num = num * 10u + (unsigned)(b[i++] - '0');
            if (num > 12) return;
        }
        if (!num || !b[i] || b[i + 1]) return;
        if (b[i] == 'A' || b[i] == 'a' || b[i] == 'B' || b[i] == 'b')
            snprintf(out, 8, "%u%c", num, b[i] == 'a' || b[i] == 'A' ? 'A' : 'B');
        else if (b[i] == 'm' || b[i] == 'd')
            snprintf(out, 8, "%u%c", (num + 6) % 12 + 1, b[i] == 'm' ? 'A' : 'B');
        return;
    }
    if (b[0] >= 'a' && b[0] <= 'g') b[0] -= 'a' - 'A';
    if (b[0] < 'A' || b[0] > 'G') return;
    size_t i = 1;
    if (b[i] == '#' || b[i] == 'b') i++;
    int minor = 0;
    if (!b[i] || !strcmp(b + i, "M") || !strcmp(b + i, "maj") || !strcmp(b + i, "major")) minor = 0;
    else if (!strcmp(b + i, "m") || !strcmp(b + i, "min") || !strcmp(b + i, "minor")) minor = 1;
    else return;
    b[i] = 0;
    if (minor) { b[i++] = 'm'; b[i] = 0; }
    for (int k = 0; k < 12; k++) {
        const char *primary = minor ? pl_minor[k] : pl_major[k];
        const char *alt = minor ? pl_minor_alt[k] : pl_major_alt[k];
        if (!strcmp(b, primary) || (*alt && !strcmp(b, alt))) {
            snprintf(out, 8, "%d%c", k + 1, minor ? 'A' : 'B');
            return;
        }
    }
}

typedef struct { uint32_t id, hash; int cam; } pl_tk;
typedef struct { pl_tk *v; int n, cap; } pl_list;
static pl_list pl_lib[2];
static int pl_lib_state[2];
static uint32_t pl_hash(const char *s) { uint32_t h = 2166136261u; for (; s && *s; s++) h = (h ^ (unsigned char)*s) * 16777619u; return h; }
static int pl_collect(const az_track *t, void *user)
{
    pl_list *l = user;
    if (l->n == l->cap) { int c = l->cap ? l->cap * 2 : 2048; pl_tk *n = realloc(l->v, sizeof *n * (size_t)c); if (!n) return 1; l->v = n; l->cap = c; }
    char cam[8]; pl_camelot(t->key, cam);
    l->v[l->n++] = (pl_tk){t->id, pl_hash(t->title), cam[0] ? atoi(cam) : 0};
    return 0;
}
static int pl_cmp(const void *a, const void *b) { uint32_t x = ((const pl_tk *)a)->id, y = ((const pl_tk *)b)->id; return x < y ? -1 : x > y; }
static void pl_read(int s)
{
    pl_list l = {0};
    int r = C->lib_tracks(MOD_ID, s, 0, pl_collect, &l);
    free(pl_lib[s].v);
    if (r < 0) { free(l.v); memset(&pl_lib[s], 0, sizeof pl_lib[s]); pl_lib_state[s] = -1; return; }
    qsort(l.v, (size_t)l.n, sizeof *l.v, pl_cmp);
    pl_lib[s] = l; pl_lib_state[s] = 1;
}
static int pl_lookup(uint32_t id, const char *title)
{
    int best = 0; uint32_t h = pl_hash(title);
    for (int s = 0; s < 2; s++) {
        if (pl_lib_state[s] != 1) continue;
        pl_tk key = {id, 0, 0};
        pl_tk *e = bsearch(&key, pl_lib[s].v, (size_t)pl_lib[s].n, sizeof key, pl_cmp);
        if (!e) continue;
        if (title && title[0] && e->hash == h) return e->cam;
        if (!best) best = e->cam;
    }
    return best;
}
static void *pl_worker(void *arg)
{
    (void)arg;
    int miss = 0;
    for (;;) {
        int missing = 0;
        for (int d = 0; d < 4; d++) {
            az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
            if (!AZ_HAS(C, deck_info) || C->deck_info(d, &di) != 0 || di.loaded != 1) continue;
            di.title[sizeof di.title - 1] = di.key[sizeof di.key - 1] = 0;
            char cam[8]; pl_camelot(di.key, cam);
            int c = cam[0] ? atoi(cam) : pl_lookup(di.content_id, di.title);
            if (!c) missing = 1;
            __atomic_store_n(&pl_cam_id[d], di.content_id, __ATOMIC_RELAXED);
            __atomic_store_n(&pl_cam[d], c, __ATOMIC_RELEASE);
        }
        miss = missing ? miss + 1 : 0;
        for (int s = 0; s < 2; s++) if (pl_lib_state[s] == 0 || (missing && miss % 30 == 1)) pl_read(s);
        struct timespec ts = {1, 0}; nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- UI ---- */
static uint32_t pl_preview[2][8];      /* UI-only copy, un-gamma'd */
static int pl_preview_on[2];
static int pl_page_open;

static long pl_beats[4] = {-1, -1, -1, -1};
static double pl_last_phase[4] = {-1, -1, -1, -1};
static double pl_now(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void pl_tick(void)
{
    int mode = pl_row_mode >= 0 ? C->row_value(pl_row_mode) : PL_IDLE;
    double t = pl_now();
    int playing[4] = {0}; double phase[4] = {-1, -1, -1, -1};
    for (int d = 0; d < 4; d++) {
        az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di; di.playing = -1;
        if (C->deck_info(d, &di) == 0 && di.loaded == 1) playing[d] = di.playing == 1;
        double bp = -1, bar = -1;
        if (playing[d] && AZ_HAS(C, deck_grid) && C->deck_grid(d, &bp, &bar) == 0) phase[d] = bp;
        pl_beats[d] = pl_count(pl_beats[d], pl_last_phase[d], phase[d], playing[d]);
        pl_last_phase[d] = phase[d];
    }
    for (int side = 0; side < 2; side++) {
        int sel = __atomic_load_n(&pl_sel[side], __ATOMIC_RELAXED);
        int deck = side + (sel == 2 ? 2 : 0);                     /* unknown selection: decks 1/2 */
        pl_rgb pads[8];
        int cam = __atomic_load_n(&pl_cam[deck], __ATOMIC_ACQUIRE);
        int on = pl_side(mode, playing[deck], t + side * 1.7, phase[deck], pl_beats[deck], cam, pads);
        if (on) for (int k = 0; k < 8; k++) {
            __atomic_store_n(&pl_cell[side][k], (uint32_t)pl_gam[pads[k].r] << 16 | (uint32_t)pl_gam[pads[k].g] << 8 | pl_gam[pads[k].b], __ATOMIC_RELAXED);
            pl_preview[side][k] = (uint32_t)pads[k].r << 16 | (uint32_t)pads[k].g << 8 | pads[k].b;
        }
        pl_preview_on[side] = on;
        __atomic_store_n(&pl_on_side[side], on, __ATOMIC_RELEASE);
    }
    static int nth;                      /* preview at ~10 fps while the page is open */
    if (pl_page_open && AZ_HAS(C, request_redraw) && ++nth % 3 == 0) C->request_redraw();
}

/* Mod Menu page: a live preview of both sides' pads (what the deck will show). */
static void pl_draw_page(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->draw_header(d, "Pad Lights");
    uint32_t text = C->color(AZ_COL_TEXT), dim = C->color(AZ_COL_TEXT_DIM);
    int top = a.y + (AZ_HAS(C, row_height) ? C->row_height(d) : 48) + 16;
    int cell = (a.w - 60) / 18; if (cell > 48) cell = 48; if (cell < 8) cell = 8;
    for (int side = 0; side < 2; side++) {
        int y = top + side * (cell * 2 + 24);
        if (y + 30 + cell > a.y + a.h) break;     /* label + one row of squares must fit */
        int sel = __atomic_load_n(&pl_sel[side], __ATOMIC_RELAXED);
        char lab[48];
        snprintf(lab, sizeof lab, "%s: Deck %d%s", side ? "Right" : "Left", side + (sel == 2 ? 3 : 1),
                 pl_preview_on[side] ? "" : "  (deck colours)");
        if (y + 27 <= a.y + a.h && C->text_width(lab, 18) <= a.w - 40) C->draw_text(d, a.x + 20, y, lab, 18, pl_preview_on[side] ? text : dim);
        for (int k = 0; k < 8; k++) {
            int x = a.x + 20 + k * (cell + 8);
            if (x + cell > a.x + a.w) break;
            uint32_t c = pl_preview_on[side] ? pl_preview[side][k] : 0x202028;
            C->fill_rect(d, x, y + 30, cell, cell, c);
        }
    }
}
static int pl_page_input(const az_event *ev, void *user) { (void)user; return ev && ev->kind == AZ_EV_BACK ? 0 : 1; }
static void pl_page_enter(void *user) { (void)user; pl_page_open = 1; }

static int pl_init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    if (__atomic_load_n(&C, __ATOMIC_ACQUIRE) != core) __atomic_store_n(&C, core, __ATOMIC_RELEASE);
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, thread_start)) return 2;
    for (int i = 0; i < 256; i++) pl_gam[i] = (unsigned char)(pow(i / 255.0, 2.2) * 255.0 + 0.5);
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        static const char *const modes[] = {"Off", "Idle show", "Beat pulse", "Phrase countdown"};
        const az_row_v1 row = {.size = sizeof row, .key = "mode", .label = "Pad lights", .kind = AZ_ROW_CHOICE,
            .choices = modes, .n_choices = 4, .default_choice = PL_IDLE,
            .help = "Idle show: pads animate only on a side whose deck is stopped or empty; the side you play keeps its hot-cue colours. Beat pulse: the playing side pulses on the beat in the track's key colour. Phrase countdown: pads fill over 32 beats, last bar flashes red."};
        pl_row_mode = C->register_row(MOD_ID, &row);
    }
    if (AZ_HAS(C, register_screen)) {
        static const az_screen_v1 scr = {.size = sizeof scr, .on_draw = pl_draw_page, .on_input = pl_page_input, .on_enter = pl_page_enter};
        C->register_screen(MOD_ID, &scr);
    }
    static int started;
    if (!started) {
        /* Without the LED filter the mod stays loaded and inert (nothing to draw on). */
        if (!AZ_HAS(C, frame_filter) || C->frame_filter(MOD_ID, 1u << AZ_BUS_ERP_LED, pl_frame, NULL) != 0) {
            if (AZ_HAS(C, log)) C->log(MOD_ID, "no pad LED access on this core: pads unchanged");
        }
        if (AZ_HAS(C, lib_tracks) && C->thread_start(MOD_ID, "pad-keys", pl_worker, NULL) != 0) return 4;
        started = 1;
    }
    return 0;
}
static const char *const TAGS[] = {"any", NULL};
static const az_mod_v1 MOD = {.abi = AZ_MOD_ABI, .size = sizeof MOD, .id = MOD_ID, .fw_tags = TAGS,
                              .init = pl_init, .tick = pl_tick};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
