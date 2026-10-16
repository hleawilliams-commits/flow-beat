/* SPDX-License-Identifier: GPL-3.0-only
 *
 * set-clock: how long is left in the set, and a warning when the playing track runs out.
 *
 *   SET 1:23:45    time left in the set. The timer starts when the first deck starts playing
 *                  (or again after "Restart set timer") and counts down from the set length
 *                  picked in the Mod Menu. Amber in the last 10 minutes, red once over.
 *   END 0:45       the last-started playing deck's time left, shown in the last minute of
 *                  its track, flashing red in the last 20 seconds.
 *
 * Everything comes from the core's deck snapshot (playing, pos_s, len_s) and the deck's
 * monotonic clock. UI thread only, repaints at most twice a second. No hooks, controls,
 * LEDs, audio, files or network. "firmware": ["any"].
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "set-clock"
#define NDECK 4
#define POLL_MS 250
#define WARN_TRACK_S 60
#define FLASH_TRACK_S 20
#define WARN_SET_S (10 * 60)

/* Top strip, right end of the waveform area (x 212..1050); the deck's own status bar with
 * its clock sits above y 40. Bottom is just above the deck panels. Tune on the deck. */
#define AREA_X1 1050
static const int CLOCK_Y[3] = { 46, 752, 46 };

static const az_core_v1 *C;
static az_surface *surf;
static int r_show = -1, r_len = -1, r_track = -1, r_pos = -1;
static const int SET_MIN[5] = { 60, 90, 120, 180, 240 };

static long set_start_ms = -1;      /* -1 = waiting for the first play */
static int was_playing[NDECK];
static long started_at[NDECK];      /* when each deck last started playing, 0 = not playing */
static long last_poll_ms = -POLL_MS;
static char g_text[96];
static uint32_t g_color;
static az_rect g_where;
static int g_shown;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void fmt_time(long s, char *buf, int n)
{
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(buf, (size_t)n, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(buf, (size_t)n, "%ld:%02ld", s / 60, s % 60);
}

static void restart(void *user)
{
    (void)user;
    set_start_ms = -1;
    C->log(MOD_ID, "set timer restarted: waiting for the first play");
}

/* The text's width, estimated: text_width belongs to the paint call (the SDK's harness checks
 * that), and the layout is decided on the UI thread before any paint. Wide enough for the
 * deck font's capitals and digits. */
static int est_width(const char *s, int px)
{
    return (int)strlen(s) * px * 64 / 100 + 2;
}

static void paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    if (!g_text[0]) return;
    C->fill_rect(d, a.x, a.y, a.w, a.h, 0x101010);
    C->draw_text(d, a.x + 6, a.y + 1, g_text, 18, g_color);
}

static int input(const az_event *ev, void *user)
{
    (void)ev; (void)user;
    return 0;
}

