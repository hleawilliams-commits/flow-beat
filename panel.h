/* SPDX-License-Identifier: GPL-3.0-only
 *
 * panel.h (flow-beat 0.6.2, "FLOW Lights"): the big lights panel on the deck screen.
 *
 * A LIGHTS button in the screen's top bar (over the Wi-Fi / Bluetooth icons) opens a panel over
 * the waveforms (x212 y48, 876 x 508). Beat FX and Custom Screen's small FlowVJ buttons stay
 * visible on the right. Two surfaces in all: the button and the panel.
 *
 *   LOOKS 1-8      FlowVJ Looks 1-8: the same "vjpad" packet Pad Lights' VJ page sends on 9100
 *   PREV NEXT HOLD AUTO SAFE   Custom Screen's FlowVJ buttons: "FVC1 <seq> <act>" to UDP 9103
 *   WHITE COLOR    SHIFT + pad 1 / 5 on the VJ page (FlowVJ white hit / colour bomb), and the
 *                  same hit on the deck's own Art-Net show
 *   STROBE (held)  FVC1 strobe (said again every second while held) and strobe_off on release;
 *                  the deck show flickers white while held (not while SAFE is on)
 *   DROP           FVC1 drop (the desk's drop scene + FlowVJ's visual drop); the deck show's drop
 *   FOG            FVC1 fog (the desk's 10 s hazer burst). The deck show never runs the hazer.
 *   BLACK          FVC1 black (the desk's blackout toggle); the deck show goes dark too
 *   COLOUR, INTENSITY, FOLLOW BEAT, DROP FLASH, FADER DIM, DECK SHOW on/off: the deck's own
 *                  Art-Net show (lights.h). Colour is the "Deck lights colours" row, DECK SHOW is
 *                  the "Lights from this deck" row, so the Mod Menu and the panel agree.
 *
 * FlowVJ state (look name, HOLD / AUTO / SAFE / BLACK lit) comes from FVS1 datagrams on UDP
 * 9104, when FlowVJ sends them there (Custom Screen owns 9102). Without them the panel still
 * sends; its lit buttons then show what was last tapped here.
 *
 * Pure code (no SDK calls, no sockets): geometry, hit tests, packets, FVS1 parsing, so
 * tests/test_panel.c checks it as it ships. */
#ifndef FLOW_BEAT_PANEL_H
#define FLOW_BEAT_PANEL_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* where things are, in screen pixels */
#define PN_X 212
#define PN_Y 48
#define PN_W 876
#define PN_H 508
#define PN_BTN_X 996
#define PN_BTN_Y 20
#define PN_BTN_W 86
#define PN_BTN_H 26
#define PN_FV_PORT 9104          /* FlowVJ -> flow-beat FVS1 (a copy of what Custom Screen gets on 9102) */
#define PN_TAP_PORT 9103         /* flow-beat -> FlowVJ FVC1 (deck_status.py) */
#define PN_FV_STALE_MS 3000
#define PN_SEQ_BASE 0x60000000u  /* FVC1 seqs: far from Custom Screen's (FlowVJ drops a seq it saw in 30 s) */
#define PN_N_BASE 0x50000000u    /* vjpad n: far from Pad Lights' own presses (FlowVJ dedupes side:n:down) */

enum {
    PN_NONE = 0,
    PN_LOOK1, PN_LOOK2, PN_LOOK3, PN_LOOK4, PN_LOOK5, PN_LOOK6, PN_LOOK7, PN_LOOK8,
    PN_PREV, PN_NEXT, PN_HOLD,
    PN_WHITE, PN_STROBE, PN_DROP, PN_COLOR, PN_FOG, PN_BLACK,
    PN_AUTO, PN_SAFE,
    PN_COL0, PN_COL1, PN_COL2, PN_COL3, PN_COL4, PN_COL5,   /* the "Deck lights colours" choices */
    PN_FOLLOW, PN_DFLASH, PN_FADER,
    PN_LEVEL,                                                /* the intensity fader (drag) */
    PN_SHOW,                                                 /* DECK SHOW on/off */
    PN_CLOSE,
    PN_IDS
};

typedef struct { int id, x, y, w, h; const char *label, *sub; } pn_btn;   /* x, y: from the panel's corner */

