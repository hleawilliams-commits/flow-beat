/* SPDX-License-Identifier: GPL-3.0-only
 * rekordbox analysis files (PIONEER/USBANLZ/.../ANLZ0000.DAT and .EXT), parsed into what the
 * waveform widgets draw. Pure functions over bytes: no SDK, no files, so it is host-tested
 * against real exports. Everything in these files is big-endian. A file is a PMAI header and a
 * run of tags, each "fourcc | header length | tag length | ...". The tags used here:
 *
 *   PQTZ (DAT)  beat grid: 24-byte header (count at +20), then 8 bytes a beat:
 *               u16 beat in bar (1..4), u16 tempo x100, u32 time in ms
 *   PWAV (DAT)  400-column preview: 20-byte header, one byte a column, height = low 5 bits
 *   PWV3 (EXT)  detail, 150 entries a second: 24-byte header (count at +16), one byte each,
 *               height = low 5 bits, whiteness = high 3 bits
 *   PWV5 (EXT)  colour detail, same rate: 24-byte header (count at +16), u16 each,
 *               red bits 15-13, green 12-10, blue 9-7, height 6-2
 *   PPTH (both) the audio file's path on the stick, UTF-16BE (header 16, length at +12)
 *
 * Detail is kept as PWV5-style u16 entries; a PWV3-only file is converted (blue to white by
 * whiteness), so the painter has one format. */
#ifndef CW_ANLZ_H
#define CW_ANLZ_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CW_RATE        150                  /* detail entries a second */
#define CW_DETAIL_MAX  (CW_RATE * 60 * 60)  /* an hour; longer tracks are cut there */
#define CW_BEATS_MAX   20000
#define CW_OV          1024                 /* overview bins, built from the detail */
#define CW_FILE_MAX    (8 * 1024 * 1024)    /* refuse bigger ANLZ files */

typedef struct {
    uint32_t n;            /* detail entries */
    uint16_t *detail;      /* PWV5 layout: rgb 3-3-3, height 5 */
    int colour;            /* 1 = from PWV5, 0 = converted from PWV3 */
    uint32_t nbeats;
    uint32_t *beat_ms;
    uint16_t *beat_tempo;  /* x100 */
    uint8_t *beat_num;     /* 1..4, 1 = downbeat */
    uint16_t ov[CW_OV];    /* overview: the loudest entry of each 1/1024 of the track */
    int peak;              /* the loudest height in the track (1..31): the painters scale to it */
} cw_track;

static uint32_t cw_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t cw_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static int cw_height(uint16_t e) { return (e >> 2) & 31; }
static uint32_t cw_rgb(uint16_t e)
{
    uint32_t r = (e >> 13) & 7, g = (e >> 10) & 7, b = (e >> 7) & 7;
    return (r * 36 + r / 2) << 16 | (g * 36 + g / 2) << 8 | (b * 36 + b / 2);
}

/* PWV3 byte -> PWV5-style entry: whiteness 0 = blue, 7 = near white, as rekordbox draws it. */
static uint16_t cw_from_pwv3(uint8_t v)
{
    unsigned h = v & 31u, w = v >> 5;
    unsigned r = w * 6 / 7, g = 2 + w * 5 / 7, b = 7;
    return (uint16_t)(r << 13 | g << 10 | b << 7 | h << 2);
}

/* Calls fn for every well-formed tag of one file. Returns the number of tags, -1 = not ANLZ. */
typedef void (*cw_tag_fn)(const uint8_t *tag, uint32_t hl, uint32_t tl, void *user);
static int cw_tags(const uint8_t *d, size_t n, cw_tag_fn fn, void *user)
{
    if (!d || n < 28 || memcmp(d, "PMAI", 4)) return -1;
    uint32_t off = cw_be32(d + 4), end = cw_be32(d + 8);
    if (end > n) end = (uint32_t)n;
    int count = 0;
    while (off >= 12 && off + 12 <= end) {
        uint32_t hl = cw_be32(d + off + 4), tl = cw_be32(d + off + 8);
        if (hl < 12 || tl < hl || tl > end - off) break;
        fn(d + off, hl, tl, user);
        count++;
        off += tl;
    }
    return count;
}

