/* SPDX-License-Identifier: GPL-3.0-only
 * Host checks for the ANLZ parser (anlz.h) against two real rekordbox exports from an OMNIS
 * stick (tests/fixtures: one with a beat grid, one with an empty grid), plus a PWV3-only file
 * and hostile input (cut files, lying lengths, wrong magic). Run from the mod folder:
 *   cc -std=c11 -Wall -Wextra -Werror tests/test_anlz.c -o /tmp/t && /tmp/t
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../anlz.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t *slurp(const char *name, size_t *n)
{
    char path[256];
    snprintf(path, sizeof path, "tests/fixtures/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing %s (run from the mod folder)\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)sz);
    *n = fread(d, 1, (size_t)sz, f);
    fclose(f);
    return d;
}

static void real_with_grid(void)
{
    size_t dn, en;
    uint8_t *dat = slurp("P018_00000792.DAT", &dn), *ext = slurp("P018_00000792.EXT", &en);
    cw_track *t = cw_parse(dat, dn, ext, en);
    CHECK(t != NULL);
    CHECK(t->n == 30595 && t->colour == 1);            /* PWV5 preferred: 204 s at 150 a second */
    CHECK(t->detail[1000] == 0x761c);
    CHECK(cw_height(0x761c) == 7);
    CHECK(t->nbeats == 486);
    CHECK(t->beat_num[0] == 1 && t->beat_tempo[0] == 14307 && t->beat_ms[0] == 338);
    CHECK(t->beat_num[1] == 2 && t->beat_ms[1] == 757);
    CHECK(t->beat_ms[485] == 203725 && t->beat_num[485] == 2);
    for (uint32_t i = 0; i < t->nbeats; i++) CHECK(t->beat_num[i] >= 1 && t->beat_num[i] <= 4);
    CHECK(cw_tempo_at(t, 60.0) > 143.0 && cw_tempo_at(t, 60.0) < 143.1);
    CHECK(cw_beat_at(t, 0) == 0 && cw_beat_at(t, 338) == 0 && cw_beat_at(t, 339) == 1);
    CHECK(cw_beat_at(t, 1e9) == t->nbeats);
    int nonzero = 0;
    for (int b = 0; b < CW_OV; b++) nonzero += cw_height(t->ov[b]) > 0;
    CHECK(nonzero > CW_OV * 9 / 10);
    CHECK(t->peak >= 12 && t->peak <= 31);
    /* the overview bin holds the loudest entry of its range */
    uint32_t a = 500 * t->n / CW_OV, z = 501 * t->n / CW_OV;
    int best = 0;
    for (uint32_t i = a; i < z; i++) if (cw_height(t->detail[i]) > best) best = cw_height(t->detail[i]);
    CHECK(cw_height(t->ov[500]) == best);
    CHECK(cw_height(cw_peak(t, a, z)) == best && cw_height(cw_peak(t, z, a)) == best);
    CHECK(cw_peak(t, -50, -10) == 0 && cw_peak(t, 1e7, 2e7) == 0);
    char path[512];
    CHECK(cw_ppth(dat, dn, path, sizeof path));
    CHECK(!strcmp(path, "/Contents/Julie Driscoll, Brian Auger & The Trinity/Jools & Brian/07 - Green Onions.flac"));
    CHECK(!cw_ppth(dat, 40, path, sizeof path) && !path[0]);   /* cut before the path ends */
    /* DAT alone: grid but no detail is no waveform */
    CHECK(cw_parse(dat, dn, NULL, 0) == NULL);
    /* EXT alone: a waveform without a grid */
    cw_track *e = cw_parse(NULL, 0, ext, en);
    CHECK(e && e->n == 30595 && e->nbeats == 0 && cw_tempo_at(e, 10) == 0);
    cw_track_free(e);
    cw_track_free(t);
    free(dat); free(ext);
}

static void real_empty_grid(void)
{
    size_t dn, en;
    uint8_t *dat = slurp("P04B_000191A5.DAT", &dn), *ext = slurp("P04B_000191A5.EXT", &en);
    cw_track *t = cw_parse(dat, dn, ext, en);
    CHECK(t && t->n == 0xee17 && t->colour == 1);
    CHECK(t->nbeats == 0 && cw_tempo_at(t, 30) == 0);
    cw_track_free(t);
    free(dat); free(ext);
}

