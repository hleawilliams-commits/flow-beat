/* SPDX-License-Identifier: GPL-3.0-only
 *
 * phrase-coach: a phrase counter on each deck, so a mix lands on the start of a phrase.
 *
 *   5/8 [#####...]   bar 5 of an 8-bar phrase; the current bar's cell fills beat by beat
 *   amber            the last bar before the next phrase
 *   green flash      the first beat of a new phrase
 *   ?                the count was lost (a jump that did not land on the grid)
 *
 * The deck's grid gives the beat phase but no bar position (deck_grid's bar_phase is -1 on
 * EP147), so the count starts where the deck first plays after a load: beat 1 of bar 1 is the
 * beat nearest that moment. Start a track from a cue on its first downbeat and the count is
 * right. "Phrase starts now" in the Mod Menu sets beat 1 on the deck that started last.
 *
 * A thread reads the decks every 10 ms. It follows the beat phase across beats, backwards
 * too; on a jump (hot cue, loop, beat jump) it works out how many beats were skipped from the
 * playhead and the beats per track-second it measured, and keeps counting when the jump lands
 * on the grid. No hooks, controls, LEDs, audio, files or network. "firmware": ["any"].
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "phrase-coach"
#define NDECK 4
#define STEP_MS 10
#define PX 16

/* Geometry, as in Stem Badge: waveform rows x 212..1050; 2 DECK rows at y 81 and 286 (130
 * and 308 with the official Phase Meter on); 4 DECK rows are a first guess. The counter sits
 * right of where Stem Badge draws, at the left end of the row. */
#define ROW_X0 212
#define BOX_X (ROW_X0 + 112)
static const int ROW2_Y[2] = { 81, 286 };
static const int ROW2_PHASE_Y[2] = { 130, 308 };
static const int ROW4_Y[4] = { 81, 253, 425, 597 };
#define TOP_Y 46
#define BOX_W 196
#define BOX_H 26

static const az_core_v1 *C;
static az_surface *box[NDECK];
static int r_show = -1, r_len = -1, r_pos = -1, r_phase_official = -1;

/* thread -> UI, all atomics */
static int g_known[NDECK];      /* 1 = counting */
static int g_beat[NDECK];       /* beats since beat 1, may be negative */
static unsigned g_start[NDECK]; /* play-start order, 0 = not playing */
/* UI -> thread */
static int g_reset[NDECK];

static double wrap_half(double x)
{
    while (x > 0.5) x -= 1.0;
    while (x <= -0.5) x += 1.0;
    return x;
}

/* ---- the counter thread ------------------------------------------------------------------ */

struct follow {
    int anchored;
    uint32_t id;
    double u;          /* beats since beat 1, unwrapped; frac(u) follows the grid's phase */
    double last_pos;
    double k;          /* beats per track-second, measured; 0 = not yet */
    double k_beats, k_secs;
};

