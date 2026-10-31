/* SPDX-License-Identifier: GPL-3.0-only
 *
 * fx.h (flow-beat 0.5.3): AZ-MODS 1.2.6+ performance state for FLOW VJ, per deck:
 *
 *   Stem FX     which of pads 5-8 (VOCAL ECHO, INST ECHO, INST BRAKE, DRUMS ECHO) is held on.
 *               From azstem.so's exported azstem_stem_fx_on(deck) (AZ-MODS 1.2.6+, not in the
 *               SDK: looked up with dlsym; missing = unknown, -1).
 *   Drum Roll   armed on the deck (SLIP LOOP, pad 8 green), HOLD (pad 7) and the selected
 *               sample (pads 1-4). AZ-MODS keeps these inside azmod.so with no export, so they
 *               are read off the pad LED frame it paints (bus_interpose.c drum_hold_led):
 *               pad 8 {0,FF,0} = on, pads 5-6 dark, pad 7 blue 0x22 = HOLD off, blue flashing
 *               0xFF / 0x00 = HOLD on, the selected sample pulses {37,FF,0} / {06,22,0}.
 *   Loop        from EP147's own deck button LEDs in the same frame (measured on the emulator,
 *               xdj-az-emu az-preview controls.rs DECK_LEDS): RELOOP/EXIT is lit while the deck
 *               holds a loop, LOOP IN and OUT blink while a loop plays or is being set.
 *
 * The LED frame shows the two decks the sides drive now (deck 3/4 after DECK 3/4 select), so
 * the drum and loop fields of a deck that is not on a side are unknown (-1).
 *
 * The bus thread (fx_led_take) packs each side into one atomic word plus a count of IN/OUT
 * light changes; the worker (fx_led_read, fx_side_deck) reads them. No locks, no syscalls on
 * the bus thread. Header-only so tests/test_fx.c compiles it as it ships. */
#ifndef FLOW_BEAT_FX_H
#define FLOW_BEAT_FX_H
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define FX_LED_LEN 0x78        /* the ERP LED frame's CRC'd part: every byte read here is inside it */
#define FX_SIDE_STRIDE 0x38    /* the right side's LEDs follow the left side's this many bytes on */
#define FX_PAD_OFF 0x1e        /* the 8 pads, 4 bytes each {R,G,B,-} */
#define FX_SEL_OFF 0x0c        /* deck select: low 4 bits */
#define FX_RELOOP_OFF 0x09     /* RELOOP/EXIT: 2-bit level at shift 0 */
#define FX_INOUT_OFF 0x0a      /* LOOP IN/CUE at shift 6, LOOP OUT at shift 4 */

/* One side as the bus thread last saw it. */
typedef struct {
    int valid;   /* a frame has been seen */
    int sel;     /* 1 = this side drives deck 1/2, 2 = deck 3/4, 0 = unknown (taken as 1/2) */
    int drum;    /* Drum Roll on (armed, pad 8 green) */
    int hold;    /* Drum Roll HOLD on */
    int smp;     /* the selected sample pad 0-3, -1 = not known yet */
    int reloop;  /* RELOOP/EXIT light level 0-3 */
    int in, out; /* LOOP IN/CUE and LOOP OUT light levels 0-3 */
} fx_side;

static inline int fx_selection(unsigned char v)
{
    unsigned p = v & 3u, s = (v >> 2) & 3u;   /* the same decoding as Pad Lights */
    return p == 2 && s == 1 ? 1 : p == 1 && s == 2 ? 2 : 0;
}

static inline int fx_px(const unsigned char *p, unsigned r, unsigned g, unsigned b)
{
    return p[0] == r && p[1] == g && p[2] == b;
}

/* Decode one side of an LED frame. prev_smp: the side's last known sample (kept while the
 * selected pad is in the dim half of its pulse). */
static inline void fx_side_parse(const unsigned char *f, int side, int prev_smp, fx_side *o)
{
    const unsigned char *b = f + side * FX_SIDE_STRIDE;
    const unsigned char *pad = b + FX_PAD_OFF;
    memset(o, 0, sizeof *o);
    o->valid = 1;
    o->sel = fx_selection(b[FX_SEL_OFF]);
    o->reloop = b[FX_RELOOP_OFF] & 3;
    o->in = (b[FX_INOUT_OFF] >> 6) & 3;
    o->out = (b[FX_INOUT_OFF] >> 4) & 3;
    const unsigned char *p7 = pad + 6 * 4;
    o->drum = fx_px(pad + 7 * 4, 0x00, 0xFF, 0x00) && fx_px(pad + 4 * 4, 0, 0, 0) &&
              fx_px(pad + 5 * 4, 0, 0, 0) && p7[0] == 0 && p7[1] == 0 &&
              (p7[2] == 0x22 || p7[2] == 0xFF || p7[2] == 0x00);
    o->hold = o->drum && p7[2] != 0x22;
    o->smp = -1;
    if (o->drum) {
        o->smp = prev_smp;
        for (int i = 0; i < 4; i++)
            if (fx_px(pad + i * 4, 0x37, 0xFF, 0x00)) o->smp = i;
    }
}

