/* SPDX-License-Identifier: GPL-3.0-only
 * 0.6.0: the folded 4-Deck Phase Strip (phase widget) and Dead-Air Guard (dead_air widget) on
 * a fake core: LOAD NEXT / ENDING / cued / DEAD AIR, the warning off the deck screen, the
 * phase lanes, and the Mod Menu switch.
 *   cc -std=gnu11 -fsanitize=address,undefined -I<sdk>/include tests/test_folded.c -lpthread -lm
 * (not TSan: the fake deck below is written by the test and read by the mod threads unlocked)
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <unistd.h>
#include "../custom-screen.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int f_shown[4]; static char *f_slot0;
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
static struct { int loaded, playing; unsigned id; double len, pos; } f_deck[4];
static int f_deck_info(int deck, az_deck_info *o) { int sz = o->size; memset(o, 0, sizeof *o); o->size = sz; o->len_s = -1;
    if (deck < 0 || deck > 3) return -1;
    o->loaded = f_deck[deck].loaded; o->playing = f_deck[deck].playing; o->content_id = f_deck[deck].id;
    if (o->loaded) { o->len_s = f_deck[deck].len; o->pos_s = f_deck[deck].pos; o->bpm = 124; snprintf(o->title, sizeof o->title, "Track %u", f_deck[deck].id); }
    return 0; }
static int f_deck_grid(int deck, double *b, double *bar) { if (!f_deck[deck].playing) return -1; *b = 0.25; *bar = -1; return 0; }
static unsigned f_views;
static az_surface *f_surface_create(const char *id, const az_surface_v1 *s) { (void)id; f_views = s->views; static char slots[4]; static int n; f_slot0 = slots; return n < 4 ? (az_surface *)&slots[n++] : NULL; }
static void f_place(az_surface *s, az_rect r) { (void)s; (void)r; }
static void f_show(az_surface *s, int on) { if (f_slot0) f_shown[(char *)s - f_slot0] = on; }
static void f_redraw(az_surface *s) { (void)s; }
static int f_thread(const char *id, const char *name, void *(*fn)(void *), void *arg) { (void)id; (void)name;
    pthread_t t; if (pthread_create(&t, NULL, fn, arg)) return -1; pthread_detach(t); return 0; }
static const char *f_data_dir(const char *id) { (void)id; return f_dir; }
static int f_cfg_get(const char *id, const char *k, int def) { (void)id; (void)k; return def; }
static void f_cfg_set(const char *id, const char *k, int v) { (void)id; (void)k; (void)v; }
static int f_touch_filter(const char *id, az_touch_fn fn, void *u) { (void)id; (void)fn; (void)u; return 0; }
static int f_get_view(void) { return f_view; }
static int f_register_row(const char *id, const az_row_v1 *r) { (void)id; (void)r; return f_rows++; }
static int f_dg_on = 1;
static int f_row_value(int h) { return h == 0 || (h == cs_dg_row && f_dg_on); }   /* on: Custom Screen, Dead-air warning; Warn = 20 s */
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


static void run(int ms) { for (int t = 0; t < ms; t += 20) { CS_MOD.tick(); usleep(20000); } }
static void paint(int slot, az_rect r) { f_clear(); cs_paint(NULL, r, (void *)(intptr_t)slot); }
static const az_rect STRIP = { 212, 46, 838, 47 };

int main(void)
{
    snprintf(f_dir, sizeof f_dir, "/tmp/cs-folded-%d", (int)getpid());
    mkdir(f_dir, 0700);
    char path[300]; snprintf(path, sizeof path, "%s/layout.txt", f_dir);
    FILE *f = fopen(path, "w");
    fputs("version 1\nname folded\n"
          "panel 1 x=0 y=80 w=140 h=60 views=4\n"
          "widget 1 title deck=1 x=8 y=2 w=130 h=28 size=18\n"
          "panel 4 x=212 y=46 w=838 h=47 views=4 bg=auto\n"
          "widget 4 phase x=0 y=0 w=838 h=47 size=10 zoom=13\n"
          "widget 4 dead_air x=0 y=0 w=838 h=47 size=22 align=c\n", f);
    fclose(f);
    f_init();
    CHECK(CS_MOD.init(&F) == 0);
    CHECK(f_views & AZ_VIEW_BIT(AZ_VIEW_BROWSE));
    run(400);
    CHECK(cs_ui.n == 2 && cs_visible[0] && cs_visible[1]);

    /* nothing loaded, nothing ever heard: no warning, empty lanes */
    paint(1, STRIP);
    CHECK(!strstr(f_drawn, "LOAD") && !strstr(f_drawn, "DEAD"));

    /* deck 1 plays, 15 s left, nothing cued: LOAD NEXT (Warn = 20 s) */
    f_deck[0] = (typeof(f_deck[0])){ 1, 1, 11, 200, 185 };
    run(600);
    paint(1, STRIP);
    printf("  painted: %s\n", f_drawn);
    CHECK(strstr(f_drawn, "LOAD NEXT  0:15") != NULL);
    CHECK(strstr(f_drawn, "1|") != NULL);   /* the phase lane label of deck 1 */
    int fills_alarm = f_fills;

    /* 8 s left: ENDING */
    f_deck[0].pos = 192; run(400);
    paint(1, STRIP);
    CHECK(strstr(f_drawn, "ENDING  0:08") != NULL);

    /* the warning shows on the browse screen too; the other panel doesn't */
    f_view = AZ_VIEW_BROWSE; run(200);
    CHECK(f_shown[1] == 1 && f_shown[0] == 0);
    f_view = AZ_VIEW_MENU; run(200);
    CHECK(f_shown[1] == 0);
    f_view = AZ_VIEW_DECK; run(200);
    CHECK(f_shown[1] == 1 && f_shown[0] == 1);

    /* deck 2 holds a different track: cued, no warning; the phase strip still draws */
    f_deck[1] = (typeof(f_deck[1])){ 1, 0, 22, 300, 0 };
    run(400);
    paint(1, STRIP);
    CHECK(!strstr(f_drawn, "ENDING") && !strstr(f_drawn, "LOAD"));
    CHECK(strstr(f_drawn, "1|") && strstr(f_drawn, "2|"));
    CHECK(f_fills > 10 && f_fills < fills_alarm + 40);

    /* off the deck screen with no warning: the strip panel hides */
    f_view = AZ_VIEW_BROWSE; run(200);
    CHECK(f_shown[1] == 0);
    f_view = AZ_VIEW_DECK; run(200);

    /* the music stops with the mic off: DEAD AIR after 1.5 s */
    f_deck[0].playing = 0; run(2000);
    paint(1, STRIP);
    CHECK(strstr(f_drawn, "DEAD AIR") != NULL);

    /* the Mod Menu switch turns the warning off */
    f_dg_on = 0; run(200);
    paint(1, STRIP);
    CHECK(!strstr(f_drawn, "DEAD AIR"));

    unlink(path); rmdir(f_dir);
    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("folded mods: all checks passed\n");
    return 0;
}
