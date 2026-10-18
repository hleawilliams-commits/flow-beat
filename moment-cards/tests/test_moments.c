/* SPDX-License-Identifier: GPL-3.0-only
 * Parsing, US time zones and DST, the card/badge/NOW schedule, dismissals, song matching. */
#include <assert.h>
#include "../moments.h"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
int main(void)
{
    CHECK(mc_parse_time("19:15") == 1155 && mc_parse_time("7:15pm") == 1155 && mc_parse_time("7:15 PM") == 1155);
    CHECK(mc_parse_time("12:05am") == 5 && mc_parse_time("12:30pm") == 750 && mc_parse_time("0:00") == 0);
    CHECK(mc_parse_time("9.30") == 570 && mc_parse_time(" 23:59 ") == 1439);
    const char *bad[] = {"", "25:00", "7:60", "13:00pm", "7", "7:5", "abc", "7:15xm", "0:00am"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(mc_parse_time(bad[i]) == -1);
    mc_show s;
    mc_parse("# c\n\n19:15 | First Dance | Ed Sheeran - Perfect | Couple to the floor\r\n"
             "7:40pm|Toasts||Mic to the best man\nnonsense line\n18:30 | Entrance\n20:00 |  | x\n", &s);
    CHECK(s.n == 3 && s.bad_line == 5);
    CHECK(s.m[0].min == 1110 && !strcmp(s.m[0].name, "Entrance") && !s.m[0].song[0]);
    CHECK(!strcmp(s.m[1].name, "First Dance") && !strcmp(s.m[1].song, "Ed Sheeran - Perfect") && !strcmp(s.m[1].line, "Couple to the floor"));
    CHECK(s.m[2].min == 1180 && !strcmp(s.m[2].line, "Mic to the best man"));
    /* Time zones. 2026-07-01 18:00 UTC = 12:00 MDT; 2026-12-01 18:00 UTC = 11:00 MST. */
    int64_t jul = (int64_t)mc_days_from_civil(2026, 7, 1) * 86400 + 18 * 3600;
    int64_t dec = (int64_t)mc_days_from_civil(2026, 12, 1) * 86400 + 18 * 3600;
    CHECK(mc_local_secs(MC_TZ_MOUNTAIN, jul) == 12 * 3600 && mc_local_secs(MC_TZ_MOUNTAIN, dec) == 11 * 3600);
    CHECK(mc_local_secs(MC_TZ_ARIZONA, jul) == 11 * 3600 && mc_local_secs(MC_TZ_PACIFIC, jul) == 11 * 3600);
    CHECK(mc_local_secs(MC_TZ_EASTERN, dec) == 13 * 3600 && mc_local_secs(MC_TZ_UTC, dec) == 18 * 3600);
    /* DST edges 2026: starts Mar 8 at 09:00 UTC (2:00 MST), ends Nov 1 at 08:00 UTC (2:00 MDT). */
    int64_t mar8 = (int64_t)mc_days_from_civil(2026, 3, 8) * 86400, nov1 = (int64_t)mc_days_from_civil(2026, 11, 1) * 86400;
    CHECK(mc_utc_offset(MC_TZ_MOUNTAIN, mar8 + 9 * 3600 - 1) == -7 * 3600 && mc_utc_offset(MC_TZ_MOUNTAIN, mar8 + 9 * 3600) == -6 * 3600);
    CHECK(mc_utc_offset(MC_TZ_MOUNTAIN, nov1 + 8 * 3600 - 1) == -6 * 3600 && mc_utc_offset(MC_TZ_MOUNTAIN, nov1 + 8 * 3600) == -7 * 3600);
    CHECK(mc_weekday(mc_days_from_civil(2026, 10, 18)) == 0);          /* a Sunday */
    /* Schedule: First Dance at 19:15 (69300 s), lead 60 s. */
    mc_parse("19:15 | First Dance\n19:40 | Toasts\n", &s);
    int fd = 1155 * 60;
    CHECK(mc_pick(&s, fd - 61, 60, -1, 0).what == MC_SHOW_NONE);
    mc_view v = mc_pick(&s, fd - 60, 60, -1, 0); CHECK(v.what == MC_SHOW_CARD && v.idx == 0 && v.secs_to == 60);
    CHECK(mc_pick(&s, fd - 49, 60, -1, 0).what == MC_SHOW_CARD);
    CHECK(mc_pick(&s, fd - 48, 60, -1, 0).what == MC_SHOW_BADGE);
    CHECK(mc_pick(&s, fd, 60, -1, 0).what == MC_SHOW_NOW && mc_pick(&s, fd + 10, 60, -1, 0).what == MC_SHOW_NOW);
    CHECK(mc_pick(&s, fd + 11, 60, -1, 0).what == MC_SHOW_NONE);
    /* Tap the card away: badge until the moment, NOW still shows; tap NOW away: gone. */
    CHECK(mc_pick(&s, fd - 58, 60, 0, MC_SHOW_CARD).what == MC_SHOW_BADGE);
    CHECK(mc_pick(&s, fd + 2, 60, 0, MC_SHOW_CARD).what == MC_SHOW_NOW);
    CHECK(mc_pick(&s, fd + 3, 60, 0, MC_SHOW_NOW).what == MC_SHOW_NONE);
    /* The next moment then takes over on its own schedule. */
    v = mc_pick(&s, 1180 * 60 - 30, 60, 0, MC_SHOW_NOW); CHECK(v.idx == 1 && v.what == MC_SHOW_BADGE);
    /* Midnight: a 00:15 last song is 30 min after 23:45. */
    mc_parse("00:15 | Last Song\n", &s);
    v = mc_pick(&s, 23 * 3600 + 59 * 60 + 30, 3600, -1, 0); CHECK(v.what == MC_SHOW_BADGE && v.secs_to == 15 * 60 + 30);
    /* Song matching. */
    CHECK(mc_song_matches("Ed Sheeran - Perfect", "Perfect", "Ed Sheeran"));
    CHECK(mc_song_matches("Ed Sheeran - Perfect", "Perfect (Acoustic)", "Ed Sheeran"));
    CHECK(mc_song_matches("Perfect", "perfect", ""));
    CHECK(!mc_song_matches("Ed Sheeran - Perfect", "Perfect", "One Direction"));
    CHECK(mc_song_matches("Walk the Moon - Shut Up and Dance (Clean)", "Shut Up And Dance", "WALK THE MOON"));
    CHECK(!mc_song_matches("", "Perfect", "") && !mc_song_matches("Perfect", "", ""));
    char d1[64] = "Beyonc\xc3\xa9 \xe2\x80\x9cHalo\xe2\x80\x9d \xe2\x80\x94 Live\xe2\x80\xa6 \xf0\x9f\x8e\xb6 \xff";
    mc_disp(d1); CHECK(!strcmp(d1, "Beyonc\xe9 \"Halo\" - Live... ? ?"));
    char d2[16] = "plain"; mc_disp(d2); CHECK(!strcmp(d2, "plain"));
    printf("moments: %d checks passed\n", checks);
    return 0;
}
