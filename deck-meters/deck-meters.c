/* SPDX-License-Identifier: GPL-3.0-only
 *
 * deck-meters: a level meter for each deck, a clip warning, and a flash on the drop.
 *
 *   Meters  one bar per deck from the deck's own output before the mixer (the core's audio
 *           tap), so it shows what each track puts out, not the channel fader. Green, amber
 *           near the top, red when the track hits full scale (held for a second).
 *   Drop    when a playing deck's energy jumps after a quieter stretch (a breakdown into the
 *           drop), a band in that deck's color flashes across the top of the screen.
 *
 * The taps run on a core thread (never the audio thread) and only do sums; the results go to
 * the UI thread through atomics. No hooks, controls, LEDs, files or network; the audio is read,
 * never changed. "firmware": ["any"].
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "deck-meters"
#define NDECK 4
#define TAP_FRAMES 1024
#define POLL_MS 50
#define CLIP_HOLD_MS 1000
#define FLASH_MS 700
#define DROP_GAP_MS 15000

static const az_core_v1 *C;
static az_surface *meters, *flash;
static int r_show = -1, r_drop = -1, r_pos = -1, r_sens = -1;

/* ---- tap side (core thread), one state per deck ------------------------------------------ */
struct tapst {
    float fast, slow;           /* energy averages: ~0.1 s and ~6 s */
    float quiet;                /* the lowest slow average lately (a breakdown) */
    int   level_milli;          /* atomic: level 0..1000 for the UI */
    int   peak_milli;           /* atomic */
    int   drop;                 /* atomic: set by the tap, cleared by the UI */
    int   sens;                 /* atomic: 0 low, 1 normal, 2 high (UI -> tap) */
    int   armed;                /* atomic: the deck is playing (UI -> tap) */
    int   warm;                 /* blocks heard while armed; no flash in the first ~4 s */
    int   cooldown;             /* blocks left before another flash can fire */
};
static struct tapst g_t[NDECK];

static void tap(const float *lr, int frames, void *user)
{
    int d = (int)(intptr_t)user;
    if (d < 0 || d >= NDECK || !lr || frames <= 0) return;
    struct tapst *s = &g_t[d];
    float sum = 0, peak = 0;
    for (int i = 0; i < frames * 2; i++) {
        float v = lr[i];
        float a = v < 0 ? -v : v;
        if (a > peak) peak = a;
        sum += v * v;
    }
    float e = sum / (float)(frames * 2);           /* mean square of this block */
    s->fast += (e - s->fast) * 0.2f;               /* ~5 blocks: about 0.1 s */
    s->slow += (e - s->slow) * 0.0035f;            /* ~280 blocks: about 6 s */
    if (s->slow < s->quiet || s->quiet <= 0) s->quiet = s->slow;
    else s->quiet += (s->slow - s->quiet) * 0.001f;   /* forgets a breakdown after ~20 s */

    /* level: square root of the fast energy, shown on a rough dB-like curve */
    float rms = 0;
    if (s->fast > 0) { rms = s->fast; for (int k = 0; k < 4; k++) rms = 0.5f * (rms + s->fast / (rms > 1e-9f ? rms : 1e-9f)); }
    int lvl = (int)(rms * 1414.0f);                /* full-scale sine at ~1000 */
    if (lvl > 1000) lvl = 1000;
    __atomic_store_n(&s->level_milli, lvl, __ATOMIC_RELAXED);
    int pk = (int)(peak * 1000.0f);
    if (pk > __atomic_load_n(&s->peak_milli, __ATOMIC_RELAXED))
        __atomic_store_n(&s->peak_milli, pk > 1000 ? 1000 : pk, __ATOMIC_RELAXED);

    /* the drop: fast energy well above the quiet stretch before it, and loud in itself */
    int sens = __atomic_load_n(&s->sens, __ATOMIC_RELAXED);
    float ratio = sens == 0 ? 8.0f : sens == 2 ? 3.0f : 5.0f;
    if (!__atomic_load_n(&s->armed, __ATOMIC_RELAXED)) { s->warm = 0; return; }
    if (s->warm < 200) { s->warm++; s->quiet = s->slow; return; }   /* ~4 s to settle */
    if (s->cooldown > 0) { s->cooldown--; s->quiet = s->slow; return; }
    if (s->quiet > 1e-6f && s->fast > s->quiet * ratio && s->fast > 0.02f) {
        __atomic_store_n(&s->drop, 1, __ATOMIC_RELEASE);
        s->cooldown = 700;                        /* one flash per drop: ~15 s */
    }
}

