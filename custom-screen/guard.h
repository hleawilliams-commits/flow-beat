/* SPDX-License-Identifier: GPL-3.0-only
 * Dead-Air Guard: decide, from deck states, fader levels, mic switches and per-deck audio
 * levels, whether the room is about to go (or has gone) silent. Pure, host-tested.
 */
#ifndef DG_GUARD_H
#define DG_GUARD_H

enum { DG_OK = 0, DG_QUIET, DG_LOAD_NEXT, DG_ENDING, DG_DEAD };

typedef struct {
    int loaded, playing;      /* deck_info */
    double remaining;         /* seconds, < 0 unknown */
    int fader;                /* channel fader high byte 0..255, < 0 unknown */
    int quiet_ms;             /* how long its audio has been near-silent while playing */
    unsigned id;              /* content id (a cued deck must hold a different track) */
} dg_deck;

typedef struct {
    int warn_s;               /* LOAD NEXT threshold, seconds */
    int ending_s;             /* ENDING threshold */
    int quiet_ms;             /* QUIET after this long near-silent */
    int use_faders, use_quiet;
} dg_cfg;

typedef struct { int state; double left; int deck; int quiet_s; } dg_out;

#define DG_FADER_OPEN 12      /* above ~5 % of travel counts as up */

static int dg_audible(const dg_deck *d, const dg_cfg *c)
{
    if (!d->loaded || !d->playing) return 0;
    if (c->use_faders && d->fader >= 0 && d->fader < DG_FADER_OPEN) return 0;
    return 1;
}
/* mic_on: a mic or TALK OVER switch is on (speeches): no DEAD/QUIET alarms.
 * was_audible_ms: how long since something was last audible (for DEAD). */
static dg_out dg_judge(const dg_deck d[4], const dg_cfg *c, int mic_on, long since_audible_ms)
{
    dg_out o = {DG_OK, -1, -1, 0};
    int n_aud = 0, all_quiet = 1, quiet_min = 1 << 30, known_all = 1;
    double last = -1;                           /* when the last audible deck runs out */
    unsigned aud_ids[4]; int naid = 0;
    for (int i = 0; i < 4; i++) {
        if (!dg_audible(&d[i], c)) continue;
        n_aud++;
        aud_ids[naid++] = d[i].id;
        if (d[i].remaining < 0) known_all = 0;
        else if (d[i].remaining > last) { last = d[i].remaining; o.deck = i; }
        if (d[i].quiet_ms < c->quiet_ms) all_quiet = 0;
        else if (d[i].quiet_ms < quiet_min) quiet_min = d[i].quiet_ms;
    }
    if (!n_aud) {
        if (!mic_on && since_audible_ms >= 1500 && since_audible_ms < 12000) o.state = DG_DEAD;
        return o;
    }
    /* Something cued: a loaded deck that isn't audible and holds a different track. */
    int cued = 0;
    for (int i = 0; i < 4 && !cued; i++) {
        if (!d[i].loaded || dg_audible(&d[i], c)) continue;
        int same = 0;
        for (int k = 0; k < naid; k++) if (d[i].id && d[i].id == aud_ids[k]) same = 1;
        if (!same) cued = 1;
    }
    if (known_all && last >= 0 && !cued) {
        o.left = last;
        if (last <= c->ending_s) { o.state = DG_ENDING; return o; }
        if (last <= c->warn_s) { o.state = DG_LOAD_NEXT; return o; }
    }
    if (c->use_quiet && !mic_on && all_quiet) { o.state = DG_QUIET; o.quiet_s = quiet_min / 1000; }
    return o;
}
#endif
