/* SPDX-License-Identifier: GPL-3.0-only
 * Pad Lights: pad colours per side for each mode. Pure, host-tested; the mod calls these
 * on the UI thread and the LED filter only copies the result.
 *
 * ERP LED frame (from the emulator): two sides, base 0x00 (left: decks 1/3) and 0x38
 * (right: decks 2/4). Pads 1-8 are RGB triples at base+0x1e+4k; the four mode buttons at
 * base+0x0e+4j. Values are linear light, so colours go through a gamma table.
 */
#ifndef PL_LIGHTS_H
#define PL_LIGHTS_H
#include <math.h>
#include <stdint.h>

enum { PL_OFF = 0, PL_IDLE, PL_BEAT, PL_PHRASE, PL_FLOW };
typedef struct { unsigned char r, g, b; } pl_rgb;

static void pl_hsv(double h, double s, double v, pl_rgb *o)
{
    h = fmod(h, 360.0); if (h < 0) h += 360.0;
    double c = v * s, x = c * (1 - fabs(fmod(h / 60.0, 2) - 1)), m = v - c, R, G, B;
    if (h < 60) { R = c; G = x; B = 0; } else if (h < 120) { R = x; G = c; B = 0; }
    else if (h < 180) { R = 0; G = c; B = x; } else if (h < 240) { R = 0; G = x; B = c; }
    else if (h < 300) { R = x; G = 0; B = c; } else { R = c; G = 0; B = x; }
    o->r = (unsigned char)((R + m) * 255 + 0.5); o->g = (unsigned char)((G + m) * 255 + 0.5); o->b = (unsigned char)((B + m) * 255 + 0.5);
}
/* Camelot number 1..12 -> hue around the wheel (0 when unknown -> cyan). */
static double pl_key_hue(int camelot) { return camelot >= 1 && camelot <= 12 ? (camelot - 1) * 30.0 : 190.0; }

/* One side's 8 pads.
 *  mode: PL_*; playing: that side's selected deck is playing; t: seconds (animation);
 *  beat_phase: 0..1 within the beat (-1 no grid); beats: beats counted since the phrase
 *  anchor (-1 unknown); camelot: the track's Camelot number or 0.
 * Returns 0 when this side should keep the deck's own colours (pass through). */
static int pl_side(int mode, int playing, double t, double beat_phase, long beats, int camelot, pl_rgb pads[8])
{
    if (mode == PL_OFF) return 0;
    if (!playing) {                                       /* idle: a slow wave, every mode */
        for (int k = 0; k < 8; k++) pl_hsv(t * 40.0 + k * 22.0, 1.0, 0.55, &pads[k]);
        return 1;
    }
    if (mode == PL_IDLE) return 0;                        /* playing: your hot cues stay */
    if (beat_phase < 0) return 0;                         /* no grid: nothing to follow */
    double flash = 1.0 - beat_phase;
    flash = flash * flash;                                /* quick decay */
    if (mode == PL_BEAT) {
        double hue = pl_key_hue(camelot);
        for (int k = 0; k < 8; k++) pl_hsv(hue + (k - 3.5) * 6.0, 1.0, 0.12 + 0.88 * flash, &pads[k]);
        return 1;
    }
    /* PL_PHRASE: 32 beats, 4 per pad; the last bar flashes red on every beat. */
    if (beats < 0) return 0;
    int bin = (int)(beats % 32), pad = bin / 4;
    for (int k = 0; k < 8; k++) {
        pl_rgb c = {0, 0, 0};
        if (bin >= 28) { c.r = (unsigned char)(255 * (0.2 + 0.8 * flash)); }
        else if (k < pad) { c.r = 0; c.g = 150; c.b = 255; }
        else if (k == pad) { c.r = 255; c.g = (unsigned char)(160 * flash); c.b = 0; }
        pads[k] = c;
    }
    return 1;
}
/* Counts beats from successive grid phases: a drop of more than half a beat is a new beat.
 * Returns the updated count; a stop or missing grid resets to -1. */
static long pl_count(long count, double last_phase, double phase, int playing)
{
    if (!playing || phase < 0) return -1;
    if (count < 0) return 0;
    if (last_phase >= 0 && phase + 0.5 < last_phase) return count + 1;
    return count;
}
/* PL_FLOW (0.2.0): the pads copy FLOW VJ's light rig — the Shocker's 8 segments, sent by the Mac's light bridge
 * (UDP 9101, "FLP1" + 8 RGB). fresh = a packet arrived in the last 1.5 s; otherwise the idle wave. Both sides
 * show the rig, left to right, on every deck, playing or not (the rig is the show). */