/* ---- UI side ------------------------------------------------------------------------------ */
static const uint32_t DECK_RGB[NDECK] = { 0x2F8CFF, 0xFF4A3D, 0x3CCB5A, 0xF2C230 };
#define BAR_W 120
#define BAR_H 5
#define BOX_W (14 + BAR_W + 4)
#define ROW_P 10
#define BOX_H (NDECK * ROW_P + 4)
static const az_rect BOX_AT[3] = {
    { 212, 46, BOX_W, BOX_H },               /* top left of the waveforms */
    { 1050 - BOX_W, 46, BOX_W, BOX_H },      /* top right */
    { 212, 752, BOX_W, BOX_H },              /* bottom */
};

static int g_lvl[NDECK], g_shown_lvl[NDECK], g_loaded[NDECK];
static long g_clip_until[NDECK];
static long g_flash_until;
static int g_flash_deck = -1, g_meters_shown, g_flash_shown, g_placed_pos = -1;
static long last_poll_ms = -POLL_MS, last_drop_ms[NDECK];

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void paint_meters(az_draw *d, az_rect a, void *user)
{
    (void)user;
    long now = now_ms();
    C->fill_rect(d, a.x, a.y, a.w, a.h, 0x101010);
    for (int k = 0; k < NDECK; k++) {
        int y = a.y + 3 + k * ROW_P;
        char num[16];
        snprintf(num, sizeof num, "%d", k + 1);
        C->draw_text(d, a.x + 3, y - 3, num, 8, g_loaded[k] ? DECK_RGB[k] : 0x505050);
        int x0 = a.x + 14;
        C->fill_rect(d, x0, y, BAR_W, BAR_H, 0x262626);
        int w = g_shown_lvl[k] * BAR_W / 1000;
        int g = BAR_W * 70 / 100, am = BAR_W * 90 / 100;
        if (w > 0) C->fill_rect(d, x0, y, w < g ? w : g, BAR_H, 0x3CCB5A);
        if (w > g) C->fill_rect(d, x0 + g, y, (w < am ? w : am) - g, BAR_H, 0xF2C230);
        if (w > am) C->fill_rect(d, x0 + am, y, w - am, BAR_H, 0xFF4A3D);
        if (g_clip_until[k] > now) C->fill_rect(d, x0 + BAR_W - 6, y - 1, 6, BAR_H + 2, 0xFF2020);
    }
}

