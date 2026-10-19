/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
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
    printf("lights: %d checks passed\n", checks);
    return 0;
}
