/* SPDX-License-Identifier: GPL-3.0-only
 * Host test with a fake core: whole-library picks from both sticks, the press-to-load path
 * through a playlist (folder rows, the shortest path), the target deck (the pair partner,
 * never a playing deck), and the navigator script it arms reaching LOAD on that deck.
 *   cc -I<sdk>/include -o t tests/test_load.c -lpthread && ./t
 */
#include <assert.h>
#include <stdarg.h>
#include "../harmonic-picks.c"

static int checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } checks++; } while (0)

/* ---- fake library: stick 0 and stick 1 ---- */
struct ft { uint32_t id; const char *title, *artist, *key; double bpm; };
static const struct ft S0[] = {
    { 1, "Seed Tune", "A", "8A", 124.0 },
    { 2, "Same Key Close", "B", "Am", 125.0 },      /* same key, 0.8% */
    { 3, "Neighbour", "C", "9A", 122.0 },           /* neighbour, 1.6% */
    { 4, "Relative", "D", "C", 124.0 },             /* relative major */
    { 5, "Clash", "E", "3B", 124.0 },               /* no fit */
    { 6, "Too Fast", "F", "8A", 140.0 },            /* 12.9%: out */
    { 7, "Half Time", "G", "8A", 62.5 },            /* x2 = 125 */
    { 8, "Only In Collection", "H", "8A", 124.5 },  /* in no playlist */
};
static const struct ft S1[] = {
    { 100, "Other Stick Pick", "I", "8A", 123.5 },  /* stick 2 */
};
/* playlists on stick 0: 10 "House" (top, row 0), 20 "Folder" (top, row 1, folder),
 * 21 "Deep" (inside 20, row 0), 22 "Peak" (inside 20, row 1) */
struct fp { int id, parent; const char *name; int folder; };
static const struct fp P0[] = { { 10, 0, "House", 0 }, { 20, 0, "Folder", 1 }, { 21, 20, "Deep", 0 }, { 22, 20, "Peak", 0 } };
static const struct fp P1[] = { { 50, 0, "Stick2", 0 } };
static const uint32_t M10[] = { 1, 4 }, M21[] = { 3, 2 }, M22[] = { 9, 9, 2 }, M50[] = { 100 };

static int f_lib_tracks(const char *id, int stick, int pl, int (*fn)(const az_track *, void *), void *user)
{
    (void)id;
    const struct ft *L = stick == 0 ? S0 : S1;
    int n = stick == 0 ? (int)(sizeof S0 / sizeof S0[0]) : 1;
    const uint32_t *m = NULL; int nm = 0;
    if (pl == 10) { m = M10; nm = 2; } else if (pl == 21) { m = M21; nm = 2; }
    else if (pl == 22) { m = M22; nm = 3; } else if (pl == 50) { m = M50; nm = 1; }
    if (pl == 0) {
        for (int i = 0; i < n; i++) {
            az_track t = { sizeof t, L[i].id, L[i].title, L[i].artist, "", L[i].key, "", L[i].bpm, 300 };
            if (fn(&t, user)) break;
        }
        return 0;
    }
    for (int k = 0; k < nm; k++) {
        az_track t = { sizeof t, m[k], "x", "y", "", "8A", "", 120, 300 };
        if (fn(&t, user)) break;
    }
    return 0;
}
static int f_lib_playlists(const char *id, int stick, int (*fn)(int, int, const char *, int, void *), void *user)
{
    (void)id;
    const struct fp *P = stick == 0 ? P0 : P1;
    int n = stick == 0 ? 4 : 1;
    for (int i = 0; i < n; i++) if (fn(P[i].id, P[i].parent, P[i].name, P[i].folder, user)) break;
    return 0;
}
/* decks: 0 plays the seed; 1 is stopped (loaded or not set by the test) */
static int deck_playing[4] = { 1, 0, 0, 0 }, deck_loaded[4] = { 1, 0, 0, 0 };
static uint32_t deck_id[4] = { 1, 0, 0, 0 };
static int f_deck_info(int d, az_deck_info *o)
{
    if (d < 0 || d > 3) return -1;
    o->loaded = deck_loaded[d]; o->playing = deck_playing[d]; o->content_id = deck_id[d];
    o->bpm = 124.0; snprintf(o->key, sizeof o->key, "8A");
    return 0;
}
static az_frame_fn g_filter;
static int f_frame_filter(const char *id, unsigned m, az_frame_fn fn, void *u) { (void)id; (void)m; (void)u; g_filter = fn; return 0; }
static int g_view = AZ_VIEW_MENU;
static int f_view(void) { return g_view; }
static void *(*g_thr)(void *);
static int f_thread_start(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    (void)id; (void)name; (void)arg; g_thr = fn;
    pthread_t t; return pthread_create(&t, NULL, fn, NULL) == 0 ? (pthread_detach(t), 0) : -1;
}
static int row_vals[16], nrows;
static int f_register_row(const char *id, const az_row_v1 *r)
{
    (void)id;
    row_vals[nrows] = r->kind == AZ_ROW_CHOICE ? r->default_choice : (r->flags & AZ_ROW_DEFAULT_ON) ? 1 : 0;
    return nrows++;
}
static int f_row_value(int h) { return h >= 0 && h < nrows ? row_vals[h] : -1; }
static int f_register_screen(const char *id, const az_screen_v1 *s) { (void)id; (void)s; return 0; }
static void f_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; }
static void f_draw_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c) { (void)d; (void)x; (void)y; (void)s; (void)px; (void)c; }
static void f_header(az_draw *d, const char *t) { (void)d; (void)t; }
static uint32_t f_color(int r) { (void)r; return 0xFFFFFF; }
static int f_deck_count(void) { return 2; }

