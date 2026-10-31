/* SPDX-License-Identifier: GPL-3.0-only
 * FLOW Status Panel: FlowVJ's state on the deck screen, and tap buttons that send back to it.
 * Pure parsing and packing, no SDK calls or sockets, so it is host-tested.
 *
 * FlowVJ -> deck, UDP 9102, a few times a second and on every change (FlowVJ's serve.py):
 *   FVS1\n seq=<n>\n look=<visual look>\n lights=<desk look>\n lock=0|1\n src=<beat source>\n
 *   bpm=<tempo>\n safe=0|1\n hold=0|1\n auto=0|1\n black=0|1\n tap=<port>\n
 * Unknown keys are ignored, missing ones read as 0 / "". lock = FlowVJ's beat clock follows an
 * outside source (the AZ, Link, MIDI clock), not its own ear or a tap.
 *
 * deck -> FlowVJ, UDP <tap> (9103) at the address the last FVS1 came from:
 *   FVC1 <seq> <act>
 */
#ifndef CS_FLOWVJ_H
#define CS_FLOWVJ_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FV_PORT 9102
#define FV_TAP_PORT 9103
#define FV_TEXT 48
#define FV_STALE_MS 3000   /* no FVS1 for this long = FlowVJ not seen: taps go back to the deck */
#define FV_TAPS 8          /* vj_tap buttons the touch filter checks, across all panels */

/* Tap actions. FlowVJ's deck-status.js runs each one (see its ACTS table). */
enum { FV_ACT_NONE = 0, FV_ACT_NEXT, FV_ACT_PREV, FV_ACT_HOLD, FV_ACT_SAFE, FV_ACT_AUTO, FV_ACT_DROP,
       FV_ACT_BLACK, FV_ACTS };
static const char *const FV_ACT_NAME[FV_ACTS] = { "", "next", "prev", "hold", "safe", "auto", "drop", "black" };
static const char *const FV_ACT_LABEL[FV_ACTS] = { "", "NEXT", "PREV", "HOLD", "SAFE", "AUTO", "DROP", "BLACK" };

typedef struct {
    unsigned seq;
    int lock, safe, hold, autop, black, tap_port, bpm10;
    char look[FV_TEXT], lights[FV_TEXT], src[12];
} fv_state;

static inline int fv_act_of(const char *s, size_t n)
{
    for (int a = 1; a < FV_ACTS; a++)
        if (strlen(FV_ACT_NAME[a]) == n && !strncmp(FV_ACT_NAME[a], s, n)) return a;
    return FV_ACT_NONE;
}

/* 1 = this action's button shows lit (its switch is on) */
static inline int fv_act_on(const fv_state *s, int act)
{
    switch (act) {
    case FV_ACT_HOLD: return s->hold;
    case FV_ACT_SAFE: return s->safe;
    case FV_ACT_AUTO: return s->autop;
    case FV_ACT_BLACK: return s->black;
    default: return 0;
    }
}

static inline void fv_copy(char *dst, size_t cap, const char *s, size_t n)
{
    if (n >= cap) n = cap - 1;
    /* don't cut a UTF-8 character in half */
    if (n == cap - 1) while (n && ((unsigned char)s[n] & 0xc0u) == 0x80u) n--;
    memcpy(dst, s, n); dst[n] = 0;
    for (size_t i = 0; i < n; i++) if ((unsigned char)dst[i] < 0x20) dst[i] = ' ';
}

static inline int fv_flag(const char *v, size_t n) { return n == 1 && v[0] == '1'; }

/* One datagram. Returns 1 and fills *out when it is an FVS1 packet, else 0 (out untouched). */
static inline int fv_parse(const char *buf, size_t len, fv_state *out)
{
    if (len < 5 || memcmp(buf, "FVS1", 4) || (buf[4] != '\n' && buf[4] != '\r')) return 0;
    fv_state s;
    memset(&s, 0, sizeof s);
    size_t i = 5;
    while (i < len) {
        size_t j = i;
        while (j < len && buf[j] != '\n') j++;
        size_t e = j;
        if (e > i && buf[e - 1] == '\r') e--;
        const char *eq = memchr(buf + i, '=', e - i);
        if (eq) {
            const char *k = buf + i, *v = eq + 1;
            size_t kn = (size_t)(eq - k), vn = (size_t)(buf + e - v);
#define FV_K(w) (kn == sizeof(w) - 1 && !memcmp(k, w, kn))
            char num[16];
            size_t nn = vn < sizeof num - 1 ? vn : sizeof num - 1;
            memcpy(num, v, nn); num[nn] = 0;
            if (FV_K("seq")) s.seq = (unsigned)strtoul(num, NULL, 10);
            else if (FV_K("look")) fv_copy(s.look, sizeof s.look, v, vn);
            else if (FV_K("lights")) fv_copy(s.lights, sizeof s.lights, v, vn);
            else if (FV_K("src")) fv_copy(s.src, sizeof s.src, v, vn);
            else if (FV_K("lock")) s.lock = fv_flag(v, vn);
            else if (FV_K("safe")) s.safe = fv_flag(v, vn);
            else if (FV_K("hold")) s.hold = fv_flag(v, vn);
            else if (FV_K("auto")) s.autop = fv_flag(v, vn);
            else if (FV_K("black")) s.black = fv_flag(v, vn);
            else if (FV_K("tap")) { long p = strtol(num, NULL, 10); s.tap_port = p > 0 && p < 65536 ? (int)p : 0; }
            else if (FV_K("bpm")) { double b = strtod(num, NULL); s.bpm10 = b > 0 && b < 400 ? (int)(b * 10 + 0.5) : 0; }
#undef FV_K
        }
        i = j + 1;
    }
    if (!s.tap_port) s.tap_port = FV_TAP_PORT;
    *out = s;
    return 1;
}

/* The tap datagram; returns its length (0 for an unknown action). */
static inline int fv_cmd(char *buf, size_t cap, unsigned seq, int act)
{
    if (act <= FV_ACT_NONE || act >= FV_ACTS) return 0;
    int n = snprintf(buf, cap, "FVC1 %u %s\n", seq, FV_ACT_NAME[act]);
    return n > 0 && (size_t)n < cap ? n : 0;
}

/* A tap button on the screen, in screen pixels. */
typedef struct { int x, y, w, h, act; } fv_tap;

/* The action under (x, y), or FV_ACT_NONE. */
static inline int fv_tap_hit(const fv_tap *t, int n, int x, int y)
{
    for (int i = 0; i < n; i++)
        if (x >= t[i].x && x < t[i].x + t[i].w && y >= t[i].y && y < t[i].y + t[i].h) return t[i].act;
    return FV_ACT_NONE;
}
#endif
