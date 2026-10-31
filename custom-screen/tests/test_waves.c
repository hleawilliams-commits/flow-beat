/* SPDX-License-Identifier: GPL-3.0-only
 * Host checks for the waveform loader and painter (waves.h) with a small fake core: a fake
 * USB in a temp folder holding two real rekordbox analyses (tests/fixtures), decks that load,
 * play, eject and swap while a second thread paints, and the painter's output written as a
 * picture (PPM) for the preview. Run from the mod folder:
 *   cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I<sdk>/include -pthread tests/test_waves.c -lm -o /tmp/w
 *   /tmp/w [out.ppm]           (also with -fsanitize=address,undefined or -fsanitize=thread)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "azmod-mod.h"
static void gp_sleep_ms(long ms) { usleep((useconds_t)ms * 1000); }
#include "../jobs.h"
#include "../waves.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- the fake core ---------------------------------------------------------------------- */
#define FW 1280
#define FH 800
static uint32_t fb[FH][FW];
static long fills;
struct az_draw { int magic; };

static void f_fill(az_draw *d, int x, int y, int w, int h, uint32_t c)
{
    (void)d;
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > FW || y + h > FH) { printf("fill off screen %d,%d %dx%d\n", x, y, w, h); fails++; return; }
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) fb[j][i] = c;
    fills++;
}
static void f_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c) { (void)d; (void)x; (void)y; (void)s; (void)px; (void)c; }
static int f_width(const char *s, int px) { return (int)strlen(s) * px / 2; }
static void f_log(const char *id, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("  [%s] ", id); vprintf(fmt, ap); printf("\n");
    va_end(ap);
}

static pthread_mutex_t deck_mu = PTHREAD_MUTEX_INITIALIZER;
static az_deck_info decks[4];
static char usb[256];

static int f_deck_info(int deck, az_deck_info *out)
{
    if (deck < 0 || deck > 3) return -1;
    pthread_mutex_lock(&deck_mu);
    int size = out->size;
    *out = decks[deck];
    out->size = size;
    pthread_mutex_unlock(&deck_mu);
    return 0;
}

static const struct { uint32_t id; const char *title, *rel; } LIB[] = {
    { 1938, "Green Onions", "/Contents/Julie Driscoll, Brian Auger & The Trinity/Jools & Brian/07 - Green Onions.flac" },
    { 102821, "Paradise City", "/Contents/Guns N' Roses/Appetite for Destruction/06 Paradise City.mp3" },
    { 77, "Not Analysed", "/Contents/nobody/never.mp3" },
};
static int lib_calls;
static int f_lib_tracks(const char *id, int stick, int pl, int (*fn)(const az_track *, void *), void *user)
{
    (void)id; (void)pl;
    __atomic_add_fetch(&lib_calls, 1, __ATOMIC_RELAXED);
    if (stick != 0) return -1;
    for (size_t i = 0; i < sizeof LIB / sizeof LIB[0]; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s%s", usb, LIB[i].rel);
        az_track t = { sizeof t, LIB[i].id, LIB[i].title, "", "", "", path, 120, 200 };
        if (fn(&t, user)) break;
    }
    return 0;
}

static az_core_v1 core;

static void set_deck(int d, uint32_t id, const char *title, double pos, int playing, double bpm)
{
    pthread_mutex_lock(&deck_mu);
    memset(&decks[d], 0, sizeof decks[d]);
    decks[d].loaded = id != 0;
    decks[d].playing = playing;
    decks[d].content_id = id;
    decks[d].pos_s = pos; decks[d].len_s = 200; decks[d].bpm = bpm;
    if (title) snprintf(decks[d].title, sizeof decks[d].title, "%s", title);
    pthread_mutex_unlock(&deck_mu);
}

static void copy(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    if (!a || !b) { printf("copy %s -> %s failed\n", from, to); exit(2); }
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, a))) fwrite(buf, 1, n, b);
    fclose(a); fclose(b);
}

static void make_usb(void)
{
    snprintf(usb, sizeof usb, "/tmp/cw-usb-%d", (int)getpid());
    char p[512];
    const char *dirs[] = { "", "/PIONEER", "/PIONEER/USBANLZ", "/PIONEER/USBANLZ/P018", "/PIONEER/USBANLZ/P018/00000792",
                           "/PIONEER/USBANLZ/P04B", "/PIONEER/USBANLZ/P04B/000191A5", "/PIONEER/USBANLZ/P04B/junk" };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) { snprintf(p, sizeof p, "%s%s", usb, dirs[i]); mkdir(p, 0755); }
    const char *f[][2] = { { "P018_00000792.DAT", "P018/00000792/ANLZ0000.DAT" }, { "P018_00000792.EXT", "P018/00000792/ANLZ0000.EXT" },
                           { "P04B_000191A5.DAT", "P04B/000191A5/ANLZ0000.DAT" }, { "P04B_000191A5.EXT", "P04B/000191A5/ANLZ0000.EXT" } };
    for (int i = 0; i < 4; i++) {
        char from[256];
        snprintf(from, sizeof from, "tests/fixtures/%s", f[i][0]);
        snprintf(p, sizeof p, "%s/PIONEER/USBANLZ/%s", usb, f[i][1]);
        copy(from, p);
    }
}

