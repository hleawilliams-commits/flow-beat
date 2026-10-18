/* SPDX-License-Identifier: GPL-3.0-only
 * Navigator: script building, frame edits over time, cancellation, the playing-deck
 * refusal and knob continuity after a script. */
#include <assert.h>
#include <stdio.h>
#include "../nav.h"
static int checks, busy;
#define CHECK(x) do { assert(x); checks++; } while (0)
static int playing(int deck) { (void)deck; return busy; }
static unsigned char F[152];
static void idle(int knob) { memset(F, 0, sizeof F); F[6] = 0x08; F[34] = (unsigned char)knob; }
static void reset(void) { nav_state = NAV_IDLE; nav_off = 0; nav_virt = -1; nav_real_last = -1; nav_drop_after_load = 0; busy = 0; }
/* Run frames every 20 ms until the script leaves RUNNING; record what the deck saw. */
static int saw_press[256], saw_click, saw_load, last_knob, minknob, maxknob;
static long run(long t, int knob, int view, int cancel_at)
{
    memset(saw_press, 0, sizeof saw_press); saw_click = saw_load = 0; minknob = 999; maxknob = -1;
    for (int i = 0; i < 4000; i++, t += 20) {
        idle(knob);
        if (cancel_at && i == cancel_at) F[33] = 0x80;   /* the DJ presses BACK */
        nav_frame(F, 152, t, view, playing);
        if (F[32]) saw_press[F[32]]++;
        if (F[33] & 0x80) saw_press[0x80 + 1]++;
        if (F[31] & 1) saw_click++;
        if (F[33] & 0x0f) saw_load = F[33] & 0x0f;
        last_knob = F[34]; if (F[34] < minknob) minknob = F[34]; if (F[34] > maxknob) maxknob = F[34];
        int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
        if (st != NAV_ARMED && st != NAV_RUNNING) return t;
    }
    return -1;
}
int main(void)
{
    nav_script s; int path[3] = {1, 0, 41};
    CHECK(!nav_build(&s, 0, 0, path, 0, -1) && !nav_build(&s, 0, 2, path, 3, -1));
    int bad[1] = {250}; CHECK(!nav_build(&s, 0, 0, bad, 1, -1));
    CHECK(!nav_build(&s, 0, 0, path, 3, 4));
    CHECK(nav_build(&s, 0, 0, path, 3, -1) && s.ops[0].op == NAV_PRESS_IF && s.ops[0].mask == 0x10);
    CHECK(s.ops[s.nops - 1].op == NAV_END);
    /* Show: PLAYLIST pressed, knob ends at 2+41, no LOAD, cursor held after (remap). */
    reset(); CHECK(nav_arm(&s)); CHECK(!nav_arm(&s));            /* one at a time */
    long t = run(1000, 128, AZ_VIEW_BROWSE, 0);
    CHECK(t > 0 && nav_state == NAV_DONE && saw_press[0x10] > 0 && saw_click >= 2 && !saw_load);
    CHECK(last_knob == 43 && minknob == 2);
    CHECK(nav_off == 43 - 128);
    idle(129); nav_frame(F, 152, t, AZ_VIEW_BROWSE, playing); CHECK(F[34] == 44);   /* real +1 -> +1 */
    /* The DJ presses LOAD: next frame drops the remap (real value passes again). */
    idle(129); F[33] = 0x04; nav_frame(F, 152, t, AZ_VIEW_BROWSE, playing);
    idle(129); nav_frame(F, 152, t, AZ_VIEW_BROWSE, playing); CHECK(F[34] == 129 && nav_off == 0);
    /* Load on a stopped deck: LOAD 2 pressed, remap released right after. */
    reset(); CHECK(nav_build(&s, 0, 0, path, 3, 1) && nav_arm(&s));
    t = run(1000, 128, AZ_VIEW_BROWSE, 0);
    CHECK(nav_state == NAV_DONE && saw_load == 0x04);
    idle(128); nav_frame(F, 152, t, AZ_VIEW_BROWSE, playing);
    idle(128); nav_frame(F, 152, t, AZ_VIEW_BROWSE, playing); CHECK(F[34] == 128 && nav_off == 0);
    /* Load refused when the deck is playing at LOAD time: no LOAD bit ever sent. */
    reset(); busy = 1; CHECK(nav_build(&s, 0, 0, path, 3, 1) && nav_arm(&s));
    run(1000, 128, AZ_VIEW_BROWSE, 0);
    CHECK(nav_state == NAV_REFUSED && !saw_load && nav_refused_deck == 2);
    /* The DJ touches a control mid-script: cancelled, their BACK passes, nothing more sent. */
    reset(); CHECK(nav_build(&s, 0, 0, path, 3, 1) && nav_arm(&s));
    run(1000, 128, AZ_VIEW_BROWSE, 150);
    CHECK(nav_state == NAV_CANCELLED && !saw_load);
    /* Armed while hands are busy: waits, doesn't start. */
    reset(); CHECK(nav_build(&s, 0, 0, path, 3, -1) && nav_arm(&s));
    idle(128); F[32] = 0x40; nav_frame(F, 152, 0, AZ_VIEW_BROWSE, playing); CHECK(nav_state == NAV_ARMED);
    /* Source switch: SOURCE pressed; reopened only when BACK left SOURCE. */
    reset(); CHECK(nav_build(&s, 1, 1, path, 3, -1) && nav_arm(&s));
    run(1000, 128, AZ_VIEW_SOURCE, 0); int src_stay = saw_press[0x80];
    reset(); CHECK(nav_build(&s, 1, 1, path, 3, -1) && nav_arm(&s));
    run(1000, 128, AZ_VIEW_BROWSE, 0); int src_left = saw_press[0x80];
    CHECK(src_stay > 0 && src_left > src_stay);
    /* PLAYLIST from the deck screen: pressed once (the second, deck-only press). From a
     * browser screen that stays a browser screen: pressed once (the first). */
    reset(); CHECK(nav_build(&s, 0, 0, path, 3, -1) && nav_arm(&s));
    run(1000, 128, AZ_VIEW_DECK, 0); int from_deck = saw_press[0x10];
    reset(); CHECK(nav_build(&s, 0, 0, path, 3, -1) && nav_arm(&s));
    run(1000, 128, AZ_VIEW_BROWSE, 0); int from_browse = saw_press[0x10];
    CHECK(from_deck > 0 && from_browse > 0 && from_deck < 80 && from_browse < 80);
    /* Short frames are ignored. */
    reset(); idle(128); CHECK(!nav_frame(F, 97, 0, AZ_VIEW_BROWSE, playing));
    printf("nav: %d checks passed\n", checks);
    return 0;
}
