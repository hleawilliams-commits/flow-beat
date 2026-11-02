/* SPDX-License-Identifier: GPL-3.0-only
 * Host checks for the usb buttons' navigator script (nav.h nav_build_source): it presses
 * SOURCE, picks the USB row, opens Local Library and ends with no LOAD; a real press by the
 * DJ cancels it; a second tap re-arms only once the first has finished.
 *   cc -std=gnu11 -fsanitize=address,undefined -I<sdk>/include tests/test_usb.c && ./a.out
 */
#include <stdio.h>
#include "../nav.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static int never_playing(int deck) { (void)deck; return 0; }

/* Run the armed script on idle 98-byte mixer frames, 10 ms apart; count SOURCE and LOAD
 * frames and the view the deck would be in (SOURCE once SOURCE was pressed). */
static int run(long ms, int *source_frames, int *load_frames, int *clicks, int cancel_at)
{
    unsigned char f[98];
    int view = AZ_VIEW_DECK; *source_frames = *load_frames = *clicks = 0;
    for (long t = 0; t < ms; t += 10) {
        memset(f, 0, sizeof f);
        if (cancel_at >= 0 && t == cancel_at) f[32] = 0x40;          /* the DJ presses BROWSE */
        nav_frame(f, sizeof f, t, view, never_playing);
        if (f[32] & 0x80) { (*source_frames)++; view = AZ_VIEW_SOURCE; }
        if (f[33] & 0x0f) (*load_frames)++;
        if (f[31] & 1) (*clicks)++;
        int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
        if (st == NAV_DONE || st == NAV_CANCELLED) return st;
    }
    return __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
}

int main(void)
{
    nav_script s;
    CHECK(!nav_build_source(&s, 2) && !nav_build_source(&s, -1));
    for (int stick = 0; stick < 2; stick++) {
        CHECK(nav_build_source(&s, stick));
        CHECK(s.deck == -1 && s.ops[s.nops - 1].op == NAV_END);
        for (int i = 0; i < s.nops; i++) CHECK(s.ops[i].op != NAV_LOAD);
        int rows_seen = 0;
        for (int i = 0; i < s.nops; i++) if (s.ops[i].op == NAV_ROWS) { CHECK(s.ops[i].n == (rows_seen ? 0 : stick)); rows_seen++; }
        CHECK(rows_seen == 2);
        CHECK(nav_arm(&s));
        CHECK(!nav_arm(&s));                                   /* busy: a second tap waits */
        int src, load, clicks;
        CHECK(run(30000, &src, &load, &clicks, -1) == NAV_DONE);
        CHECK(src >= 90 && load == 0 && clicks >= 2);          /* SOURCE held ~1 s, two knob pushes */
        CHECK(nav_arm(&s));                                    /* finished: may arm again */
        CHECK(run(30000, &src, &load, &clicks, 2500) == NAV_CANCELLED);
        nav_off = 0; nav_virt = -1;
    }
    if (fails) { printf("usb: %d failed\n", fails); return 1; }
    printf("usb: all passed\n");
    return 0;
}
