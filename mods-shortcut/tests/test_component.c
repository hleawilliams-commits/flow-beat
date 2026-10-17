/* SPDX-License-Identifier: GPL-3.0-only
 * Full component test with an SDK fake that includes mixer frame_filter, missing
 * from the pinned upstream harness. Invokes the actual registered callbacks. */
#include <assert.h>
#include <stdio.h>
#include "../mods-shortcut.c"

struct az_surface { int token; };
struct az_draw { int token; };
static struct az_surface fixture_surface;
static az_surface_v1 fixture_desc;
static az_frame_fn fixture_filter;
static void *fixture_filter_user;
static az_rect fixture_rect;
static int fixture_shown, fixture_count = 4, fixture_view = AZ_VIEW_DECK;
static int fixture_row = 1, fixture_banners, fixture_filters, checks;
static int fixture_fail_surface, fixture_fail_filter;
static char fixture_label[32];
#define CHECK(x) do { assert(x); checks++; } while (0)
static int mock_view(void) { return fixture_view; }
static int mock_count(void) { return fixture_count; }
static int mock_row_value(int h) { CHECK(h == 7); return fixture_row; }
static int mock_register_row(const char *id, const az_row_v1 *r)
{
    CHECK(!strcmp(id, MOD_ID)); CHECK(!strcmp(r->key, "show"));
    CHECK(r->kind == AZ_ROW_TOGGLE && r->flags == AZ_ROW_DEFAULT_ON); return 7;
}
static az_surface *mock_create(const char *id, const az_surface_v1 *s)
{
    CHECK(!strcmp(id, MOD_ID)); CHECK(s->views == AZ_VIEW_BIT(AZ_VIEW_DECK));
    CHECK(s->layer == AZ_LAYER_OVER_EP147); fixture_desc = *s;
    return fixture_fail_surface ? NULL : &fixture_surface;
}
static void mock_place(az_surface *s, az_rect r)
{
    CHECK(s == &fixture_surface); CHECK(r.x >= 0 && r.x + r.w <= 1280);
    CHECK(r.y == 50 && r.h == 30); fixture_rect = r;
}
static void mock_show(az_surface *s, int on)
{
    CHECK(s == &fixture_surface); fixture_shown = on;
}
static int mock_filter(const char *id, unsigned bus, az_frame_fn fn, void *user)
{
    CHECK(!strcmp(id, MOD_ID)); CHECK(bus == (1u << AZ_BUS_MIXER_IN));
    fixture_filters++; fixture_filter = fn; fixture_filter_user = user;
    return fixture_fail_filter ? -1 : 0;
}
static void box(int x, int y, int w, int h)
{
    CHECK(x >= fixture_rect.x && y >= fixture_rect.y);
    CHECK(x + w <= fixture_rect.x + fixture_rect.w && y + h <= fixture_rect.y + fixture_rect.h);
}
static void mock_fill(az_draw *d, int x, int y, int w, int h, uint32_t c)
{
    (void)d; (void)c; box(x, y, w, h);
}
static void mock_rect(az_draw *d, int x, int y, int w, int h, int stroke, uint32_t c)
{
    (void)d; (void)c; CHECK(stroke == 1); box(x, y, w, h);
}
static void mock_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c)
{
    (void)d; (void)c; CHECK(px == 16); CHECK(!strcmp(s, "MODS"));
    box(x, y, 40, 20); snprintf(fixture_label, sizeof fixture_label, "%s", s);
}
static uint32_t mock_color(int slot) { (void)slot; return 0xA866FF; }
static void mock_banner(const char *id, const char *s, int ms)
{
    CHECK(!strcmp(id, MOD_ID)); CHECK(!strcmp(s, "Tap MOD MENU in the source list"));
    CHECK(ms == 2200); fixture_banners++;
}
static az_core_v1 fixture_core = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_core_v1),
    .view = mock_view, .deck_count = mock_count, .row_value = mock_row_value,
    .register_row = mock_register_row, .surface_create = mock_create,
    .surface_place = mock_place, .surface_show = mock_show, .frame_filter = mock_filter,
    .fill_rect = mock_fill, .draw_rect = mock_rect, .draw_text = mock_text,
    .color = mock_color, .banner = mock_banner,
};
static void reset(void)
{
    gp_shortcut_ready = gp_shortcut_pending = gp_shortcut_phase = gp_shortcut_sent = 0;
    gp_shortcut_notice = gp_shortcut_knob_known = 0;
    gp_shortcut_last_request = -10000;
    gp_button = NULL; gp_button_shown = gp_button_available = 0; gp_button_row = -1;
    fixture_shown = fixture_banners = fixture_filters = 0;
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
    struct az_draw draw = {0}; fixture_desc.on_paint(&draw, fixture_rect, fixture_desc.user);
    CHECK(!strcmp(fixture_label, "MODS"));
    CHECK(input(AZ_EV_DOWN, 1100, 60) == 0 && !gp_shortcut_pending);
    CHECK(input(AZ_EV_TAP, 1079, 60) == 1 && !gp_shortcut_pending);
    CHECK(input(AZ_EV_TAP, 1190, 60) == 1 && !gp_shortcut_pending);
    CHECK(input(AZ_EV_TAP, 1100, 49) == 1 && !gp_shortcut_pending);
    CHECK(input(AZ_EV_TAP, 1100, 80) == 1 && !gp_shortcut_pending);
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && gp_shortcut_pending);
    long started = gp_shortcut_requested_at;
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && gp_shortcut_requested_at == started);
    CHECK(input(AZ_EV_UP, 1100, 60) == 0);
    unsigned char f[152] = {0}; f[6] = 8;
    CHECK(fixture_filter(AZ_BUS_MIXER_IN, f, sizeof f, fixture_filter_user) == AZ_FRAME_OK);
    CHECK(f[32] == 0x80 && f[6] == 0);
    fixture_view = AZ_VIEW_SOURCE; memset(f, 0, sizeof f); f[6] = 8;
    fixture_filter(AZ_BUS_MIXER_IN, f, sizeof f, fixture_filter_user);
    CHECK(f[32] == 0 && f[6] == 8 && !gp_shortcut_pending);
    tick(); CHECK(!fixture_shown && fixture_banners == 1);
    tick(); CHECK(fixture_banners == 1);
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && !gp_shortcut_pending);
    fixture_view = AZ_VIEW_DECK; fixture_count = 2; tick();
    CHECK(fixture_shown && fixture_rect.x == 900 && fixture_rect.w == 110);
    fixture_desc.on_paint(&draw, fixture_rect, fixture_desc.user);
    fixture_row = 0; tick(); CHECK(!fixture_shown);
    CHECK(input(AZ_EV_TAP, 950, 60) == 1 && !gp_shortcut_pending);
    fixture_row = 1; tick(); CHECK(fixture_shown);
    int views[] = {AZ_VIEW_SOURCE, AZ_VIEW_BROWSE, AZ_VIEW_SEARCH, AZ_VIEW_OTHER, AZ_VIEW_MENU};
    for (unsigned i = 0; i < sizeof views / sizeof *views; i++) {
        fixture_view = views[i]; tick(); CHECK(!fixture_shown);
    }
    reset(); absent = fixture_core; absent.frame_filter = NULL;
    CHECK(init(&absent) == 0); tick(); CHECK(!fixture_shown);
    CHECK(input(AZ_EV_TAP, 1100, 60) == 1 && !gp_shortcut_pending);
    reset(); fixture_fail_filter = 1;
    CHECK(init(&fixture_core) == 0); tick(); CHECK(!fixture_shown);
    printf("PASS: %d MODS component assertions\n", checks);
}
