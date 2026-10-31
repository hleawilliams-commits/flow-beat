/* SPDX-License-Identifier: GPL-3.0-only
 * End-to-end check of the FLOW Status Panel on a fake core, with real UDP on 127.0.0.1:
 * a layout with flowvj / vj_lights / vj_tap widgets, FVS1 packets in on 9102, taps out on
 * 9103, the panel's paint, and the touch rules (taken only on the deck screen while FlowVJ
 * answers, otherwise passed to the deck).
 *   cc -std=gnu11 -I<sdk>/include tests/test_flowvj_live.c -lpthread -lm
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <unistd.h>
#include "../custom-screen.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- the fake core ---------------------------------------------------------------- */
static int f_view = AZ_VIEW_DECK, f_rows;
static char f_drawn[4096];          /* every string painted since the last f_clear() */
static int f_fills;
static char f_dir[256];
static void f_clear(void) { f_drawn[0] = 0; f_fills = 0; }
static void f_log(const char *id, const char *fmt, ...) { (void)id; va_list ap; va_start(ap, fmt); fprintf(stderr, "  log: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap); }
static void f_text(az_draw *d, int x, int y, const char *s, int px, uint32_t rgb) { (void)d; (void)x; (void)y; (void)px; (void)rgb;
    strncat(f_drawn, s, sizeof f_drawn - strlen(f_drawn) - 2); strcat(f_drawn, "|"); }
static void f_fill(az_draw *d, int x, int y, int w, int h, uint32_t rgb) { (void)d; (void)x; (void)y; (void)w; (void)h; (void)rgb; f_fills++; }
static int f_width(const char *s, int px) { return (int)strlen(s) * px * 6 / 10; }
static uint32_t f_color(int role) { return role == AZ_COL_BG ? 0x101010 : 0xFFFFFF; }
static int f_deck_count(void) { return 4; }
static int f_deck_info(int deck, az_deck_info *o) { (void)deck; int sz = o->size; memset(o, 0, sizeof *o); o->size = sz; o->len_s = -1; return 0; }
static int f_deck_grid(int deck, double *b, double *bar) { (void)deck; (void)b; (void)bar; return -1; }
static az_surface *f_surface_create(const char *id, const az_surface_v1 *s) { (void)id; (void)s; static char slots[4]; static int n; return n < 4 ? (az_surface *)&slots[n++] : NULL; }
static void f_place(az_surface *s, az_rect r) { (void)s; (void)r; }
static void f_show(az_surface *s, int on) { (void)s; (void)on; }
static void f_redraw(az_surface *s) { (void)s; }
static int f_thread(const char *id, const char *name, void *(*fn)(void *), void *arg) { (void)id; (void)name;
    pthread_t t; if (pthread_create(&t, NULL, fn, arg)) return -1; pthread_detach(t); return 0; }
static const char *f_data_dir(const char *id) { (void)id; return f_dir; }
static int f_cfg_get(const char *id, const char *k, int def) { (void)id; (void)k; return def; }
static void f_cfg_set(const char *id, const char *k, int v) { (void)id; (void)k; (void)v; }
static int f_touch_filter(const char *id, az_touch_fn fn, void *u) { (void)id; (void)fn; (void)u; return 0; }
static int f_get_view(void) { return f_view; }
static int f_register_row(const char *id, const az_row_v1 *r) { (void)id; (void)r; return f_rows++; }
static int f_row_value(int h) { return h == 0; }   /* "Custom Screen" on, everything else at 0 */
static void f_banner(const char *id, const char *t, int ms) { (void)id; (void)ms; fprintf(stderr, "  banner: %s\n", t); }

static az_core_v1 F;
static void f_init(void)
{
    memset(&F, 0, sizeof F);
    F.abi = AZ_MOD_ABI; F.size = sizeof F;
    F.log = f_log; F.draw_text = f_text; F.fill_rect = f_fill; F.text_width = f_width; F.color = f_color;
    F.deck_count = f_deck_count; F.deck_info = f_deck_info; F.deck_grid = f_deck_grid;
    F.surface_create = f_surface_create; F.surface_place = f_place; F.surface_show = f_show; F.surface_redraw = f_redraw;
    F.thread_start = f_thread; F.data_dir = f_data_dir; F.cfg_get = f_cfg_get; F.cfg_set = f_cfg_set;
    F.touch_filter = f_touch_filter; F.view = f_get_view; F.register_row = f_register_row; F.row_value = f_row_value;
    F.banner = f_banner;
}

/* ---- FlowVJ's side: one socket on 127.0.0.1:9103 sends FVS1 and receives the taps -------- */
static int vj;
static void vj_open(void)
{
    vj = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(FV_TAP_PORT), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if (bind(vj, (struct sockaddr *)&a, sizeof a)) { perror("bind 9103"); exit(2); }
    struct timeval tv = { 0, 400000 };
    setsockopt(vj, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}
static void vj_send(const char *pkt)
{
    struct sockaddr_in d = { .sin_family = AF_INET, .sin_port = htons(FV_PORT), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    sendto(vj, pkt, strlen(pkt), 0, (struct sockaddr *)&d, sizeof d);
}
static int vj_recv(char *buf, size_t cap)
{
    ssize_t n = recv(vj, buf, cap - 1, 0);
    if (n < 0) n = 0;
    buf[n] = 0;
    return (int)n;
}

static void run(int ms) { for (int t = 0; t < ms; t += 20) { CS_MOD.tick(); usleep(20000); } }
static void paint_vj(void) { f_clear(); cs_paint(NULL, (az_rect){ 1090, 490, 180, 96 }, (void *)(intptr_t)0); }
static int touch(int x, int y) { az_touch t = { 0, x, y, 1 }; return cs_touch(&t, NULL); }

int main(void)
{
    snprintf(f_dir, sizeof f_dir, "/tmp/cs-flowvj-%d", (int)getpid());
    mkdir(f_dir, 0700);
    char path[300]; snprintf(path, sizeof path, "%s/layout.txt", f_dir);
    FILE *f = fopen(path, "w");
    fputs("version 1\nname FLOW status\n"
          "panel 1 x=1090 y=490 w=180 h=96 views=4,24 bg=auto border=accent bw=2 edge=top\n"
          "widget 1 flowvj x=8 y=6 w=166 h=26 size=15 text=VJ \n"
          "widget 1 vj_lights x=8 y=34 w=166 h=20 size=13 color=dim text=LIGHTS \n"
          "widget 1 vj_tap act=next x=8 y=60 w=52 h=30 size=13\n"
          "widget 1 vj_tap act=hold x=64 y=60 w=52 h=30 size=13\n"
          "widget 1 vj_tap act=safe x=120 y=60 w=52 h=30 size=13\n", f);
    fclose(f);
    f_init(); vj_open();
    CHECK(CS_MOD.init(&F) == 0);
    CHECK(fv_sock >= 0);
    run(400);
    CHECK(cs_ui.n == 1 && cs_visible[0]);

    /* before FlowVJ: grey text, and a tap on NEXT goes to the deck */
    paint_vj();
    CHECK(strstr(f_drawn, "VJ FLOW not seen") != NULL);
    CHECK(strstr(f_drawn, "LIGHTS --") != NULL);
    CHECK(touch(1090 + 8 + 10, 490 + 60 + 10) == AZ_TOUCH_PASS);

    /* noise on the port is ignored (flow-beat's own broadcast) */
    vj_send("{\"flowbeat\":1,\"type\":\"beat\"}");
    run(200);
    CHECK(!fv_live);

    vj_send("FVS1\nseq=1\nlook=Haunt 07\nlights=CHILL 2\nlock=1\nsrc=az\nbpm=128.0\nsafe=1\nhold=0\nauto=1\nblack=0\ntap=9103\n");
    run(300);
    CHECK(fv_live);
    paint_vj();
    CHECK(strstr(f_drawn, "VJ Haunt") != NULL);   /* cut to fit beside the badges */
    CHECK(strstr(f_drawn, "LIGHTS CHILL 2") != NULL);
    CHECK(strstr(f_drawn, "SAFE|") != NULL && strstr(f_drawn, "LOCK|") != NULL && !strstr(f_drawn, "HOLD|HOLD"));
    CHECK(strstr(f_drawn, "NEXT|") != NULL);
    printf("  painted: %s\n", f_drawn);

    /* NEXT: taken, and FVC1 next arrives at 9103 */
    char got[128];
    CHECK(touch(1090 + 8 + 10, 490 + 60 + 10) == AZ_TOUCH_TAKE);
    CHECK(vj_recv(got, sizeof got) > 0 && !strcmp(got, "FVC1 1 next\n"));
    printf("  tap 1: %s", got);
    /* SAFE, and the gap between buttons passes */
    CHECK(touch(1090 + 120 + 5, 490 + 60 + 5) == AZ_TOUCH_TAKE);
    CHECK(vj_recv(got, sizeof got) > 0 && !strcmp(got, "FVC1 2 safe\n"));
    CHECK(touch(1090 + 61, 490 + 70) == AZ_TOUCH_PASS);
    /* the rest of the screen is the deck's */
    CHECK(touch(640, 400) == AZ_TOUCH_PASS);

    /* in BROWSE no panel shows: the same spot goes to the deck */
    f_view = AZ_VIEW_BROWSE; run(100);
    CHECK(touch(1090 + 8 + 10, 490 + 60 + 10) == AZ_TOUCH_PASS);
    f_view = AZ_VIEW_DECK; run(100);

    /* HOLD lights up from FlowVJ's state; BLACK shows on the lights line */
    vj_send("FVS1\nseq=2\nlook=Haunt 07\nlights=CHILL 2\nlock=0\nhold=1\nblack=1\n");
    run(300);
    paint_vj();
    CHECK(strstr(f_drawn, "HOLD|") != NULL && strstr(f_drawn, "BLACK|") != NULL && !strstr(f_drawn, "LOCK|"));
    CHECK(strstr(f_drawn, "LIGHTS BLACKOUT") != NULL);

    /* FlowVJ goes quiet: after 3 s the buttons let go and the panel says so */
    run(3300);
    CHECK(!fv_live);
    CHECK(touch(1090 + 8 + 10, 490 + 60 + 10) == AZ_TOUCH_PASS);
    paint_vj();
    CHECK(strstr(f_drawn, "VJ FLOW not seen") != NULL);
    CHECK(vj_recv(got, sizeof got) == 0);   /* nothing sent without a tap */

    unlink(path); rmdir(f_dir);
    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("flowvj live: all checks passed\n");
    return 0;
}
