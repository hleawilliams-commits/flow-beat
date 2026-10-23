/* SPDX-License-Identifier: GPL-3.0-only
 *
 * stem-badge: a small badge on each deck that says whether the loaded track has stems.
 *
 *   STEMS     green: stems were found and the stems engine plays this deck
 *   STEMS     grey:  stems were found but the engine is not playing them on this deck
 *   NO STEMS  red:   no stems for this track, so the stem pads do nothing
 *
 * All of it comes from the core's deck snapshot (deck_info has_stems / stems_on). It runs on
 * the UI thread only, re-reads the decks 4 times a second and repaints only on a change.
 * No hooks, controls, LEDs, audio, files or network. "firmware": ["any"].
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "stem-badge"
#define NDECK 4
#define POLL_MS 250
#define PX 16

/* Geometry, as in Key next to BPM: waveform rows x 212..1050; 2 DECK rows at y 81 and 286
 * (130 and 308 with the official Phase Meter on); 4 DECK rows are a first guess. The badge
 * sits at the left end of the row so it stays clear of Key next to BPM on the right. */
#define ROW_X0 212
#define ROW_X1 1050
static const int ROW2_Y[2] = { 81, 286 };
static const int ROW2_PHASE_Y[2] = { 130, 308 };
static const int ROW4_Y[4] = { 81, 253, 425, 597 };
#define TOP_Y 46

static const az_core_v1 *C;
static az_surface *badge[NDECK];
static int r_show = -1, r_which = -1, r_pos = -1, r_phase_official = -1;
static long last_poll_ms = -POLL_MS;

enum { ST_HIDDEN = 0, ST_ON, ST_OFF, ST_NONE };
static struct { int state; az_rect where; int shown; } g_d[NDECK];

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static const char *label(int st, int deck, int numbered, char *buf, int n)
{
    const char *t = st == ST_NONE ? "NO STEMS" : "STEMS";
    if (numbered) snprintf(buf, (size_t)n, "%d %s", deck + 1, t);
    else snprintf(buf, (size_t)n, "%s", t);
    return buf;
}

static int g_numbered;

/* The text's width, estimated: text_width belongs to the paint call (the SDK's harness checks
 * that), and the layout is decided on the UI thread before any paint. Wide enough for the
 * deck font's capitals and digits. */
static int est_width(const char *s, int px)
{
    return (int)strlen(s) * px * 64 / 100 + 2;
}

static void paint(az_draw *d, az_rect a, void *user)
{
    int deck = (int)(intptr_t)user;
    if (deck < 0 || deck >= NDECK) return;
    int st = g_d[deck].state;
    if (st == ST_HIDDEN) return;
    uint32_t bg = st == ST_ON ? 0x1E7A3A : st == ST_OFF ? 0x3A3A3A : 0x8A1F1F;
    char buf[24];
    C->fill_rect(d, a.x, a.y, a.w, a.h, bg);
    C->draw_text(d, a.x + 6, a.y + 1, label(st, deck, g_numbered, buf, sizeof buf), PX, 0xFFFFFF);
}

static int input(const az_event *ev, void *user)
{
    (void)ev; (void)user;
    return 0;
}

