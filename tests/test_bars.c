/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Link on Deck: the bar lookup from a track's rekordbox PQTZ beat grid (bars_set / bars_at in
 * flow-beat.c), on a computer. Builds flow-beat.c into this test so the static helpers are
 * reachable.   make -C tests bars
 */
#include "../flow-beat.c"

/* Link itself is not part of this test */
int flowlink_enable(int on) { (void)on; return 0; }
void flowlink_step(const flowlink_in *in, flowlink_out *out) { (void)in; memset(out, 0, sizeof *out); }
int flowlink_peers(void) { return -1; }

static int fails;
static void expect(int got, int want, const char *what, double pos, double ph)
{
    if (got != want) {
        printf("FAIL %s: pos %.3f ph %.3f -> %d, want %d\n", what, pos, ph, got, want);
        fails++;
    }
}

static void put32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

int main(void)
{
    /* a PWAV-like tag first (skipped), then PQTZ: 400 beats at 120 BPM from 100 ms, the first
     * beat being beat 3 of its bar (so the first "1" is the third beat) */
    enum { N = 400 };
    static unsigned char blob[64 + 24 + 8 * N];
    size_t n = 0;
    memcpy(blob, "PWAV", 4); put32(blob + 4, 20); put32(blob + 8, 64); n = 64;
    unsigned char *t = blob + n;
    memcpy(t, "PQTZ", 4); put32(t + 4, 24); put32(t + 8, 24 + 8 * N); put32(t + 12, 0); put32(t + 16, 0x80000); put32(t + 20, N);
    for (int i = 0; i < N; i++) {
        unsigned char *e = t + 24 + 8 * i;
        unsigned num = (unsigned)((i + 2) % 4) + 1;
        e[0] = 0; e[1] = (unsigned char)num; e[2] = 0x2e; e[3] = 0xe0;   /* 120.00 */
        put32(e + 4, 100u + 500u * (unsigned)i);
    }
    n += 24 + 8 * N;
    bars_set(0, 1234, blob, n);

    for (int i = 0; i < N - 1; i++) {
        int want = (i + 2) % 4;                 /* 0 = the "1" */
        for (double ph = 0.0; ph < 1.0; ph += 0.05) {
            double pos = (100 + 500 * i + 500 * ph) / 1000.0;
            expect(bars_at(0, 1234, pos, ph), want, "on the grid", pos, ph);
            /* the deck's phase and playhead disagree by a few ms (read at different moments) */
            expect(bars_at(0, 1234, pos + 0.012, ph), want, "playhead 12 ms ahead", pos + 0.012, ph);
            if (pos > 0.2) expect(bars_at(0, 1234, pos - 0.012, ph), want, "playhead 12 ms behind", pos - 0.012, ph);
        }
    }
    expect(bars_at(0, 999, 10.0, 0.5), -1, "another track", 10.0, 0.5);
    expect(bars_at(1, 1234, 10.0, 0.5), -1, "a deck with no grid", 10.0, 0.5);
    expect(bars_at(0, 1234, -1.0, 0.5), -1, "playhead unknown", -1.0, 0.5);
    expect(bars_at(0, 1234, 10.0, -1.0), -1, "no phase", 10.0, -1.0);

    /* a track without PQTZ clears the deck */
    bars_set(0, 77, blob, 64);
    expect(bars_at(0, 77, 10.0, 0.5), -1, "no PQTZ tag", 10.0, 0.5);
    /* a truncated tag never reads past the blob */
    bars_set(0, 55, blob, 64 + 24 + 8 * 10 + 3);
    expect(bars_at(0, 55, 0.1 + 0.25, 0.5), -1, "truncated tag (refused)", 0.35, 0.5);

    printf("%s: %d failure%s\n", fails ? "FAILED" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
