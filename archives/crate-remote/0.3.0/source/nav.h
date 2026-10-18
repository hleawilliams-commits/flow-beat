/* SPDX-License-Identifier: GPL-3.0-only
 * Crate Remote's deck navigator: plays a short script of panel presses and browse-knob
 * turns through the mixer input filter, the same inputs a DJ makes, to put the native
 * browser on one playlist row and optionally press LOAD for a stopped deck.
 *
 * What the emulator showed (ep147-9e9454e357389ece):
 *  - PLAYLIST opens the playlist tree; a knob click opens the highlighted folder/playlist.
 *  - The deck reads byte 34 as an absolute counter and turns the cursor by the plain
 *    difference between frames (no wrap handling). One frame may jump many rows.
 *  - A big up-turn right after a list opens is clamped at the top, so "go to 2, then to
 *    2 + n" lands on row n of any list up to ~250 rows.
 *  - SOURCE opens with focus on the left list or on the source's right pane; BACK moves
 *    focus left, or leaves SOURCE when focus already was left.
 *
 * Threads: the UI thread arms a script (nav_arm) only while the navigator is idle; the
 * mixer bus thread is the sole runner. Shared words are atomics; the script itself is
 * written before the ARMED store (release) and read after its load (acquire).
 */
#ifndef CR_NAV_H
#define CR_NAV_H
#include <stdint.h>
#include <string.h>
#include "azmod-mod.h"

enum { NAV_PRESS = 1, NAV_CLICK, NAV_WAIT, NAV_TOP, NAV_ROWS, NAV_PRESS_IF,
       NAV_LOAD, NAV_END };
/* NAV_PRESS_IF conditions (op.n): press only when the view matches. */
enum { NAV_WHEN_NOT_SOURCE = 1, NAV_WHEN_NOT_DECK, NAV_WHEN_DECK };
typedef struct { uint8_t op, off, mask; uint16_t ms; uint16_t n; } nav_op;

enum { NAV_IDLE = 0, NAV_ARMED, NAV_RUNNING, NAV_DONE, NAV_CANCELLED, NAV_REFUSED };
#define NAV_MAX_OPS 96

/* Panel bits (mixer frame): byte 32 BROWSE 0x40, SOURCE 0x80, PLAYLIST 0x10; byte 33
 * BACK 0x80, LOAD 1..4 = 0x08/0x04/0x02/0x01; byte 31 bit 0 knob push; byte 6 bit 3 is
 * the shared press latch (cleared while any panel button is held). */
static const uint8_t nav_load_mask[4] = {0x08, 0x04, 0x02, 0x01};

typedef struct {
    nav_op ops[NAV_MAX_OPS];
    int nops;
    int deck;                 /* LOAD target 0..3, or -1 */
} nav_script;

/* Shared state. */
static int nav_state;                 /* __atomic: NAV_* */
static nav_script nav_s;              /* written by UI before ARMED, read by bus after */
static int nav_refused_deck;          /* __atomic: deck that was playing at LOAD time */
/* Bus-thread-only runner state. */
static int nav_pc;
static long nav_t0;                   /* when the current op started (ms) */
static int nav_phase;                 /* sub-step inside an op */
static int nav_virt = -1;             /* counter value the deck last saw from us */
static int nav_off;                   /* knob remap after a script: out = real + off */
static int nav_real_last = -1;        /* last real counter seen */
static int nav_drop_after_load;       /* drop the remap once the DJ presses LOAD */

/* ---- script building (UI thread) ---- */
static void nav_add(nav_script *s, uint8_t op, uint8_t off, uint8_t mask, uint16_t ms, uint16_t n)
{
    if (s->nops < NAV_MAX_OPS) s->ops[s->nops++] = (nav_op){op, off, mask, ms, n};
}
/* Hold times from the emulator: screen tabs want ~1 s, BACK must stay short (a long BACK
 * closes a whole menu), a knob push ~0.35 s. */
