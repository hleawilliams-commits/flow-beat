/* SPDX-License-Identifier: GPL-3.0-only
 * SDK-only shortcut: a title-area button opens this mod's page in its own surface.
 * No simulated native input, firmware hooks, cross-mod calls or playback changes.
 * Include after draw/input/on_enter. All state is owned by the UI thread.
 */
static az_surface *quick_surface;
static int quick_open, quick_shown, quick_row = -1;
static az_rect quick_close;

static void redraw(void)
{
    if (quick_open && quick_surface) C->surface_redraw(quick_surface);
    else if (AZ_HAS(C, request_redraw)) C->request_redraw();
}

static void quick_close_page(void)
{
    if (!quick_open) return;
    quick_open = 0;
    g_page_on = 0;
    C->surface_focus(quick_surface, 0);
    C->surface_place(quick_surface, (az_rect){QUICK_X, 16, 156, 40});
    C->surface_redraw(quick_surface);
}

static void quick_paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->fill_rect(d, a.x, a.y, a.w, a.h, C->color(AZ_COL_BG));
    if (!quick_open) {
        if (a.w >= 70 && a.h >= 32) {
            C->draw_rect(d, a.x, a.y, a.w, a.h, 1, C->color(AZ_COL_HI));
            C->draw_text(d, a.x + 10, a.y + 7, QUICK_LABEL, 16, C->color(AZ_COL_HI));
        }
        return;
    }
    draw(d, a, NULL);
    quick_close = (az_rect){a.x + a.w - 96, a.y + 4, 88, 36};
    if (a.w >= 200 && a.h >= 40) {
        C->fill_rect(d, quick_close.x, quick_close.y, quick_close.w, quick_close.h, C->color(AZ_COL_BG));
        C->draw_rect(d, quick_close.x, quick_close.y, quick_close.w, quick_close.h, 1, C->color(AZ_COL_TEXT_DIM));
        C->draw_text(d, quick_close.x + 12, quick_close.y + 4, "Close", 18, C->color(AZ_COL_TEXT));
    } else quick_close = (az_rect){0, 0, 0, 0};
}

static int quick_input(const az_event *ev, void *user)
{
    (void)user;
    if (!ev) return 0;
    if (!quick_open) {
        if (ev->kind != AZ_EV_TAP) return 0;
        if (!C->surface_focus(quick_surface, 1)) return 1;
        quick_open = 1;
        C->surface_place(quick_surface, (az_rect){24, 76, 1232, 616});
        on_enter(NULL);
        redraw();
        return 1;
    }
    if (ev->kind == AZ_EV_BACK || (ev->kind == AZ_EV_TAP &&
        ev->x >= quick_close.x && ev->x < quick_close.x + quick_close.w &&
        ev->y >= quick_close.y && ev->y < quick_close.y + quick_close.h)) {
        quick_close_page();
        return 1;
    }
    input(ev, NULL);
    return 1; /* Never pass a dashboard tap through to the native screen beneath it. */
}

static void quick_tick(void)
{
    if (!quick_surface) return;
    int want = quick_row < 0 || C->row_value(quick_row) == 1;
    if (quick_open && (!want || C->view() != AZ_VIEW_DECK)) quick_close_page();
    if (want != quick_shown) {
        C->surface_show(quick_surface, want);
        quick_shown = want;
    }
}

static void quick_init(void)
{
    if (!AZ_HAS(C, view) || !AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) ||
        !AZ_HAS(C, surface_show) || !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, surface_focus) ||
        !AZ_HAS(C, fill_rect) || !AZ_HAS(C, draw_rect)) return;
    static const az_surface_v1 surface = {
        .size = sizeof(az_surface_v1), .layer = AZ_LAYER_OVER_EP147,
        .views = AZ_VIEW_BIT(AZ_VIEW_DECK), .on_paint = quick_paint, .on_input = quick_input,
    };
    quick_surface = C->surface_create(MOD_ID, &surface);
    if (!quick_surface) return; /* The registered Mod Menu page remains available. */
    C->surface_place(quick_surface, (az_rect){QUICK_X, 16, 156, 40});
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        static const az_row_v1 row = {
            .size = sizeof(az_row_v1), .key = "quick-button", .label = QUICK_LABEL " waveform button",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "A shortcut in the WAVEFORM title area. Close or BACK returns to the waveforms.",
        };
        quick_row = C->register_row(MOD_ID, &row);
    }
}
