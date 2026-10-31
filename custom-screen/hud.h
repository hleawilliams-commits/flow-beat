/* SPDX-License-Identifier: GPL-3.0-only
 * Mix hints drawn inside the existing headers: no new boxes. Pure functions so
 * the rules are host-tested. The reference for a deck is the other playing deck
 * that started first (usually the track you're mixing out of).
 */
#ifndef GP_HUD_H
#define GP_HUD_H
#include <math.h>
#include <stdlib.h>

enum { GP_MATCH_UNKNOWN = 0, GP_MATCH_GOOD, GP_MATCH_RISKY, GP_MATCH_CLASH };
enum { GP_ALERT_NONE = 0, GP_ALERT_AMBER, GP_ALERT_RED };
#define GP_GAP_SHOW_BPM 8.0  /* show the tempo gap only when it's close enough to blend */

/* "8A" / "12B" -> 1..12 and 0 (A, minor) / 1 (B, major). 0 = not Camelot. */
static int gp_camelot_parse(const char *k, int *num, int *major)
{
    if (!k || k[0] < '1' || k[0] > '9') return 0;
    int n = k[0] - '0', i = 1;
    if (k[1] >= '0' && k[1] <= '9') { n = n * 10 + (k[1] - '0'); i = 2; }
    if (n < 1 || n > 12 || (k[i] != 'A' && k[i] != 'B') || k[i + 1]) return 0;
    *num = n; *major = k[i] == 'B';
    return 1;
}

/* Good: same key, one step round the wheel, or relative major/minor.
 * Risky: two steps, or one step plus a mode change. Clash: anything else. */
static int gp_key_match(const char *a, const char *b)
{
    int na, ma, nb, mb;
    if (!gp_camelot_parse(a, &na, &ma) || !gp_camelot_parse(b, &nb, &mb)) return GP_MATCH_UNKNOWN;
    int d = abs(na - nb); if (d > 6) d = 12 - d;
    if (ma == mb && d <= 1) return GP_MATCH_GOOD;
    if (ma != mb && d == 0) return GP_MATCH_GOOD;
    if ((ma == mb && d == 2) || (ma != mb && d == 1)) return GP_MATCH_RISKY;
    return GP_MATCH_CLASH;
}

/* Only a playing deck with a known remaining time warns. */
static int gp_time_alert(int playing, double remaining_s)
{
    if (!playing || !isfinite(remaining_s) || remaining_s < 0) return GP_ALERT_NONE;
    if (remaining_s <= 10) return GP_ALERT_RED;
    if (remaining_s <= 30) return GP_ALERT_AMBER;
    return GP_ALERT_NONE;
}

/* started[i] = when deck i began playing (ms, > 0), 0 = not playing. */
static int gp_reference_deck(int deck, const long started[4])
{
    int best = -1;
    for (int i = 0; i < 4; i++) {
        if (i == deck || started[i] <= 0) continue;
        if (best < 0 || started[i] < started[best] || (started[i] == started[best] && i < best)) best = i;
    }
    return best;
}

/* The other loaded deck that fits deck best: key match first (unknown ranks last), then the
 * smallest tempo gap, then the lower deck. -1 = no other deck loaded. */
static int gp_mix_partner(int deck, const int loaded[4], const char *const key[4], const double bpm[4], int *match)
{
    int best = -1, bm = 0;
    double bg = 0;
    for (int j = 0; j < 4; j++) {
        if (j == deck || !loaded[j]) continue;
        int m = gp_key_match(key[deck], key[j]);
        int rank = m ? m : 4;
        double g = fabs(bpm[j] - bpm[deck]);
        if (!isfinite(g)) g = 1e9;
        if (best < 0 || rank < bm || (rank == bm && g < bg)) { best = j; bm = rank; bg = g; }
    }
    if (match) *match = best >= 0 && bm < 4 ? bm : GP_MATCH_UNKNOWN;
    return best;
}

/* Where a Camelot key sits on a wheel of radius r around (0,0): 12 at the top, clockwise, the
 * B (major) ring outside and the A (minor) ring at 55 %. 0 = not a Camelot key. */
static int gp_wheel_pos(const char *key, double r, double *x, double *y)
{
    int n, major;
    if (!gp_camelot_parse(key, &n, &major)) return 0;
    double a = (n - 3) / 12.0 * 6.283185307179586, rr = major ? r : r * 0.55;
    *x = cos(a) * rr; *y = sin(a) * rr;
    return 1;
}

/* Tempo gap to the reference, in BPM, when both are known and within blend range. */
static int gp_bpm_gap(double mine, double ref, double *gap)
{
    if (!isfinite(mine) || !isfinite(ref) || mine <= 0 || ref <= 0) return 0;
    double g = mine - ref;
    if (fabs(g) > GP_GAP_SHOW_BPM) return 0;
    *gap = fabs(g) < 0.05 ? 0 : g;
    return 1;
}
/* BPM fit: which track tempos can be pitched onto bpm within +/-range_pct (the pitch fader's
 * reach), straight, at half time (k = 0.5) or at double time (k = 2). A track at X fits when
 * X * (1 + p) = bpm * k for some |p| <= range, so X runs from bpm*k/(1+r) up to bpm*k/(1-r).
 * Whole BPMs, inner (rounded towards the middle). 0 = no tempo or no range. */
static int gp_bpm_fit_range(double bpm, double range_pct, double k, int *lo, int *hi)
{
    if (!isfinite(bpm) || bpm <= 0 || !isfinite(range_pct) || range_pct <= 0 || range_pct >= 100 || k <= 0) return 0;
    double r = range_pct / 100;
    *lo = (int)ceil(bpm * k / (1 + r) - 1e-9);
    *hi = (int)floor(bpm * k / (1 - r) + 1e-9);
    return *lo <= *hi;
}

enum { GP_FIT_SAME = 0, GP_FIT_HALF, GP_FIT_DOUBLE };

/* How far mine has to move to sit on ref: straight, or locked at half / double time
 * (mine * (1 + pct/100) = ref * 0.5 or * 2). Picks the smallest move; straight wins a tie
 * within 0.05 %. *how = GP_FIT_*. 0 = a tempo is unknown. */
static int gp_pitch_to(double mine, double ref, int *how, double *pct)
{
    if (!isfinite(mine) || !isfinite(ref) || mine <= 0 || ref <= 0) return 0;
    static const double kk[3] = { 1, 0.5, 2 };
    int best = 0;
    double bp = (ref / mine - 1) * 100;
    for (int c = 1; c < 3; c++) {
        double p = (ref * kk[c] / mine - 1) * 100;
        if (fabs(p) + 0.05 < fabs(bp)) { best = c; bp = p; }
    }
    if (!(fabs(bp) <= 1000)) return 0;   /* a nonsense BPM (a broken read): no move to show */
    *how = best;
    *pct = fabs(bp) < 0.05 ? 0 : bp;
    return 1;
}
#endif