typedef struct { cw_track *t; int got_pwv5, got_pwv3, got_grid, failed; } cw_parse_ctx;

static void cw_take_tag(const uint8_t *g, uint32_t hl, uint32_t tl, void *user)
{
    cw_parse_ctx *c = (cw_parse_ctx *)user;
    cw_track *t = c->t;
    if (!memcmp(g, "PQTZ", 4) && hl >= 24 && !c->got_grid) {
        uint32_t n = cw_be32(g + 20);
        if (n > (tl - hl) / 8) n = (tl - hl) / 8;
        if (n > CW_BEATS_MAX) n = CW_BEATS_MAX;
        if (!n) return;
        t->beat_ms = (uint32_t *)malloc(n * sizeof *t->beat_ms);
        t->beat_tempo = (uint16_t *)malloc(n * sizeof *t->beat_tempo);
        t->beat_num = (uint8_t *)malloc(n);
        if (!t->beat_ms || !t->beat_tempo || !t->beat_num) { c->failed = 1; return; }
        const uint8_t *e = g + hl;
        for (uint32_t i = 0; i < n; i++, e += 8) {
            t->beat_num[i] = e[1] >= 1 && e[1] <= 4 && !e[0] ? e[1] : 0;
            t->beat_tempo[i] = cw_be16(e + 2);
            t->beat_ms[i] = cw_be32(e + 4);
        }
        t->nbeats = n;
        c->got_grid = 1;
    } else if (!memcmp(g, "PWV5", 4) && hl >= 24 && !c->got_pwv5) {
        uint32_t n = cw_be32(g + 16);
        if (cw_be32(g + 12) != 2) return;
        if (n > (tl - hl) / 2) n = (tl - hl) / 2;
        if (n > CW_DETAIL_MAX) n = CW_DETAIL_MAX;
        if (!n) return;
        uint16_t *d = (uint16_t *)malloc(n * sizeof *d);
        if (!d) { c->failed = 1; return; }
        for (uint32_t i = 0; i < n; i++) d[i] = cw_be16(g + hl + 2 * i);
        free(t->detail);
        t->detail = d; t->n = n; t->colour = 1;
        c->got_pwv5 = 1;
    } else if (!memcmp(g, "PWV3", 4) && hl >= 24 && !c->got_pwv5 && !c->got_pwv3) {
        uint32_t n = cw_be32(g + 16);
        if (cw_be32(g + 12) != 1) return;
        if (n > tl - hl) n = tl - hl;
        if (n > CW_DETAIL_MAX) n = CW_DETAIL_MAX;
        if (!n) return;
        uint16_t *d = (uint16_t *)malloc(n * sizeof *d);
        if (!d) { c->failed = 1; return; }
        for (uint32_t i = 0; i < n; i++) d[i] = cw_from_pwv3(g[hl + i]);
        t->detail = d; t->n = n; t->colour = 0;
        c->got_pwv3 = 1;
    }
}

static void cw_track_free(cw_track *t)
{
    if (!t) return;
    free(t->detail); free(t->beat_ms); free(t->beat_tempo); free(t->beat_num);
    free(t);
}

/* The overview: the loudest entry in each bin, so short peaks stay visible. */
static void cw_build_overview(cw_track *t)
{
    memset(t->ov, 0, sizeof t->ov);
    t->peak = 1;
    if (!t->n) return;
    for (uint32_t b = 0; b < CW_OV; b++) {
        uint32_t a = (uint32_t)((uint64_t)b * t->n / CW_OV), z = (uint32_t)((uint64_t)(b + 1) * t->n / CW_OV);
        if (z <= a) z = a + 1;
        if (z > t->n) z = t->n;
        uint16_t best = 0;
        for (uint32_t i = a; i < z; i++) if (cw_height(t->detail[i]) > cw_height(best)) best = t->detail[i];
        t->ov[b] = best;
        if (cw_height(best) > t->peak) t->peak = cw_height(best);
    }
}