static int pl_flow(int fresh, const pl_rgb flow[8], double t, pl_rgb pads[8])
{
    if (!fresh) { for (int k = 0; k < 8; k++) pl_hsv(t * 40.0 + k * 22.0, 1.0, 0.55, &pads[k]); return 1; }
    for (int k = 0; k < 8; k++) pads[k] = flow[k];
    return 1;
}
/* ---- VJ Pads (0.3.0) + LIGHTS pads (0.4.0): pad pages that play FLOW instead of the deck ----
 * HOT CUE + BEAT JUMP pressed together turns a side's pads into the VJ page (FLOW VJ's Looks); HOT CUE + BEAT LOOP
 * turns them into the LIGHTS page (the lights desk, the same pads as the Maschine MK3). Any pad-mode button on its own
 * turns the page off again (that press goes to the deck as usual), so going from one page to the other is just the
 * other combo. On a page the pads never reach the deck (so SHIFT + pad can't delete a hot cue), and a pad still held
 * when the page closes stays ours until it is released. ERP input byte: pads = bits 0-7, pad-mode buttons = bits 0-3
 * of the byte before (HOT CUE 1, BEAT LOOP 2, SLIP LOOP 4, BEAT JUMP 8). Runs on the input bus thread: no state but s. */
#define PL_VJ_ENTER 0x9u                                   /* HOT CUE + BEAT JUMP: VJ page */
#define PL_LT_ENTER 0x3u                                   /* HOT CUE + BEAT LOOP: LIGHTS page */
#define PL_SM_ENTER 0x5u                                   /* HOT CUE + SLIP LOOP: SAMPLES page (0.5.0) */
enum { PL_PAGE_VJ = 0, PL_PAGE_LIGHTS = 1, PL_PAGE_SAMPLES = 2 };
/* enabled: bit 0 = the VJ + lights pads row, bit 1 = the Sample pads row */
#define PL_EN_VJ 1
#define PL_EN_SAMPLES 2
typedef struct { int on, wait_release, page; unsigned held; } pl_vjs;
static int pl_page_enabled(int page, int enabled) { return page == PL_PAGE_SAMPLES ? (enabled & PL_EN_SAMPLES) != 0 : (enabled & PL_EN_VJ) != 0; }
/* Returns the bits to clear before the deck sees the frame: pads in bits 0-7, mode buttons in 8-11. */
static unsigned pl_vj_in(pl_vjs *s, int enabled, unsigned modes, unsigned pads)
{
    modes &= 0xfu; pads &= 0xffu;
    unsigned eat_modes = 0;
    if (s->on) {
        if (!pl_page_enabled(s->page, enabled)) s->on = 0;
        else if (s->wait_release) { if (!modes) s->wait_release = 0; else eat_modes = modes; }
        else if (modes) s->on = 0;                         /* back to the deck's pads; the press passes */
    } else {
        int page = -1;
        if ((enabled & PL_EN_VJ) && (modes & PL_VJ_ENTER) == PL_VJ_ENTER) page = PL_PAGE_VJ;
        else if ((enabled & PL_EN_VJ) && (modes & PL_LT_ENTER) == PL_LT_ENTER) page = PL_PAGE_LIGHTS;
        else if ((enabled & PL_EN_SAMPLES) && (modes & PL_SM_ENTER) == PL_SM_ENTER) page = PL_PAGE_SAMPLES;
        if (page >= 0) { s->page = page; s->on = 1; s->wait_release = 1; eat_modes = modes; } /* the deck keeps its pad mode */
    }
    if (s->on) s->held = pads;
    else s->held &= pads;                                  /* held page pads stay ours until let go */
    return s->held | eat_modes << 8;
}
/* A page's colours. VJ: pads 1-8 = FLOW's Looks 1-8, each its own dim hue. LIGHTS: pads 1-8 = the desk looks set in
 * FlowVJ's deck-lights.json, warm-to-cool so the two pages never look alike. The pad fired last is bright and pulses on
 * the beat (beat_phase 0..1, -1 = no grid: steady). SHIFT shows the hit layer's colours. A held pad is white.
 * look: 0-7, or -1 none yet. */
