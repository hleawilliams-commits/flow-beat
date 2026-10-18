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

/* Tempo gap to the reference, in BPM, when both are known and within blend range. */
static int gp_bpm_gap(double mine, double ref, double *gap)
{
    if (!isfinite(mine) || !isfinite(ref) || mine <= 0 || ref <= 0) return 0;
    double g = mine - ref;
    if (fabs(g) > GP_GAP_SHOW_BPM) return 0;
    *gap = fabs(g) < 0.05 ? 0 : g;
    return 1;
}
#endif
