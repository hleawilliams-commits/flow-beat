/* SPDX-License-Identifier: GPL-3.0-only
 * A two-step path to the real Mod Menu, using documented SDK input/surfaces.
 * SOURCE opens first; the DJ then selects the official MOD MENU source row.
 */
#include <string.h>
#include "azmod-mod.h"
#include "shortcut.h"
#define MOD_ID "mods-shortcut"

static const az_core_v1 *C;
static az_surface *gp_button;
static az_rect gp_button_where;
static int gp_button_shown, gp_button_available, gp_button_row = -1;
static int gp_button_dim = -1; /* last painted state: 1 = every input lane off */

/* Optional input-transparent mask for the stock WAVEFORM title only. */
static az_surface *gp_heading;
static int gp_heading_row = -1, gp_heading_shown;
static int gp_heading_theme = -1;

static uint32_t gp_heading_background(void)
{
    /* SDK fill_rect already applies azm_theme(): pass stock black, not an
     * already-themed ground color. The latter gets tinted twice. Pinned keys
     * 0..5 are stock/official themes; 6 is this workspace's Flow Neon extension. */
    int theme = AZ_HAS(C, ui_theme) ? C->ui_theme() : -1;
    return theme >= 0 && theme <= 6 ? 0x000000u : 0xffffffffu;
}
static void gp_heading_paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    uint32_t color = gp_heading_background();
    if (color != 0xffffffffu) C->fill_rect(d, a.x, a.y, a.w, a.h, color);
}
static void gp_heading_update(void)
{
    if (!gp_heading) return;
    uint32_t color = gp_heading_background();
    int want = color != 0xffffffffu && C->view() == AZ_VIEW_DECK &&
               (gp_heading_row < 0 || C->row_value(gp_heading_row) == 1);
    if (want != gp_heading_shown) {
        gp_heading_shown = want;
        C->surface_show(gp_heading, want);
    }
    int theme = AZ_HAS(C, ui_theme) ? C->ui_theme() : -1;
    if (theme != gp_heading_theme) {
        gp_heading_theme = theme;
        if (want) C->surface_redraw(gp_heading);
    }
}
static void gp_heading_init(void)
{
    if (!AZ_HAS(C, surface_redraw)) return;
    const az_surface_v1 s = {
        .size = sizeof s, .layer = AZ_LAYER_OVER_EP147, .views = AZ_VIEW_BIT(AZ_VIEW_DECK),
        .on_paint = gp_heading_paint, /* no on_input: touches pass through */
    };
    gp_heading = C->surface_create(MOD_ID, &s);
    if (!gp_heading) return; /* No spare surface: preserve the working MODS button. */
    C->surface_place(gp_heading, (az_rect){0, 0, 174, 46});
    C->surface_show(gp_heading, 0);
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        const az_row_v1 row = {
            .size = sizeof row, .key = "hide-heading", .label = "Hide WAVEFORM heading",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "Leave the WAVEFORM title area blank. The mask passes touches through and leaves tabs and waveforms clear. Off restores the stock heading.",
        };
        gp_heading_row = C->register_row(MOD_ID, &row);
    }
}

static az_rect gp_button_rect(void)
{
    return C->deck_count() == 4 ? (az_rect){1080, 50, 110, 30}
                              : (az_rect){900, 50, 110, 30};
}
static int gp_button_enabled(void)
{
    return gp_button_available && (gp_button_row < 0 || C->row_value(gp_button_row) == 1);
}
static void gp_button_paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->fill_rect(d, a.x, a.y, a.w, a.h, C->color(AZ_COL_BG));
    if (a.w < 70 || a.h < 30) return;
    /* Dim = the core switched every input lane off: taps explain instead of failing silently. */
    uint32_t ink = C->color(gp_shortcut_running() ? AZ_COL_HI : AZ_COL_TEXT_DIM);
    C->draw_rect(d, a.x, a.y, a.w, a.h, 1, ink);
    C->draw_text(d, a.x + 28, a.y + 3, "MODS", 16, ink);
}
static int gp_button_input(const az_event *ev, void *user)
{
    (void)user;
    if (!ev || ev->kind != AZ_EV_TAP) return 0;
    if (!gp_button_enabled() || C->view() != AZ_VIEW_DECK) return 1;
    az_rect r = gp_button_where;
    if (ev->x < r.x || ev->x >= r.x + r.w || ev->y < r.y || ev->y >= r.y + r.h) return 1;
    if (!gp_shortcut_running()) {
        GP_SHORTCUT_LOG("MODS tap: refused, every input lane is off");
        if (AZ_HAS(C, banner)) C->banner(MOD_ID, "MODS is off until restart. Use SOURCE > MOD MENU", 3000);
        return 1;
    }
    if (!gp_shortcut_request(gp_shortcut_clock_ms()))
        GP_SHORTCUT_LOG("MODS tap: ignored (request already pending or within 500 ms)");
    return 1;
}
static void tick(void)
{
    if (!gp_button) return;
    az_rect r = gp_button_rect();
    if (memcmp(&r, &gp_button_where, sizeof r)) {
        gp_button_where = r;
        C->surface_place(gp_button, r);
    }
    int want = gp_button_enabled() && C->view() == AZ_VIEW_DECK;
    if (want != gp_button_shown) {
        gp_button_shown = want;
        C->surface_show(gp_button, want);
    }
    gp_shortcut_tick();
    int dim = !gp_shortcut_running();
    if (dim != gp_button_dim) {
        int changed = gp_button_dim >= 0; /* the first paint already uses the current state */
        gp_button_dim = dim;
        if (changed && gp_button_shown && AZ_HAS(C, surface_redraw)) C->surface_redraw(gp_button);
    }
    gp_heading_update();
}
static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, view) || !AZ_HAS(C, deck_count) || !AZ_HAS(C, surface_create) ||
        !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) || !AZ_HAS(C, fill_rect) ||
        !AZ_HAS(C, draw_rect) || !AZ_HAS(C, draw_text) || !AZ_HAS(C, color)) return 2;
    const az_surface_v1 s = {
        .size = sizeof s, .layer = AZ_LAYER_OVER_EP147, .views = AZ_VIEW_BIT(AZ_VIEW_DECK),
        .on_paint = gp_button_paint, .on_input = gp_button_input,
    };
    gp_button = C->surface_create(MOD_ID, &s);
    if (!gp_button) return 3;
    gp_button_where = gp_button_rect();
    C->surface_place(gp_button, gp_button_where);
    gp_button_available = gp_shortcut_init(C, MOD_ID);
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        const az_row_v1 row = {
            .size = sizeof row, .key = "show", .label = "MODS source shortcut",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "MODS opens SOURCE. Tap the official MOD MENU entry to open your mods. A dim MODS button means the deck switched its input off until restart: use SOURCE > MOD MENU. No track loading or playback changes.",
        };
        gp_button_row = C->register_row(MOD_ID, &row);
    }
    /* Missing/refused services keep the button hidden. A later filter quarantine
     * is not exposed by the SDK; request timeout supplies the manual fallback. */
    C->surface_show(gp_button, 0);
    gp_heading_init();
    return 0;
}
static const char *const TAGS[] = {"any", NULL};
static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI, .size = sizeof MOD, .id = MOD_ID, .fw_tags = TAGS,
    .init = init, .tick = tick,
};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
