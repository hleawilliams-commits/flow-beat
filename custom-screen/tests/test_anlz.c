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

/* The fixture's PSSI is masked (an export): mood mid, 20 phrases, ending on beat 469. */
static void real_phrases(void)
{
    size_t dn, en;
    uint8_t *dat = slurp("P018_00000792.DAT", &dn), *ext = slurp("P018_00000792.EXT", &en);
    cw_track *t = cw_parse(dat, dn, ext, en);
    CHECK(t && t->nphr == 20 && t->mood == 2 && t->phr_end == 469);
    CHECK(t->phr_beat[0] == 1 && t->phr_kind[0] == CW_PH_INTRO);
    CHECK(t->phr_beat[1] == 5 && t->phr_kind[1] == CW_PH_BRIDGE);
    CHECK(t->phr_beat[3] == 45 && t->phr_kind[3] == CW_PH_DROP);
    CHECK(t->phr_beat[5] == 61 && t->phr_kind[5] == CW_PH_VERSE);
    CHECK(t->phr_beat[19] == 437 && t->phr_kind[19] == CW_PH_OUTRO);
    CHECK(!strcmp(cw_phrase_name(CW_PH_DROP, 2), "CHORUS") && !strcmp(cw_phrase_name(CW_PH_DROP, 1), "DROP"));
    /* beat n (1-based) starts at beat_ms[n-1] */
    #define AT(n) (t->beat_ms[(n) - 1] / 1000.0)
    CHECK(cw_phrase_at(t, 0, NULL) == -1 && cw_bars_to_drop(t, 0) == 12);   /* 45 beats to go */
    CHECK(cw_phrase_at(t, AT(1), NULL) == 0);
    CHECK(cw_phrase_at(t, AT(4), NULL) == 0 && cw_phrase_at(t, AT(5), NULL) == 1);
    CHECK(cw_bars_to_drop(t, AT(5)) == 10 && cw_bars_to_drop(t, AT(44)) == 1);
    CHECK(cw_phrase_at(t, AT(45), NULL) == 3 && cw_bars_to_drop(t, AT(45)) == 0);
    CHECK(cw_phrase_at(t, AT(61), NULL) == 5 && cw_bars_to_drop(t, AT(61)) == 23);   /* next at 153 */
    CHECK(cw_phrase_at(t, AT(468), NULL) == 19 && cw_bars_to_drop(t, AT(468)) == -1);
    CHECK(cw_phrase_at(t, AT(469), NULL) == -1 && cw_phrase_at(t, 1e6, NULL) == -1);
    /* the bar meter: bar 1 is the phrase's own first downbeat, not where play was pressed */
    {
        int bar = 0, beat = 0;
        CHECK(cw_bar_at(t, AT(1), 4, &bar, &beat) && bar == 1 && beat == 1);
        CHECK(cw_bar_at(t, AT(8), 4, &bar, &beat) && bar == 1 && beat == 4);   /* phrase 2 starts at 5 */
        CHECK(cw_bar_at(t, AT(9), 4, &bar, &beat) && bar == 2 && beat == 1);
        CHECK(cw_bar_at(t, AT(21), 4, &bar, &beat) && bar == 1 && beat == 1);
        CHECK(cw_bar_at(t, AT(21), 8, &bar, &beat) && bar == 5);
        CHECK(cw_bar_at(t, AT(44), 4, &bar, &beat) && bar == 3 && beat == 4);
        CHECK(cw_bar_at(t, AT(45), 4, &bar, &beat) && bar == 1 && beat == 1);   /* the drop */
        CHECK(cw_bar_at(t, AT(468), 8, &bar, &beat) && bar == 8 && beat == 4);
        CHECK(cw_bar_at(t, AT(473), 4, &bar, &beat) && bar == 2);              /* past the last phrase */
        CHECK(!cw_bar_at(t, -1, 4, &bar, &beat));
    }
    #undef AT
    /* EXT alone: phrases, but no grid to place the playhead in them */
    cw_track *e = cw_parse(NULL, 0, ext, en);
    CHECK(e && e->nphr == 20 && cw_phrase_at(e, 30, NULL) == -1 && cw_bars_to_drop(e, 30) == -1);  /* no grid: no position */
    { int bar, beat; CHECK(!cw_bar_at(e, 30, 4, &bar, &beat)); }
    cw_track_free(e);
    cw_track_free(t);
    free(dat); free(ext);
}

/* PMAI + PWV3 (so the track parses) + a plain PSSI, as rekordbox writes it on the computer. */
static size_t make_pssi(uint8_t *b, unsigned mood, const uint16_t *beat, const uint16_t *kind, int n, uint16_t end)
{
    size_t w = make_pwv3(b, 300, 300);
    uint8_t *g = b + w;
    uint32_t tl = 32 + 24 * (uint32_t)n;
    memset(g, 0, tl);
    memcpy(g, "PSSI", 4);
    g[7] = 32; g[10] = tl >> 8; g[11] = tl & 255;
    g[15] = 24; g[17] = (uint8_t)n; g[19] = (uint8_t)mood; g[26] = end >> 8; g[27] = end & 255;
    for (int k = 0; k < n; k++) {
        uint8_t *r = g + 32 + 24 * k;
        r[1] = (uint8_t)(k + 1); r[2] = beat[k] >> 8; r[3] = beat[k] & 255; r[5] = (uint8_t)kind[k];
    }
    w += tl;
    b[8] = w >> 24; b[9] = w >> 16; b[10] = w >> 8; b[11] = w;
    return w;
}

static void plain_phrases(void)
{
    static uint8_t b[4096];
    const uint16_t beat[] = { 1, 33, 65, 97, 129 }, kind[] = { 1, 2, 5, 3, 6 }, bad[] = { 1, 33, 33, 97, 129 };
    size_t n = make_pssi(b, 1, beat, kind, 5, 161);
    cw_track *t = cw_parse(NULL, 0, b, n);
    CHECK(t && t->nphr == 5 && t->mood == 1);
    CHECK(t && t->phr_kind[1] == CW_PH_BUILD && t->phr_kind[2] == CW_PH_DROP && t->phr_kind[3] == CW_PH_BREAK && t->phr_kind[4] == CW_PH_OUTRO);
    cw_track_free(t);
    n = make_pssi(b, 1, bad, kind, 5, 161);          /* beats must climb */
    t = cw_parse(NULL, 0, b, n);
    CHECK(t && t->nphr == 0);
    cw_track_free(t);
    n = make_pssi(b, 7, beat, kind, 5, 161);          /* no such mood */
    t = cw_parse(NULL, 0, b, n);
    CHECK(t && t->nphr == 0);
    cw_track_free(t);
    n = make_pssi(b, 1, beat, kind, 5, 161);
    b[n - 24 * 5 - 32 + 17] = 200;                    /* count bigger than the tag */
    t = cw_parse(NULL, 0, b, n);
    CHECK(t && t->nphr == 0);
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
    real_phrases();
    plain_phrases();
    hostile();
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("anlz: all passed\n");
    return 0;
}
