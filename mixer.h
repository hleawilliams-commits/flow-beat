/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat 0.5.0, FLOW Mixer Link: the XDJ-AZ's mixer panel, read from its input frame
 * (AZ_BUS_MIXER_IN, 128 bytes) and turned into one "mixer" packet for FLOW VJ.
 *
 * The AZ mixes audio in hardware, but the panel MCU still reports every knob and fader to
 * EP147 on the mixer bus, so the positions are there even though the sound is not.
 * Byte offsets are from Kyle Hosman's AZ-EMU (az-preview/src/controls.rs: MX_AT, MX10,
 * MIXER_BUTTONS, found by reading EP147's own huiid::Mixer control objects and by sweeping
 * one knob at a time on a deck) and the AZ-MODS core (bus_interpose.c: Beat FX bytes).
 *
 * Most controls are 10-bit: the top 8 bits in one byte, the low 2 bits in a shared byte
 * (four controls per byte, shifts 6, 4, 2, 0). Sent as 0..1, 3 decimals.
 *
 * Buttons on this bus are momentary (the bit is set while held), so the selected Sound
 * Color FX and the Beat FX channel are latched here from the presses; they read "?" until
 * the first press after EP147 starts. Pure code: no core calls, so tests run it on a host.
 */
#ifndef FLOW_BEAT_MIXER_H
#define FLOW_BEAT_MIXER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MIXER_FRAME 128

/* a 10-bit control: high byte, low-bits byte, shift */
typedef struct { uint8_t hi, lo, sh; } mx10;

/* channels 1..4: TRIM, HI, MID, LOW, COLOR, fader */
static const mx10 MX_TRIM[4]  = { {0x24, 0x28, 6}, {0x29, 0x2d, 6}, {0x2e, 0x32, 6}, {0x33, 0x37, 6} };
static const mx10 MX_HI[4]    = { {0x25, 0x28, 4}, {0x2a, 0x2d, 4}, {0x2f, 0x32, 4}, {0x34, 0x37, 4} };
static const mx10 MX_MID[4]   = { {0x26, 0x28, 2}, {0x2b, 0x2d, 2}, {0x30, 0x32, 2}, {0x35, 0x37, 2} };
static const mx10 MX_LOW[4]   = { {0x27, 0x28, 0}, {0x2c, 0x2d, 0}, {0x31, 0x32, 0}, {0x36, 0x37, 0} };
static const mx10 MX_COLOR[4] = { {0x3d, 0x41, 6}, {0x3e, 0x41, 4}, {0x3f, 0x41, 2}, {0x40, 0x41, 0} };
static const mx10 MX_FADER[4] = { {0x0a, 0x0e, 6}, {0x0b, 0x0e, 4}, {0x0c, 0x0e, 2}, {0x0d, 0x0e, 0} };
static const mx10 MX_XFADER   = { 0x0f, 0x10, 0 };
static const mx10 MX_MASTER   = { 0x38, 0x3c, 6 };
static const mx10 MX_M_HI     = { 0x39, 0x3c, 4 };
static const mx10 MX_M_MID    = { 0x3a, 0x3c, 2 };
static const mx10 MX_M_LOW    = { 0x3b, 0x3c, 0 };
static const mx10 MX_BOOTH    = { 0x4a, 0x4b, 0 };
static const mx10 MX_FX_LEVEL = { 0x45, 0x46, 0 };   /* Beat FX LEVEL/DEPTH */
#define MX_CFX_PARAM 0x4e                            /* Sound Color FX PARAMETER, 8 bits only */

#define MX_CFX_BTNS  0x15   /* Sound Color FX buttons (MASTER CUE 0x40, LINK CUE 0x80 share it) */
#define MX_BFX_SEL   0x16   /* Beat FX: bit 0 ON/OFF press, high nibble SELECT (Gray code) */
#define MX_BFX_CH    0x17   /* Beat FX channel buttons */
#define MX_BFX_TIME  0x1a   /* Beat FX TIME encoder: signed 16-bit counter, low byte first */
#define MX_XPAD_POS  0x1d   /* X-PAD finger position 0..124 */
#define MX_XPAD_ZONE 0x1e   /* X-PAD zone bitmap, non-zero while touched */
#define MX_BFX_STATE 0x54   /* bit 0x80: the panel's Beat FX active latch */
#define MX_BFX_POS   0x55   /* high nibble: Beat FX SELECT as 0..15 */