/* the 4 DECK waveform area: rows 3-1-2-4 at y 80/202/324/446, x 212..1088 */
static const int ROW_Y[4] = { 202, 324, 80, 446 };
static void paint_all(int mono)
{
    az_draw d = { 1 };
    az_rect a = { 212, 80, 876, 480 };
    cw_style st = { 0xFFFFFF, 0xA5A2B0, 0x2A2633, 0x0C0812, 0 };
    f_fill(&d, a.x, a.y, a.w, a.h, 0x0C0812);
    for (int deck = 0; deck < 4; deck++) {
        int y = ROW_Y[deck];
        cw_paint_wave(&d, a, 232, y + 22, 836, 70, deck, 16, 1, mono ? 0x32D5FF : 0, &st);
        cw_paint_overview(&d, a, 232, y + 96, 836, 18, deck, 0, &st);
    }
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", FW, FH);
    for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++) {
        uint32_t c = fb[y][x];
        unsigned char px[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void run_job(int n) { for (int i = 0; i < n; i++) { cw_job(NULL); cw_log_flush(); } }

/* ---- concurrency: one thread loads while another paints --------------------------------- */
static int stop;
static void *painter(void *arg)
{
    (void)arg;
    long n = 0;
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) { paint_all(n & 1); n++; }
    return (void *)n;
}

int main(int argc, char **argv)
{
    core.abi = AZ_MOD_ABI; core.size = sizeof core;
    core.fill_rect = f_fill; core.draw_text = f_text; core.text_width = f_width; core.log = f_log;
    core.deck_info = f_deck_info; core.lib_tracks = f_lib_tracks;
    (void)cs_jobs_start;                        /* the tests call the job directly instead */
    make_usb();
    CHECK(cw_init(&core, "custom-screen") == 0);

    /* nothing wanted: nothing read */
    set_deck(0, 1938, "Green Onions", 60.0, 1, 143.07);
    run_job(3);
    CHECK(lib_calls == 0 && cw_deck[0].status == CW_EMPTY);

    cw_set_wanted(1);
    set_deck(1, 102821, "Paradise City", 100.0, 0, 100.0);
    set_deck(2, 77, "Not Analysed", 10.0, 1, 128.0);
    set_deck(3, 0, NULL, 0, 0, 0);
    run_job(1);
    CHECK(cw_deck[0].status == CW_READY && cw_deck[0].t && cw_deck[0].t->nbeats == 486);
    CHECK(cw_deck[1].status == CW_READY && cw_deck[1].t && cw_deck[1].t->nbeats == 0);
    CHECK(cw_deck[2].status == CW_MISSING && !cw_deck[2].t);
    CHECK(cw_deck[3].status == CW_EMPTY);
    CHECK(cw_idx[0].n == 2);                    /* the junk folder is skipped */
    CHECK(cw_moving(0) && !cw_moving(1) && !cw_moving(2) && !cw_moving(3));   /* only a playing, ready deck scrolls */
    int calls = lib_calls;
    run_job(5);                                 /* no change: no more library reads */
    CHECK(lib_calls == calls);
    run_job(CW_RETRY_MS / CS_JOBS_PERIOD_MS);   /* the missing one is retried, once */
    CHECK(lib_calls == calls + 1 && cw_deck[2].status == CW_MISSING);

    /* paint: waveform pixels where the playheads are, nothing outside the panel */
    memset(fb, 0, sizeof fb);
    fills = 0;
    paint_all(0);
    CHECK(fills > 0 && fills < 8000);           /* merged columns, not one rect per pixel */
    int lit = 0;
    for (int y = ROW_Y[0] + 22; y < ROW_Y[0] + 92; y++) for (int x = 232; x < 1068; x++) lit += fb[y][x] != 0x0C0812;
    CHECK(lit > 836 * 70 / 10);
    for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++)
        if ((x < 212 || x >= 1088 || y < 80 || y >= 560) && fb[y][x]) { CHECK(!"painted outside the panel"); y = FH; break; }
    CHECK(fb[ROW_Y[0] + 57][232 + 418] == 0xFFFFFF || fb[ROW_Y[0] + 57][232 + 417] == 0xFFFFFF);   /* centre playhead */
    if (argc > 1) write_ppm(argv[1]);

    /* eject frees, a new load replaces */
    set_deck(0, 0, NULL, 0, 0, 0);
    run_job(1);
    CHECK(cw_deck[0].status == CW_EMPTY && !cw_deck[0].t);
    set_deck(0, 102821, "Paradise City", 30.0, 1, 101.0);
    run_job(1);
    CHECK(cw_deck[0].status == CW_READY && cw_deck[0].t && cw_deck[0].t->nbeats == 0);
    /* not wanted any more: everything freed */
    cw_set_wanted(0);
    run_job(1);
    for (int d = 0; d < 4; d++) CHECK(!cw_deck[d].t && cw_deck[d].status == CW_EMPTY);

    /* loads, swaps and ejects on one thread while another paints (run under TSan) */
    cw_set_wanted(1);
    pthread_t th;
    pthread_create(&th, NULL, painter, NULL);
    for (int r = 0; r < 60; r++) {
        int k = r % 3;
        set_deck(r & 3, k == 2 ? 0 : LIB[k].id, k == 2 ? NULL : LIB[k].title, r * 1.5, r & 1, 120);
        run_job(1);
    }
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    void *painted;
    pthread_join(th, &painted);
    CHECK((long)painted > 0);
    cw_set_wanted(0);
    run_job(1);
    for (int d = 0; d < 4; d++) CHECK(!cw_deck[d].t);
    for (int s = 0; s < 2; s++) free(cw_idx[s].e);

    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", usb);
    if (system(cmd) != 0) printf("could not remove %s\n", usb);
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("waves: all passed (%ld paints in the concurrent run)\n", (long)painted);
    return 0;
}
