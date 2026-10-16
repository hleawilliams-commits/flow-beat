/* SPDX-License-Identifier: GPL-3.0-only
 *
 * phase-strip: a phase meter for all four decks, as a strip over the deck screen.
 *
 * The official AZ-MODS Phase Meter draws two bar rulers for the two decks of the 2 DECK view
 * and has no 4 DECK version (the deck program's 4-deck layout is not hooked). This mod needs
 * no hook: it reads every deck's beat phase through the core (deck_grid, 0..1 inside the
 * beat) and draws one lane per playing deck, beat ticks scrolling under a fixed playhead
 * line. Ticks that line up across the lanes are decks in phase. There is no bar position in
 * the deck's grid, so every tick is a beat; the downbeat is yours to count.
 *
 * It runs on the UI thread only (tick and the surface's on_paint), redraws at most
 * MAX_REDRAWS a second while a deck plays, and goes quiet when nothing plays. No hooks, no
 * controls, no LEDs, no audio, no files, no network. "firmware": ["any"].
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "phase-strip"
#define NDECK 4
#define REDRAW_MS 40          /* 25 a second at most */

/* Geometry (1280 x 800). The waveform rows show between the left deck panel and the Beat FX
 * panel, x 212..1050 (from the AZ-MODS core's notes). The strip's y is a Mod Menu choice,
 * because the 4 DECK layout is not documented anywhere yet; tune it on the deck. */
#define STRIP_X 212
#define STRIP_W 838
#define LANE_H 10
#define STRIP_H (NDECK * (LANE_H + 1) + 3)
#define BEAT_PX 64            /* one beat across the lane */
static const int STRIP_Y[3] = { 46, 72, 752 };

static const az_core_v1 *C;
static az_surface *strip;
static int r_show = -1, r_when = -1, r_pos = -1;
static int shown, placed_y = -1;
static long last_redraw_ms = -REDRAW_MS;
static int any_playing_last = -1;

/* deck colors: 1 blue, 2 red, 3 green, 4 amber */
static const uint32_t DECK_RGB[NDECK] = { 0x2F8CFF, 0xFF4A3D, 0x3CCB5A, 0xF2C230 };

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    const uint32_t text = C->color(AZ_COL_TEXT);
    int mid = a.x + a.w / 2;
    for (int deck = 0; deck < NDECK; deck++) {
        int y = a.y + 2 + deck * (LANE_H + 1);
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        if (C->deck_info(deck, &di) != 0 || di.loaded != 1) continue;
        char num[16];
        snprintf(num, sizeof num, "%d", deck + 1);
        C->draw_text(d, a.x + 2, y - 2, num, 8, di.playing == 1 ? DECK_RGB[deck] : dim);
        /* the lane */
        C->fill_rect(d, a.x + 14, y + LANE_H / 2, a.w - 16, 1, dim);
        double beat = -1.0, bar = -1.0;
        if (di.playing != 1 || C->deck_grid(deck, &beat, &bar) != 0 || beat < 0.0 || beat > 1.0)
            continue;
        /* beat ticks: the current beat's start is `beat` beats behind the playhead */
        int lane_x0 = a.x + 14, lane_x1 = a.x + a.w - 2;
        for (int k = -8; k <= 8; k++) {
            int x = mid + (int)(((double)k - beat) * BEAT_PX);
            if (x < lane_x0 || x >= lane_x1) continue;
            C->fill_rect(d, x - 1, y, 3, LANE_H, DECK_RGB[deck]);
        }
    }
    /* the playhead line, over every lane */
    C->fill_rect(d, mid - 1, a.y, 2, a.h, text);
}

static int input(const az_event *ev, void *user)
{
    (void)ev; (void)user;
    return 0;   /* taps go through to the deck */
}

static void tick(void)
{
    if (!strip) return;
    int show = r_show < 0 || C->row_value(r_show) == 1;
    int when = r_when >= 0 ? C->row_value(r_when) : 0;       /* 0 = 4 decks only, 1 = always */
    int pos = r_pos >= 0 ? C->row_value(r_pos) : 0;
    int n = C->deck_count();
    int want = show && (when == 1 || n == 4);

    int y = STRIP_Y[(pos >= 0 && pos < 3) ? pos : 0];
    if (want && y != placed_y) {
        C->surface_place(strip, (az_rect){ STRIP_X, y, STRIP_W, STRIP_H });
        placed_y = y;
    }
    if (want != shown) {
        C->surface_show(strip, want);
        shown = want;
        any_playing_last = -1;
    }
    if (!want) return;

    int any_playing = 0;
    for (int deck = 0; deck < NDECK; deck++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        if (C->deck_info(deck, &di) == 0 && di.loaded == 1 && di.playing == 1) any_playing = 1;
    }
    long now = now_ms();
    /* moving ticks: repaint at the capped rate; nothing playing: repaint once on the change */
    if ((any_playing || any_playing != any_playing_last) && now - last_redraw_ms >= REDRAW_MS) {
        last_redraw_ms = now;
        any_playing_last = any_playing;
        C->surface_redraw(strip);
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_count) || !AZ_HAS(C, deck_info) || !AZ_HAS(C, deck_grid) ||
        !AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) ||
        !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, draw_text) || !AZ_HAS(C, fill_rect) ||
        !AZ_HAS(C, color)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck grid or surface services (1.2.3 or newer)");
        return 2;
    }

    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
    static const char *const WHEN[] = { "4 DK", "All" };
    static const char *const POS[] = { "Top", "Band", "Low" };
    az_row_v1 row;
    memset(&row, 0, sizeof row);
    row.size = sizeof row;
    row.key = "show"; row.label = "4-deck phase strip"; row.kind = AZ_ROW_TOGGLE;
    row.flags = AZ_ROW_DEFAULT_ON;
    row.help = "Beat ticks for every playing deck. Lined up = in phase.";
    r_show = C->register_row(MOD_ID, &row);
    memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
    row.key = "when"; row.label = "Show"; row.kind = AZ_ROW_CHOICE;
    row.choices = WHEN; row.n_choices = 2; row.default_choice = 0;
    row.help = "4 DK = 4 DECK view only, All = every view.";
    r_when = C->register_row(MOD_ID, &row);
    row.key = "position"; row.label = "Position"; row.choices = POS; row.n_choices = 3; row.default_choice = 0;
    row.help = "Where the strip sits on the screen.";
    r_pos = C->register_row(MOD_ID, &row);
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: defaults (on, 4 DECK only, top)");
    }

    az_surface_v1 desc;
    memset(&desc, 0, sizeof desc);
    desc.size = sizeof desc;
    desc.layer = AZ_LAYER_OVER_EP147;
    desc.views = AZ_VIEW_BIT(AZ_VIEW_DECK);
    desc.on_paint = paint;
    desc.on_input = input;
    strip = C->surface_create(MOD_ID, &desc);
    if (!strip) { C->log(MOD_ID, "surface refused"); return 3; }
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