static const struct { uint8_t bit; const char *name; } CFX_BTN[] = {
    { 0x02, "SPACE" }, { 0x10, "DUB ECHO" }, { 0x01, "CRUSH" },
    { 0x08, "SWEEP" }, { 0x20, "NOISE" },    { 0x04, "FILTER" },
};
static const struct { uint8_t bit; const char *name; } BFX_CH_BTN[] = {
    { 0x80, "1" }, { 0x40, "2" }, { 0x20, "3" }, { 0x10, "4" },
    { 0x01, "MIC" }, { 0x08, "CF.A" }, { 0x04, "CF.B" }, { 0x02, "MASTER" },
};
#define N_CFX ((int)(sizeof CFX_BTN / sizeof CFX_BTN[0]))
#define N_BFX_CH ((int)(sizeof BFX_CH_BTN / sizeof BFX_CH_BTN[0]))

typedef struct {
    uint16_t trim[4], hi[4], mid[4], low[4], color[4], fader[4];   /* 0..1023 */
    uint16_t xf, master, m_hi, m_mid, m_low, booth, fx_level, cfx_param;
    int cfx;        /* index into CFX_BTN, -1 off, -2 not known yet */
    int bfx_ch;     /* index into BFX_CH_BTN, -2 not known yet */
    int bfx_sel;    /* 0..15 */
    int bfx_on;     /* the panel's active latch */
    int bfx_time;   /* the TIME encoder's counter */
    int xpad_pos, xpad_touch;
} mixer_state;

/* press edges between two calls: the latch state lives here */
typedef struct {
    uint8_t cfx_btns, ch_btns;
    int have;
    int cfx, bfx_ch;
} mixer_latch;

static inline void mixer_latch_init(mixer_latch *l)
{
    memset(l, 0, sizeof *l);
    l->cfx = -2;
    l->bfx_ch = -2;
}

static inline uint16_t mx10_get(const unsigned char *f, mx10 c)
{
    return (uint16_t)(f[c.hi] << 2 | ((f[c.lo] >> c.sh) & 3));
}

/* frame: at least MIXER_FRAME bytes. Returns 0, or -1 if the frame is too short. */
static inline int mixer_decode(const unsigned char *f, int len, mixer_latch *l, mixer_state *s)
{
    if (!f || len < MIXER_FRAME) return -1;
    memset(s, 0, sizeof *s);
    for (int c = 0; c < 4; c++) {
        s->trim[c] = mx10_get(f, MX_TRIM[c]);
        s->hi[c] = mx10_get(f, MX_HI[c]);
        s->mid[c] = mx10_get(f, MX_MID[c]);
        s->low[c] = mx10_get(f, MX_LOW[c]);
        s->color[c] = mx10_get(f, MX_COLOR[c]);
        s->fader[c] = mx10_get(f, MX_FADER[c]);
    }
    s->xf = mx10_get(f, MX_XFADER);
    s->master = mx10_get(f, MX_MASTER);
    s->m_hi = mx10_get(f, MX_M_HI);
    s->m_mid = mx10_get(f, MX_M_MID);
    s->m_low = mx10_get(f, MX_M_LOW);
    s->booth = mx10_get(f, MX_BOOTH);
    s->fx_level = mx10_get(f, MX_FX_LEVEL);
    s->cfx_param = (uint16_t)(f[MX_CFX_PARAM] << 2);

    /* Sound Color FX: a press selects that FX, pressing the lit one again turns it off.
     * Beat FX channel: a press selects that channel. The first frame seen is not a press. */
    uint8_t cb = f[MX_CFX_BTNS] & 0x3f, hb = f[MX_BFX_CH];
    if (l->have) {
        for (int i = 0; i < N_CFX; i++)
            if ((cb & CFX_BTN[i].bit) && !(l->cfx_btns & CFX_BTN[i].bit))
                l->cfx = l->cfx == i ? -1 : i;
        for (int i = 0; i < N_BFX_CH; i++)
            if ((hb & BFX_CH_BTN[i].bit) && !(l->ch_btns & BFX_CH_BTN[i].bit))
                l->bfx_ch = i;
    }
    l->cfx_btns = cb;
    l->ch_btns = hb;
    l->have = 1;
    s->cfx = l->cfx;
    s->bfx_ch = l->bfx_ch;

    s->bfx_sel = f[MX_BFX_POS] >> 4;
    s->bfx_on = (f[MX_BFX_STATE] & 0x80) != 0;
    s->bfx_time = (int16_t)(f[MX_BFX_TIME] | f[MX_BFX_TIME + 1] << 8);
    s->xpad_pos = f[MX_XPAD_POS] & 0x7f;
    s->xpad_touch = f[MX_XPAD_ZONE] != 0;
    return 0;
}