static const pl_rgb pl_vj_hits[8] = {
    {255, 255, 255}, {200, 200, 255}, {255, 120, 0}, {0, 200, 255}, {255, 0, 200}, {120, 0, 255}, {0, 255, 80}, {255, 0, 0}
};
/* LIGHTS SHIFT layer (deck-lights.json defaults): BLINDER, STROBE, BLACKOUT, DROP, BUILD 8, ON THE DROP, FULL SHOCK, COLOR BOMB */
static const pl_rgb pl_lt_hits[8] = {
    {255, 255, 255}, {200, 200, 255}, {255, 0, 0}, {255, 170, 0}, {255, 90, 0}, {0, 255, 80}, {0, 200, 255}, {255, 0, 200}
};
static void pl_page(int page, unsigned held, int shift, int look, double beat_phase, pl_rgb pads[8])
{
    double flash = beat_phase < 0 ? 1.0 : (1.0 - beat_phase) * (1.0 - beat_phase);
    for (int k = 0; k < 8; k++) {
        if (held >> k & 1u) { pads[k].r = pads[k].g = pads[k].b = 255; continue; }
        if (shift) { pl_rgb c = page == PL_PAGE_LIGHTS ? pl_lt_hits[k] : pl_vj_hits[k]; pads[k].r = c.r / 3; pads[k].g = c.g / 3; pads[k].b = c.b / 3; continue; }
        double hue = page == PL_PAGE_LIGHTS ? 15.0 + k * 30.0 : k * 45.0;
        pl_hsv(hue, page == PL_PAGE_LIGHTS ? 0.85 : 1.0, k == look ? 0.55 + 0.45 * flash : 0.18, &pads[k]);
    }
}
/* ---- SAMPLES page (0.5.0): pads 1-4 play the bank's four sounds, pads 5-8 pick Bank 1-4 ----
 * The sound comes from AZ-MODS's own Drum Roll sampler (azstem.so, USB folder DRUM ROLLS/Bank N, files named kick,
 * snare, clap, hihat = pads 1-4), mixed into the side's deck. A one-shot is a roll held just long enough for one hit:
 * with a grid on a playing deck the hit waits for the next `sub`-beat boundary, otherwise it fires at once, and the
 * next hit comes `sub` beats after the first. So we let go half a subdivision after the hit we expect: anywhere in
 * that window gives exactly one hit, and letting go never cuts the sound (the tail plays out).
 *  beat_phase 0..1 (-1 = no grid), playing: the deck is moving, bpm: the deck's tempo (<= 0 unknown = 120, as azstem),
 *  sub: 1, 0.5 or 0.25 beats. Returns milliseconds from the press to the release. */
static const float pl_sm_subs[3] = {1.0f, 0.5f, 0.25f};   /* "Sample pads" row 1-3 -> azstem rate index 0-2 */
static int pl_sm_release_ms(double beat_phase, int playing, double bpm, double sub)
{
    if (!(bpm > 0)) bpm = 120.0;
    if (bpm < 40.0) bpm = 40.0; else if (bpm > 220.0) bpm = 220.0;
    double per = 60000.0 / bpm * sub, wait = 0;
    if (playing && beat_phase >= 0) {
        double ph = fmod(beat_phase, sub) / sub;           /* 0..1 of this subdivision gone */
        wait = (1.0 - ph) * per;
    }
    int ms = (int)(wait + per * 0.5 + 0.5);
    return ms < 20 ? 20 : ms;
}
/* SAMPLES page colours. Pads 1-4: four hues, the one played last bright and pulsing; pads 5-8: the banks, the live
 * one bright blue, banks that are on the USB dim, missing ones dark. No sampler (azstem missing or no DRUM ROLLS
 * folder): pads 1-4 glow dim red. bank 1-4 (0 = none yet), banks: bit n = Bank n+1 found, last: 0-3 or -1. */
static void pl_sm_page(unsigned held, int ok, int bank, int banks, int last, double beat_phase, pl_rgb pads[8])
{
    double flash = beat_phase < 0 ? 1.0 : (1.0 - beat_phase) * (1.0 - beat_phase);
    static const double hue[4] = {0.0, 45.0, 120.0, 280.0};
    for (int k = 0; k < 8; k++) {
        if (held >> k & 1u) { pads[k].r = pads[k].g = pads[k].b = 255; continue; }
        if (k < 4) {
            if (!ok) { pads[k].r = 60; pads[k].g = pads[k].b = 0; continue; }
            pl_hsv(hue[k], 1.0, k == last ? 0.55 + 0.45 * flash : 0.22, &pads[k]);
        } else {
            int b = k - 3;
            if (b == bank) { pads[k].r = 40; pads[k].g = 120; pads[k].b = 255; }
            else if (banks >> (k - 4) & 1) { pads[k].r = 10; pads[k].g = 30; pads[k].b = 70; }
            else pads[k].r = pads[k].g = pads[k].b = 0;
        }
    }
}
#endif