#define PN_LEVEL_TOP 314         /* the fader's travel, panel y: 100 % at the top */
#define PN_LEVEL_BOT 452

static const pn_btn PN_BTNS[] = {
    { PN_LOOK1, 14, 62, 104, 70, "1", "LOOK" },   { PN_LOOK2, 126, 62, 104, 70, "2", "LOOK" },
    { PN_LOOK3, 238, 62, 104, 70, "3", "LOOK" },  { PN_LOOK4, 350, 62, 104, 70, "4", "LOOK" },
    { PN_LOOK5, 14, 140, 104, 70, "5", "LOOK" },  { PN_LOOK6, 126, 140, 104, 70, "6", "LOOK" },
    { PN_LOOK7, 238, 140, 104, 70, "7", "LOOK" }, { PN_LOOK8, 350, 140, 104, 70, "8", "LOOK" },
    { PN_PREV, 14, 222, 160, 40, "< PREV", NULL }, { PN_NEXT, 182, 222, 160, 40, "NEXT >", NULL },
    { PN_HOLD, 350, 222, 104, 40, "HOLD", NULL },
    { PN_WHITE, 478, 62, 120, 70, "WHITE", "flash" },  { PN_STROBE, 606, 62, 120, 70, "STROBE", "hold" },
    { PN_DROP, 734, 62, 120, 70, "DROP", "now" },      { PN_COLOR, 478, 140, 120, 70, "COLOR", "bomb" },
    { PN_FOG, 606, 140, 120, 70, "FOG", "burst" },     { PN_BLACK, 734, 140, 120, 70, "BLACK", "out" },
    { PN_AUTO, 478, 222, 184, 40, "AUTO LOOKS", NULL }, { PN_SAFE, 670, 222, 184, 40, "SAFE (no strobe)", NULL },
    { PN_COL0, 14, 304, 92, 72, "CYCLE", NULL },   { PN_COL1, 114, 304, 92, 72, "RISO", NULL },
    { PN_COL2, 214, 304, 92, 72, "FLOW", NULL },   { PN_COL3, 314, 304, 92, 72, "SUNSET", NULL },
    { PN_COL4, 414, 304, 92, 72, "ICE", NULL },    { PN_COL5, 514, 304, 92, 72, "WHITE", NULL },
    { PN_FOLLOW, 14, 410, 190, 52, "FOLLOW BEAT", "chase on the beat" },
    { PN_DFLASH, 212, 410, 190, 52, "DROP FLASH", "white hit on drops" },
    { PN_FADER, 410, 410, 190, 52, "FADER DIM", "loudest fader dims" },
    { PN_LEVEL, 676, 304, 180, 158, "INTENSITY", NULL },
    { PN_SHOW, 600, 6, 170, 28, "DECK SHOW", NULL },
    { PN_CLOSE, 784, 6, 80, 28, "CLOSE", NULL },
};
#define PN_NBTNS ((int)(sizeof PN_BTNS / sizeof PN_BTNS[0]))

