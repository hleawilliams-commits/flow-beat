/* SPDX-License-Identifier: GPL-3.0-only
 * A small waveform-header shortcut to the native playable browser.
 * The custom suggestions/acapella page remains accessible through Mod Menu.
 */
#include "native-shortcut.h"
static az_surface *quick_surface;
static int quick_shown, quick_row = -1;

static void redraw(void)
{
    if (AZ_HAS(C, request_redraw)) C->request_redraw();
}

static void quick_paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->fill_rect(d, a.x, a.y, a.w, a.h, C->color(AZ_COL_BG));
    if (a.w < 70 || a.h < 32) return;
    C->draw_rect(d, a.x, a.y, a.w, a.h, 1, C->color(AZ_COL_HI));
    C->draw_text(d, a.x + 10, a.y + 6, QUICK_LABEL, 16, C->color(AZ_COL_HI));
}

static int quick_input(const az_event *ev, void *user)
{
    (void)user;
    if (!ev || ev->kind != AZ_EV_TAP) return 0;
    if (!native_filter_ok || C->view() != AZ_VIEW_DECK) return 1;
    if (quick_row >= 0 && C->row_value(quick_row) != 1) return 1;
    native_request_browse(now_ms());
    return 1;
}

static void quick_tick(void)
{
    if (!quick_surface) return;
    int want = native_filter_ok && (quick_row < 0 || C->row_value(quick_row) == 1);
    if (want != quick_shown) {
        C->surface_show(quick_surface, want);
        quick_shown = want;
    }
}

static void quick_init(void)
{
    if (!AZ_HAS(C, view) || !AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) ||
        !AZ_HAS(C, surface_show) || !AZ_HAS(C, fill_rect) || !AZ_HAS(C, draw_rect) ||
        !AZ_HAS(C, frame_filter)) return;
    if (C->frame_filter(MOD_ID, 1u << AZ_BUS_MIXER_IN, native_frame, NULL) != 0) return;
    native_filter_ok = 1;
    static const az_surface_v1 surface = {
        .size = sizeof(az_surface_v1), .layer = AZ_LAYER_OVER_EP147,
        .views = AZ_VIEW_BIT(AZ_VIEW_DECK), .on_paint = quick_paint, .on_input = quick_input,
    };
    quick_surface = C->surface_create(MOD_ID, &surface);
    if (!quick_surface) return;
    /* End at y=44, above Phase Strip's y=46; clear of native deck/view controls. */
    C->surface_place(quick_surface, (az_rect){QUICK_X, 8, 156, 36});
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        static const az_row_v1 row = {
            .size = sizeof(az_row_v1), .key = "quick-button", .label = "STEM PICKS browser shortcut",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "Set native Track Filter: BPM + KEY + MASTER DECK. Enable filter. PICKS opens the current browser; LOAD 1-4 loads your choice. It preserves filter on/off and category.",
        };
        quick_row = C->register_row(MOD_ID, &row);
    }
}
