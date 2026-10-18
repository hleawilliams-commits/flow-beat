/* SPDX-License-Identifier: GPL-3.0-only
 * Request parsing, query integers, PIN gate, JSON escaping and HTTP headers. */
#include <assert.h>
#include "../server.h"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static int json_is(const char *in, const char *want)
{
    cr_sb b = {0}; cr_json_str(&b, in); int ok = !strcmp(b.s, want); cr_sb_free(&b); return ok;
}
int main(void)
{
    cr_req r;
    CHECK(cr_parse_request("GET / HTTP/1.1\r\nHost: x\r\n\r\n", &r) && r.get && !strcmp(r.path, "/") && !r.query[0]);
    CHECK(cr_parse_request("GET /api/tracks?stick=1&pin=1234 HTTP/1.1\r\n", &r) &&
          !strcmp(r.path, "/api/tracks") && !strcmp(r.query, "stick=1&pin=1234"));
    CHECK(cr_parse_request("POST /api/queue/add?id=5 HTTP/1.1\r\n", &r) && !r.get);
    const char *bad[] = {"", "GET", "GET  HTTP/1.1", "GET api HTTP/1.1", "PUT / HTTP/1.1", "GET /x", "GET /x\r\n",
                         "DELETE /api HTTP/1.1"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!cr_parse_request(bad[i], &r));
    char longp[200] = "GET /"; memset(longp + 5, 'a', 100); strcpy(longp + 105, " HTTP/1.1");
    CHECK(!cr_parse_request(longp, &r));                       /* path too long: refused, not cut */
    char longq[400] = "GET /a?"; memset(longq + 7, 'q', 300); strcpy(longq + 307, " HTTP/1.1");
    CHECK(!cr_parse_request(longq, &r));
    CHECK(cr_qint("stick=1&pin=0042", "pin", -1) == 42 && cr_qint("stick=1&pin=0042", "stick", -1) == 1);
    CHECK(cr_qint("xpin=5&pin=7", "pin", -1) == 7);             /* exact names only */
    CHECK(cr_qint("pin=", "pin", -1) == -1 && cr_qint("pin=12a", "pin", -1) == -1 && cr_qint("", "pin", 9) == 9);
    CHECK(cr_qint("i=-3", "i", 0) == -3 && cr_qint("id=999999999999", "id", -1) == -1);
    CHECK(cr_pin_ok("pin=4821", 4821) && cr_pin_ok("a=1&pin=4821&b=2", 4821));
    CHECK(!cr_pin_ok("pin=4822", 4821) && !cr_pin_ok("", 4821) && !cr_pin_ok("pin=-4821", 4821) && !cr_pin_ok("pin=48210", 4821));
    CHECK(json_is("Access Denied", "\"Access Denied\""));
    CHECK(json_is("a\"b\\c", "\"a\\\"b\\\\c\""));
    CHECK(json_is("tab\there\n", "\"tab\\u0009here\\u000a\""));
    CHECK(json_is("深夜のミックス 🎶", "\"深夜のミックス 🎶\""));
    CHECK(json_is("bad\xff\xc0x", "\"bad\xef\xbf\xbd\xef\xbf\xbdx\""));          /* invalid UTF-8 -> U+FFFD */
    CHECK(json_is("\xed\xa0\x80", "\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\""));   /* surrogate encoded: rejected */
    CHECK(json_is("\xe2\x82", "\"\xef\xbf\xbd\xef\xbf\xbd\""));                /* truncated sequence */
    CHECK(json_is(NULL, "\"\""));
    cr_sb h = {0}; cr_http_head(&h, 403, "application/json", 12);
    CHECK(!strncmp(h.s, "HTTP/1.1 403 Forbidden\r\n", 24) && strstr(h.s, "Content-Length: 12\r\n") && strstr(h.s, "\r\n\r\n"));
    cr_sb_free(&h);
    cr_sb big = {0}; for (int i = 0; i < 100000; i++) cr_sb_fmt(&big, "[%d,", i);
    CHECK(!big.oom && big.n > 600000 && big.s[big.n] == 0); cr_sb_free(&big);
    printf("server: %d checks passed\n", checks);
    return 0;
}