static void nav_press(nav_script *s, uint8_t off, uint8_t mask, uint16_t hold, uint16_t after)
{
    nav_add(s, NAV_PRESS, off, mask, hold, 0);
    nav_add(s, NAV_WAIT, 0, 0, after, 0);
}
static void nav_rows(nav_script *s, uint16_t n, int click_after)
{
    nav_add(s, NAV_TOP, 0, 0, 300, 0);
    nav_add(s, NAV_ROWS, 0, 0, 600, n);
    if (click_after) { nav_add(s, NAV_CLICK, 0, 0, 350, 0); nav_add(s, NAV_WAIT, 0, 0, 1800, 0); }
}
/* Build: optional source switch to `stick`, then PLAYLIST and one row per level.
 * path[] are 0-based rows, each < 250. deck = -1 for "show only". */
static int nav_build(nav_script *s, int select_stick, int stick, const int *path, int depth, int deck)
{
    memset(s, 0, sizeof *s);
    s->deck = deck;
    if (depth < 1 || depth > 8 || stick < 0 || stick > 1 || deck < -1 || deck > 3) return 0;
    for (int i = 0; i < depth; i++) if (path[i] < 0 || path[i] > 249) return 0;
    if (select_stick) {
        nav_press(s, 32, 0x80, 1000, 1500);                 /* SOURCE */
        nav_press(s, 33, 0x80, 150, 800);                   /* BACK: focus to the left list */
        nav_add(s, NAV_PRESS_IF, 32, 0x80, 1000, NAV_WHEN_NOT_SOURCE); /* BACK left SOURCE: reopen */
        nav_add(s, NAV_WAIT, 0, 0, 1500, 0);
        nav_rows(s, (uint16_t)stick, 1);                    /* USB row: focus moves right */
        nav_rows(s, 0, 1);                                  /* Local Library is the top row */
        nav_add(s, NAV_WAIT, 0, 0, 1500, 0);
    }
    /* PLAYLIST toggles its own screen: from another browser screen it switches to the
     * playlist tree, on the playlist screen it closes it. Press, then press again if that
     * closed the browser; from the deck screen one press opens it. */
    nav_add(s, NAV_PRESS_IF, 32, 0x10, 1000, NAV_WHEN_NOT_DECK);
    nav_add(s, NAV_WAIT, 0, 0, 1500, 0);
    nav_add(s, NAV_PRESS_IF, 32, 0x10, 1000, NAV_WHEN_DECK);
    nav_add(s, NAV_WAIT, 0, 0, 2000, 0);
    /* PLAYLIST reopens the last list visited. BACK climbs one level and does nothing at
     * the top of the tree, so eight BACKs always reach the root. */
    for (int i = 0; i < 8; i++) nav_press(s, 33, 0x80, 150, 600);
    for (int i = 0; i < depth; i++) nav_rows(s, (uint16_t)path[i], i < depth - 1);
    if (deck >= 0) {
        nav_add(s, NAV_WAIT, 0, 0, 400, 0);
        nav_add(s, NAV_LOAD, 33, nav_load_mask[deck], 150, 0);
        nav_add(s, NAV_WAIT, 0, 0, 400, 0);
    }
    nav_add(s, NAV_END, 0, 0, 0, 0);
    return s->nops < NAV_MAX_OPS;
}
/* UI thread: start a script if the navigator is idle or finished. */
static int nav_arm(const nav_script *s)
{
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
    if (st == NAV_ARMED || st == NAV_RUNNING) return 0;
    nav_s = *s;
    __atomic_store_n(&nav_state, NAV_ARMED, __ATOMIC_RELEASE);
    return 1;
}

/* ---- running (mixer bus thread) ---- */
/* Real input the DJ made this frame (before our edits): any panel button, knob push or
 * knob turn cancels a script. */
static int nav_real_input(const unsigned char *f, int knob_moved)
{
    return f[32] || f[33] || (f[31] & 1) || knob_moved;
}
static void nav_finish(int state, int real)
{
    /* Keep the cursor still: from now on the deck sees real + off. */
    if (nav_virt >= 0 && real >= 0) nav_off = nav_virt - real;
    __atomic_store_n(&nav_state, state, __ATOMIC_RELEASE);
}
/* deck_playing(deck) -> 1 playing / unknown, 0 stopped; injected for host tests. */
typedef int (*nav_playing_fn)(int deck);