/* Packed: bit 0 valid, 1-2 sel, 3 drum, 4 hold, 5-7 smp+1, 8-9 reloop, 10-11 in, 12-13 out. */
static inline uint32_t fx_pack(const fx_side *s)
{
    return (uint32_t)(s->valid & 1) | (uint32_t)(s->sel & 3) << 1 | (uint32_t)(s->drum & 1) << 3 |
           (uint32_t)(s->hold & 1) << 4 | (uint32_t)((s->smp + 1) & 7) << 5 | (uint32_t)(s->reloop & 3) << 8 |
           (uint32_t)(s->in & 3) << 10 | (uint32_t)(s->out & 3) << 12;
}
static inline void fx_unpack(uint32_t w, fx_side *s)
{
    s->valid = w & 1;
    s->sel = (w >> 1) & 3;
    s->drum = (w >> 3) & 1;
    s->hold = (w >> 4) & 1;
    s->smp = (int)((w >> 5) & 7) - 1;
    s->reloop = (w >> 8) & 3;
    s->in = (w >> 10) & 3;
    s->out = (w >> 12) & 3;
}

typedef struct {
    _Atomic uint32_t word[2];     /* fx_pack of each side */
    _Atomic unsigned blinks[2];   /* LOOP IN / OUT light changes seen on each side */
} fx_leds;

/* Bus thread, every LED frame (after AZ-MODS and the mods before us painted it). */
static inline void fx_led_take(fx_leds *L, const unsigned char *f, int len)
{
    if (!f || len < FX_LED_LEN) return;
    for (int side = 0; side < 2; side++) {
        fx_side was, now;
        fx_unpack(atomic_load_explicit(&L->word[side], memory_order_relaxed), &was);
        fx_side_parse(f, side, was.drum ? was.smp : -1, &now);
        if (was.valid && (was.in != now.in || was.out != now.out))
            atomic_fetch_add_explicit(&L->blinks[side], 1, memory_order_relaxed);
        atomic_store_explicit(&L->word[side], fx_pack(&now), memory_order_release);
    }
}

/* ---- the worker: blink detection and the per-deck view ---- */

/* A loop plays while LOOP IN / OUT keep changing: "blinking" = a change within the window,
 * which follows the measured blink (2.5 half-periods, 250..1200 ms). */
typedef struct {
    unsigned blinks;
    double last_ms, half_ms;
} fx_blink;

static inline int fx_blinking(fx_blink *b, unsigned blinks, double now_ms)
{
    if (blinks != b->blinks) {
        double gap = b->last_ms > 0 ? now_ms - b->last_ms : 0;
        if (gap > 30 && gap < 1500) b->half_ms = b->half_ms > 0 ? 0.7 * b->half_ms + 0.3 * gap : gap;
        b->blinks = blinks;
        b->last_ms = now_ms;
    }
    double win = b->half_ms > 0 ? 2.5 * b->half_ms : 900;
    if (win < 250) win = 250;
    if (win > 1200) win = 1200;
    return b->last_ms > 0 && now_ms - b->last_ms < win;
}

/* One deck as sent: -1 = unknown. */
typedef struct {
    int sfx;    /* bit f = Stem FX pad 5+f held on; -1 = this AZ-MODS has no Stem FX export */
    int drum, hold, smp;
    int loop;   /* 0 none, 1 a loop is held (RELOOP lit, not looping), 2 looping, 3 a loop being set */
} fx_deck;

/* Which deck (0..3) a side shows. */
static inline int fx_side_deck(const fx_side *s, int side)
{
    return side + (s->sel == 2 ? 2 : 0);
}

static inline int fx_loop_of(const fx_side *s, int blinking)
{
    if (blinking) return s->reloop ? 2 : 3;
    return s->reloop ? 1 : 0;
}

/* The four decks from both sides (sfx filled by the caller). */
static inline void fx_decks(const fx_side sides[2], const int blinking[2], fx_deck out[4])
{
    for (int d = 0; d < 4; d++) {
        out[d].drum = out[d].hold = out[d].smp = out[d].loop = -1;
    }
    for (int side = 0; side < 2; side++) {
        const fx_side *s = &sides[side];
        if (!s->valid) continue;
        fx_deck *o = &out[fx_side_deck(s, side)];
        o->drum = s->drum;
        o->hold = s->hold;
        o->smp = s->drum ? s->smp : -1;
        o->loop = fx_loop_of(s, blinking[side]);
    }
}

static inline int fx_deck_same(const fx_deck *a, const fx_deck *b)
{
    return a->sfx == b->sfx && a->drum == b->drum && a->hold == b->hold && a->smp == b->smp && a->loop == b->loop;
}

/* {"flowbeat":1,"type":"fx","seq":N,"decks":[{"d":1,"sfx":0,"drum":0,"hold":0,"smp":-1,"loop":0},..4]} */
#include <stdio.h>
static inline int fx_json(char *buf, int cap, unsigned seq, const fx_deck d[4])
{
    int n = snprintf(buf, (size_t)cap, "{\"flowbeat\":1,\"type\":\"fx\",\"seq\":%u,\"decks\":[", seq);
    for (int i = 0; i < 4 && n < cap; i++)
        n += snprintf(buf + n, (size_t)(cap - n), "%s{\"d\":%d,\"sfx\":%d,\"drum\":%d,\"hold\":%d,\"smp\":%d,\"loop\":%d}",
                      i ? "," : "", i + 1, d[i].sfx, d[i].drum, d[i].hold, d[i].smp, d[i].loop);
    if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), "]}");
    return n < cap ? n : cap - 1;
}
#endif
