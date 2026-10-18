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

enum { PL_OFF = 0, PL_IDLE, PL_BEAT, PL_PHRASE };
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
#endif
