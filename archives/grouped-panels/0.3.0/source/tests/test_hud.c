/* SPDX-License-Identifier: GPL-3.0-only
 * Mix-hint rules: Camelot matching, time alerts, reference deck and tempo gap. */
#include <assert.h>
#include <stdio.h>
#include "../hud.h"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
int main(void)
{
    int n, m;
    CHECK(gp_camelot_parse("8A", &n, &m) && n == 8 && !m);
    CHECK(gp_camelot_parse("12B", &n, &m) && n == 12 && m);
    const char *bad[] = {"", "0A", "13A", "8", "8C", "8a", "A8", "8AB", "123A", NULL, "Am"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!gp_camelot_parse(bad[i], &n, &m));
    /* Every pair on the wheel classifies by distance and mode. */
    for (int a = 1; a <= 12; a++) for (int b = 1; b <= 12; b++) for (int ma = 0; ma < 2; ma++) for (int mb = 0; mb < 2; mb++) {
        char ka[4], kb[4];
        snprintf(ka, sizeof ka, "%d%c", a, ma ? 'B' : 'A'); snprintf(kb, sizeof kb, "%d%c", b, mb ? 'B' : 'A');
        int d = a > b ? a - b : b - a; if (d > 6) d = 12 - d;
        int want = (ma == mb && d <= 1) || (ma != mb && d == 0) ? GP_MATCH_GOOD :
                   (ma == mb && d == 2) || (ma != mb && d == 1) ? GP_MATCH_RISKY : GP_MATCH_CLASH;
        CHECK(gp_key_match(ka, kb) == want);
        CHECK(gp_key_match(ka, kb) == gp_key_match(kb, ka));
    }
    CHECK(gp_key_match("12A", "1A") == GP_MATCH_GOOD);   /* wraps round the wheel */
    CHECK(gp_key_match("8A", "8B") == GP_MATCH_GOOD);    /* relative major/minor */
    CHECK(gp_key_match("8A", "3B") == GP_MATCH_CLASH);
    CHECK(gp_key_match("8A", "") == GP_MATCH_UNKNOWN && gp_key_match(NULL, "8A") == GP_MATCH_UNKNOWN);
    /* Alerts only while playing, with a known time. */
    CHECK(gp_time_alert(1, 31) == GP_ALERT_NONE && gp_time_alert(1, 30) == GP_ALERT_AMBER);
    CHECK(gp_time_alert(1, 10.5) == GP_ALERT_AMBER && gp_time_alert(1, 10) == GP_ALERT_RED);
    CHECK(gp_time_alert(1, 0) == GP_ALERT_RED && gp_time_alert(0, 5) == GP_ALERT_NONE);
    CHECK(gp_time_alert(1, -1) == GP_ALERT_NONE && gp_time_alert(1, NAN) == GP_ALERT_NONE);
    /* Reference = the other playing deck that started first; never itself. */
    long none[4] = {0, 0, 0, 0}, two[4] = {500, 900, 0, 0}, three[4] = {700, 300, 300, 0};
    for (int d = 0; d < 4; d++) CHECK(gp_reference_deck(d, none) == -1);
    CHECK(gp_reference_deck(0, two) == 1 && gp_reference_deck(1, two) == 0 && gp_reference_deck(2, two) == 0);
    CHECK(gp_reference_deck(0, three) == 1 && gp_reference_deck(1, three) == 2 && gp_reference_deck(3, three) == 1);
    /* Tempo gap: within 8 BPM, near-zero snaps to 0, unknowns hide it. */
    double g = 99;
    CHECK(gp_bpm_gap(128.4, 128.0, &g) && fabs(g - 0.4) < 1e-9);
    CHECK(gp_bpm_gap(124.0, 128.0, &g) && fabs(g + 4.0) < 1e-9);
    CHECK(gp_bpm_gap(128.02, 128.0, &g) && g == 0);
    CHECK(!gp_bpm_gap(140, 128, &g) && !gp_bpm_gap(0, 128, &g) && !gp_bpm_gap(128, NAN, &g));
    CHECK(gp_bpm_gap(136.0, 128.0, &g));
    printf("hud: %d mix-hint checks passed\n", checks);
    return 0;
}
