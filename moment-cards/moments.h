/* SPDX-License-Identifier: GPL-3.0-only
 * Moment Cards: run-of-show parsing, US time zones, and which card to show when. Pure
 * functions, host-tested; no SDK calls.
 *
 * MOMENTS.txt, one moment per line (blank lines and # comments ignored):
 *     19:15 | First Dance | Perfect - Ed Sheeran | Couple to the floor, circle up
 *     7:40pm | Toasts | | Mic to the best man
 * Fields: time (24 h, or h:mm am/pm) | moment | song (optional) | MC line (optional).
 */
#ifndef MC_MOMENTS_H
#define MC_MOMENTS_H
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MC_MAX 40
typedef struct { int min; char name[48], song[96], line[200]; } mc_moment;  /* min: minutes after midnight */
typedef struct { mc_moment m[MC_MAX]; int n, bad_line; } mc_show;

static void mc_trim(char *s)
{
    char *p = s; while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}
/* "19:15", "7:15pm", "7:15 PM", "12:05am" -> minutes after midnight, -1 if not a time. */
static int mc_parse_time(const char *s)
{
    int h = 0, m = 0, i = 0;
    while (s[i] == ' ') i++;
    if (!isdigit((unsigned char)s[i])) return -1;
    while (isdigit((unsigned char)s[i])) h = h * 10 + (s[i++] - '0');
    if (s[i] != ':' && s[i] != '.') return -1;
    i++;
    if (!isdigit((unsigned char)s[i]) || !isdigit((unsigned char)s[i + 1])) return -1;
    m = (s[i] - '0') * 10 + (s[i + 1] - '0'); i += 2;
    while (s[i] == ' ') i++;
    int ampm = 0;
    if ((s[i] == 'a' || s[i] == 'A') && (s[i + 1] == 'm' || s[i + 1] == 'M')) ampm = 1;
    else if ((s[i] == 'p' || s[i] == 'P') && (s[i + 1] == 'm' || s[i + 1] == 'M')) ampm = 2;
    if (ampm) { if (h < 1 || h > 12) return -1; h = h % 12 + (ampm == 2 ? 12 : 0); i += 2; }
    while (s[i] == ' ') i++;
    if (s[i] || h > 23 || m > 59) return -1;
    return h * 60 + m;
}
static void mc_field(char *dst, size_t n, const char *src, size_t len)
{
    if (len >= n) len = n - 1;
    memcpy(dst, src, len); dst[len] = 0; mc_trim(dst);
}
/* Parse the whole file. Moments are kept in file order (sorted by time afterwards). */
static void mc_parse(const char *text, mc_show *out)
{
    memset(out, 0, sizeof *out);
    int lineno = 0;
    for (const char *p = text; p && *p && out->n < MC_MAX; ) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[512];
        mc_field(line, sizeof line, p, len);
        lineno++;
        p = e ? e + 1 : NULL;
        if (!line[0] || line[0] == '#') continue;
        char *f[4] = {line, NULL, NULL, NULL};
        for (int k = 1; k < 4; k++) { char *bar = f[k - 1] ? strchr(f[k - 1], '|') : NULL; if (bar) { *bar = 0; f[k] = bar + 1; } }
        char tbuf[24]; mc_field(tbuf, sizeof tbuf, f[0], strlen(f[0]));
        int t = mc_parse_time(tbuf);
        if (t < 0 || !f[1]) { if (!out->bad_line) out->bad_line = lineno; continue; }
        mc_moment *m = &out->m[out->n];
        m->min = t;
        mc_field(m->name, sizeof m->name, f[1], strlen(f[1]));
        if (f[2]) mc_field(m->song, sizeof m->song, f[2], strlen(f[2]));
        if (f[3]) mc_field(m->line, sizeof m->line, f[3], strlen(f[3]));
        if (!m->name[0]) { if (!out->bad_line) out->bad_line = lineno; continue; }
        out->n++;
    }
    for (int i = 1; i < out->n; i++)            /* stable insertion sort by time */
        for (int j = i; j > 0 && out->m[j - 1].min > out->m[j].min; j--) {
            mc_moment t = out->m[j]; out->m[j] = out->m[j - 1]; out->m[j - 1] = t;
        }
}

/* ---- time zones: US rules, no TZ database needed ---- */
enum { MC_TZ_MOUNTAIN = 0, MC_TZ_ARIZONA, MC_TZ_PACIFIC, MC_TZ_CENTRAL, MC_TZ_EASTERN, MC_TZ_UTC };
static long mc_days_from_civil(long y, unsigned m, unsigned d)   /* days since 1970-01-01 */
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
static int mc_weekday(long days) { return (int)((days % 7 + 11) % 7); }   /* 0 = Sunday */
/* The n-th Sunday (n >= 1) of month m in year y, as days since epoch. */
static long mc_nth_sunday(long y, unsigned m, int n)
{
    long d1 = mc_days_from_civil(y, m, 1);
    return d1 + (7 - mc_weekday(d1)) % 7 + 7L * (n - 1);
}
/* UTC offset in seconds for zone at UTC instant t. US DST: 2nd Sunday of March 2:00 local
 * standard time to 1st Sunday of November 2:00 local daylight time. */