/* DAT and EXT bytes (either may be NULL) -> a track with at least a waveform, or NULL. */
static cw_track *cw_parse(const uint8_t *dat, size_t dat_n, const uint8_t *ext, size_t ext_n)
{
    cw_track *t = (cw_track *)calloc(1, sizeof *t);
    if (!t) return NULL;
    cw_parse_ctx c = { t, 0, 0, 0, 0 };
    if (ext) cw_tags(ext, ext_n, cw_take_tag, &c);
    if (dat) cw_tags(dat, dat_n, cw_take_tag, &c);
    if (c.failed || !t->n) { cw_track_free(t); return NULL; }
    cw_build_overview(t);
    return t;
}

/* The first beat at or after ms (binary search); nbeats when none. */
static uint32_t cw_beat_at(const cw_track *t, double ms)
{
    uint32_t lo = 0, hi = t->nbeats;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (t->beat_ms[mid] < ms) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/* The track's own tempo (not the pitched one) at a position, from the grid; 0 = no grid. */
static double cw_tempo_at(const cw_track *t, double pos_s)
{
    if (!t->nbeats) return 0;
    uint32_t i = cw_beat_at(t, pos_s * 1000.0);
    if (i >= t->nbeats) i = t->nbeats - 1;
    else if (i > 0) i--;
    double bpm = t->beat_tempo[i] / 100.0;
    return bpm >= 40 && bpm <= 300 ? bpm : 0;
}

/* The loudest entry from a to b (entry indices, any order, clipped); 0 = silence/outside. */
static uint16_t cw_peak(const cw_track *t, double a, double b)
{
    if (b < a) { double s = a; a = b; b = s; }
    long ia = (long)a, ib = (long)b;
    if (ib == ia) ib = ia + 1;
    if (ia < 0) ia = 0;
    if (ib > (long)t->n) ib = (long)t->n;
    uint16_t best = 0;
    for (long i = ia; i < ib; i++) if (cw_height(t->detail[i]) > cw_height(best)) best = t->detail[i];
    return best;
}

/* UTF-16BE -> UTF-8 for PPTH. Returns bytes written (out is always terminated). */
static int cw_utf16be_to_utf8(const uint8_t *in, int n, char *out, int cap)
{
    int o = 0;
    if (cap < 1) return 0;
    for (int i = 0; i + 1 < n && o < cap - 5; i += 2) {
        uint32_t c = (uint32_t)in[i] << 8 | in[i + 1];
        if (!c) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
            uint32_t d = (uint32_t)in[i + 2] << 8 | in[i + 3];
            if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i += 2; }
        }
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | c >> 6); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out[o++] = (char)(0xE0 | c >> 12); out[o++] = (char)(0x80 | (c >> 6 & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else { out[o++] = (char)(0xF0 | c >> 18); out[o++] = (char)(0x80 | (c >> 12 & 0x3F)); out[o++] = (char)(0x80 | (c >> 6 & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
    return o;
}

/* The PPTH path in the first bytes of a file, as UTF-8; 0 = none. */
static int cw_ppth(const uint8_t *d, size_t n, char *out, int cap)
{
    out[0] = 0;
    if (!d || n < 28 || memcmp(d, "PMAI", 4)) return 0;
    uint32_t off = cw_be32(d + 4);
    while (off >= 12 && off + 16 <= n) {
        uint32_t hl = cw_be32(d + off + 4), tl = cw_be32(d + off + 8);
        if (hl < 12 || tl < hl) return 0;
        if (!memcmp(d + off, "PPTH", 4)) {
            uint32_t len = cw_be32(d + off + 12);
            if (hl < 16 || len > 4000 || off + hl + len > n) return 0;
            return cw_utf16be_to_utf8(d + off + hl, (int)len, out, cap) > 0;
        }
        off += tl;
    }
    return 0;
}
#endif