static inline int pn_in(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}
/* screen x, y -> the button under it (PN_NONE: none). Only the panel's own buttons. */
static inline int pn_hit(int x, int y)
{
    if (!pn_in(x, y, PN_X, PN_Y, PN_W, PN_H)) return PN_NONE;
    for (int i = 0; i < PN_NBTNS; i++) {
        const pn_btn *b = &PN_BTNS[i];
        if (pn_in(x, y, PN_X + b->x, PN_Y + b->y, b->w, b->h)) return b->id;
    }
    return PN_NONE;
}
static inline int pn_in_panel(int x, int y) { return pn_in(x, y, PN_X, PN_Y, PN_W, PN_H); }
static inline int pn_in_button(int x, int y)
{
    /* a few pixels of slop: the top bar is small */
    return pn_in(x, y, PN_BTN_X - 4, PN_BTN_Y - 6, PN_BTN_W + 8, PN_BTN_H + 12);
}
static inline const pn_btn *pn_btn_of(int id)
{
    for (int i = 0; i < PN_NBTNS; i++) if (PN_BTNS[i].id == id) return &PN_BTNS[i];
    return NULL;
}
/* screen y on the fader -> intensity 0..100 */
static inline int pn_level_at(int y)
{
    int top = PN_Y + PN_LEVEL_TOP, bot = PN_Y + PN_LEVEL_BOT;
    int v = (int)(((double)(bot - y) / (bot - top)) * 100.0 + 0.5);
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

/* ---- what goes out ------------------------------------------------------------------------- */
/* Custom Screen's FVC1 acts that FlowVJ's deck_status.py accepts (its ACTS) */
static inline const char *pn_act(int id)
{
    switch (id) {
    case PN_PREV: return "prev";
    case PN_NEXT: return "next";
    case PN_HOLD: return "hold";
    case PN_AUTO: return "auto";
    case PN_SAFE: return "safe";
    case PN_DROP: return "drop";
    case PN_BLACK: return "black";
    case PN_FOG: return "fog";
    case PN_STROBE: return "strobe";
    default: return NULL;
    }
}
/* vjpad: Looks are pads 1-8; WHITE = SHIFT + 1 (dmx_hit), COLOR = SHIFT + 5 (colour bomb) */
static inline int pn_vjpad(int id, int *pad, int *shift)
{
    if (id >= PN_LOOK1 && id <= PN_LOOK8) { *pad = id - PN_LOOK1 + 1; *shift = 0; return 1; }
    if (id == PN_WHITE) { *pad = 1; *shift = 1; return 1; }
    if (id == PN_COLOR) { *pad = 5; *shift = 1; return 1; }
    return 0;
}
static inline int pn_vjpad_json(char *buf, int cap, int pad, int down, int shift, unsigned n)
{
    int len = snprintf(buf, (size_t)cap, "{\"flowbeat\":1,\"type\":\"vjpad\",\"side\":1,\"pad\":%d,\"down\":%d,\"shift\":%d,\"n\":%u}",
                       pad, down ? 1 : 0, shift ? 1 : 0, n);
    return len < 0 || len >= cap ? 0 : len;
}
static inline int pn_fvc(char *buf, int cap, unsigned seq, const char *act)
{
    int len = snprintf(buf, (size_t)cap, "FVC1 %u %s", seq, act);
    return len < 0 || len >= cap ? 0 : len;
}

/* ---- FlowVJ's state (FVS1, the same text Custom Screen reads) ------------------------------ */
typedef struct {
    int hold, safe, autop, black;
    char look[40];
} pn_fv;

/* Latin-1 for the deck's text: drop control bytes, map UTF-8 to '?' where it isn't Latin-1 */
static inline void pn_copy(char *dst, int cap, const char *s, int n)
{
    int o = 0;
    for (int i = 0; i < n && o < cap - 1; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) continue;
        if (c < 0x80) { dst[o++] = (char)c; continue; }
        if ((c == 0xc2 || c == 0xc3) && i + 1 < n) {              /* U+0080..U+00FF */
            unsigned char d = (unsigned char)s[++i];
            dst[o++] = (char)(c == 0xc2 ? d : d + 0x40);
            continue;
        }
        while (i + 1 < n && ((unsigned char)s[i + 1] & 0xc0) == 0x80) i++;   /* skip the rest */
        dst[o++] = '?';
    }
    dst[o] = 0;
}
/* 1 = a FVS1 datagram, read into *out */
static inline int pn_fv_parse(const char *s, int n, pn_fv *out)
{
    if (n < 4 || memcmp(s, "FVS1", 4) != 0) return 0;
    memset(out, 0, sizeof *out);
    int i = 4;
    while (i < n) {
        int e = i;
        while (e < n && s[e] != '\n') e++;
        const char *eq = memchr(s + i, '=', (size_t)(e - i));
        if (eq) {
            int kl = (int)(eq - (s + i));
            const char *v = eq + 1;
            int vl = (int)(s + e - v);
            if (kl == 4 && !memcmp(s + i, "look", 4)) pn_copy(out->look, (int)sizeof out->look, v, vl);
            else if (kl == 4 && !memcmp(s + i, "hold", 4)) out->hold = vl > 0 && v[0] == '1';
            else if (kl == 4 && !memcmp(s + i, "safe", 4)) out->safe = vl > 0 && v[0] == '1';
            else if (kl == 4 && !memcmp(s + i, "auto", 4)) out->autop = vl > 0 && v[0] == '1';
            else if (kl == 5 && !memcmp(s + i, "black", 5)) out->black = vl > 0 && v[0] == '1';
        }
        i = e + 1;
    }
    return 1;
}

#endif
