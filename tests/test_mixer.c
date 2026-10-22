/* SPDX-License-Identifier: GPL-3.0-only
 * FLOW Mixer Link (mixer.h) on a host: cc -I.. -o /tmp/tm test_mixer.c && /tmp/tm | python3 -m json.tool
 * The idle frame is the one AZ-EMU replays (faders and master down, every knob at 12 o'clock). */
#include <assert.h>
#include <stdio.h>
#include "../mixer.h"

static const unsigned char IDLE[MIXER_FRAME] = {
    3, 1, 2, 1, 1, 1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 124, 2, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 255,
    0, 0, 0, 0, 128, 0, 123, 128, 128, 128, 64, 123, 128, 128, 128, 0, 128, 128, 128, 128, 192,
    128, 128, 128, 128, 0, 0, 128, 128, 128, 0, 128, 128, 128, 128, 0, 128, 128, 128, 42, 2, 255,
    0, 128, 0, 192, 128, 121, 128, 0, 0, 0, 0, 0, 1, 0, 10, 0, 176, 4, 244, 1, 0, 88, 0, 0, 82, 17,
};

static void set10(unsigned char *f, mx10 c, unsigned v)
{
    f[c.hi] = (unsigned char)(v >> 2);
    f[c.lo] = (unsigned char)((f[c.lo] & ~(3u << c.sh)) | ((v & 3u) << c.sh));
}

int main(void)
{
    unsigned char f[MIXER_FRAME];
    memcpy(f, IDLE, sizeof f);
    mixer_latch l;
    mixer_state s, t;
    mixer_latch_init(&l);

    assert(mixer_decode(f, 64, &l, &s) == -1);                 /* short frame */
    assert(mixer_decode(f, MIXER_FRAME, &l, &s) == 0);
    for (int c = 0; c < 4; c++) assert(s.fader[c] == 0);       /* idle: faders down */
    assert(s.master == 0);
    assert(s.hi[2] >> 2 == 128 && s.mid[3] >> 2 == 128);       /* knobs at 12 o'clock */
    assert(s.cfx == -2 && s.bfx_ch == -2);                     /* no press seen yet */
    assert(s.bfx_sel == 0 && !s.xpad_touch);

    /* each fader, its low bits in the shared byte, and nobody else's */
    set10(f, MX_FADER[0], 1023);
    set10(f, MX_FADER[2], 513);
    set10(f, MX_XFADER, 1000);
    set10(f, MX_LOW[1], 3);
    mixer_decode(f, MIXER_FRAME, &l, &t);
    assert(t.fader[0] == 1023 && t.fader[1] == 0 && t.fader[2] == 513 && t.fader[3] == 0);
    assert(t.xf == 1000 && t.low[1] == 3 && t.hi[1] == s.hi[1] && t.trim[1] == s.trim[1]);
    assert(mixer_changed(&s, &t));

    /* jitter of one count is not a move */
    mixer_state u = t;
    u.fader[3] = 1;
    assert(!mixer_changed(&t, &u));
    u.fader[3] = 2;
    assert(mixer_changed(&t, &u));

    /* Color FX: press FILTER = FILTER, press again = off, press CRUSH = CRUSH */
    f[MX_CFX_BTNS] = 0x04; mixer_decode(f, MIXER_FRAME, &l, &s); assert(s.cfx == 5);
    f[MX_CFX_BTNS] = 0x04; mixer_decode(f, MIXER_FRAME, &l, &s); assert(s.cfx == 5);   /* held: one press */
    f[MX_CFX_BTNS] = 0x00; mixer_decode(f, MIXER_FRAME, &l, &s); assert(s.cfx == 5);
    f[MX_CFX_BTNS] = 0x04; mixer_decode(f, MIXER_FRAME, &l, &s); assert(s.cfx == -1);
    f[MX_CFX_BTNS] = 0x41; mixer_decode(f, MIXER_FRAME, &l, &s); assert(s.cfx == 2);   /* MASTER CUE ignored */
    f[MX_CFX_BTNS] = 0x00;

    /* Beat FX: channel latch, SELECT, active, TIME, X-PAD */
    f[MX_BFX_CH] = 0x02; mixer_decode(f, MIXER_FRAME, &l, &s); assert(s.bfx_ch == 7);
    f[MX_BFX_CH] = 0x00; mixer_decode(f, MIXER_FRAME, &l, &s); assert(s.bfx_ch == 7);
    f[MX_BFX_POS] = 0x30; f[MX_BFX_STATE] = 0x80;
    f[MX_BFX_TIME] = 0xfe; f[MX_BFX_TIME + 1] = 0xff;
    f[MX_XPAD_POS] = 62; f[MX_XPAD_ZONE] = 0x08;
    mixer_decode(f, MIXER_FRAME, &l, &s);
    assert(s.bfx_sel == 3 && s.bfx_on == 1 && s.bfx_time == -2 && s.xpad_pos == 62 && s.xpad_touch == 1);

    /* the packet fits a datagram with room to spare, and survives a tiny buffer */
    char buf[1400];
    int n = mixer_json(buf, (int)sizeof buf, 7, &s);
    assert(n > 0 && n < 700 && buf[n - 1] == '}' && (int)strlen(buf) == n);
    char small[40];
    assert(mixer_json(small, (int)sizeof small, 7, &s) == (int)sizeof small - 1);
    fprintf(stderr, "mixer.h: all checks passed (%d bytes)\n", n);
    puts(buf);
    return 0;
}