/* One mixer frame. now = monotonic ms (read only when a script is live or a remap is on);
 * view = the SDK's current view. Returns 1 if the frame was edited. */
static int nav_frame(unsigned char *f, int len, long now, int view, nav_playing_fn playing)
{
    if (len < 98) return 0;
    int real = f[34];
    int knob_moved = nav_real_last >= 0 && real != nav_real_last;
    nav_real_last = real;
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);

    if (st != NAV_ARMED && st != NAV_RUNNING) {
        if (!nav_off) return 0;
        /* After a "show": once the DJ presses LOAD the landing has served its purpose;
         * drop the remap (the cursor jumps back by `off`, after the load). */
        if (f[33] & 0x0f) nav_drop_after_load = 1;
        else if (nav_drop_after_load) { nav_drop_after_load = 0; nav_off = 0; nav_virt = -1; return 0; }
        f[34] = (unsigned char)((real + nav_off) & 0xff);
        return 1;
    }
    if (st == NAV_ARMED) {
        if (nav_real_input(f, knob_moved)) return 0;          /* wait for idle hands */
        nav_pc = 0; nav_phase = 0; nav_t0 = now; nav_drop_after_load = 0;
        nav_virt = (real + nav_off) & 0xff;                   /* continue from what the deck saw */
        __atomic_store_n(&nav_state, NAV_RUNNING, __ATOMIC_RELEASE);
    } else if (nav_real_input(f, knob_moved)) {
        nav_finish(NAV_CANCELLED, real);                      /* the DJ's input passes untouched */
        f[34] = (unsigned char)((real + nav_off) & 0xff);
        return 1;
    }
    for (int guard = 0; guard < 4 && nav_pc < nav_s.nops; guard++) {
        const nav_op *o = &nav_s.ops[nav_pc];
        long el = now - nav_t0;
        int next = 0;
        switch (o->op) {
        case NAV_PRESS: case NAV_CLICK: case NAV_LOAD:
            if (o->op == NAV_LOAD && nav_phase == 0) {
                if (playing(nav_s.deck)) {                    /* never load over a playing deck */
                    __atomic_store_n(&nav_refused_deck, nav_s.deck + 1, __ATOMIC_RELAXED);
                    nav_finish(NAV_REFUSED, real);
                    f[34] = (unsigned char)(nav_virt & 0xff);
                    return 1;
                }
                nav_phase = 1;
            }
            if (el < o->ms) {
                if (o->op == NAV_CLICK) f[31] |= 1;
                else f[o->off] |= o->mask;
                f[6] &= (unsigned char)~0x08;
            } else next = 1;
            break;
        case NAV_WAIT:
            next = el >= o->ms;
            break;
        case NAV_PRESS_IF:
            if (nav_phase == 0) {
                int want = o->n == NAV_WHEN_NOT_SOURCE ? view != AZ_VIEW_SOURCE :
                           o->n == NAV_WHEN_NOT_DECK ? view != AZ_VIEW_DECK : view == AZ_VIEW_DECK;
                if (!want) { next = 1; break; }
                nav_phase = 1;
            }
            if (el < o->ms) { f[o->off] |= o->mask; f[6] &= (unsigned char)~0x08; } else next = 1;
            break;
        case NAV_TOP:
            nav_virt = 2;                                     /* clamped at the list's top */
            next = el >= o->ms;
            break;
        case NAV_ROWS:
            nav_virt = 2 + o->n;
            next = el >= o->ms;
            break;
        case NAV_END:
            nav_finish(NAV_DONE, real);
            if (nav_s.deck >= 0) nav_drop_after_load = 1;    /* loaded: release the remap next frame */
            f[34] = (unsigned char)(nav_virt & 0xff);
            return 1;
        default:
            next = 1;
        }
        if (!next) break;
        nav_pc++; nav_phase = 0; nav_t0 = now;
    }
    f[34] = (unsigned char)(nav_virt & 0xff);
    return 1;
}
#endif
