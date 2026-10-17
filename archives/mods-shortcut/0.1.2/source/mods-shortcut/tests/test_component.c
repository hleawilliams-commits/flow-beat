/* SPDX-License-Identifier: GPL-3.0-only
 * Full component test with an SDK fake that includes mixer frame_filter, missing
 * from the pinned upstream harness. Invokes the actual registered callbacks. */
#include <assert.h>
#include <stdio.h>
#include "../mods-shortcut.c"

struct az_surface { int token; };
struct az_draw { int token; };
static struct az_surface fixture_surface, fixture_heading_surface;
static az_surface_v1 fixture_desc, fixture_heading_desc;
static az_frame_fn fixture_filter;
static void *fixture_filter_user;
static az_rect fixture_rect, fixture_heading_rect, fixture_paint_rect;
static int fixture_shown, fixture_count = 4, fixture_view = AZ_VIEW_DECK;
static int fixture_row = 1, fixture_heading_row = 1, fixture_banners, fixture_filters, checks;
static int fixture_created, fixture_heading_shown, fixture_redraws, fixture_theme = 6;
static int fixture_fills, fixture_texts, fixture_rects;
static uint32_t fixture_fill_color;
static int fixture_fail_surface, fixture_fail_filter;
static char fixture_label[32];
#define CHECK(x) do { assert(x); checks++; } while (0)
static int mock_view(void) { return fixture_view; }
static int mock_count(void) { return fixture_count; }
static int mock_theme(void) { return fixture_theme; }
static int mock_row_value(int h) { CHECK(h == 7 || h == 8); return h == 7 ? fixture_row : fixture_heading_row; }
static int mock_register_row(const char *id, const az_row_v1 *r)
{
    CHECK(!strcmp(id, MOD_ID));
    CHECK(!strcmp(r->key, "show") || !strcmp(r->key, "hide-heading"));
    CHECK(r->kind == AZ_ROW_TOGGLE && r->flags == AZ_ROW_DEFAULT_ON);
    return !strcmp(r->key, "show") ? 7 : 8;
}
static az_surface *mock_create(const char *id, const az_surface_v1 *s)
{
    CHECK(!strcmp(id, MOD_ID)); CHECK(s->views == AZ_VIEW_BIT(AZ_VIEW_DECK));
    CHECK(s->layer == AZ_LAYER_OVER_EP147);
    fixture_created++;
    if (fixture_created == fixture_fail_surface) return NULL;
    if (fixture_created == 1) { fixture_desc = *s; return &fixture_surface; }
    fixture_heading_desc = *s; return &fixture_heading_surface;
}
static void mock_place(az_surface *s, az_rect r)
{
    CHECK(s == &fixture_surface || s == &fixture_heading_surface);
    CHECK(r.x >= 0 && r.x + r.w <= 1280);
    if (s == &fixture_surface) { CHECK(r.y == 50 && r.h == 30); fixture_rect = r; }
    else { CHECK(r.x == 0 && r.y == 0 && r.w == 174 && r.h == 46); fixture_heading_rect = r; }
}
static void mock_show(az_surface *s, int on)
{
    CHECK(s == &fixture_surface || s == &fixture_heading_surface);
    if (s == &fixture_surface) fixture_shown = on; else fixture_heading_shown = on;
}
static void mock_redraw(az_surface *s) { CHECK(s == &fixture_heading_surface); fixture_redraws++; }
static int mock_filter(const char *id, unsigned bus, az_frame_fn fn, void *user)
{
    CHECK(!strcmp(id, MOD_ID)); CHECK(bus == (1u << AZ_BUS_MIXER_IN));
    fixture_filters++; fixture_filter = fn; fixture_filter_user = user;
    return fixture_fail_filter ? -1 : 0;
}
static void box(int x, int y, int w, int h)
{
    CHECK(x >= fixture_paint_rect.x && y >= fixture_paint_rect.y);
    CHECK(x + w <= fixture_paint_rect.x + fixture_paint_rect.w && y + h <= fixture_paint_rect.y + fixture_paint_rect.h);
}
static void mock_fill(az_draw *d, int x, int y, int w, int h, uint32_t c)
{
    (void)d; fixture_fills++; fixture_fill_color = c; box(x, y, w, h);
}
static void mock_rect(az_draw *d, int x, int y, int w, int h, int stroke, uint32_t c)
{
    (void)d; (void)c; fixture_rects++; CHECK(stroke == 1); box(x, y, w, h);
}
static void mock_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c)
{
    (void)d; (void)c;
    fixture_texts++; CHECK(px == 16);
    box(x, y, (int)(strlen(s) * px * 0.65), (int)(px * 1.5));
    if (!strcmp(s, "MODS")) snprintf(fixture_label, sizeof fixture_label, "%s", s);
}
static uint32_t mock_color(int slot) { (void)slot; return 0xA866FF; }
static void mock_banner(const char *id, const char *s, int ms)
{
    CHECK(!strcmp(id, MOD_ID)); CHECK(!strcmp(s, "Tap MOD MENU in the source list"));
    CHECK(ms == 2200); fixture_banners++;
}
static az_core_v1 fixture_core = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_core_v1),
    .view = mock_view, .deck_count = mock_count, .ui_theme = mock_theme, .row_value = mock_row_value,
    .register_row = mock_register_row, .surface_create = mock_create,
    .surface_place = mock_place, .surface_show = mock_show, .surface_redraw = mock_redraw, .frame_filter = mock_filter,
    .fill_rect = mock_fill, .draw_rect = mock_rect, .draw_text = mock_text,
    .color = mock_color, .banner = mock_banner,
};
static void reset(void)
{
    gp_shortcut_ready = 0;
    gp_shortcut_request_word = gp_shortcut_completion = 0;
    gp_shortcut_pulse_token = gp_shortcut_until = gp_shortcut_notice_token = 0;
    gp_shortcut_notice = gp_shortcut_knob_known = 0;
    gp_shortcut_last_request = -10000;
    gp_button = NULL; gp_button_shown = gp_button_available = 0; gp_button_row = -1;
    gp_heading = NULL; gp_heading_row = -1; gp_heading_shown = 0;
    gp_heading_theme = -1;
    fixture_shown = fixture_banners = fixture_filters = 0;
    fixture_created = fixture_heading_shown = fixture_redraws = 0; fixture_heading_row = 1;
    fixture_theme = 6; fixture_fills = fixture_texts = fixture_rects = 0;
    fixture_view = AZ_VIEW_DECK; fixture_count = 4; fixture_row = 1;
    fixture_fail_surface = fixture_fail_filter = 0;
    fixture_filter = NULL; fixture_label[0] = 0;
}
static int input(int kind, int x, int y)
{
    az_event e = {.kind = kind, .x = x, .y = y};
    return fixture_desc.on_input(&e, fixture_desc.user);
}
int main(void)
{
    reset(); CHECK(init(NULL) == 1);
    az_core_v1 absent = fixture_core; absent.abi = -1; CHECK(init(&absent) == 1);
    absent = fixture_core; absent.draw_rect = NULL; CHECK(init(&absent) == 2);
    reset(); fixture_fail_surface = 1; CHECK(init(&fixture_core) == 3); CHECK(!fixture_filters);
    reset(); CHECK(init(&fixture_core) == 0); CHECK(fixture_filters == 1 && fixture_filter);
    CHECK(fixture_rect.x == 1080 && fixture_rect.w == 110); CHECK(!fixture_shown);
    tick(); CHECK(fixture_shown);
    struct az_draw draw = {0}; fixture_paint_rect = fixture_rect; fixture_desc.on_paint(&draw, fixture_rect, fixture_desc.user);
    CHECK(!strcmp(fixture_label, "MODS"));
    CHECK(input(AZ_EV_DOWN, 1100, 60) == 0 && !gp_shortcut_request_word);
    CHECK(input(AZ_EV_TAP, 1079, 60) == 1 && !gp_shortcut_request_word);
    CHECK(input(AZ_EV_TAP, 1190, 60) == 1 && !gp_shortcut_request_word);
    CHECK(input(AZ_EV_TAP, 1100, 49) == 1 && !gp_shortcut_request_word);
    CHECK(input(AZ_EV_TAP, 1100, 80) == 1 && !gp_shortcut_request_word);
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && gp_shortcut_request_word);
    long started = gp_shortcut_last_request;
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && gp_shortcut_last_request == started);
    CHECK(input(AZ_EV_UP, 1100, 60) == 0);
    unsigned char f[152] = {0}; f[6] = 8;
    CHECK(fixture_filter(AZ_BUS_MIXER_IN, f, sizeof f, fixture_filter_user) == AZ_FRAME_OK);
    CHECK(f[32] == 0x80 && f[6] == 0);
    fixture_view = AZ_VIEW_SOURCE; memset(f, 0, sizeof f); f[6] = 8;
    fixture_filter(AZ_BUS_MIXER_IN, f, sizeof f, fixture_filter_user);
    CHECK(f[32] == 0 && f[6] == 8 && !gp_shortcut_request_word);
    tick(); CHECK(!fixture_shown && fixture_banners == 1);
    tick(); CHECK(fixture_banners == 1);
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && !gp_shortcut_request_word);
    fixture_view = AZ_VIEW_DECK; fixture_count = 2; tick();
    CHECK(fixture_shown && fixture_rect.x == 900 && fixture_rect.w == 110);
    fixture_paint_rect = fixture_rect; fixture_desc.on_paint(&draw, fixture_rect, fixture_desc.user);
    fixture_row = 0; tick(); CHECK(!fixture_shown);
    CHECK(input(AZ_EV_TAP, 950, 60) == 1 && !gp_shortcut_request_word);
    fixture_row = 1; tick(); CHECK(fixture_shown);
    int views[] = {AZ_VIEW_SOURCE, AZ_VIEW_BROWSE, AZ_VIEW_SEARCH, AZ_VIEW_OTHER, AZ_VIEW_MENU};
    for (unsigned i = 0; i < sizeof views / sizeof *views; i++) {
        fixture_view = views[i]; tick(); CHECK(!fixture_shown);
    }
    /* The heading mask paints one blank rectangle and never takes a touch. */
    fixture_view = AZ_VIEW_DECK; tick();
    CHECK(fixture_created == 2 && fixture_heading_shown);
    CHECK(fixture_heading_desc.on_input == NULL && MOD.on_frame == NULL);
    fixture_paint_rect = fixture_heading_rect;
    int fills = fixture_fills, texts = fixture_texts, rects = fixture_rects;
    fixture_heading_desc.on_paint(&draw, fixture_heading_rect, fixture_heading_desc.user);
    CHECK(fixture_fills == fills + 1 && fixture_texts == texts && fixture_rects == rects);
    CHECK(fixture_fill_color == 0x000000u); /* SDK themes this stock black exactly once. */
    int redraws = fixture_redraws;
    fixture_theme = 1; gp_heading_update(); CHECK(fixture_redraws == redraws + 1);
    fixture_heading_desc.on_paint(&draw, fixture_heading_rect, fixture_heading_desc.user);
    CHECK(fixture_fill_color == 0x000000u); /* Day also starts from stock black. */
    fixture_theme = 0; CHECK(gp_heading_background() == 0x000000u);
    fixture_theme = 99; gp_heading_update();
    CHECK(gp_heading_background() == 0xffffffffu && !fixture_heading_shown && fixture_shown);
    fixture_theme = 6; gp_heading_update();
    fixture_heading_row = 0; gp_heading_update();
    CHECK(!fixture_heading_shown && fixture_shown);
    fixture_heading_row = 1; gp_heading_update(); CHECK(fixture_heading_shown);
    CHECK(fixture_redraws > 0);
    reset(); absent = fixture_core; absent.surface_redraw = NULL;
    CHECK(init(&absent) == 0); tick(); CHECK(fixture_shown && gp_heading == NULL);
    reset(); fixture_fail_surface = 2;
    CHECK(init(&fixture_core) == 0); tick(); CHECK(fixture_shown && gp_heading == NULL);
    reset(); absent = fixture_core; absent.frame_filter = NULL;
    CHECK(init(&absent) == 0); tick(); CHECK(!fixture_shown);
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && !gp_shortcut_request_word);
    reset(); fixture_fail_filter = 1;
    CHECK(init(&fixture_core) == 0); tick(); CHECK(!fixture_shown);
    printf("PASS: %d MODS component assertions\n", checks);
}
