/* SPDX-License-Identifier: GPL-3.0-only
 *
 * lights.h (flow-beat 0.6.0, "Lights from this deck"): the deck runs a small light show itself
 * and sends it as Art-Net (ArtDmx, UDP 6454) to an Art-Net node, so the lights play with no
 * computer at the gig. FLOW VJ's lights desk stays the big show; this is the laptop-free one.
 *
 *   colours   two-colour palettes (A main, B accent). "Cycle" moves to the next palette every
 *             16 bars and swaps A/B every 8; the others hold one palette.
 *   beat      the 4BAR's pods chase the beat of the bar, the Shocker's 8 segments chase eighths,
 *             each Spectra tower runs a bright band up the tower on every beat, pars pump with
 *             the playing deck's bass (its own audio, before the mixer).
 *   breakdown the bass drops away for 2 bars: everything slows to a dim A->B fade.
 *   drop      the bass comes back after a breakdown: one beat of full white (the hit), then
 *             4 bars at full level with the band running twice a beat.
 *   mixer     the playing channel's fader dims the rig (25 % floor), like FLOW's MIXER FOLLOW.
 *   idle      nothing playing: a slow 20 % breath in the palette.
 *   panel     0.6.2, the FLOW Lights panel (panel.h): follow beat off (a steady wash), drop flash off,
 *             intensity, blackout, white / accent hits, DROP now, and a held white flicker (8 a second).
 *
 * Fixtures are the ones FLOW VJ's dmx_patch.json uses (same channel maps as lightbridge.py),
 * listed as "type@address[:flip]" separated by commas in flow-beat:artnet_patch. Anything not
 * listed (hazer, Scorpion laser) is never sent: those channels stay 0. No fixture strobe
 * channel is ever used: the flashes are the one-beat white hit on a drop and the panel's hits and
 * held STROBE, made with the colour channels.
 *
 * Pure code (no sockets, no clock): flow-beat.c feeds it a step at a time and sends the bytes.
 * Header-only so tests/test_lights.c compiles it as it ships. */
#ifndef FLOW_BEAT_LIGHTS_H
#define FLOW_BEAT_LIGHTS_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LT_PORT 6454
#define LT_MAX_FIX 24
#define LT_PKT 530                      /* ArtDmx header 18 + 512 slots */

/* the default patch: MLT's rig (FLOW VJ dmx_patch.json, 2026-10-17), less hazer and laser */
#define LT_DEFAULT_PATCH "4bar_tri_15@59,slimpar_h6_8@74,slimpar_h6_8@82,ezpar_t6_3@90," \
                         "colorband_spectra_164@104,colorband_spectra_164@268," \
                         "shocker_panel_47@432,ezpar_t6_3@479"

enum { LT_4BAR = 1, LT_SLIMPAR, LT_EZPAR3, LT_EZPAR7, LT_RGB3, LT_SPECTRA, LT_SHOCK47, LT_SHOCK18 };
static const struct { const char *name; int kind, span; } LT_TYPES[] = {
    { "4bar_tri_15", LT_4BAR, 15 },          { "slimpar_h6_8", LT_SLIMPAR, 8 },
    { "ezpar_t6_3", LT_EZPAR3, 3 },          { "ezpar_t6_7", LT_EZPAR7, 7 },
    { "rgb3", LT_RGB3, 3 },                  { "colorband_spectra_164", LT_SPECTRA, 164 },
    { "shocker_panel_47", LT_SHOCK47, 47 },  { "shocker_panel_18", LT_SHOCK18, 18 },
};

typedef struct { int kind, addr, span, flip, nth; } lt_fix;   /* nth: which one of its kind (0, 1, ..) */