static long mc_utc_offset(int tz, int64_t t)
{
    static const int std_h[] = {-7, -7, -8, -6, -5, 0};
    static const int dst[] = {1, 0, 1, 1, 1, 0};
    if (tz < 0 || tz > MC_TZ_UTC) tz = MC_TZ_MOUNTAIN;
    long std = std_h[tz] * 3600L;
    if (!dst[tz]) return std;
    long days = (long)((t + std) >= 0 ? (t + std) / 86400 : ((t + std) - 86399) / 86400);
    /* civil year of the local standard date */
    long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    unsigned mo = mp + (mp < 10 ? 3 : -9);
    if (mo <= 2) y++;
    int64_t start = (int64_t)mc_nth_sunday(y, 3, 2) * 86400 + 2 * 3600 - std;          /* UTC */
    int64_t end = (int64_t)mc_nth_sunday(y, 11, 1) * 86400 + 2 * 3600 - (std + 3600);   /* UTC */
    return (t >= start && t < end) ? std + 3600 : std;
}
/* Seconds after local midnight. */
static int mc_local_secs(int tz, int64_t t)
{
    int64_t l = t + mc_utc_offset(tz, t);
    long s = (long)(l % 86400); if (s < 0) s += 86400;
    return (int)s;
}

/* ---- what to show ---- */
enum { MC_SHOW_NONE = 0, MC_SHOW_BADGE, MC_SHOW_CARD, MC_SHOW_NOW };
typedef struct { int what, idx; int secs_to; } mc_view;
/* now_s = seconds after local midnight. lead = seconds before a moment the card appears.
 * Card for 12 s from lead, then a badge until the moment; "NOW" card for 10 s after it.
 * dismissed_idx/dismissed_what: the DJ tapped this card away. Moments more than 90 min
 * past are ignored so a midnight wrap can't resurrect early ones. */
static mc_view mc_pick(const mc_show *s, int now_s, int lead, int dismissed_idx, int dismissed_what)
{
    mc_view v = {MC_SHOW_NONE, -1, 0};
    for (int i = 0; i < s->n; i++) {
        int d = s->m[i].min * 60 - now_s;                 /* seconds until the moment */
        if (d < -12 * 3600) d += 86400;                   /* tonight's 00:30 after 23:00 */
        if (d > 12 * 3600) d -= 86400;
        if (d < -10) continue;                            /* past (beyond the NOW card) */
        if (d > lead) break;                              /* sorted: nothing sooner */
        int what = d <= 0 ? MC_SHOW_NOW : (lead - d) < 12 ? MC_SHOW_CARD : MC_SHOW_BADGE;
        if (i == dismissed_idx && what != MC_SHOW_NONE && what <= dismissed_what) what = MC_SHOW_BADGE;
        if (i == dismissed_idx && dismissed_what == MC_SHOW_NOW) what = MC_SHOW_NONE;
        v.what = what; v.idx = i; v.secs_to = d;
        return v;
    }
    return v;
}
/* Does the moment's song look loaded? Case-insensitive: the song's title part (after
 * "Artist - " if present) inside the deck title, or the whole field. */
static int mc_ieq_in(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return 0;
    for (const char *h = hay; *h; h++) {
        size_t k = 0;
        while (k < n && h[k] && tolower((unsigned char)h[k]) == tolower((unsigned char)needle[k])) k++;
        if (k == n) return 1;
    }
    return 0;
}
static int mc_song_matches(const char *song, const char *deck_title, const char *deck_artist)
{
    if (!song[0] || !deck_title[0]) return 0;
    const char *dash = strstr(song, " - ");
    char title[96];
    snprintf(title, sizeof title, "%s", dash ? dash + 3 : song);
    char *paren = strchr(title, '(');                     /* "(Extended Mix)" etc. optional */
    if (paren && paren > title) { *paren = 0; mc_trim(title); }
    if (!title[0]) return 0;
    if (!mc_ieq_in(deck_title, title)) return 0;
    if (dash) {                                           /* artist given: require it too */
        char artist[96]; size_t al = (size_t)(dash - song);
        if (al >= sizeof artist) al = sizeof artist - 1;
        memcpy(artist, song, al); artist[al] = 0; mc_trim(artist);
        if (artist[0] && deck_artist[0] && !mc_ieq_in(deck_artist, artist) && !mc_ieq_in(deck_title, artist)) return 0;
    }
    return 1;
}
/* Surface text is drawn as Latin-1, not UTF-8 (a UTF-8 "·" shows as "Â·"). Convert in
 * place: Latin-1 characters keep their accents, typographic quotes/dashes become ASCII,
 * anything else becomes '?'. The result is never longer than the input. */
static void mc_disp(char *s)
{
    unsigned char *r = (unsigned char *)s, *w = r;
    while (*r) {
        unsigned c = *r, cp = 0; int n = 0;
        if (c < 0x80) { *w++ = *r++; continue; }
        if (c >= 0xc2 && c <= 0xdf) { n = 2; cp = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { n = 3; cp = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 4; cp = c & 0x07; }
        int ok = n > 0;
        for (int i = 1; ok && i < n; i++) { if ((r[i] & 0xc0) != 0x80) ok = 0; else cp = cp << 6 | (r[i] & 0x3f); }
        if (!ok) { *w++ = '?'; r++; continue; }
        r += n;
        if (cp <= 0xff) *w++ = (unsigned char)cp;
        else if (cp == 0x2018 || cp == 0x2019) *w++ = '\'';
        else if (cp == 0x201c || cp == 0x201d) *w++ = '"';
        else if (cp == 0x2013 || cp == 0x2014) *w++ = '-';
        else if (cp == 0x2026) { *w++ = '.'; *w++ = '.'; *w++ = '.'; }   /* 3 bytes in, 3 out */
        else *w++ = '?';
    }
    *w = 0;
}
#endif
