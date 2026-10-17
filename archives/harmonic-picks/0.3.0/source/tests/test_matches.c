#include <assert.h>
#include <stdarg.h>
#include "../harmonic-picks.c"

static az_deck_info decks[4];
static char drawn[16384];
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static int info(int deck, az_deck_info *out) { *out = decks[deck]; return 0; }
static void text(az_draw *d, int x, int y, const char *s, int px, uint32_t col)
{
    (void)d; (void)x; (void)y; (void)px; (void)col;
    size_t n = strlen(drawn);
    snprintf(drawn + n, sizeof drawn - n, "%s\n", s);
}
static void header(az_draw *d, const char *s) { text(d, 0, 0, s, 20, 0); }
static uint32_t color(int n) { return (uint32_t)n; }
static void fake_redraw(void) {}
static void paint(void) { drawn[0] = 0; draw(NULL, (az_rect){0, 0, 1280, 800}, NULL); }
static void tap(int n)
{
    az_rect r = g_buttons[n];
    az_event ev = {.kind = AZ_EV_TAP, .x = r.x + 12, .y = r.y + 12};
    CHECK(input(&ev, NULL) == 1);
}
static void add(uint32_t id, const char *title, const char *key, double bpm, int stick)
{
    az_track t = {.id = id, .title = title, .artist = "Test", .key = key, .bpm = bpm};
    on_track(&t, &stick);
}
int main(void)
{
    static const az_core_v1 core = {.abi = AZ_MOD_ABI, .size = sizeof core,
        .deck_info = info, .draw_text = text, .draw_header = header, .color = color,
        .request_redraw = fake_redraw};
    C = &core;
    int idx, minor;
    CHECK(parse_key("Am", &idx, &minor) && idx == 7 && minor == 1);
    CHECK(parse_key("1m", &idx, &minor) && idx == 7 && minor == 1);
    CHECK(!parse_key("99999999999999A", &idx, &minor));
    CHECK(key_step(11, 1, 0, 1) == 1);
    CHECK(key_step(7, 1, 7, 0) == 1);
    CHECK(key_step(7, 1, 8, 0) == -1);
    float factor;
    CHECK(fabsf(match_pitch(123, 120, &factor) - 2.5f) < .001f && factor == 1);
    CHECK(fabsf(match_pitch(120, 125, &factor) + 4) < .001f && factor == 1);
    CHECK(fabsf(match_pitch(126, 63, &factor)) < .001f && factor == 2);
    CHECK(fabsf(match_pitch(63, 126, &factor)) < .001f && factor == .5f);
    add(1, "Reference", "8A", 120, 0);
    add(2, "Same key", "8A", 123, 0);
    add(3, "Relative vocal", "8B", 120, 1);
    add(4, "Wrong key", "2B", 120, 0);
    add(5, "Too slow", "8A", 112.9, 0); /* old gap formula wrongly admitted this */
    add(6, "Half time", "9A", 60, 1);
    add(7, "Invalid", "8A", NAN, 0);
    CHECK(g_nlib == 6);
    struct pick picks[40]; int n;
    rank(1, 7, 1, 120, 0, picks, &n);
    CHECK(n == 3);
    for (int i = 0; i < n; i++) CHECK(picks[i].id != 1 && picks[i].id != 4 && picks[i].id != 5);
    rank(1, 7, 1, 120, 1, picks, &n);
    CHECK(n == 1 && picks[0].id == 3 && picks[0].stick == 1);
    rank(1, 7, 1, NAN, 0, picks, &n); CHECK(n == 0);
    rank(1, -1, 1, 120, 0, picks, &n); CHECK(n == 0);
    decks[0] = (az_deck_info){.loaded = 1, .playing = 1, .content_id = 1, .bpm = 120,
        .title = "Reference", .artist = "Test", .key = "8A"};
    decks[1] = (az_deck_info){.loaded = 1, .playing = 1, .content_id = 2, .bpm = 123,
        .title = "Same key", .artist = "Test", .key = "8A"};
    g_lib_ready = g_nlib; g_lib_done = g_show_done = g_reader_ok = 1; g_show_lib = g_nlib;
    set_seed(); CHECK(g_seed_deck == -1 && g_seed_idx == -1);
    paint(); tap(0); CHECK(g_seed_deck == 0 && g_seed_bpm == 120);
    int gen = g_seed_gen;
    set_seed(); CHECK(g_seed_gen == gen);
    decks[0].bpm = 126; set_seed(); CHECK(g_seed_bpm == 126 && g_seed_gen == gen + 1);
    decks[0].playing = 0; set_seed(); CHECK(g_seed_deck == 0);
    paint(); CHECK(strstr(drawn, "Finding matches"));
    tap(2); CHECK(g_seed_deck == 2 && g_seed_idx == -1 && g_seed_bpm == 0);
    tap(1); CHECK(g_seed_deck == 1 && g_seed_bpm == 123);
    tap(4); CHECK(g_seed_vox_only == 1);
    az_event click = {.kind = AZ_EV_CLICK}; input(&click, NULL); CHECK(g_seed_vox_only == 0);
    /* Library fallback must not use a same-id track from a different USB. */
    decks[1].key[0] = 0; strcpy(decks[1].title, "Other song"); set_seed(); CHECK(g_seed_idx == -1);
    strcpy(decks[1].title, "Same key"); set_seed(); CHECK(g_seed_idx == 7);
    add(2, "Same key", "3B", 123, 1); g_lib_ready = g_nlib;
    set_seed(); CHECK(g_seed_idx == -1);
    strcpy(decks[1].key, "8A"); set_seed(); CHECK(g_seed_idx == 7);
    decks[1].bpm = 0; set_seed(); CHECK(g_seed_bpm == 0);
    decks[1].bpm = 123; set_seed();
    g_nshow = 40; g_show_gen = g_seed_gen;
    for (int i = 0; i < 40; i++) g_show[i] = (struct pick){.idx = 7, .minor = 1, .bpm = 123, .factor = 1};
    paint(); CHECK(g_fit > 0 && g_fit < 40);
    tap(6); CHECK(g_scroll == g_fit);
    for (int i = 0; i < 10; i++) { tap(6); paint(); }
    CHECK(g_scroll == 40 - g_fit);
    tap(5); CHECK(g_scroll == 40 - 2 * g_fit);
    g_show_gen--; paint(); CHECK(strstr(drawn, "Finding matches") && !strstr(drawn, "Artist / track"));
    az_event back = {.kind = AZ_EV_BACK}; CHECK(input(&back, NULL) == 0 && !g_page_on);
    printf("PASS: %d matching, metadata, live-tempo and input checks\n", checks);
    return 0;
}
