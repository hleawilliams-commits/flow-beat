/* SPDX-License-Identifier: GPL-3.0-only
 * Moment Cards: a wedding run-of-show on the deck. A minute ahead of each moment a big
 * card shows the moment, a countdown, the song (and whether a deck has it loaded) and the
 * MC line; a tap shrinks it to a small countdown badge; at the moment a "NOW" card shows
 * briefly. Reads MOMENTS.txt from the USB root. Read-only: no controls, audio or library.
 *
 * Threads: a reader thread checks the file every 5 s (disk work stays off the UI thread)
 * and publishes a parsed copy under a mutex the UI only trylocks.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "azmod-mod.h"
#include "moments.h"

#define MOD_ID "moment-cards"
static const az_core_v1 *C;

/* Shared: the parsed show and where it came from (reader writes, UI trylocks). */
static pthread_mutex_t mc_mu = PTHREAD_MUTEX_INITIALIZER;
static mc_show mc_pub;
static char mc_src[96];              /* "USB: MOMENTS.txt" or "" */
static unsigned mc_pub_seq;          /* __atomic */
static char mc_root[320], mc_data[320];   /* from init (data_dir touches the disk) */

/* UI thread. */
static mc_show mc_ui;
static unsigned mc_ui_seq;
static int mc_row_on = -1, mc_row_tz = -1, mc_row_lead = -1;
static az_surface *mc_card, *mc_badge;
static int mc_card_shown, mc_badge_shown, mc_last_sec = -1;
static mc_view mc_v = {MC_SHOW_NONE, -1, 0};
static int mc_dis_idx = -1, mc_dis_what = 0;
static long mc_test_until;           /* "Show test card": seconds (deck clock) */
static char mc_loaded[64];           /* "LOADED · Deck 2" or "" for the shown moment */

