/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../lights.h"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
int main(void)
{
    pl_rgb p[8];
    /* Off: never overrides. Idle show: only when not playing. */
    CHECK(!pl_side(PL_OFF, 0, 1, -1, -1, 0, p) && !pl_side(PL_OFF, 1, 1, 0.2, 5, 3, p));
    CHECK(pl_side(PL_IDLE, 0, 1, -1, -1, 0, p) && !pl_side(PL_IDLE, 1, 1, 0.2, 5, 3, p));
    CHECK(pl_side(PL_BEAT, 0, 1, -1, -1, 0, p) && pl_side(PL_PHRASE, 0, 1, -1, -1, 0, p));  /* idle side animates in every mode */
    /* Idle wave moves over time. */
    pl_rgb a[8], b[8]; pl_side(PL_IDLE, 0, 0.0, -1, -1, 0, a); pl_side(PL_IDLE, 0, 1.0, -1, -1, 0, b);
    CHECK(a[0].r != b[0].r || a[0].g != b[0].g || a[0].b != b[0].b);
    /* Playing without a grid: keep the deck's colours. */
    CHECK(!pl_side(PL_BEAT, 1, 1, -1, 4, 3, p) && !pl_side(PL_PHRASE, 1, 1, 0.3, -1, 3, p));
    /* Beat pulse: brightest at the beat, dim before the next. */
    pl_side(PL_BEAT, 1, 1, 0.0, 4, 8, a); pl_side(PL_BEAT, 1, 1, 0.9, 4, 8, b);
    CHECK(a[3].r + a[3].g + a[3].b > 3 * (b[3].r + b[3].g + b[3].b));
    /* Key colours differ by key. */
    pl_rgb k1[8], k7[8]; pl_side(PL_BEAT, 1, 1, 0.0, 0, 1, k1); pl_side(PL_BEAT, 1, 1, 0.0, 0, 7, k7);
    CHECK(k1[3].r != k7[3].r || k1[3].g != k7[3].g || k1[3].b != k7[3].b);
    /* Phrase: 4 beats per pad, past pads blue, current orange, last bar red on all pads. */
    pl_side(PL_PHRASE, 1, 1, 0.0, 9, 0, p);                 /* beat 9 -> pad 2 */
    CHECK(p[0].b == 255 && p[1].b == 255 && p[2].r == 255 && p[3].r == 0 && p[3].g == 0 && p[3].b == 0);
    pl_side(PL_PHRASE, 1, 1, 0.0, 29, 0, p);                /* last bar */
    for (int k = 0; k < 8; k++) CHECK(p[k].r > 200 && p[k].g == 0 && p[k].b == 0);
    pl_side(PL_PHRASE, 1, 1, 0.0, 32, 0, p); CHECK(p[0].r == 255 && p[1].b == 0); /* wraps to a new phrase */
    /* Beat counter. */
    CHECK(pl_count(-1, -1, 0.1, 1) == 0 && pl_count(0, 0.1, 0.5, 1) == 0 && pl_count(0, 0.95, 0.02, 1) == 1);
    CHECK(pl_count(7, 0.6, 0.4, 1) == 7 && pl_count(7, 0.5, 0.1, 0) == -1 && pl_count(7, 0.5, -1, 1) == -1);
        /* FLOW lights (0.2.0): fresh = the rig's colours as sent, stale = the idle wave */
    { pl_rgb f[8]; for (int k = 0; k < 8; k++) { f[k].r = (unsigned char)(k * 30); f[k].g = 0; f[k].b = 200; }
      pl_rgb q[8]; CHECK(pl_flow(1, f, 0, q) && q[3].r == 90 && q[3].b == 200);
      pl_rgb w[8], w2[8]; pl_flow(0, f, 0, w); pl_side(PL_IDLE, 0, 0, -1, -1, 0, w2); CHECK(w[0].r == w2[0].r && w[0].g == w2[0].g && w[0].b == w2[0].b); }
    /* VJ Pads (0.3.0): HOT CUE + BEAT JUMP opens the page, a mode button alone closes it */
    { pl_vjs v = {0, 0, 0, 0};
      CHECK(pl_vj_in(&v, 0, 0x9, 0) == 0 && !v.on);                       /* row off: nothing happens */
      CHECK(pl_vj_in(&v, 1, 0x1, 0x04) == 0 && !v.on);                    /* HOT CUE alone: deck's */
      unsigned e = pl_vj_in(&v, 1, 0x9, 0); CHECK(v.on && e == 0x900);    /* the combo opens, the deck keeps its mode */
      CHECK(pl_vj_in(&v, 1, 0x8, 0) == 0x800 && v.on);                    /* still releasing: eaten */
      CHECK(pl_vj_in(&v, 1, 0x0, 0x05) == 0x05 && v.on);                  /* pads 1 + 3 are ours */
      CHECK(pl_vj_in(&v, 1, 0x0, 0x00) == 0 && v.on);
      CHECK(pl_vj_in(&v, 1, 0x0, 0x80) == 0x80);                          /* pad 8 held ... */
      CHECK(pl_vj_in(&v, 1, 0x2, 0x80) == 0x80 && !v.on);                 /* BEAT LOOP closes, its press passes, pad 8 stays ours */
      CHECK(pl_vj_in(&v, 1, 0x0, 0x81) == 0x80);                          /* a new pad 1 press is the deck's */
      CHECK(pl_vj_in(&v, 1, 0x0, 0x01) == 0);                             /* pad 8 let go: all the deck's */
      pl_vj_in(&v, 1, 0x9, 0); pl_vj_in(&v, 1, 0, 0x10);
      CHECK(pl_vj_in(&v, 0, 0, 0x10) == 0x10 && !v.on);                   /* row switched off mid-hold: closes, held pad stays ours */
      CHECK(pl_vj_in(&v, 0, 0, 0) == 0); }
    { pl_rgb q[8], r[8];
      pl_page(PL_PAGE_VJ, 0x04, 0, 1, 0.0, q); CHECK(q[2].r == 255 && q[2].g == 255 && q[2].b == 255);   /* held = white */
      CHECK(q[1].r + q[1].g + q[1].b > 2 * (q[0].r + q[0].g + q[0].b));                   /* running Look bright */
      pl_page(PL_PAGE_VJ, 0, 0, 1, 0.9, r); CHECK(q[1].r + q[1].g + q[1].b > r[1].r + r[1].g + r[1].b);  /* ... and pulsing */
      pl_page(PL_PAGE_VJ, 0, 1, -1, -1, q); CHECK(q[7].r > 0 && q[7].g == 0 && q[7].b == 0); }           /* SHIFT: hit colours */
    /* LIGHTS pads (0.4.0): HOT CUE + BEAT LOOP opens the LIGHTS page; the other combo goes straight to the other page */
    { pl_vjs v = {0, 0, 0, 0};
      CHECK(pl_vj_in(&v, 1, 0x2, 0) == 0 && !v.on);                       /* BEAT LOOP alone: deck's */
      CHECK(pl_vj_in(&v, 1, 0x3, 0) == 0x300 && v.on && v.page == PL_PAGE_LIGHTS);
      CHECK(pl_vj_in(&v, 1, 0x0, 0x02) == 0x02);                          /* pad 2 is ours */
      CHECK(pl_vj_in(&v, 1, 0x0, 0x00) == 0);
      CHECK(pl_vj_in(&v, 1, 0x1, 0) == 0 && !v.on);                       /* HOT CUE first: closes, its press passes */
      CHECK(pl_vj_in(&v, 1, 0x9, 0) == 0x900 && v.on && v.page == PL_PAGE_VJ);   /* + BEAT JUMP: VJ page */
      CHECK(pl_vj_in(&v, 1, 0x0, 0) == 0 && v.on);
      CHECK(pl_vj_in(&v, 1, 0xb, 0) == 0 && !v.on);                       /* all three while on: closes */
      CHECK(pl_vj_in(&v, 1, 0xb, 0) == 0xb00 && v.on && v.page == PL_PAGE_VJ); /* still held: VJ wins */
      pl_vjs w = {0, 0, 0, 0}; CHECK(pl_vj_in(&w, 0, 0x3, 0) == 0 && !w.on); }   /* row off: nothing */
    { pl_rgb q[8], r[8];
      pl_page(PL_PAGE_LIGHTS, 0, 0, 2, 0.0, q); pl_page(PL_PAGE_VJ, 0, 0, 2, 0.0, r);
      CHECK(q[0].r != r[0].r || q[0].g != r[0].g || q[0].b != r[0].b);   /* the pages look different */
      CHECK(q[2].r + q[2].g + q[2].b > 2 * (q[0].r + q[0].g + q[0].b));   /* the look fired last is bright */
      pl_page(PL_PAGE_LIGHTS, 0, 1, -1, -1, q); CHECK(q[2].r > 0 && q[2].g == 0 && q[2].b == 0);   /* SHIFT 3 = BLACKOUT red */
      pl_page(PL_PAGE_LIGHTS, 0x80, 1, -1, -1, q); CHECK(q[7].r == 255 && q[7].g == 255 && q[7].b == 255); }
    /* SAMPLES pads (0.5.0): HOT CUE + SLIP LOOP opens it, only with the Sample pads row on */
    { pl_vjs v = {0, 0, 0, 0};
      CHECK(pl_vj_in(&v, PL_EN_VJ, 0x5, 0) == 0 && !v.on);                /* VJ row only: HC + SL is the deck's */
      CHECK(pl_vj_in(&v, PL_EN_SAMPLES, 0x9, 0) == 0 && !v.on);           /* samples only: HC + BJ is the deck's */
      CHECK(pl_vj_in(&v, PL_EN_SAMPLES, 0x0, 0) == 0);
      CHECK(pl_vj_in(&v, PL_EN_SAMPLES, 0x5, 0) == 0x500 && v.on && v.page == PL_PAGE_SAMPLES);
      CHECK(pl_vj_in(&v, PL_EN_SAMPLES, 0x0, 0x11) == 0x11);              /* pads 1 + 5 are ours */
      CHECK(pl_vj_in(&v, PL_EN_VJ, 0x0, 0x01) == 0x01 && !v.on);          /* samples row off: closes, pad 1 stays ours */
      CHECK(pl_vj_in(&v, PL_EN_VJ, 0x0, 0x00) == 0);
      pl_vjs w = {0, 0, 0, 0};
      CHECK(pl_vj_in(&w, PL_EN_VJ | PL_EN_SAMPLES, 0x5, 0) == 0x500 && w.page == PL_PAGE_SAMPLES);
      CHECK(pl_vj_in(&w, PL_EN_VJ | PL_EN_SAMPLES, 0x0, 0) == 0 && w.on);
      CHECK(pl_vj_in(&w, PL_EN_VJ | PL_EN_SAMPLES, 0x1, 0) == 0 && !w.on); /* HOT CUE alone closes */
      CHECK(pl_vj_in(&w, PL_EN_VJ | PL_EN_SAMPLES, 0x3, 0) == 0x300 && w.page == PL_PAGE_LIGHTS); }
    /* one-shot release: always after the expected hit and before the next one */
    { double bpms[] = {70, 100, 128, 174, 0, 300};
      for (int b = 0; b < 6; b++) for (int r = 0; r < 3; r++) for (int i = 0; i <= 20; i++) {
          double ph = i / 20.0 * 0.999, sub = pl_sm_subs[r];
          double bpm = bpms[b] > 0 ? (bpms[b] > 220 ? 220 : bpms[b]) : 120, per = 60000.0 / bpm * sub;
          double hit = (1.0 - fmod(ph, sub) / sub) * per;            /* where azstem's grid puts the hit */
          int ms = pl_sm_release_ms(ph, 1, bpms[b], sub);
          CHECK(ms > hit + per * 0.4 && ms < hit + per * 0.6);
          int free_ms = pl_sm_release_ms(ph, 0, bpms[b], sub);         /* stopped deck: the hit is at once */
          CHECK(free_ms > per * 0.4 && free_ms < per * 0.6);
      }
      CHECK(pl_sm_release_ms(-1, 1, 128, 1.0) == pl_sm_release_ms(0.3, 0, 128, 1.0));   /* no grid = at once */
      CHECK(pl_sm_release_ms(0.5, 1, 0.0 / 0.0, 1.0) > 0); }
    { pl_rgb q[8], r[8];
      pl_sm_page(0, 1, 2, 0x7, 1, 0.0, q);
      CHECK(q[1].r + q[1].g + q[1].b > 2 * (q[0].r + q[0].g + q[0].b));   /* the sound played last is bright */
      CHECK(q[5].b == 255 && q[4].b == 70 && q[7].r == 0 && q[7].g == 0 && q[7].b == 0);   /* bank 2 live, bank 4 missing */
      pl_sm_page(0x08, 0, 0, 0, -1, -1, r);
      CHECK(r[3].r == 255 && r[3].g == 255 && r[0].r == 60 && r[0].g == 0);   /* held white; no sampler: dim red */
      pl_page(PL_PAGE_VJ, 0, 0, -1, -1, r); CHECK(q[0].r != r[0].r || q[0].g != r[0].g || q[0].b != r[0].b); }
    printf("lights: %d checks passed\n", checks);
    return 0;
}