static void tick(void)
{
    long now = now_ms();
    if (now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;

    int show = r_show < 0 || C->row_value(r_show) == 1;
    int len_i = r_len >= 0 ? C->row_value(r_len) : 1;
    int track_warn = r_track < 0 || C->row_value(r_track) == 1;
    int pos = r_pos >= 0 ? C->row_value(r_pos) : 0;
    if (len_i < 0 || len_i > 4) len_i = 1;

    /* the lead deck: the playing deck that started last */
    int lead = -1, any_playing = 0;
    double lead_left = -1;
    for (int d = 0; d < NDECK; d++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        int playing = C->deck_info(d, &di) == 0 && di.loaded == 1 && di.playing == 1;
        if (playing && !was_playing[d]) started_at[d] = now;
        if (!playing) started_at[d] = 0;
        was_playing[d] = playing;
        if (!playing) continue;
        any_playing = 1;
        if (lead < 0 || started_at[d] > started_at[lead]) {
            lead = d;
            lead_left = (di.len_s > 0 && di.pos_s >= 0) ? di.len_s - di.pos_s : -1;
        }
    }
    if (any_playing && set_start_ms < 0) set_start_ms = now;

    char text[96] = "";
    uint32_t color = 0xFFFFFF;
    if (show) {
        char t[32];
        if (set_start_ms >= 0) {
            long left = SET_MIN[len_i] * 60L - (now - set_start_ms) / 1000;
            if (left >= 0) { fmt_time(left, t, sizeof t); snprintf(text, sizeof text, "SET %s", t); }
            else { fmt_time(-left, t, sizeof t); snprintf(text, sizeof text, "SET +%s", t); }
            color = left < 0 ? 0xFF4A3D : left < WARN_SET_S ? 0xF2C230 : 0xFFFFFF;
        } else {
            fmt_time(SET_MIN[len_i] * 60L, t, sizeof t);
            snprintf(text, sizeof text, "SET %s ready", t);
            color = 0xA0A0A0;
        }
        if (track_warn && lead >= 0 && lead_left >= 0 && lead_left <= WARN_TRACK_S) {
            fmt_time((long)(lead_left + 0.5), t, sizeof t);
            size_t l = strlen(text);
            snprintf(text + l, sizeof text - l, "   END %d %s", lead + 1, t);
            int flash = lead_left <= FLASH_TRACK_S && (now / 500) % 2 == 0;
            color = flash ? 0xFF4A3D : (color == 0xFFFFFF ? 0xF2C230 : color);
        }
    }

    int w = text[0] ? est_width(text, 18) + 12 : 0;
    az_rect r = { 0, 0, 0, 0 };
    if (text[0]) r = (az_rect){ pos == 2 ? 212 : AREA_X1 - w, CLOCK_Y[(pos >= 0 && pos < 3) ? pos : 0], w, 18 * 3 / 2 + 2 };
    int changed = strcmp(text, g_text) != 0 || color != g_color || memcmp(&r, &g_where, sizeof r) != 0;
    snprintf(g_text, sizeof g_text, "%s", text);
    g_color = color;
    if (!surf) return;
    if (text[0]) {
        if (memcmp(&r, &g_where, sizeof r) != 0) C->surface_place(surf, r);
        if (!g_shown) { C->surface_show(surf, 1); g_shown = 1; }
        if (changed) C->surface_redraw(surf);
    } else if (g_shown) {
        C->surface_show(surf, 0);
        g_shown = 0;
    }
    g_where = r;
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) ||
        !AZ_HAS(C, surface_show) || !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, draw_text) ||
        !AZ_HAS(C, fill_rect)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck or surface services (1.2.3 or newer)");
        return 2;
    }
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        static const char *const LEN[] = { "1h", "1.5h", "2h", "3h", "4h" };
        static const char *const POS[] = { "Right", "Low", "Left" };
        az_row_v1 row;
        memset(&row, 0, sizeof row);
        row.size = sizeof row;
        row.key = "show"; row.label = "Set clock"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Time left in the set, and a warning as the track ends.";
        r_show = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "length"; row.label = "Set length"; row.kind = AZ_ROW_CHOICE;
        row.choices = LEN; row.n_choices = 5; row.default_choice = 1;
        row.help = "Starts on the first play.";
        r_len = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "track"; row.label = "Track warning"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Last minute of the playing track, flashing at 20 s.";
        r_track = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "position"; row.label = "Position"; row.kind = AZ_ROW_CHOICE;
        row.choices = POS; row.n_choices = 3; row.default_choice = 0;
        row.help = "Top right, bottom, or top left.";
        r_pos = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "restart"; row.label = "Restart set timer"; row.kind = AZ_ROW_ACTION;
        row.help = "Starts again on the next play.";
        row.on_activate = restart;
        C->register_row(MOD_ID, &row);
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: defaults (on, 90 min, warning on)");
    }
    az_surface_v1 desc;
    memset(&desc, 0, sizeof desc);
    desc.size = sizeof desc;
    desc.layer = AZ_LAYER_OVER_EP147;
    desc.views = AZ_VIEW_BIT(AZ_VIEW_DECK);
    desc.on_paint = paint;
    desc.on_input = input;
    surf = C->surface_create(MOD_ID, &desc);
    if (!surf) { C->log(MOD_ID, "surface refused"); return 3; }
    return 0;
}

static const char *const FW_TAGS[] = { "any", NULL };
static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = tick,
    .on_frame = NULL,
};

AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