/* ---- reader thread ---- */
static void *mc_reader(void *arg)
{
    (void)arg;
    time_t seen_m = 0; off_t seen_s = -1; char seen_p[400] = "";
    for (;;) {
        char path[400] = ""; struct stat st;
        const char *cands[2] = {mc_root, mc_data};
        const char *names[2] = {"MOMENTS.txt", "moments.txt"};
        for (int i = 0; i < 2 && !path[0]; i++) {
            if (!cands[i][0]) continue;
            for (int k = 0; k < 2 && !path[0]; k++) {
                char p[400]; snprintf(p, sizeof p, "%s/%s", cands[i], names[k]);
                if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) snprintf(path, sizeof path, "%s", p);
            }
        }
        if (!path[0]) {
            if (seen_p[0]) {                              /* file gone: clear the show */
                pthread_mutex_lock(&mc_mu); memset(&mc_pub, 0, sizeof mc_pub); mc_src[0] = 0; pthread_mutex_unlock(&mc_mu);
                __atomic_add_fetch(&mc_pub_seq, 1u, __ATOMIC_RELEASE);
                seen_p[0] = 0;
            }
        } else if (strcmp(path, seen_p) || st.st_mtime != seen_m || st.st_size != seen_s) {
            static char buf[32768];
            FILE *f = fopen(path, "re");
            size_t n = 0;
            if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
            buf[n] = 0;
            static mc_show parsed;
            mc_parse(buf, &parsed);
            pthread_mutex_lock(&mc_mu);
            mc_pub = parsed;
            snprintf(mc_src, sizeof mc_src, "%s", strstr(path, "/MODS/") ? "the mod's data folder" : "USB: MOMENTS.txt");
            pthread_mutex_unlock(&mc_mu);
            __atomic_add_fetch(&mc_pub_seq, 1u, __ATOMIC_RELEASE);
            snprintf(seen_p, sizeof seen_p, "%s", path); seen_m = st.st_mtime; seen_s = st.st_size;
        }
        struct timespec ts = {5, 0};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- helpers (UI thread) ---- */
static int mc_on(void) { return mc_row_on < 0 || C->row_value(mc_row_on) == 1; }
static int mc_tz(void) { return mc_row_tz >= 0 ? C->row_value(mc_row_tz) : MC_TZ_MOUNTAIN; }
static int mc_lead(void)
{
    static const int s[] = {60, 120, 300};
    int i = mc_row_lead >= 0 ? C->row_value(mc_row_lead) : 0;
    return s[i >= 0 && i < 3 ? i : 0];
}
static void mc_fmt_time(char *out, size_t n, int min)
{
    int h = min / 60, m = min % 60;
    snprintf(out, n, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
}
static void mc_fmt_count(char *out, size_t n, int secs)
{
    if (secs < 0) secs = 0;
    if (secs >= 3600) snprintf(out, n, "%d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60);
    else snprintf(out, n, "%d:%02d", secs / 60, secs % 60);
}
static void mc_check_loaded(const mc_moment *m)
{
    mc_loaded[0] = 0;
    if (!m->song[0] || !AZ_HAS(C, deck_info)) return;
    for (int d = 0; d < 4; d++) {
        az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
        if (C->deck_info(d, &di) != 0 || di.loaded != 1) continue;
        di.title[sizeof di.title - 1] = di.artist[sizeof di.artist - 1] = 0;
        if (mc_song_matches(m->song, di.title, di.artist)) { snprintf(mc_loaded, sizeof mc_loaded, "LOADED on Deck %d", d + 1); return; }
    }
}
/* Fit text by shrinking, then by cutting whole UTF-8 characters. Returns the size used. */
static int mc_fit(char *s, int px, int minpx, int width)
{
    mc_disp(s);
    while (px > minpx && AZ_HAS(C, text_width) && C->text_width(s, px) > width) px -= 4;
    size_t n = strlen(s);
    while (n && AZ_HAS(C, text_width) && C->text_width(s, px) > width) {
        do { --n; } while (n && ((unsigned char)s[n] & 0xc0) == 0x80);
        s[n] = 0;
    }
    return px;
}
/* Draw up to `lines` wrapped lines of `text` at px; returns the next y. */
static int mc_wrap(az_draw *d, int x, int y, int width, const char *text, int px, int lines, uint32_t rgb)
{
    char rest[256]; snprintf(rest, sizeof rest, "%s", text);
    char *p = rest;
    for (int l = 0; l < lines && *p; l++) {
        char line[256]; size_t best = 0, len = strlen(p);
        for (size_t i = 1; i <= len; i++) {
            if (i < len && p[i] != ' ') continue;
            char t[256]; memcpy(t, p, i); t[i] = 0;
            if (!AZ_HAS(C, text_width) || C->text_width(t, px) <= width) best = i; else break;
        }
        if (!best) best = len;
        memcpy(line, p, best); line[best] = 0;
        if (l == lines - 1 && best < len) mc_fit(line, px, px, width);
        C->draw_text(d, x, y, line, px, rgb);
        y += px * 3 / 2;
        p += best; while (*p == ' ') p++;
    }
    return y;
}

/* ---- painting ---- */
#define MC_BG 0x0B0912u
#define MC_PINK 0xF0389Cu
#define MC_CYAN 0x32D5FFu
#define MC_GREEN 0x3CD070u
#define MC_AMBER 0xD49A21u
static void mc_paint_card(az_draw *d, az_rect a, void *user)
{
    (void)user;
    if (mc_v.idx < 0 || mc_v.idx >= mc_ui.n) return;
    const mc_moment *m = &mc_ui.m[mc_v.idx];
    int now = mc_v.what == MC_SHOW_NOW;
    uint32_t text = 0xF3EEFC, dim = 0xA39CB6, accent = now ? MC_PINK : MC_CYAN;
    C->fill_rect(d, a.x, a.y, a.w, a.h, MC_BG);
    C->fill_rect(d, a.x, a.y, a.w, 10, accent);
    int x = a.x + 70, w = a.w - 140, y = a.y + 60;
    char t[96], line[256];
    mc_fmt_time(t, sizeof t, m->min);
    snprintf(line, sizeof line, "%s  -  %s", now ? "NOW" : "NEXT", t);
    C->draw_text(d, x, y, line, 28, accent); y += 60;
    char name[64]; snprintf(name, sizeof name, "%s", m->name);
    for (char *c = name; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    int px = mc_fit(name, 96, 56, w);
    C->draw_text(d, x, y, name, px, text); y += px * 3 / 2 + 6;
    if (!now) {
        char cd[32]; mc_fmt_count(cd, sizeof cd, mc_v.secs_to);
        snprintf(line, sizeof line, "in %s", cd);
        C->draw_text(d, x, y, line, 56, accent); y += 100;
    } else y += 20;
    if (m->song[0]) {
        snprintf(line, sizeof line, "%s", m->song); int spx = mc_fit(line, 36, 24, w);
        C->draw_text(d, x, y, line, spx, text); y += 58;
        C->draw_text(d, x, y, mc_loaded[0] ? mc_loaded : "NOT LOADED", 28, mc_loaded[0] ? MC_GREEN : MC_AMBER); y += 62;
    }
    if (m->line[0]) {
        char q[230]; snprintf(q, sizeof q, "%s", m->line); mc_disp(q);
        if (!strchr(q, '"')) { char t2[232]; snprintf(t2, sizeof t2, "\"%s\"", q); snprintf(q, sizeof q, "%.229s", t2); }
        y = mc_wrap(d, x, y, w, q, 34, 3, text) + 10;
    }
    if (mc_v.idx + 1 < mc_ui.n) {
        const mc_moment *nx = &mc_ui.m[mc_v.idx + 1];
        mc_fmt_time(t, sizeof t, nx->min);
        snprintf(line, sizeof line, "Then: %s at %s", nx->name, t);
        int npx = mc_fit(line, 24, 16, w - 220);
        C->draw_text(d, x, a.y + a.h - 70, line, npx, dim);
    }
    C->draw_text(d, a.x + a.w - 250, a.y + a.h - 70, "tap to hide", 24, dim);
}
static void mc_paint_badge(az_draw *d, az_rect a, void *user)
{
    (void)user;
    if (mc_v.idx < 0 || mc_v.idx >= mc_ui.n) return;
    const mc_moment *m = &mc_ui.m[mc_v.idx];
    C->fill_rect(d, a.x, a.y, a.w, a.h, MC_BG);
    C->fill_rect(d, a.x, a.y, 6, a.h, mc_v.what == MC_SHOW_NOW ? MC_PINK : MC_CYAN);
    char cd[32], line[120];
    mc_fmt_count(cd, sizeof cd, mc_v.secs_to);
    snprintf(line, sizeof line, "%s in %s", m->name, cd);
    int px = mc_fit(line, 20, 14, a.w - 20);
    C->draw_text(d, a.x + 14, a.y + 2, line, px, 0xF3EEFC);
}
static int mc_card_input(const az_event *ev, void *user)
{
    (void)user;
    if (!ev || ev->kind != AZ_EV_TAP) return 1;
    mc_dis_idx = mc_v.idx; mc_dis_what = mc_v.what;    /* hide: shrink to the badge, or away */
    mc_test_until = 0;
    return 1;
}
static int mc_badge_input(const az_event *ev, void *user)
{
    (void)user;
    if (ev && ev->kind == AZ_EV_TAP) { mc_dis_idx = -1; mc_dis_what = 0; mc_last_sec = -1; }  /* expand */
    return 1;
}
static void mc_test(void *user)
{
    (void)user;
    mc_test_until = (long)time(NULL) + 15;
    mc_dis_idx = -1; mc_dis_what = 0;
}

static void mc_show_surface(az_surface *s, int *shown, int want)
{
    if (s && want != *shown) { *shown = want; C->surface_show(s, want); }
}
static void mc_tick(void)
{
    unsigned seq = __atomic_load_n(&mc_pub_seq, __ATOMIC_ACQUIRE);
    if (seq != mc_ui_seq && pthread_mutex_trylock(&mc_mu) == 0) {
        mc_ui = mc_pub; pthread_mutex_unlock(&mc_mu); mc_ui_seq = seq;
        mc_dis_idx = -1; mc_dis_what = 0; mc_last_sec = -1;
    }
    long t = (long)time(NULL);
    int now_s = mc_local_secs(mc_tz(), t);
    mc_view v = {MC_SHOW_NONE, -1, 0};
    if (mc_on() && mc_ui.n) {
        if (t < mc_test_until) {                          /* test: the next moment, as a card */
            mc_view nx = mc_pick(&mc_ui, now_s, 86400, -1, 0);
            v.what = MC_SHOW_CARD; v.idx = nx.idx >= 0 ? nx.idx : 0;
            v.secs_to = nx.idx >= 0 ? nx.secs_to : 0;
        } else v = mc_pick(&mc_ui, now_s, mc_lead(), mc_dis_idx, mc_dis_what);
    }
    int changed = v.what != mc_v.what || v.idx != mc_v.idx;
    mc_v = v;
    int card = v.what == MC_SHOW_CARD || v.what == MC_SHOW_NOW, badge = v.what == MC_SHOW_BADGE;
    int view = AZ_HAS(C, view) ? C->view() : AZ_VIEW_DECK;
    if (view == AZ_VIEW_MENU) card = badge = 0;         /* never over the Mod Menu */
    if (v.idx >= 0 && (changed || now_s != mc_last_sec)) mc_check_loaded(&mc_ui.m[v.idx]);
    mc_show_surface(mc_card, &mc_card_shown, card);
    mc_show_surface(mc_badge, &mc_badge_shown, badge);
    if ((card || badge) && (changed || now_s != mc_last_sec)) {
        if (card) C->surface_redraw(mc_card);
        if (badge) C->surface_redraw(mc_badge);
    }
    mc_last_sec = now_s;
}

/* ---- Mod Menu page ---- */
static void mc_draw_page(az_draw *d, az_rect a, void *user)
{
    (void)user;
    uint32_t text = C->color(AZ_COL_TEXT), dim = C->color(AZ_COL_TEXT_DIM), hi = C->color(AZ_COL_HI);
    C->draw_header(d, "Moment Cards");
    int x = a.x + (a.w >= 600 ? 30 : 8), w = a.w - (x - a.x) - 8;
    int y = a.y + (AZ_HAS(C, row_height) ? C->row_height(d) : 48) + 12;
    char line[200], src[96] = "", t[24];
    if (pthread_mutex_trylock(&mc_mu) == 0) { snprintf(src, sizeof src, "%s", mc_src); pthread_mutex_unlock(&mc_mu); }
    int now_s = mc_local_secs(mc_tz(), (long)time(NULL));
    static const char *const zn[] = {"Mountain", "Arizona", "Pacific", "Central", "Eastern", "UTC"};
    int tz = mc_tz();
    mc_fmt_time(t, sizeof t, now_s / 60);
    snprintf(line, sizeof line, "Deck clock: %s (%s). Wrong? Fix the time zone row, or the deck's clock.", t, zn[tz >= 0 && tz < 6 ? tz : 0]);
    if (y + 33 <= a.y + a.h) { int px = mc_fit(line, 22, 14, w); C->draw_text(d, x, y, line, px, text); y += 38; }
    if (!src[0]) {
        const char *help[] = {"No run-of-show yet. Put MOMENTS.txt at the top of the USB, one line per moment:",
                              "19:15 | First Dance | Perfect - Ed Sheeran | Couple to the floor, circle up",
                              "Time | moment | song (optional) | MC line (optional). Times like 7:15pm work too."};
        for (int i = 0; i < 3 && y + 30 <= a.y + a.h; i++) {
            snprintf(line, sizeof line, "%s", help[i]); int px = mc_fit(line, 20, 12, w);
            C->draw_text(d, x, y, line, px, i == 1 ? hi : dim); y += 32;
        }
        return;
    }
    snprintf(line, sizeof line, "%d moments from %s%s", mc_ui.n, src, mc_ui.bad_line ? " (some lines skipped: check the format)" : "");
    if (y + 30 <= a.y + a.h) { int px = mc_fit(line, 20, 12, w); C->draw_text(d, x, y, line, px, dim); y += 36; }
    for (int i = 0; i < mc_ui.n && y + 30 <= a.y + a.h; i++) {
        const mc_moment *m = &mc_ui.m[i];
        mc_fmt_time(t, sizeof t, m->min);
        snprintf(line, sizeof line, "%-9s %s%s%s", t, m->name, m->song[0] ? "  -  " : "", m->song);
        int px = mc_fit(line, 22, 12, w);
        C->draw_text(d, x, y, line, px, i == mc_v.idx ? hi : text); y += 32;
    }
}
static int mc_page_input(const az_event *ev, void *user) { (void)user; return ev && ev->kind == AZ_EV_BACK ? 0 : 1; }

static int mc_init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    if (__atomic_load_n(&C, __ATOMIC_ACQUIRE) != core) __atomic_store_n(&C, core, __ATOMIC_RELEASE);
    if (!AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) ||
        !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, draw_text) || !AZ_HAS(C, fill_rect) || !AZ_HAS(C, thread_start) ||
        !AZ_HAS(C, text_width) || !AZ_HAS(C, data_dir)) return 2;
    static int started;
    if (!started) {
        /* The USB root is the data folder's ancestor: <root>/MODS/community-mods/<id>/data. */
        const char *dd = C->data_dir(MOD_ID);
        if (dd) {
            snprintf(mc_data, sizeof mc_data, "%s", dd);
            const char *mods = strstr(dd, "/MODS/");
            if (mods) snprintf(mc_root, sizeof mc_root, "%.*s", (int)(mods - dd), dd);
        }
    }
    const az_surface_v1 card = {.size = sizeof card, .layer = AZ_LAYER_OVER_EP147,
        .views = AZ_VIEW_BIT(AZ_VIEW_DECK) | AZ_VIEW_BIT(AZ_VIEW_BROWSE) | AZ_VIEW_BIT(AZ_VIEW_SOURCE) | AZ_VIEW_BIT(AZ_VIEW_SEARCH),
        .on_paint = mc_paint_card, .on_input = mc_card_input};
    const az_surface_v1 badge = {.size = sizeof badge, .layer = AZ_LAYER_OVER_EP147,
        .views = AZ_VIEW_BIT(AZ_VIEW_DECK) | AZ_VIEW_BIT(AZ_VIEW_BROWSE),
        .on_paint = mc_paint_badge, .on_input = mc_badge_input};
    /* Fresh surfaces every init: a Reload drops the previous instance's. */
    mc_card = C->surface_create(MOD_ID, &card);
    if (mc_card) C->surface_place(mc_card, (az_rect){0, 0, 1280, 800});
    mc_badge = C->surface_create(MOD_ID, &badge);
    if (mc_badge) C->surface_place(mc_badge, (az_rect){470, 50, 340, 30});
    if (!mc_card) return 3;
    C->surface_show(mc_card, 0); mc_card_shown = 0;
    if (mc_badge) { C->surface_show(mc_badge, 0); mc_badge_shown = 0; }
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        const az_row_v1 on = {.size = sizeof on, .key = "on", .label = "Moment cards", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "Shows the next moment from MOMENTS.txt on the USB: a full card a minute ahead (tap to shrink it to a countdown), then a short NOW card."};
        mc_row_on = C->register_row(MOD_ID, &on);
        static const char *const zones[] = {"Mountain", "Arizona", "Pacific", "Central", "Eastern", "UTC"};
        const az_row_v1 tz = {.size = sizeof tz, .key = "tz", .label = "Time zone", .kind = AZ_ROW_CHOICE,
            .parent = "on", .choices = zones, .n_choices = 6, .default_choice = 0,
            .help = "The venue's time zone, with US daylight saving. Check the deck clock on this mod's page."};
        mc_row_tz = C->register_row(MOD_ID, &tz);
        static const char *const leads[] = {"1 minute", "2 minutes", "5 minutes"};
        const az_row_v1 lead = {.size = sizeof lead, .key = "lead", .label = "Card appears", .kind = AZ_ROW_CHOICE,
            .parent = "on", .choices = leads, .n_choices = 3, .default_choice = 0,
            .help = "How long before each moment the card appears. The countdown badge stays until the moment."};
        mc_row_lead = C->register_row(MOD_ID, &lead);
        const az_row_v1 test = {.size = sizeof test, .key = "test", .label = "Show test card", .kind = AZ_ROW_ACTION,
            .parent = "on", .on_activate = mc_test,
            .help = "Shows the next moment's card for 15 seconds, to check it before the event."};
        C->register_row(MOD_ID, &test);
    }
    if (AZ_HAS(C, register_screen)) {
        static const az_screen_v1 scr = {.size = sizeof scr, .on_draw = mc_draw_page, .on_input = mc_page_input};
        C->register_screen(MOD_ID, &scr);
    }
    if (!started) {
        if (C->thread_start(MOD_ID, "moments", mc_reader, NULL) != 0) return 4;
        started = 1;
    }
    return 0;
}
static const char *const TAGS[] = {"any", NULL};
static const az_mod_v1 MOD = {.abi = AZ_MOD_ABI, .size = sizeof MOD, .id = MOD_ID, .fw_tags = TAGS,
                              .init = mc_init, .tick = mc_tick};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