/* "type@addr[:flip],..." -> fix[]; returns how many, and in *bad the first entry it skipped (or -1). */
static inline int lt_parse_patch(const char *s, lt_fix *fix, int max, int *bad)
{
    int n = 0, idx = 0;
    if (bad) *bad = -1;
    while (s && *s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        char item[64];
        int k = 0;
        while (*s && *s != ',' && k < (int)sizeof item - 1) item[k++] = *s++;
        while (*s && *s != ',') s++;
        while (k > 0 && item[k - 1] == ' ') k--;
        item[k] = 0;
        char *at = strchr(item, '@');
        int ok = 0;
        if (at) {
            *at = 0;
            char *opt = strchr(at + 1, ':');
            if (opt) *opt++ = 0;
            char *end = NULL;
            long addr = strtol(at + 1, &end, 10);
            for (size_t t = 0; t < sizeof LT_TYPES / sizeof LT_TYPES[0]; t++) {
                if (strcmp(item, LT_TYPES[t].name) != 0) continue;
                if (end == at + 1 || *end || addr < 1 || addr + LT_TYPES[t].span - 1 > 512) break;
                if (n >= max) break;
                fix[n].kind = LT_TYPES[t].kind;
                fix[n].addr = (int)addr;
                fix[n].span = LT_TYPES[t].span;
                fix[n].flip = opt && strcmp(opt, "flip") == 0;
                fix[n].nth = 0;
                for (int j = 0; j < n; j++) if (fix[j].kind == fix[n].kind) fix[n].nth++;
                n++;
                ok = 1;
                break;
            }
        }
        if (!ok && bad && *bad < 0) *bad = idx;
        idx++;
    }
    return n;
}

/* ---- Art-Net ------------------------------------------------------------------------------ */

/* An ArtDmx packet (Art-Net 4, OpCode 0x5000) for one universe of 512 slots. port_addr is the
 * 15-bit Port-Address (Net << 8 | Sub-Net << 4 | Universe). Returns its length. */
static inline int lt_artdmx(uint8_t *p, uint8_t seq, int port_addr, const uint8_t *dmx)
{
    memcpy(p, "Art-Net\0", 8);
    p[8] = 0x00; p[9] = 0x50;            /* OpCode, little endian */
    p[10] = 0; p[11] = 14;               /* protocol version 14, big endian */
    p[12] = seq;                         /* 1..255, 0 = sequencing off */
    p[13] = 0;                           /* physical port */
    p[14] = (uint8_t)(port_addr & 0xff); /* SubUni */
    p[15] = (uint8_t)((port_addr >> 8) & 0x7f);   /* Net */
    p[16] = 0x02; p[17] = 0x00;          /* length 512, big endian */
    memcpy(p + 18, dmx, 512);
    return LT_PKT;
}

/* Is it an ArtDmx packet? Then its Port-Address, else -1. */
static inline int lt_artdmx_universe(const uint8_t *p, int n)
{
    if (n < 18 || memcmp(p, "Art-Net\0", 8) != 0 || p[8] != 0x00 || p[9] != 0x50) return -1;
    return ((p[15] & 0x7f) << 8) | p[14];
}

/* ---- the show ----------------------------------------------------------------------------- */

typedef struct { uint8_t r, g, b; } lt_rgb;
/* palettes: A, B. 0 riso (MLT's house look: riso red / teal), 1 FLOW, 2 sunset, 3 ice, 4 white (0.6.2) */
#define LT_NPAL 5
static const lt_rgb LT_PAL[LT_NPAL][2] = {
    { { 255, 36, 24 }, { 0, 178, 168 } },
    { { 230, 0, 200 }, { 0, 200, 255 } },
    { { 255, 110, 0 }, { 120, 0, 255 } },
    { { 0, 70, 255 }, { 150, 230, 255 } },
    { { 255, 255, 255 }, { 255, 170, 90 } },          /* 0.6.2: white / warm white */
};
#define LT_CHOICES { "Cycle", "Riso red/teal", "FLOW pink/blue", "Sunset", "Ice", "White" }
#define LT_NCHOICES 6