static inline int mx_absdiff(int a, int b) { return a > b ? a - b : b - a; }

static inline int mx_moved(const uint16_t *a, const uint16_t *b, int n)
{
    for (int i = 0; i < n; i++)
        if (mx_absdiff(a[i], b[i]) > 1) return 1;
    return 0;
}

/* 1 if a and b differ by more than knob jitter (1 count of 1023) or in any switch */
static inline int mixer_changed(const mixer_state *a, const mixer_state *b)
{
    if (mx_moved(a->trim, b->trim, 4) || mx_moved(a->hi, b->hi, 4) || mx_moved(a->mid, b->mid, 4) ||
        mx_moved(a->low, b->low, 4) || mx_moved(a->color, b->color, 4) || mx_moved(a->fader, b->fader, 4))
        return 1;
    const uint16_t ga[8] = { a->xf, a->master, a->m_hi, a->m_mid, a->m_low, a->booth, a->fx_level, a->cfx_param };
    const uint16_t gb[8] = { b->xf, b->master, b->m_hi, b->m_mid, b->m_low, b->booth, b->fx_level, b->cfx_param };
    if (mx_moved(ga, gb, 8)) return 1;
    return a->cfx != b->cfx || a->bfx_ch != b->bfx_ch || a->bfx_sel != b->bfx_sel ||
           a->bfx_on != b->bfx_on || a->bfx_time != b->bfx_time ||
           a->xpad_pos != b->xpad_pos || a->xpad_touch != b->xpad_touch;
}

static inline double mx_unit(uint16_t v) { return v / 1023.0; }

/* the "mixer" packet as JSON; returns its length */
static inline int mixer_json(char *buf, int cap, unsigned seq, const mixer_state *s)
{
    int n = snprintf(buf, (size_t)cap, "{\"flowbeat\":1,\"type\":\"mixer\",\"seq\":%u,\"ch\":[", seq);
    for (int c = 0; c < 4 && n < cap; c++)
        n += snprintf(buf + n, (size_t)(cap - n),
                      "%s{\"c\":%d,\"fader\":%.3f,\"trim\":%.3f,\"hi\":%.3f,\"mid\":%.3f,\"low\":%.3f,\"color\":%.3f}",
                      c ? "," : "", c + 1, mx_unit(s->fader[c]), mx_unit(s->trim[c]), mx_unit(s->hi[c]),
                      mx_unit(s->mid[c]), mx_unit(s->low[c]), mx_unit(s->color[c]));
    if (n >= cap) return cap - 1;
    const char *cfx = s->cfx >= 0 ? CFX_BTN[s->cfx].name : s->cfx == -1 ? "" : "?";
    const char *bch = s->bfx_ch >= 0 ? BFX_CH_BTN[s->bfx_ch].name : "?";
    n += snprintf(buf + n, (size_t)(cap - n),
                  "],\"xf\":%.3f,\"master\":%.3f,\"mhi\":%.3f,\"mmid\":%.3f,\"mlow\":%.3f,\"booth\":%.3f,"
                  "\"cfx\":\"%s\",\"cfxp\":%.3f,"
                  "\"bfx\":{\"sel\":%d,\"on\":%d,\"ch\":\"%s\",\"lvl\":%.3f,\"time\":%d},"
                  "\"xpad\":{\"pos\":%d,\"touch\":%d}}",
                  mx_unit(s->xf), mx_unit(s->master), mx_unit(s->m_hi), mx_unit(s->m_mid), mx_unit(s->m_low),
                  mx_unit(s->booth), cfx, mx_unit(s->cfx_param), s->bfx_sel, s->bfx_on, bch,
                  mx_unit(s->fx_level), s->bfx_time, s->xpad_pos, s->xpad_touch);
    return n < cap ? n : cap - 1;
}

#endif
