/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../guard.h"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static dg_deck D[4];
static dg_cfg cfg = {30, 10, 4000, 1, 1};
static void reset(void) { memset(D, 0, sizeof D); for (int i = 0; i < 4; i++) { D[i].remaining = -1; D[i].fader = -1; } }
static void play(int i, double left, int fader, unsigned id) { D[i].loaded = D[i].playing = 1; D[i].remaining = left; D[i].fader = fader; D[i].id = id; }
static void cue(int i, unsigned id) { D[i].loaded = 1; D[i].playing = 0; D[i].id = id; }
int main(void)
{
    reset(); play(0, 120, 200, 1); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_OK);
    reset(); play(0, 25, 200, 1); dg_out o = dg_judge(D, &cfg, 0, 0); CHECK(o.state == DG_LOAD_NEXT && o.left == 25 && o.deck == 0);
    reset(); play(0, 8, 200, 1); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_ENDING);
    /* A different track cued on another deck: fine. The same track (duplicate load) isn't. */
    reset(); play(0, 8, 200, 1); cue(1, 2); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_OK);
    reset(); play(0, 8, 200, 1); cue(1, 1); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_ENDING);
    /* Two audible decks: the later end counts. */
    reset(); play(0, 8, 200, 1); play(1, 200, 200, 2); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_OK);
    reset(); play(0, 8, 200, 1); play(1, 22, 200, 2); o = dg_judge(D, &cfg, 0, 0); CHECK(o.state == DG_LOAD_NEXT && o.left == 22 && o.deck == 1);
    /* Fader down = cueing in headphones: not heard, so it doesn't save you, and counts as cued. */
    reset(); play(0, 8, 200, 1); play(1, 200, 0, 2); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_OK);   /* deck 2 is the cue */
    reset(); play(0, 8, 0, 1); CHECK(dg_judge(D, &cfg, 0, 3000).state == DG_DEAD);                   /* only deck, fader down */
    dg_cfg nof = cfg; nof.use_faders = 0;
    reset(); play(0, 8, 0, 1); CHECK(dg_judge(D, &nof, 0, 0).state == DG_ENDING);
    /* Unknown faders count as up; unknown length can't warn. */
    reset(); play(0, 8, -1, 1); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_ENDING);
    reset(); play(0, -1, 200, 1); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_OK);
    reset(); play(0, -1, 200, 1); play(1, 5, 200, 2); CHECK(dg_judge(D, &cfg, 0, 0).state == DG_OK);  /* one unknown: no claim */
    /* Dead air: after 1.5 s, for 12 s, never with a mic on. */
    reset(); CHECK(dg_judge(D, &cfg, 0, 1000).state == DG_OK);
    CHECK(dg_judge(D, &cfg, 0, 1500).state == DG_DEAD && dg_judge(D, &cfg, 0, 11999).state == DG_DEAD);
    CHECK(dg_judge(D, &cfg, 0, 12000).state == DG_OK && dg_judge(D, &cfg, 1, 3000).state == DG_OK);
    /* Quiet: every audible deck near-silent for 4 s; not with a mic on; not when off. */
    reset(); play(0, 120, 200, 1); D[0].quiet_ms = 5200; o = dg_judge(D, &cfg, 0, 0); CHECK(o.state == DG_QUIET && o.quiet_s == 5);
    CHECK(dg_judge(D, &cfg, 1, 0).state == DG_OK);
    dg_cfg noq = cfg; noq.use_quiet = 0; CHECK(dg_judge(D, &noq, 0, 0).state == DG_OK);
    reset(); play(0, 120, 200, 1); play(1, 120, 200, 2); D[0].quiet_ms = 9000; CHECK(dg_judge(D, &cfg, 0, 0).state == DG_OK); /* deck 2 loud */
    /* Ending beats quiet. */
    reset(); play(0, 6, 200, 1); D[0].quiet_ms = 9000; CHECK(dg_judge(D, &cfg, 0, 0).state == DG_ENDING);
    printf("guard: %d checks passed\n", checks);
    return 0;
}