typedef struct {
    int playing;        /* a deck plays */
    double bpm;         /* the playing deck's tempo */
    double ph;          /* its beat phase 0..1, -1 unknown */
    int beat;           /* 1 on the step a new beat starts */
    int bar_beat;       /* 0..3 (0 = the "1"), -1 unknown */
    double bass;        /* its bass level now 0..1 (peak since the last step) */
    double dim;         /* 0..1 master level (fader), 1 if unknown */
    int look;           /* the colour row: 0 Cycle, 1.. a fixed palette */
    double dt_ms;       /* time since the last step */
    /* 0.6.2, the FLOW Lights panel. All 0 = the show exactly as before. */
    int no_follow;      /* 1: no beat chase or bass pump: a steady wash that slowly blends A and B */
    int no_dropflash;   /* 1: a drop is still found, but gives no white hit and no 4 bars at full */
    double cut;         /* 0..1 taken off the whole rig (the panel's INTENSITY is 1 - cut) */
    int black;          /* 1: everything dark */
    int strobe;         /* 1: white flicker while held (8 a second, 40 ms on), over everything but black */
} lt_in;

typedef struct {
    double t_ms;        /* show clock */
    double slow;        /* the bass level the track normally plays at (frozen in breakdowns) */
    double beat_peak;   /* the loudest bass in the current beat */
    int low_beats;      /* beats in a row well under it */
    int breakdown;      /* 1 while the bass is away */
    int beats;          /* beats since play started */
    int bars;           /* bars since play started (counted on the "1", or every 4 beats) */
    double hit_ms;      /* >0: the white hit, ms left */
    int drop_beats;     /* >0: drop mode, in beats left */
    double fade;        /* breakdown cross-fade 0..1 */
    int drops;          /* drops seen (for logs and tests) */
    double flash_ms;    /* 0.6.2: >0 a hit from the panel, ms left (also when nothing plays) */
    int flash_accent;   /* that hit is the accent colour B, not white */
} lt_state;

/* 0.6.2: the panel's hits. One beat long (250 ms when the tempo is unknown). */
static inline double lt_beat_ms(double bpm) { return bpm > 40 && bpm < 300 ? 60000.0 / bpm : 250.0; }
static inline void lt_flash(lt_state *s, double bpm, int accent)
{
    s->flash_ms = lt_beat_ms(bpm);
    s->flash_accent = accent;
}
/* DROP from the panel: the white hit now, and 4 bars at full if a deck plays. */
static inline void lt_drop_now(lt_state *s, double bpm, int playing)
{
    lt_flash(s, bpm, 0);
    if (playing) { s->drop_beats = 16; s->breakdown = 0; s->low_beats = 0; s->fade = 0; }
}

static inline void lt_init(lt_state *s) { memset(s, 0, sizeof *s); }

