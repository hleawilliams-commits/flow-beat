/* SPDX-License-Identifier: GPL-3.0-only
 * Crate Remote's request handling, with no sockets or SDK calls so it is host-tested:
 * a growable string, JSON escaping, request-line and query parsing, the PIN check.
 */
#ifndef CR_SERVER_H
#define CR_SERVER_H
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- growable string (HTTP thread only) ---- */
typedef struct { char *s; size_t n, cap; int oom; } cr_sb;

static void cr_sb_reserve(cr_sb *b, size_t more)
{
    if (b->oom || b->n + more + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->n + more + 1) cap *= 2;
    char *p = realloc(b->s, cap);
    if (!p) { b->oom = 1; return; }
    b->s = p; b->cap = cap;
}
static void cr_sb_put(cr_sb *b, const char *s, size_t n)
{
    cr_sb_reserve(b, n);
    if (b->oom) return;
    memcpy(b->s + b->n, s, n); b->n += n; b->s[b->n] = 0;
}
static void cr_sb_str(cr_sb *b, const char *s) { cr_sb_put(b, s, strlen(s)); }
static void cr_sb_fmt(cr_sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void cr_sb_fmt(cr_sb *b, const char *fmt, ...)
{
    char tmp[256];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) cr_sb_put(b, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}
static void cr_sb_free(cr_sb *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; b->oom = 0; }

/* A JSON string literal. Control bytes are escaped; invalid UTF-8 bytes become U+FFFD so a
 * badly tagged file can't break the page. NULL is an empty string. */
static void cr_json_str(cr_sb *b, const char *s)
{
    cr_sb_put(b, "\"", 1);
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    while (*p) {
        unsigned c = *p;
        if (c == '"' || c == '\\') { char e[2] = {'\\', (char)c}; cr_sb_put(b, e, 2); p++; continue; }
        if (c < 0x20) { cr_sb_fmt(b, "\\u%04x", c); p++; continue; }
        if (c < 0x80) { cr_sb_put(b, (const char *)p, 1); p++; continue; }
        int n = c >= 0xf0 && c <= 0xf4 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 && c <= 0xdf ? 2 : 0;
        int ok = n > 0;
        for (int i = 1; ok && i < n; i++) ok = (p[i] & 0xc0) == 0x80;
        if (ok && n == 3) ok = !(c == 0xe0 && p[1] < 0xa0) && !(c == 0xed && p[1] >= 0xa0);
        if (ok && n == 4) ok = !(c == 0xf0 && p[1] < 0x90) && !(c == 0xf4 && p[1] >= 0x90);
        if (ok) { cr_sb_put(b, (const char *)p, (size_t)n); p += n; }
        else { cr_sb_str(b, "\xef\xbf\xbd"); p++; }
    }
    cr_sb_put(b, "\"", 1);
}

/* ---- requests ---- */
typedef struct { int get; char path[64]; char query[256]; } cr_req;

/* "GET /api/x?a=1 HTTP/1.1\r\n..." -> 1; anything else -> 0. Paths and queries that don't
 * fit are refused rather than cut. */
static int cr_parse_request(const char *buf, cr_req *r)
{
    memset(r, 0, sizeof *r);
    const char *p;
    if (!strncmp(buf, "GET ", 4)) { r->get = 1; p = buf + 4; }
    else if (!strncmp(buf, "POST ", 5)) { p = buf + 5; }
    else return 0;
    if (*p != '/') return 0;
    const char *end = p;
    while (*end && *end != ' ' && *end != '\r' && *end != '\n') end++;
    if (*end != ' ') return 0;
    const char *q = memchr(p, '?', (size_t)(end - p));
    size_t pl = (size_t)((q ? q : end) - p);
    if (pl >= sizeof r->path) return 0;
    memcpy(r->path, p, pl);
    if (q) {
        size_t ql = (size_t)(end - q - 1);
        if (ql >= sizeof r->query) return 0;
        memcpy(r->query, q + 1, ql);
    }
    return 1;
}

/* Integer query parameter; def when absent or not a plain decimal number. */
static long cr_qint(const char *query, const char *name, long def)
{
    size_t nl = strlen(name);
    for (const char *p = query; p && *p; ) {
        if (!strncmp(p, name, nl) && p[nl] == '=') {
            const char *v = p + nl + 1;
            int neg = *v == '-';
            if (neg) v++;
            if (*v < '0' || *v > '9') return def;
            long x = 0;
            for (; *v >= '0' && *v <= '9'; v++) {
                if (x > 100000000L) return def;
                x = x * 10 + (*v - '0');
            }
            if (*v && *v != '&') return def;
            return neg ? -x : x;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return def;
}

/* The PIN gate: every /api/ call carries pin=NNNN. Constant-time compare. */
static int cr_pin_ok(const char *query, int pin)
{
    long got = cr_qint(query, "pin", -1);
    unsigned diff = (unsigned)(got ^ pin) | (got < 0);
    return diff == 0;
}

static void cr_http_head(cr_sb *b, int code, const char *type, size_t len)
{
    const char *text = code == 200 ? "OK" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" :
                       code == 503 ? "Service Unavailable" : "Bad Request";
    cr_sb_fmt(b, "HTTP/1.1 %d %s\r\n", code, text);
    cr_sb_fmt(b, "Content-Type: %s\r\n", type);
    cr_sb_fmt(b, "Content-Length: %zu\r\n", len);
    cr_sb_str(b, "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
}
#endif
