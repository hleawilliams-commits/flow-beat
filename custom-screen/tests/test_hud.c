/* SPDX-License-Identifier: GPL-3.0-only
 * Host checks for the mix hints (hud.h). Run from the mod folder:
 *   cc -std=c11 -Wall -Wextra -Werror tests/test_hud.c -lm -o /tmp/t && /tmp/t
 */
#include <stdio.h>
#include "../hud.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    CHECK(gp_key_match("8A", "8A") == GP_MATCH_GOOD && gp_key_match("8A", "9A") == GP_MATCH_GOOD);
    CHECK(gp_key_match("8A", "8B") == GP_MATCH_GOOD && gp_key_match("8A", "10A") == GP_MATCH_RISKY);
    CHECK(gp_key_match("12A", "1A") == GP_MATCH_GOOD && gp_key_match("8A", "3B") == GP_MATCH_CLASH);
    CHECK(gp_key_match("", "8A") == GP_MATCH_UNKNOWN);

    int ld[4] = { 1, 1, 1, 1 }, m = -1;
    const char *k[4] = { "8A", "3B", "10A", "9A" };
    double bpm[4] = { 124, 124, 124, 130 };
    CHECK(gp_mix_partner(0, ld, k, bpm, &m) == 3 && m == GP_MATCH_GOOD);   /* key beats tempo */
    k[3] = "8B";  bpm[3] = 124.5; k[2] = "7A"; bpm[2] = 124.2;
    CHECK(gp_mix_partner(0, ld, k, bpm, &m) == 2 && m == GP_MATCH_GOOD);   /* tie on key: closer tempo */
    ld[2] = ld[3] = 0;
    CHECK(gp_mix_partner(0, ld, k, bpm, &m) == 1 && m == GP_MATCH_CLASH);  /* a clash still names the deck */
    k[1] = "";
    CHECK(gp_mix_partner(0, ld, k, bpm, &m) == 1 && m == GP_MATCH_UNKNOWN);
    ld[1] = 0;
    CHECK(gp_mix_partner(0, ld, k, bpm, &m) == -1 && m == GP_MATCH_UNKNOWN);
    ld[1] = ld[2] = 1; k[1] = "1B"; k[2] = "";                               /* unknown key ranks under a clash */
    CHECK(gp_mix_partner(0, ld, k, bpm, &m) == 1 && m == GP_MATCH_CLASH);
    CHECK(gp_time_alert(1, 9) == GP_ALERT_RED && gp_time_alert(1, 20) == GP_ALERT_AMBER && gp_time_alert(0, 5) == GP_ALERT_NONE);
    const long st[4] = { 0, 200, 100, 0 };
    CHECK(gp_reference_deck(1, st) == 2 && gp_reference_deck(2, st) == 1 && gp_reference_deck(3, st) == 2);
    double gap = 9;
    CHECK(gp_bpm_gap(125, 124, &gap) && gap == 1 && !gp_bpm_gap(140, 124, &gap));
    /* BPM fit: 124 at +/-6 % takes 117..131 straight, 59..65 at half time, 234..263 at double */
    int lo = 0, hi = 0;
    CHECK(gp_bpm_fit_range(124, 6, 1, &lo, &hi) && lo == 117 && hi == 131);
    CHECK(gp_bpm_fit_range(124, 6, 0.5, &lo, &hi) && lo == 59 && hi == 65);
    CHECK(gp_bpm_fit_range(124, 6, 2, &lo, &hi) && lo == 234 && hi == 263);
    CHECK(gp_bpm_fit_range(174, 8, 1, &lo, &hi) && lo == 162 && hi == 189);
    CHECK(gp_bpm_fit_range(100, 10, 1, &lo, &hi) && lo == 91 && hi == 111);   /* 90.9.. / ..111.1 */
    CHECK(!gp_bpm_fit_range(0, 6, 1, &lo, &hi) && !gp_bpm_fit_range(124, 0, 1, &lo, &hi));
    /* pitch_to: the smallest move, straight or locked at half / double time */
    int how = -1; double pct = 0;
    CHECK(gp_pitch_to(121, 124, &how, &pct) && how == GP_FIT_SAME && fabs(pct - 2.479) < 0.01);
    CHECK(gp_pitch_to(128, 124, &how, &pct) && how == GP_FIT_SAME && fabs(pct + 3.125) < 0.001);
    CHECK(gp_pitch_to(87, 172, &how, &pct) && how == GP_FIT_HALF && fabs(pct + 1.149) < 0.01);  /* 87 rides 172 at half time */
    CHECK(gp_pitch_to(172, 87, &how, &pct) && how == GP_FIT_DOUBLE && fabs(pct - 1.163) < 0.01);
    CHECK(gp_pitch_to(124, 124.04, &how, &pct) && how == GP_FIT_SAME && pct == 0);
    CHECK(gp_pitch_to(90, 135, &how, &pct) && how == GP_FIT_HALF && fabs(pct + 25) < 0.001);  /* no fit: still the smallest move */
    CHECK(!gp_pitch_to(0, 124, &how, &pct) && !gp_pitch_to(124, NAN, &how, &pct));
    double wx, wy;
    CHECK(gp_wheel_pos("12B", 20, &wx, &wy) && fabs(wx) < 1e-9 && fabs(wy + 20) < 1e-9);    /* 12 at the top */
    CHECK(gp_wheel_pos("3A", 20, &wx, &wy) && fabs(wx - 11) < 1e-9 && fabs(wy) < 1e-9);      /* minor ring at 55 % */
    CHECK(gp_wheel_pos("6B", 20, &wx, &wy) && fabs(wx) < 1e-9 && fabs(wy - 20) < 1e-9);
    CHECK(!gp_wheel_pos("--", 20, &wx, &wy) && !gp_wheel_pos("13A", 20, &wx, &wy));
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("hud: all passed\n");
    return 0;
}