static void *counter(void *arg)
{
    (void)arg;
    struct follow f[NDECK];
    memset(f, 0, sizeof f);
    unsigned starts = 0;
    unsigned since[NDECK] = { 0 };
    for (;;) {
        for (int d = 0; d < NDECK; d++) {
            az_deck_info di;
            memset(&di, 0, sizeof di);
            di.size = (int)sizeof di;
            double ph = -1.0, bar = -1.0;
            int ok = C->deck_info(d, &di) == 0 && di.loaded == 1 && C->deck_grid(d, &ph, &bar) == 0 &&
                     ph >= 0.0 && ph <= 1.0;
            int playing = ok && di.playing == 1;
            if (playing && !since[d]) since[d] = ++starts;
            else if (!playing) since[d] = 0;
            __atomic_store_n(&g_start[d], since[d], __ATOMIC_RELAXED);

            struct follow *t = &f[d];
            if (!ok || di.content_id != t->id) {
                memset(t, 0, sizeof *t);
                t->id = ok ? di.content_id : 0;
                __atomic_store_n(&g_known[d], 0, __ATOMIC_RELAXED);
                if (!ok) continue;
            }
            if (ph >= 1.0) ph = 0.0;
            if (!t->anchored) {
                if (!playing) continue;
                /* beat 1 is the beat nearest the moment the deck starts */
                t->u = ph < 0.5 ? ph : ph - 1.0;
                t->anchored = 1;
                t->last_pos = di.pos_s;
                __atomic_store_n(&g_known[d], 1, __ATOMIC_RELAXED);
            } else {
                double dpos = di.pos_s - t->last_pos;
                double dph = wrap_half(ph - (t->u - floor(t->u)));
                /* a normal 10 ms step moves the playhead a few ms (more after a stall); a
                 * jump moves it further than the phase can tell apart: 0.4 of a beat */
                double lim = di.bpm > 30.0 ? 0.4 * 60.0 / di.bpm : 0.2;
                if (lim > 0.2) lim = 0.2;
                int jump = fabs(dpos) > lim;
                if (!jump) {
                    t->u += dph;
                    if (dpos > 0.002) {
                        t->k_beats += dph;
                        t->k_secs += dpos;
                        if (t->k_secs > 4.0) t->k = t->k_beats / t->k_secs;
                    }
                } else {
                    double k = t->k > 0.0 ? t->k : di.bpm / 60.0;
                    double guess = t->u + dpos * k;
                    double whole = floor(guess - ph + 0.5);
                    double miss = fabs(guess - (whole + ph));
                    t->u = whole + ph;
                    /* off the grid by more than a third of a beat: the count is lost until
                     * the next load or "Phrase starts now" */
                    if (miss > 0.33 || k <= 0.0) __atomic_store_n(&g_known[d], 0, __ATOMIC_RELAXED);
                    /* before 4 s of playing the deck's BPM stands in for the grid's, and the
                     * pitch makes it off by up to a few %: only short jumps are trusted then */
                    if (t->k <= 0.0 && fabs(dpos * k) > 6.0) __atomic_store_n(&g_known[d], 0, __ATOMIC_RELAXED);
                }
                t->last_pos = di.pos_s;
            }
            if (__atomic_exchange_n(&g_reset[d], 0, __ATOMIC_ACQ_REL)) {
                t->u -= floor(t->u + 0.5);
                if (!t->anchored) {
                    t->anchored = 1;
                    t->last_pos = di.pos_s;
                }
                __atomic_store_n(&g_known[d], 1, __ATOMIC_RELAXED);
            }
            __atomic_store_n(&g_beat[d], (int)floor(t->u), __ATOMIC_RELAXED);
        }
        struct timespec ts = { 0, STEP_MS * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- the counters on screen -------------------------------------------------------------- */

enum { ST_HIDDEN = 0, ST_WAIT, ST_LOST, ST_COUNT };
static struct {
    int state, bar, beat, bars, flash, numbered;
    az_rect where;
    int shown;
} g_d[NDECK];

static void paint(az_draw *d, az_rect a, void *user)
{
    int deck = (int)(intptr_t)user;
    if (deck < 0 || deck >= NDECK || g_d[deck].state == ST_HIDDEN) return;
    int bars = g_d[deck].bars > 0 ? g_d[deck].bars : 8;
    int bar = g_d[deck].bar, beat = g_d[deck].beat;
    int last = g_d[deck].state == ST_COUNT && bar == bars;
    uint32_t bg = g_d[deck].flash ? 0x1E7A3A : 0x202028;
    C->fill_rect(d, a.x, a.y, a.w, a.h, bg);

    char t[16];
    if (g_d[deck].state == ST_COUNT) snprintf(t, sizeof t, "%d/%d", bar, bars);
    else snprintf(t, sizeof t, "?");
    if (g_d[deck].numbered) {
        char n[24];
        snprintf(n, sizeof n, "%d %s", deck + 1, t);
        snprintf(t, sizeof t, "%.15s", n);
    }
    uint32_t tc = g_d[deck].state == ST_COUNT ? (last ? 0xF2C230 : 0xFFFFFF) : 0x8A8A8A;
    int text_w = 58;
    if (a.h >= PX + 2 && a.w > text_w) C->draw_text(d, a.x + 5, a.y + (a.h - PX * 3 / 2) / 2 + 1, t, PX, tc);

    /* one cell per bar; the current bar's cell fills beat by beat */
    int x0 = a.x + text_w, x1 = a.x + a.w - 4;
    if (x1 - x0 < bars * 2) return;
    int gap = bars > 8 ? 2 : 3;
    int cw = (x1 - x0 - gap * (bars - 1)) / bars;
    if (cw < 1) return;
    int cy = a.y + 5, ch = a.h - 10;
    if (ch < 2) return;
    for (int i = 1; i <= bars; i++) {
        int x = x0 + (i - 1) * (cw + gap);
        uint32_t on = last ? 0xF2C230 : 0x3CCB5A;
        C->fill_rect(d, x, cy, cw, ch, 0x3A3A44);
        if (g_d[deck].state != ST_COUNT) continue;
        if (i < bar) C->fill_rect(d, x, cy, cw, ch, on);
        else if (i == bar) C->fill_rect(d, x, cy, cw * beat / 4, ch, on);
    }
}

static int input(const az_event *ev, void *user)
{
    (void)ev; (void)user;
    return 0;
}

static int lead_deck(void)
{
    int best = -1;
    unsigned bs = 0;
    for (int d = 0; d < NDECK; d++) {
        unsigned s = __atomic_load_n(&g_start[d], __ATOMIC_RELAXED);
        if (s > bs) { bs = s; best = d; }
    }
    return best;
}

static void phrase_now(void *user)
{
    (void)user;
    int d = lead_deck();
    if (d < 0) {
        if (AZ_HAS(C, banner)) C->banner(MOD_ID, "Phrase coach: no deck is playing", 1500);
        return;
    }
    __atomic_store_n(&g_reset[d], 1, __ATOMIC_RELEASE);
    if (AZ_HAS(C, banner)) {
        char b[48];
        snprintf(b, sizeof b, "Deck %d: phrase starts now", d + 1);
        C->banner(MOD_ID, b, 1200);
    }
}

static void tick(void)
{
    int show = r_show < 0 || C->row_value(r_show) == 1;
    int bars = r_len >= 0 && C->row_value(r_len) == 1 ? 16 : 8;
    int top_pick = r_pos >= 0 && C->row_value(r_pos) == 1;
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
        st[d] = ST_WAIT;
    }
    int numbered = n == 2 && high_in_use;
    int top = numbered || top_pick;

    int x_top = ROW_X0;
    for (int d = 0; d < NDECK; d++) {
        int s = show ? st[d] : ST_HIDDEN;
        if (!top && n == 2 && d >= 2) s = ST_HIDDEN;
        int bar = 0, beat = 0, flash = 0;
        if (s) {
            int known = __atomic_load_n(&g_known[d], __ATOMIC_RELAXED);
            int b = __atomic_load_n(&g_beat[d], __ATOMIC_RELAXED);
            /* before the first play the deck just waits: the counter shows its empty cells */
            if (known) {
                int len = bars * 4;
                int m = ((b % len) + len) % len;
                bar = m / 4 + 1;
                beat = m % 4 + 1;
                flash = m == 0 && __atomic_load_n(&g_start[d], __ATOMIC_RELAXED) != 0;
                s = ST_COUNT;
            } else {
                s = __atomic_load_n(&g_start[d], __ATOMIC_RELAXED) ? ST_LOST : ST_WAIT;
            }
        }
        az_rect r = { 0, 0, 0, 0 };
        if (s) {
            if (top) { r = (az_rect){ x_top, TOP_Y, BOX_W, BOX_H }; x_top += BOX_W + 6; }
            else {
                int y = n == 4 ? ROW4_Y[d] : (phase_on ? ROW2_PHASE_Y[d & 1] : ROW2_Y[d & 1]);
                r = (az_rect){ BOX_X, y + 4, BOX_W, BOX_H };
            }
        }
        int moved = memcmp(&r, &g_d[d].where, sizeof r) != 0;
        int changed = s != g_d[d].state || bar != g_d[d].bar || beat != g_d[d].beat ||
                      flash != g_d[d].flash || bars != g_d[d].bars || numbered != g_d[d].numbered || moved;
        g_d[d].state = s; g_d[d].bar = bar; g_d[d].beat = beat; g_d[d].flash = flash;
        g_d[d].bars = bars; g_d[d].numbered = numbered; g_d[d].where = r;
        if (!box[d]) continue;
        if (s) {
            if (moved) C->surface_place(box[d], r);
            if (!g_d[d].shown) { C->surface_show(box[d], 1); g_d[d].shown = 1; }
            if (changed) C->surface_redraw(box[d]);
        } else if (g_d[d].shown) {
            C->surface_show(box[d], 0);
            g_d[d].shown = 0;
        }
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_count) || !AZ_HAS(C, deck_info) || !AZ_HAS(C, deck_grid) ||
        !AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) ||
        !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, draw_text) || !AZ_HAS(C, fill_rect) ||
        !AZ_HAS(C, thread_start)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck, grid, surface or thread services (1.2.3 or newer)");
        return 2;
    }
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        static const char *const LEN[] = { "8 bars", "16 bars" };
        static const char *const POS[] = { "On the deck", "Screen top" };
        az_row_v1 row;
        memset(&row, 0, sizeof row);
        row.size = sizeof row;
        row.key = "show"; row.label = "Phrase coach"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Bar count per deck. Amber = last bar, green = new phrase.";
        r_show = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "length"; row.label = "Phrase"; row.kind = AZ_ROW_CHOICE;
        row.choices = LEN; row.n_choices = 2; row.default_choice = 0;
        row.help = "Bars in one phrase.";
        r_len = C->register_row(MOD_ID, &row);
        row.key = "position"; row.label = "Position"; row.choices = POS; row.n_choices = 2; row.default_choice = 0;
        row.help = "Left end of each deck's row, or the screen top.";
        r_pos = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "now"; row.label = "Phrase starts now"; row.kind = AZ_ROW_ACTION;
        row.help = "Sets bar 1 on the deck you started last.";
        row.on_activate = phrase_now;
        C->register_row(MOD_ID, &row);
        if (AZ_HAS(C, row_lookup)) r_phase_official = C->row_lookup("phase-meter");
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: defaults (on, 8 bars, on the row)");
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
        box[d] = C->surface_create(MOD_ID, &desc);
        if (!box[d]) { C->log(MOD_ID, "surface %d refused", d + 1); return 3; }
    }
    if (C->thread_start(MOD_ID, "phrase counter", counter, NULL) != 0) {
        C->log(MOD_ID, "thread_start refused: no counting");
        return 4;
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