static inline double lt_clamp(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline lt_rgb lt_mix(lt_rgb a, lt_rgb b, double k)
{
    k = lt_clamp(k);
    lt_rgb o = { (uint8_t)lrint(a.r + (b.r - a.r) * k), (uint8_t)lrint(a.g + (b.g - a.g) * k),
                 (uint8_t)lrint(a.b + (b.b - a.b) * k) };
    return o;
}
static inline lt_rgb lt_scale(lt_rgb a, double k)
{
    k = lt_clamp(k);
    lt_rgb o = { (uint8_t)lrint(a.r * k), (uint8_t)lrint(a.g * k), (uint8_t)lrint(a.b * k) };
    return o;
}
static inline void lt_put(uint8_t *dmx, const lt_fix *f, int ch, int v)
{
    if (ch < 1 || ch > f->span) return;
    int a = f->addr + ch - 2;          /* addr is 1-based, ch is 1-based */
    if (a < 0 || a > 511) return;
    dmx[a] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* Updates the beat / breakdown / drop state for one step. The drop is caught on the step its
 * kick's bass arrives (not at the next beat), so the white hit lands on the drop. */
static inline void lt_track(lt_state *s, const lt_in *in)
{
    s->t_ms += in->dt_ms;
    if (s->hit_ms > 0) s->hit_ms -= in->dt_ms;
    if (s->flash_ms > 0) s->flash_ms -= in->dt_ms;
    if (!in->playing) {
        s->beats = s->bars = s->low_beats = s->breakdown = s->drop_beats = 0;
        s->hit_ms = 0;
        s->beat_peak = 0;
        s->fade += (0 - s->fade) * lt_clamp(in->dt_ms / 800.0);
        return;
    }
    if (in->bass > s->beat_peak) s->beat_peak = in->bass;
    if (s->breakdown && s->slow > 0.02 && in->bass >= 0.7 * s->slow) {
        double bpm = in->bpm > 40 && in->bpm < 300 ? in->bpm : 120;
        if (!in->no_dropflash) {
            s->hit_ms = 60000.0 / bpm;                 /* one beat of white */
            s->drop_beats = 16;                        /* then 4 bars at full */
        }
        s->breakdown = 0;
        s->low_beats = 0;
        s->drops++;
        s->fade = 0;                                   /* the drop snaps back, no fade */
    }
    if (in->beat) {
        double peak = s->beat_peak;
        s->beat_peak = in->bass;
        s->beats++;
        if (in->bar_beat == 0 || (in->bar_beat < 0 && s->beats % 4 == 1)) s->bars++;
        if (s->drop_beats > 0) s->drop_beats--;
        if (s->slow < 0.02) {
            s->slow = peak;                            /* first beats: learn the level */
        } else if (peak < 0.4 * s->slow) {
            if (++s->low_beats >= 8) s->breakdown = 1; /* 2 bars without the bass */
        } else {
            s->low_beats = 0;
            if (!s->breakdown) s->slow += (peak - s->slow) * 0.12;   /* follow the track's level */
        }
    }
    double want = s->breakdown ? 1.0 : 0.0;
    s->fade += (want - s->fade) * lt_clamp(in->dt_ms / 1500.0);
}

/* The palette for now. */
static inline void lt_palette(const lt_state *s, int look, lt_rgb *a, lt_rgb *b)
{
    int p = look >= 1 && look <= LT_NPAL ? look - 1 : (s->bars / 16) % 4;   /* Cycle skips White */
    int swap = look == 0 && (s->bars / 8) % 2 == 1;
    *a = LT_PAL[p][swap];
    *b = LT_PAL[p][!swap];
}

/* 0.6.2: scales only the colour and white channels by k (0..1): dimmer, mode and master
 * channels stay as they are (the Shocker's background dimmer runs backwards, 255 = 0 %). */
static inline void lt_master(const lt_fix *fix, int nfix, uint8_t *dmx, double k)
{
    k = lt_clamp(k);
    for (int i = 0; i < nfix; i++) {
        const lt_fix *f = &fix[i];
        int a = 0, b = -1, c = 0, d = -1;              /* channels a..b and c..d */
        switch (f->kind) {
        case LT_4BAR: a = 4; b = 15; break;
        case LT_SLIMPAR: a = 2; b = 4; c = 6; d = 6; break;
        case LT_EZPAR3: case LT_RGB3: case LT_EZPAR7: a = 1; b = 3; break;
        case LT_SPECTRA: a = 1; b = 160; break;
        case LT_SHOCK47: a = 15; b = 46; break;
        case LT_SHOCK18: a = 3; b = 5; c = 12; d = 12; break;
        }
        for (int ch = 1; ch <= f->span; ch++) {
            if (!((ch >= a && ch <= b) || (ch >= c && ch <= d))) continue;
            int at = f->addr + ch - 2;
            if (at >= 0 && at < 512) dmx[at] = (uint8_t)lrint(dmx[at] * k);
        }
    }
}

/* One frame of the show into dmx[512] (cleared first). */
static inline void lt_render(const lt_state *s, const lt_in *in, const lt_fix *fix, int nfix, uint8_t *dmx)
{
    memset(dmx, 0, 512);
    lt_rgb A, B;
    lt_palette(s, in->look, &A, &B);
    if (in->black) return;
    double dim = in->dim < 0 ? 1 : 0.25 + 0.75 * lt_clamp(in->dim);
    double ph = in->ph >= 0 ? in->ph : fmod(s->t_ms / 500.0, 1.0);
    int bb = in->bar_beat >= 0 ? in->bar_beat : (s->beats + 3) % 4;
    int strobe_on = in->strobe && fmod(s->t_ms, 125.0) < 40.0;
    if (in->strobe && !strobe_on) return;                         /* the dark half of the flicker */
    int hit = (in->playing && s->hit_ms > 0) || s->flash_ms > 0 || strobe_on;
    lt_rgb hitc = { 255, 255, 255 };
    int white = 1;                                                /* fixture white channels on the hit */
    if (!strobe_on && !(in->playing && s->hit_ms > 0) && s->flash_ms > 0 && s->flash_accent) { hitc = B; white = 0; }
    int drop = in->playing && s->drop_beats > 0;
    int wash = !in->playing || in->no_follow;                     /* no chase: the slow blend */
    double brk = s->fade;                              /* 1 = full breakdown look */
    double breath = 0.5 - 0.5 * cos(s->t_ms / 4000.0 * 2 * M_PI);
    double pump = lt_clamp(0.55 + 0.6 * (s->slow > 0.02 ? in->bass / s->slow : 0));

    for (int i = 0; i < nfix; i++) {
        const lt_fix *f = &fix[i];
        lt_rgb c;
        double lvl;
        if (hit) {
            c = hitc;
            lvl = 1;
        } else if (!in->playing) {                     /* idle: slow breath */
            c = lt_mix(A, B, breath);
            lvl = 0.08 + 0.12 * breath;
        } else if (in->no_follow) {                    /* follow off: a steady wash */
            c = lt_mix(A, B, breath);
            lvl = 0.8;
        } else {
            c = (f->nth % 2) ? B : A;
            lvl = drop ? 1.0 : pump * (1 - brk) + 0.35 * brk;
            if (brk > 0) c = lt_mix(c, lt_mix(A, B, breath), brk);
        }
        lvl *= hit ? 1 : dim;
        switch (f->kind) {
        case LT_4BAR: {                               /* ch1 RGB mode, 2 dimmer, 3 strobe, pods 4-15 */
            lt_put(dmx, f, 1, 0); lt_put(dmx, f, 2, 255); lt_put(dmx, f, 3, 0);
            for (int p = 0; p < 4; p++) {
                lt_rgb pc;
                if (wash || hit) pc = lt_scale(c, lvl);
                else {
                    int on = p == (f->flip ? 3 - bb : bb);
                    double k = on ? 1.0 - 0.5 * ph : 0.25;     /* the beat's pod, fading through the beat */
                    k = k * (1 - brk) + 0.3 * brk;
                    pc = lt_scale(on ? B : A, k * (drop ? 1 : dim));
                    if (brk > 0) pc = lt_scale(c, 0.3 * dim);
                }
                lt_put(dmx, f, 4 + 3 * p, pc.r); lt_put(dmx, f, 5 + 3 * p, pc.g); lt_put(dmx, f, 6 + 3 * p, pc.b);
            }
            break;
        }
        case LT_SLIMPAR: {                            /* 1 master, 2-4 RGB, 5 amber, 6 white, 7 UV, 8 strobe */
            lt_rgb o = lt_scale(c, lvl);
            lt_put(dmx, f, 1, 255);
            lt_put(dmx, f, 2, o.r); lt_put(dmx, f, 3, o.g); lt_put(dmx, f, 4, o.b);
            lt_put(dmx, f, 6, hit && white ? 255 : 0);
            break;
        }
        case LT_EZPAR3: case LT_RGB3: case LT_EZPAR7: {
            lt_rgb o = lt_scale(c, lvl);
            lt_put(dmx, f, 1, o.r); lt_put(dmx, f, 2, o.g); lt_put(dmx, f, 3, o.b);
            if (f->kind == LT_EZPAR7) lt_put(dmx, f, 7, 255);     /* dimmer open */
            break;
        }
        case LT_SPECTRA: {                            /* 40 RGBW pixels, 161 strobe, 162 dimmer, 163 tilt, 164 speed */
            double band = drop ? fmod(ph * 2, 1.0) : ph;          /* where the band is, 0 bottom .. 1 top */
            for (int p = 0; p < 40; p++) {
                double y = (f->flip ? 39 - p : p) / 39.0;
                lt_rgb pc;
                double k;
                if (wash || hit) { pc = c; k = lvl; }
                else {
                    pc = lt_mix(A, B, y);                         /* A at the bottom, B at the top */
                    if (f->nth % 2) pc = lt_mix(B, A, y);
                    double d = fabs(y - band);
                    double glow = d < 0.12 ? 1 - d / 0.12 : 0;
                    k = (0.3 + 0.7 * glow * (1 - brk)) * (drop ? 1 : dim);
                    if (brk > 0) { pc = lt_mix(pc, c, brk); k = k * (1 - brk) + 0.3 * brk * dim; }
                }
                lt_rgb o = lt_scale(pc, k);
                lt_put(dmx, f, 4 * p + 1, o.r); lt_put(dmx, f, 4 * p + 2, o.g); lt_put(dmx, f, 4 * p + 3, o.b);
                lt_put(dmx, f, 4 * p + 4, hit && white ? 255 : 0);
            }
            lt_put(dmx, f, 161, 0); lt_put(dmx, f, 162, 255);
            lt_put(dmx, f, 163, 128); lt_put(dmx, f, 164, 200);
            break;
        }
        case LT_SHOCK47: {                            /* masters, bg dimmer 255 = 0 %, segments 15-38 RGB, 39-46 W */
            lt_put(dmx, f, 1, 255); lt_put(dmx, f, 2, 255); lt_put(dmx, f, 8, 255); lt_put(dmx, f, 9, 255);
            int lead = (bb * 2 + (ph >= 0.5)) % 8;
            for (int k = 0; k < 8; k++) {
                lt_rgb pc;
                if (wash || hit) pc = lt_scale(c, lvl);
                else {
                    int seg = f->flip ? 7 - k : k;
                    double kk = seg == lead ? 1.0 : 0.2;
                    kk = kk * (1 - brk) + 0.3 * brk;
                    pc = lt_scale(seg == lead ? B : A, kk * (drop ? 1 : dim));
                    if (brk > 0) pc = lt_scale(c, 0.3 * dim);
                }
                lt_put(dmx, f, 15 + 3 * k, pc.r); lt_put(dmx, f, 16 + 3 * k, pc.g); lt_put(dmx, f, 17 + 3 * k, pc.b);
                lt_put(dmx, f, 39 + k, hit && white ? 255 : 0);
            }
            break;
        }
        case LT_SHOCK18: {                            /* 1-2 masters, 3-5 RGB, 11 bg dimmer 255 = 0 %, 12 white */
            lt_rgb o = lt_scale(c, lvl);
            lt_put(dmx, f, 1, 255); lt_put(dmx, f, 2, 255);
            lt_put(dmx, f, 3, o.r); lt_put(dmx, f, 4, o.g); lt_put(dmx, f, 5, o.b);
            lt_put(dmx, f, 11, 255); lt_put(dmx, f, 12, hit && white ? 255 : 0);
            break;
        }
        }
    }
    if (in->cut > 0) lt_master(fix, nfix, dmx, 1 - in->cut);
}

#endif