static void wait_ms(int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
static int find_pick(uint32_t id)
{
    for (int i = 0; i < g_nshow; i++) if (g_show[i].id == id) return i;
    return -1;
}

int main(void)
{
    static az_core_v1 core;
    core.abi = AZ_MOD_ABI; core.size = sizeof core;
    core.log = f_log; core.register_screen = f_register_screen; core.deck_info = f_deck_info;
    core.draw_text = f_draw_text; core.draw_header = f_header; core.color = f_color;
    core.lib_tracks = f_lib_tracks; core.lib_playlists = f_lib_playlists; core.thread_start = f_thread_start;
    core.frame_filter = f_frame_filter; core.view = f_view; core.register_row = f_register_row;
    core.row_value = f_row_value; core.deck_count = f_deck_count;
    const az_mod_v1 *m = az_mod_v1_entry();
    CHECK(m->init(&core) == 0 && g_filter_ok && g_reader_ok && nrows == 3);

    /* whole library, both sticks: picks come from outside any playlist and from USB 2 */
    on_enter(NULL);
    for (int i = 0; i < 200 && g_nshow == 0; i++) { m->tick(); wait_ms(10); if (!g_show_lib) { last_poll_ms = -POLL_MS; } }
    for (int i = 0; i < 50; i++) { m->tick(); wait_ms(10); }
    CHECK(g_nshow == 6);                                   /* 2,3,4,7,8,100 */
    CHECK(find_pick(5) < 0 && find_pick(6) < 0 && find_pick(1) < 0);
    CHECK(find_pick(8) >= 0 && find_pick(100) >= 0 && g_show[find_pick(100)].stick == 1);
    CHECK(g_show[0].step == 0);                            /* a same-key pick ranks first */

    /* 3% range drops the 1.6% neighbour? no: 1.6 < 3 stays; 10% adds nothing new here */
    row_vals[2] = 0; last_poll_ms = -POLL_MS; m->tick();
    for (int i = 0; i < 50; i++) { m->tick(); wait_ms(10); }
    CHECK(g_range == 3.0f && find_pick(3) >= 0);
    row_vals[2] = 1; last_poll_ms = -POLL_MS; m->tick();
    for (int i = 0; i < 50; i++) { m->tick(); wait_ms(10); }
    CHECK(g_range == 6.0f);

    /* target deck: seed on deck 1 -> deck 2; deck 2 playing -> none in a 2-deck view */
    CHECK(target_deck() == 1);
    deck_playing[1] = 1; deck_loaded[1] = 1;
    CHECK(target_deck() == -1);
    deck_playing[1] = 0; deck_loaded[1] = 0;

    /* a pick in no playlist: a clear message, nothing armed */
    request_load(&g_show[find_pick(8)]);
    for (int i = 0; i < 100 && g_waiting_path; i++) { m->tick(); wait_ms(10); }
    CHECK(!g_waiting_path && strstr(g_status, "no playlist") && nav_state == NAV_IDLE);

    /* "Same Key Close" (id 2) is in Folder/Deep row 1 and Folder/Peak row 2: Deep wins */
    request_load(&g_show[find_pick(2)]);
    for (int i = 0; i < 100 && g_waiting_path; i++) { m->tick(); wait_ms(10); }
    CHECK(nav_state == NAV_ARMED);
    CHECK(g_active.depth == 3 && g_active.path[0] == 1 && g_active.path[1] == 0 && g_active.path[2] == 1);
    CHECK(g_active.deck == 1 && strstr(g_active.where, "Folder / Deep / #2"));

    /* run the script through the mixer filter: it must press LOAD 2 (0x04) */
    unsigned char f[152];
    int saw_load = 0;
    long frames = 0;
    for (; frames < 12000; frames++) {      /* 5 ms frames: up to 60 s of script */
        memset(f, 0, sizeof f); f[6] = 0x08; f[34] = 100;
        g_filter(AZ_BUS_MIXER_IN, f, sizeof f, NULL);
        if (f[33] & 0x0f) saw_load = f[33] & 0x0f;
        if (f[32] & 0x10) g_view = AZ_VIEW_BROWSE;          /* PLAYLIST opened the tree */
        int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
        if (st == NAV_DONE || st == NAV_CANCELLED || st == NAV_REFUSED) break;
        wait_ms(5);
        if (frames % 20 == 0) m->tick();
    }
    printf("script ran %ld frames, state %d, load 0x%02x\n", frames, nav_state, saw_load);
    CHECK(nav_state == NAV_DONE && saw_load == 0x04);
    m->tick();
    CHECK(g_verify_left > 0 && strstr(g_status, "Loading on Deck 2"));
    deck_loaded[1] = 1; deck_id[1] = 2;
    m->tick();
    CHECK(g_verify_left == 0 && strstr(g_status, "Loaded on Deck 2"));

    /* USB 2 pick: the script switches source first (SOURCE pressed) */
    deck_loaded[1] = 0; deck_id[1] = 0;
    g_settle_until = 0;
    request_load(&g_show[find_pick(100)]);
    for (int i = 0; i < 100 && g_waiting_path; i++) { m->tick(); wait_ms(10); }
    CHECK(nav_state == NAV_ARMED && g_active.stick == 1 && g_active.depth == 2 && g_active.path[1] == 0);
    CHECK(nav_s.ops[0].op == NAV_PRESS && nav_s.ops[0].off == 32 && nav_s.ops[0].mask == 0x80);

    printf("test_load: %d checks passed\n", checks);
    return 0;
}