static void tick(void)
{
    long now = now_ms();
    if (now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;

    int show = r_show < 0 || C->row_value(r_show) == 1;
    int which = r_which >= 0 ? C->row_value(r_which) : 0;   /* 0 = every deck, 1 = only NO STEMS */
    int pos = r_pos >= 0 ? C->row_value(r_pos) : 0;         /* 0 = on the row, 1 = screen top */
    int phase_on = r_phase_official >= 0 && C->row_value(r_phase_official) == 1;
    int n = C->deck_count();
    int high_in_use = 0;
    int st[NDECK];

    for (int d = 0; d < NDECK; d++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        st[d] = ST_HIDDEN;
        if (C->deck_info(d, &di) != 0 || di.loaded != 1) continue;
        if (d >= 2) high_in_use = 1;
        if (di.has_stems == 1) st[d] = di.stems_on == 1 ? ST_ON : ST_OFF;
        else if (di.has_stems == 0) st[d] = ST_NONE;
        if (which == 1 && st[d] != ST_NONE) st[d] = ST_HIDDEN;
    }
    /* 2/4 DECK with decks 3/4 in use: the core does not say which pair shows, so every badge
     * carries its deck number and they sit in one strip at the top. */
    int numbered = (n == 2 && high_in_use);
    int top = numbered || pos == 1;
    int numbered_changed = numbered != g_numbered;
    g_numbered = numbered;

    int x_top = ROW_X1;
    for (int d = NDECK - 1; d >= 0; d--) {
        int s = show ? st[d] : ST_HIDDEN;
        if (!top && n == 2 && d >= 2) s = ST_HIDDEN;
        char buf[24];
        int w = s ? est_width(label(s, d, numbered, buf, sizeof buf), PX) + 12 : 0;
        int h = PX * 3 / 2 + 2;
        az_rect r = { 0, 0, 0, 0 };
        if (s) {
            if (top) { x_top -= w + 6; r = (az_rect){ x_top, TOP_Y, w, h }; }
            else {
                int y = n == 4 ? ROW4_Y[d] : (phase_on ? ROW2_PHASE_Y[d & 1] : ROW2_Y[d & 1]);
                r = (az_rect){ ROW_X0 + 4, y + 4, w, h };
            }
        }
        int changed = s != g_d[d].state || memcmp(&r, &g_d[d].where, sizeof r) != 0 || numbered_changed;
        g_d[d].state = s;
        g_d[d].where = r;
        if (!badge[d]) continue;
        if (s) {
            if (changed) C->surface_place(badge[d], r);
            if (!g_d[d].shown) { C->surface_show(badge[d], 1); g_d[d].shown = 1; }
            if (changed) C->surface_redraw(badge[d]);
        } else if (g_d[d].shown) {
            C->surface_show(badge[d], 0);
            g_d[d].shown = 0;
        }
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_count) || !AZ_HAS(C, deck_info) || !AZ_HAS(C, surface_create) ||
        !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) || !AZ_HAS(C, surface_redraw) ||
        !AZ_HAS(C, draw_text) || !AZ_HAS(C, fill_rect)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck or surface services (1.2.3 or newer)");
        return 2;
    }
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        static const char *const WHICH[] = { "All decks", "No stems only" };
        static const char *const POS[] = { "On the deck", "Screen top" };
        az_row_v1 row;
        memset(&row, 0, sizeof row);
        row.size = sizeof row;
        row.key = "show"; row.label = "Stem badge"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Green = stems playing, grey = stems off, red = no stems.";
        r_show = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "which"; row.label = "Show on"; row.kind = AZ_ROW_CHOICE;
        row.choices = WHICH; row.n_choices = 2; row.default_choice = 0;
        row.help = "Every loaded deck, or only tracks with no stems.";
        r_which = C->register_row(MOD_ID, &row);
        row.key = "position"; row.label = "Position"; row.choices = POS; row.n_choices = 2; row.default_choice = 0;
        row.help = "Left end of each deck's row, or the screen top.";
        r_pos = C->register_row(MOD_ID, &row);
        if (AZ_HAS(C, row_lookup)) r_phase_official = C->row_lookup("phase-meter");
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: defaults (on, every deck, on the row)");
    }
    for (int d = 0; d < NDECK; d++) {
        az_surface_v1 desc;
        memset(&desc, 0, sizeof desc);
        desc.size = sizeof desc;
        desc.layer = AZ_LAYER_OVER_EP147;
        desc.views = AZ_VIEW_BIT(AZ_VIEW_DECK);
        desc.on_paint = paint;
        desc.on_input = input;
        desc.user = (void *)(intptr_t)d;
        badge[d] = C->surface_create(MOD_ID, &desc);
        if (!badge[d]) { C->log(MOD_ID, "surface %d refused", d + 1); return 3; }
    }
    return 0;
}

static const char *const FW_TAGS[] = { "any", NULL };
static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = tick,
    .on_frame = NULL,
};

AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