static void paint_flash(az_draw *d, az_rect a, void *user)
{
    (void)user;
    if (g_flash_deck < 0) return;
    C->fill_rect(d, a.x, a.y, a.w, a.h, DECK_RGB[g_flash_deck]);
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
    int drop_on = r_drop < 0 || C->row_value(r_drop) == 1;
    int pos = r_pos >= 0 ? C->row_value(r_pos) : 0;
    int sens = r_sens >= 0 ? C->row_value(r_sens) : 1;
    if (pos < 0 || pos > 2) pos = 0;

    int changed = 0;
    for (int k = 0; k < NDECK; k++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        int loaded = C->deck_info(k, &di) == 0 && di.loaded == 1;
        int playing = loaded && di.playing == 1;
        __atomic_store_n(&g_t[k].armed, playing && drop_on, __ATOMIC_RELAXED);
        __atomic_store_n(&g_t[k].sens, sens, __ATOMIC_RELAXED);
        int lvl = loaded ? __atomic_load_n(&g_t[k].level_milli, __ATOMIC_RELAXED) : 0;
        /* fall back slowly, rise at once */
        g_lvl[k] = lvl > g_lvl[k] ? lvl : g_lvl[k] - (g_lvl[k] - lvl) / 3;
        if (__atomic_exchange_n(&g_t[k].peak_milli, 0, __ATOMIC_RELAXED) >= 990) {
            if (g_clip_until[k] <= now) changed = 1;
            g_clip_until[k] = now + CLIP_HOLD_MS;
        }
        int q = g_lvl[k] / 25;                     /* repaint at the precision shown */
        if (q != g_shown_lvl[k] / 25 || loaded != g_loaded[k]) changed = 1;
        g_shown_lvl[k] = g_lvl[k];
        g_loaded[k] = loaded;
        if (__atomic_exchange_n(&g_t[k].drop, 0, __ATOMIC_ACQUIRE) && drop_on && playing &&
            now - last_drop_ms[k] > DROP_GAP_MS) {
            last_drop_ms[k] = now;
            g_flash_deck = k;
            g_flash_until = now + FLASH_MS;
            C->log(MOD_ID, "drop on deck %d", k + 1);
        }
    }

    if (meters) {
        if (show && pos != g_placed_pos) { C->surface_place(meters, BOX_AT[pos]); g_placed_pos = pos; changed = 1; }
        if (show != g_meters_shown) { C->surface_show(meters, show); g_meters_shown = show; changed = 1; }
        if (show && changed) C->surface_redraw(meters);
    }
    if (flash) {
        int want = g_flash_until > now && g_flash_deck >= 0;
        if (want != g_flash_shown) {
            C->surface_show(flash, want);
            if (want) C->surface_redraw(flash);
            g_flash_shown = want;
        }
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, audio_tap) || !AZ_HAS(C, surface_create) ||
        !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) || !AZ_HAS(C, surface_redraw) ||
        !AZ_HAS(C, draw_text) || !AZ_HAS(C, fill_rect)) {
        C->log(MOD_ID, "this AZ-MODS lacks the audio tap or surface services (1.2.3 or newer)");
        return 2;
    }
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        static const char *const POS[] = { "Left", "Right", "Low" };
        static const char *const SENS[] = { "Low", "Mid", "High" };
        az_row_v1 row;
        memset(&row, 0, sizeof row);
        row.size = sizeof row;
        row.key = "show"; row.label = "Deck meters"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Each deck's level before the mixer. Red = the track clips.";
        r_show = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "position"; row.label = "Position"; row.kind = AZ_ROW_CHOICE;
        row.choices = POS; row.n_choices = 3; row.default_choice = 0;
        row.help = "Top left, top right, or bottom.";
        r_pos = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row;
        row.key = "drop"; row.label = "Drop flash"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Flashes the deck's color across the top when its drop hits.";
        r_drop = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "drop";
        row.key = "sens"; row.label = "Sensitivity"; row.kind = AZ_ROW_CHOICE;
        row.choices = SENS; row.n_choices = 3; row.default_choice = 1;
        row.help = "High catches small drops, Low only big ones.";
        r_sens = C->register_row(MOD_ID, &row);
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: defaults (meters on, flash on)");
    }
    az_surface_v1 desc;
    memset(&desc, 0, sizeof desc);
    desc.size = sizeof desc;
    desc.layer = AZ_LAYER_OVER_EP147;
    desc.views = AZ_VIEW_BIT(AZ_VIEW_DECK);
    desc.on_paint = paint_meters;
    desc.on_input = input;
    meters = C->surface_create(MOD_ID, &desc);
    desc.on_paint = paint_flash;
    flash = C->surface_create(MOD_ID, &desc);
    if (!meters || !flash) { C->log(MOD_ID, "surface refused"); return 3; }
    C->surface_place(flash, (az_rect){ 0, 40, 1280, 6 });
    for (int k = 0; k < NDECK; k++)
        if (C->audio_tap(MOD_ID, AZ_TAP_DECK(k), TAP_FRAMES, tap, (void *)(intptr_t)k) != 0)
            C->log(MOD_ID, "audio tap for deck %d refused", k + 1);
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