/* A file built by hand: PMAI + one PWV3 tag. */
static size_t make_pwv3(uint8_t *b, uint32_t n, uint32_t claim)
{
    memset(b, 0, 28 + 24 + n);
    memcpy(b, "PMAI", 4);
    uint32_t total = 28 + 24 + n;
    b[7] = 28; b[8] = total >> 24; b[9] = total >> 16; b[10] = total >> 8; b[11] = total;
    uint8_t *g = b + 28;
    memcpy(g, "PWV3", 4);
    g[7] = 24;
    uint32_t tl = 24 + n;
    g[8] = tl >> 24; g[9] = tl >> 16; g[10] = tl >> 8; g[11] = tl;
    g[15] = 1;
    g[16] = claim >> 24; g[17] = claim >> 16; g[18] = claim >> 8; g[19] = claim;
    for (uint32_t i = 0; i < n; i++) g[24 + i] = (uint8_t)((i % 8) << 5 | (i % 32));
    return total;
}

static void pwv3_only(void)
{
    static uint8_t b[4096];
    size_t n = make_pwv3(b, 3000, 3000);
    cw_track *t = cw_parse(NULL, 0, b, n);
    CHECK(t && t->n == 3000 && t->colour == 0);
    CHECK(cw_height(t->detail[5]) == 5 && cw_height(t->detail[31]) == 31);
    CHECK(cw_rgb(t->detail[0]) == 0x0049FF);       /* whiteness 0: blue */
    CHECK((cw_rgb(t->detail[7]) >> 16) > 200);      /* whiteness 7: near white */
    cw_track_free(t);
    /* a count bigger than the tag is cut to the tag */
    n = make_pwv3(b, 100, 1000000);
    t = cw_parse(NULL, 0, b, n);
    CHECK(t && t->n == 100);
    cw_track_free(t);
}

static void hostile(void)
{
    size_t en;
    uint8_t *ext = slurp("P018_00000792.EXT", &en);
    CHECK(cw_parse(NULL, 0, NULL, 0) == NULL);
    CHECK(cw_parse(NULL, 0, ext, 27) == NULL);
    for (size_t cut = 0; cut < en; cut += 997) {      /* every cut is handled, none overreads */
        cw_track *t = cw_parse(NULL, 0, ext, cut);
        cw_track_free(t);
    }
    uint8_t *bad = malloc(en);
    memcpy(bad, ext, en);
    bad[0] = 'X';
    CHECK(cw_parse(NULL, 0, bad, en) == NULL);
    memcpy(bad, ext, en);
    bad[8] = bad[9] = bad[10] = bad[11] = 0xff;      /* file length lies: clipped to the buffer */
    cw_track *t = cw_parse(NULL, 0, bad, en);
    CHECK(t && t->n == 30595);
    cw_track_free(t);
    memcpy(bad, ext, en);
    uint32_t off = cw_be32(bad + 4);                 /* first tag's length runs past the end */
    bad[off + 8] = 0x7f;
    CHECK(cw_parse(NULL, 0, bad, en) == NULL);
    free(bad); free(ext);
    char out[16];
    static const uint8_t u16[] = { 0x00, 'A', 0xD8, 0x3D, 0xDE, 0x00, 0x00, 'B' };
    CHECK(cw_utf16be_to_utf8(u16, 8, out, sizeof out) == 6 && !memcmp(out, "A\xF0\x9F\x98\x80" "B", 6));
    CHECK(cw_utf16be_to_utf8(u16, 8, out, 8) == 5 && !strcmp(out, "A\xF0\x9F\x98\x80"));  /* cut cleanly, terminated */
    CHECK(cw_utf16be_to_utf8(u16, 8, out, 1) == 0 && !out[0]);
}

int main(void)
{
    real_with_grid();
    real_empty_grid();
    pwv3_only();
    hostile();
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("anlz: all passed\n");
    return 0;
}
