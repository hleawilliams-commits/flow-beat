/* SPDX-License-Identifier: GPL-3.0-only
 * Custom Screen: up to four click-through panels on the deck screen, laid out by a text file
 * (layout.txt) that the Deck Screen Designer writes. The file is re-read when it changes, so a
 * new layout shows without reloading the mod. Deck readouts come from Grouped Panels' reader
 * (data.h), the selection and layout-tab logic from its layout.h, the mix rules from hud.h.
 * The wave and overview widgets (4-Deck Waveforms) read the tracks' rekordbox analysis on the
 * slow-work thread (jobs.h, waves.h).
 * FLOW Status Panel (0.2.0): FlowVJ's look, lights, LOCK / SAFE / HOLD from UDP 9102 and tap
 * buttons that send back to it (flowvj.h). The file worker owns the socket, so no extra thread.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "azmod-mod.h"
#include "layout.h"
#include "data.h"
#include "hud.h"
#include "layout_file.h"
#include "default_layout.h"
#include "jobs.h"
#include "waves.h"
#define MOD_ID "custom-screen"

static const az_core_v1 *C;
static az_surface *cs_surface[CS_PANELS];
static int cs_show_row = -1, cs_view_row = -1;

/* ---- the layout: the worker parses into cs_shared, the UI copies it into cs_ui ---------- */
static cs_layout cs_parsed, cs_shared, cs_ui;
static pthread_mutex_t cs_layout_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned cs_layout_serial, cs_layout_seen;   /* atomics */
static int cs_force_reload;                          /* atomic: Mod Menu "Reload layout" */
static int cs_from;                                  /* atomic: 0 built-in, 1 assets, 2 data */
static char cs_data_path[512], cs_assets_path[512];  /* set in init, read by the worker */
static char cs_file_buf[CS_FILE_MAX];

/* ---- deck readouts (UI thread) --------------------------------------------------------- */
typedef struct {
    int loaded, playing, has_stems, stems_on, bpm10, phrase_known, bar, beat, remain, alert, match, has_gap, gap10;
    char key[8], title[128], artist[128];
} cs_view;
static cs_view cs_v[4];
static long cs_started[4];
static int cs_selected[4];
static unsigned cs_select_bits, cs_serial, cs_seen_serial;
static long cs_last_frame, cs_last_poll = -1000;
static int cs_tab_seen, cs_tab_saved, cs_layout_now = -1, cs_minute = -1;
static int cs_visible[CS_PANELS];
static unsigned cs_wave_seen;
static long cs_wave_frame;
#define CS_WAVE_FPS 25

static int cs_has_wave(const cs_panel *p)
{
    for (int k = 0; p && k < p->n; k++) if (p->wd[k].type == CS_WAVE || p->wd[k].type == CS_OVERVIEW) return 1;
    return 0;
}
static az_rect cs_at[CS_PANELS];

#define CS_ACCENT 0x32D5FFu

/* ---- FlowVJ: the worker receives into fv_shared, the UI copies it into fv_ui ------------- */
static int fv_sock = -1;                             /* atomic: opened once in init, used by the worker */
static fv_state fv_shared, fv_ui;
static pthread_mutex_t fv_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned fv_serial, fv_seen;                  /* atomics */
static long fv_rx_ms;                                /* UI: when the tick last saw a new FVS1 */
static unsigned fv_pending;                          /* atomic: bit per FV_ACT_* tapped, the worker sends */
static int fv_live;                                  /* atomic: FlowVJ seen lately (the touch filter reads it) */
/* The vj_tap buttons on screen, double-buffered: the UI writes the idle copy and flips. */
static fv_tap fv_taps[2][FV_TAPS];
static int fv_tap_n[2], fv_tap_cur;                  /* fv_tap_cur atomic */
static long fv_flash_until[FV_ACTS];                 /* UI: a tapped button shows lit briefly */
static unsigned fv_flash_bits;                       /* atomic: taps the UI hasn't flashed yet */

static long cs_now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ---- the file worker: stat once a second, read and parse only on a change -------------- */
static int cs_read_file(const char *path, long long *mtime, long long *size)
{
    struct stat st;
    if (!path[0] || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    *mtime = (long long)st.st_mtime; *size = (long long)st.st_size;
    return 1;
}

static void cs_publish(const char *txt, size_t n, int from)
{
    cs_parse(txt, n, &cs_parsed);
    pthread_mutex_lock(&cs_layout_mu);
    cs_shared = cs_parsed;
    pthread_mutex_unlock(&cs_layout_mu);
    __atomic_store_n(&cs_from, from, __ATOMIC_RELEASE);
    __atomic_add_fetch(&cs_layout_serial, 1u, __ATOMIC_ACQ_REL);
}

/* ---- FlowVJ socket, worker thread only: wait up to wait_ms, take every datagram, send taps ---- */
static void fv_step(int wait_ms)
{
    static struct sockaddr_in from;   /* where the last FVS1 came from: taps go back there */
    static int have_from, tap_port;
    static unsigned seq;
    int sock = __atomic_load_n(&fv_sock, __ATOMIC_ACQUIRE);
    if (sock < 0) { gp_sleep_ms(wait_ms); return; }
    struct pollfd pf = { .fd = sock, .events = POLLIN };
    if (poll(&pf, 1, wait_ms) > 0 && (pf.revents & POLLIN)) {
        char buf[1024];
        for (int k = 0; k < 16; k++) {
            struct sockaddr_in a; socklen_t al = sizeof a;
            ssize_t n = recvfrom(sock, buf, sizeof buf, MSG_DONTWAIT, (struct sockaddr *)&a, &al);
            if (n <= 0) break;
            fv_state st;
            if (!fv_parse(buf, (size_t)n, &st)) continue;   /* flow-beat's own broadcast and the like */
            from = a; have_from = 1; tap_port = st.tap_port;
            pthread_mutex_lock(&fv_mu);
            fv_shared = st;
            pthread_mutex_unlock(&fv_mu);
            __atomic_add_fetch(&fv_serial, 1u, __ATOMIC_ACQ_REL);
        }
    }
    unsigned acts = __atomic_exchange_n(&fv_pending, 0u, __ATOMIC_ACQ_REL);
    if (!acts || !have_from) return;
    struct sockaddr_in to = from;
    to.sin_port = htons((uint16_t)tap_port);
    for (int act = 1; act < FV_ACTS; act++) {
        if (!(acts & (1u << act))) continue;
        char pkt[48];
        int n = fv_cmd(pkt, sizeof pkt, ++seq, act);
        if (n > 0) sendto(sock, pkt, (size_t)n, MSG_DONTWAIT, (const struct sockaddr *)&to, sizeof to);
        if (AZ_HAS(C, log)) C->log(MOD_ID, "FlowVJ tap: %s", FV_ACT_NAME[act]);
    }
}

static int fv_open(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(FV_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -1; }
    return s;
}

static void *cs_file_worker(void *arg)
{
    (void)arg;
    long long last_mtime = -1, last_size = -1;
    int last_from = -1;
    unsigned steps = 0;
    for (;;) {
        int force = __atomic_exchange_n(&cs_force_reload, 0, __ATOMIC_ACQ_REL);
        /* FlowVJ packets and taps are handled at least every 100 ms; the layout file is checked
         * every tenth step (about once a second, a little sooner while FlowVJ is sending) */
        if (!force && steps++ % 10) { fv_step(100); continue; }
        long long mt = 0, sz = 0;
        int from = 0;
        const char *path = NULL;
        if (cs_read_file(cs_data_path, &mt, &sz)) { from = 2; path = cs_data_path; }
        else if (cs_read_file(cs_assets_path, &mt, &sz)) { from = 1; path = cs_assets_path; }
        if (force || from != last_from || mt != last_mtime || sz != last_size) {
            size_t n = 0;
            if (path) {
                FILE *f = fopen(path, "rb");
                if (f) { n = fread(cs_file_buf, 1, sizeof cs_file_buf, f); fclose(f); }
                else from = 0;
            }
            if (from) cs_publish(cs_file_buf, n, from);
            else cs_publish(CS_DEFAULT_LAYOUT, strlen(CS_DEFAULT_LAYOUT), 0);
            last_from = from; last_mtime = mt; last_size = sz;
        }
        fv_step(100);
    }
    return NULL;
}

/* ---- touch and LED watchers (copied from Grouped Panels) ------------------------------- */
static int cs_touch(const az_touch *t, void *user)
{
    (void)user;
    if (!t || !t->down || C->view() != AZ_VIEW_DECK) return AZ_TOUCH_PASS;
    int tab = gp_layout_tab(t->x, t->y);
    if (tab) __atomic_store_n(&cs_tab_seen, tab, __ATOMIC_RELAXED);
    /* A vj_tap button takes its touch only while FlowVJ is answering; otherwise the deck gets it. */
    if (__atomic_load_n(&fv_live, __ATOMIC_ACQUIRE)) {
        int cur = __atomic_load_n(&fv_tap_cur, __ATOMIC_ACQUIRE);
        int act = fv_tap_hit(fv_taps[cur], fv_tap_n[cur], t->x, t->y);
        if (act) {
            __atomic_or_fetch(&fv_pending, 1u << act, __ATOMIC_ACQ_REL);
            __atomic_or_fetch(&fv_flash_bits, 1u << act, __ATOMIC_ACQ_REL);
            return AZ_TOUCH_TAKE;
        }
    }
    return AZ_TOUCH_PASS;
}

static int cs_led_frame(int bus, unsigned char *f, int len)
{
    if (bus != AZ_BUS_ERP_LED || !f || len < 120) return AZ_FRAME_OK;
    unsigned bits = (unsigned)gp_selection(f[0x0c]) | ((unsigned)gp_selection(f[0x44]) << 2);
    __atomic_store_n(&cs_select_bits, bits, __ATOMIC_RELEASE);
    __atomic_add_fetch(&cs_serial, 1u, __ATOMIC_RELEASE);
    return AZ_FRAME_OK;
}

static int cs_two_deck_tab(void)
{
    int pick = cs_view_row >= 0 ? C->row_value(cs_view_row) : 0;
    if (pick == 1) return GP_LAYOUT_2;
    if (pick == 2) return GP_LAYOUT_24;
    return __atomic_load_n(&cs_tab_seen, __ATOMIC_RELAXED);
}

/* ---- painting -------------------------------------------------------------------------- */
/* Surface text is Latin-1: convert UTF-8 in place (same rules as Grouped Panels). */
static void cs_disp(char *s)
{
    unsigned char *r = (unsigned char *)s, *w = r;
    while (*r) {
        unsigned c = *r, cp = 0; int n = 0;
        if (c < 0x80) { *w++ = *r++; continue; }
        if (c >= 0xc2 && c <= 0xdf) { n = 2; cp = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { n = 3; cp = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 4; cp = c & 0x07; }
        int ok = n > 0;
        for (int i = 1; ok && i < n; i++) { if ((r[i] & 0xc0) != 0x80) ok = 0; else cp = cp << 6 | (r[i] & 0x3f); }
        if (!ok) { *w++ = '?'; r++; continue; }
        r += n;
        if (cp <= 0xff) *w++ = (unsigned char)cp;
        else if (cp == 0x2018 || cp == 0x2019) *w++ = '\'';
        else if (cp == 0x201c || cp == 0x201d) *w++ = '"';
        else if (cp == 0x2013 || cp == 0x2014) *w++ = '-';
        else if (cp == 0x2026) { *w++ = '.'; *w++ = '.'; *w++ = '.'; }
        else *w++ = '?';
    }
    *w = 0;
}

static void cs_fit(char *s, int px, int width)
{
    size_t n = strlen(s);
    while (n && C->text_width(s, px) > width) { s[--n] = 0; }
}

typedef struct { int day; uint32_t text, muted, cell, accent, bg; } cs_theme;

static uint32_t cs_resolve(uint32_t c, uint32_t automatic, const cs_theme *t)
{
    switch (c) {
    case CS_COL_AUTO: return automatic;
    case CS_COL_DIM: return t->muted;
    case CS_COL_ACCENT: return t->accent;
    case CS_COL_CELL: return t->cell;
    case CS_COL_NONE: return t->bg;
    default: return c & 0xFFFFFFu;
    }
}

/* fill_rect clipped to the panel's area */
static void cs_fill(az_draw *d, az_rect a, int x, int y, int w, int h, uint32_t c)
{
    int x0 = x > a.x ? x : a.x, y0 = y > a.y ? y : a.y;
    int x1 = x + w < a.x + a.w ? x + w : a.x + a.w, y1 = y + h < a.y + a.h ? y + h : a.y + a.h;
    if (x1 > x0 && y1 > y0) C->fill_rect(d, x0, y0, x1 - x0, y1 - y0, c);
}

static void cs_text(az_draw *d, az_rect a, const cs_widget *w, int x, int y, int width, const char *s, uint32_t c)
{
    char buf[192];
    snprintf(buf, sizeof buf, "%s", s);
    cs_disp(buf);
    if (width < 1) return;
    cs_fit(buf, w->size, width);
    if (!buf[0]) return;
    int tw = C->text_width(buf, w->size);
    int tx = w->align == CS_ALIGN_C ? x + (width - tw) / 2 : w->align == CS_ALIGN_R ? x + width - tw : x;
    int ty = y + (w->h - w->size * 3 / 2) / 2;
    if (ty < a.y) ty = a.y;
    C->draw_text(d, tx, ty, buf, w->size, c);
}

static int cs_deck_of(int deck)
{
    if (deck == CS_DECK_L || deck == CS_DECK_R) {
        int side = deck == CS_DECK_R;
        for (int i = side; i < 4; i += 2) if (cs_selected[i]) return i;
        return -1;
    }
    return deck >= 0 && deck < 4 ? deck : -1;
}

static void cs_mmss(int s, char *out, size_t n) { snprintf(out, n, "-%d:%02d", s / 60, s % 60); }

static void cs_widget_paint(az_draw *d, az_rect a, const cs_widget *w, const cs_theme *t)
{
    int x = a.x + w->x, y = a.y + w->y;
    int i = cs_deck_of(w->deck);
    const cs_view *v = i >= 0 ? &cs_v[i] : NULL;
    int loaded = v && v->loaded;
    char s[192];
    switch (w->type) {
    case CS_DECK_NUM: {
        int sel = i >= 0 && cs_selected[i], al = loaded ? v->alert : 0;
        uint32_t block = al == GP_ALERT_RED ? 0xE5484D : al == GP_ALERT_AMBER ? 0xD49A21 : sel ? CS_ACCENT : cs_resolve(w->color, t->cell, t);
        cs_fill(d, a, x, y, w->w, w->h, block);
        cs_widget c = *w; c.align = CS_ALIGN_C;
        snprintf(s, sizeof s, "%c", i >= 0 ? '1' + i : '?');
        cs_text(d, a, &c, x, y, w->w, s, (sel || al) ? 0x07131A : t->text);
        break; }
    case CS_TITLE:
        snprintf(s, sizeof s, "%s%s", loaded ? w->text : "", loaded ? (v->title[0] ? v->title : "Track loaded") : "No track loaded");
        cs_text(d, a, w, x, y, w->w, s, loaded ? cs_resolve(w->color, t->text, t) : t->muted);
        break;
    case CS_ARTIST:
        if (loaded && v->artist[0]) { snprintf(s, sizeof s, "%s%s", w->text, v->artist); cs_text(d, a, w, x, y, w->w, s, cs_resolve(w->color, t->muted, t)); }
        break;
    case CS_KEY: {
        int known = loaded && v->key[0];
        uint32_t automatic = !known ? t->muted : v->match == GP_MATCH_GOOD ? (t->day ? 0x167A3A : 0x3CD070) :
            v->match == GP_MATCH_RISKY ? 0xD49A21 : v->match == GP_MATCH_CLASH ? 0xE5484D : t->text;
        snprintf(s, sizeof s, "%s%s", w->text, known ? v->key : "--");
        cs_text(d, a, w, x, y, w->w, s, known ? cs_resolve(w->color, automatic, t) : t->muted);
        break; }
    case CS_BPM:
        if (loaded && v->bpm10 > 0) snprintf(s, sizeof s, "%s%d.%d", w->text, v->bpm10 / 10, v->bpm10 % 10);
        else snprintf(s, sizeof s, "%s--.-", w->text);
        cs_text(d, a, w, x, y, w->w, s, loaded && v->bpm10 > 0 ? cs_resolve(w->color, t->text, t) : t->muted);
        break;
    case CS_BPM_GAP:
        if (loaded && v->has_gap) {
            if (v->gap10 == 0) snprintf(s, sizeof s, "%s\xC2\xB1" "0.0", w->text);
            else snprintf(s, sizeof s, "%s%c%d.%d", w->text, v->gap10 > 0 ? '+' : '-', abs(v->gap10) / 10, abs(v->gap10) % 10);
            cs_text(d, a, w, x, y, w->w, s, v->gap10 == 0 ? t->accent : cs_resolve(w->color, t->muted, t));
        }
        break;
    case CS_PHRASE: {
        int k = loaded && v->phrase_known;
        if (k) snprintf(s, sizeof s, "%s%d/8", w->text, v->bar);
        else snprintf(s, sizeof s, "%s%s", w->text, loaded ? "?" : "--");
        cs_text(d, a, w, x, y, w->w, s, k ? (v->bar == 8 ? 0xD49A21 : cs_resolve(w->color, t->accent, t)) : t->muted);
        break; }
    case CS_PHRASE_BAR: {
        int k = loaded && v->phrase_known, cw = (w->w - 21) / 8;
        uint32_t c = k && v->bar == 8 ? 0xD49A21 : cs_resolve(w->color, t->accent, t);
        if (cw < 1) break;
        for (int n = 0; n < 8; n++) {
            int cx = x + n * (w->w - cw) / 7;
            cs_fill(d, a, cx, y, cw, w->h, t->cell);
            if (!k) continue;
            int fw = n < v->bar - 1 ? cw : n == v->bar - 1 ? cw * v->beat / 4 : 0;
            if (fw > 0) cs_fill(d, a, cx, y, fw, w->h, c);
        }
        break; }
    case CS_BEAT: {
        int cw = (w->w - 9) / 4;
        if (cw < 1) break;
        for (int n = 0; n < 4; n++) {
            int on = loaded && v->playing && v->phrase_known && v->beat == n + 1;
            cs_fill(d, a, x + n * (w->w - cw) / 3, y, cw, w->h, on ? (n == 0 ? t->text : cs_resolve(w->color, t->accent, t)) : t->cell);
        }
        break; }
    case CS_TIME_LEFT: {
        int ok = loaded && v->remain >= 0;
        char tm[24] = "--:--";
        if (ok) cs_mmss(v->remain, tm, sizeof tm);
        snprintf(s, sizeof s, "%s%s", w->text, tm);
        uint32_t c = !ok ? t->muted : v->alert == GP_ALERT_RED ? 0xE5484D : v->alert == GP_ALERT_AMBER ? 0xD49A21 : cs_resolve(w->color, t->text, t);
        cs_text(d, a, w, x, y, w->w, s, c);
        break; }
    case CS_STEMS:
        /* Only while the stems engine is playing this deck: found-but-off draws nothing. */
        if (loaded && v->stems_on) {
            cs_fill(d, a, x, y, w->w, w->h, cs_resolve(w->color, 0x1E7A3A, t));
            cs_text(d, a, w, x + 6, y, w->w - 6, w->text[0] ? w->text : "STEMS", 0xFFFFFF);
        }
        break;
    case CS_STATE:
        if (loaded) { snprintf(s, sizeof s, "%s%s", w->text, v->playing ? "PLAY" : "PAUSE");
            cs_text(d, a, w, x, y, w->w, s, v->playing ? cs_resolve(w->color, 0x3CD070, t) : t->muted); }
        break;
    case CS_SIDE:
        if (i >= 0 && cs_selected[i]) cs_text(d, a, w, x, y, w->w, (i & 1) ? "RIGHT" : "LEFT", cs_resolve(w->color, t->accent, t));
        break;
    case CS_CLOCK: {
        time_t now = time(NULL); struct tm tmv;
        if (localtime_r(&now, &tmv)) { snprintf(s, sizeof s, "%s%02d:%02d", w->text, tmv.tm_hour, tmv.tm_min); cs_text(d, a, w, x, y, w->w, s, cs_resolve(w->color, t->text, t)); }
        break; }
    case CS_LABEL:
        cs_text(d, a, w, x, y, w->w, w->text, cs_resolve(w->color, t->text, t));
        break;
    case CS_BOX:
        cs_fill(d, a, x, y, w->w, w->h, cs_resolve(w->color, t->cell, t));
        break;
    case CS_FLOWVJ: {
        int live = __atomic_load_n(&fv_live, __ATOMIC_ACQUIRE);
        if (!live) { snprintf(s, sizeof s, "%sFLOW not seen", w->text); cs_text(d, a, w, x, y, w->w, s, t->muted); break; }
        /* badges from the right: BLACK, SAFE, HOLD, LOCK */
        static const char *const names[4] = { "BLACK", "SAFE", "HOLD", "LOCK" };
        const int on[4] = { fv_ui.black, fv_ui.safe, fv_ui.hold, fv_ui.lock };
        const uint32_t fill[4] = { 0xE5484D, 0xD49A21, t->text, t->accent };
        int bpx = w->size > 12 ? w->size - 4 : 8, right = x + w->w;
        for (int b = 0; b < 4; b++) {
            if (!on[b]) continue;
            int bw = C->text_width(names[b], bpx) + 10;
            if (right - bw < x + 40) break;
            right -= bw;
            cs_fill(d, a, right, y + 2, bw, w->h - 4, fill[b]);
            int ty = y + 2 + (w->h - 4 - bpx * 3 / 2) / 2;
            C->draw_text(d, right + 5, ty < a.y ? a.y : ty, names[b], bpx, 0x07131A);
            right -= 4;
        }
        const char *look = fv_ui.look[0] ? fv_ui.look : fv_ui.lights[0] ? fv_ui.lights : "FLOW";
        snprintf(s, sizeof s, "%s%s", w->text, look);
        cs_text(d, a, w, x, y, right - x, s, cs_resolve(w->color, t->accent, t));
        break; }
    case CS_VJ_LIGHTS: {
        int live = __atomic_load_n(&fv_live, __ATOMIC_ACQUIRE);
        if (live && fv_ui.black) snprintf(s, sizeof s, "%sBLACKOUT", w->text);
        else snprintf(s, sizeof s, "%s%s", w->text, live && fv_ui.lights[0] ? fv_ui.lights : "--");
        cs_text(d, a, w, x, y, w->w, s, live ? (fv_ui.black ? 0xE5484D : cs_resolve(w->color, t->text, t)) : t->muted);
        break; }
    case CS_VJ_TAP: {
        int live = __atomic_load_n(&fv_live, __ATOMIC_ACQUIRE), act = w->act;
        if (!live || !act) {   /* FlowVJ gone: a dim outline, and the touch goes to the deck */
            cs_fill(d, a, x, y, w->w, 1, t->cell); cs_fill(d, a, x, y + w->h - 1, w->w, 1, t->cell);
            cs_fill(d, a, x, y, 1, w->h, t->cell); cs_fill(d, a, x + w->w - 1, y, 1, w->h, t->cell);
            cs_widget c = *w; c.align = CS_ALIGN_C;
            cs_text(d, a, &c, x, y, w->w, w->text[0] ? w->text : act ? FV_ACT_LABEL[act] : "?", t->muted);
            break;
        }
        int lit = fv_act_on(&fv_ui, act) || fv_flash_until[act] > cs_now_ms();
        uint32_t on = act == FV_ACT_SAFE ? 0xD49A21 : act == FV_ACT_BLACK ? 0xE5484D : cs_resolve(w->color, t->accent, t);
        cs_fill(d, a, x, y, w->w, w->h, lit ? on : t->cell);
        cs_widget c = *w; c.align = CS_ALIGN_C;
        cs_text(d, a, &c, x, y, w->w, w->text[0] ? w->text : FV_ACT_LABEL[act], lit ? 0x07131A : t->text);
        break; }
    case CS_WAVE:
    case CS_OVERVIEW: {
        cw_style st = { t->text, t->muted, t->day ? 0xC4C8CF : 0x2A2633, t->bg, t->day };
        uint32_t mono = w->color == CS_COL_AUTO ? 0 : cs_resolve(w->color, 0, t);
        if (w->type == CS_WAVE) cw_paint_wave(d, a, x, y, w->w, w->h, i, w->zoom, w->grid, mono, &st);
        else cw_paint_overview(d, a, x, y, w->w, w->h, i, mono, &st);
        break; }
    }
}

/* UI thread: rebuild the touch filter's button list (screen pixels) from the visible panels. */
static void fv_publish_taps(void)
{
    int cur = __atomic_load_n(&fv_tap_cur, __ATOMIC_ACQUIRE), nxt = !cur, n = 0;
    for (int slot = 0; slot < cs_ui.n && slot < CS_PANELS; slot++) {
        if (!cs_visible[slot]) continue;
        const cs_panel *p = &cs_ui.p[slot];
        for (int k = 0; k < p->n && n < FV_TAPS; k++) {
            const cs_widget *w = &p->wd[k];
            if (w->type != CS_VJ_TAP || !w->act) continue;
            fv_taps[nxt][n++] = (fv_tap){ p->x + w->x, p->y + w->y, w->w, w->h, w->act };
        }
    }
    fv_tap_n[nxt] = n;
    __atomic_store_n(&fv_tap_cur, nxt, __ATOMIC_RELEASE);
}

/* The longest paint, logged every 10 s while waveforms are up, so an emulator or deck run
 * shows whether the column drawing fits the frame. Overlay thread only. */
static long cs_paint_max_us, cs_paint_log_ms, cs_paint_count;

static void cs_paint_timed(az_draw *d, az_rect a, void *user);
static void cs_paint(az_draw *d, az_rect a, void *user)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    cs_paint_timed(d, a, user);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_nsec - t0.tv_nsec) / 1000L;
    if (us > cs_paint_max_us) cs_paint_max_us = us;
    cs_paint_count++;
    long now = t1.tv_sec * 1000L + t1.tv_nsec / 1000000L;
    if (now - cs_paint_log_ms >= 10000) {
        if (cs_paint_log_ms && __atomic_load_n(&cw_wanted, __ATOMIC_ACQUIRE))
            C->log(MOD_ID, "paint: %ld panels in 10 s, longest %ld us", cs_paint_count, cs_paint_max_us);
        cs_paint_log_ms = now; cs_paint_max_us = 0; cs_paint_count = 0;
    }
}

static void cs_paint_timed(az_draw *d, az_rect a, void *user)
{
    int slot = (int)(intptr_t)user;
    if (slot < 0 || slot >= cs_ui.n) return;
    const cs_panel *p = &cs_ui.p[slot];
    cs_theme t;
    t.day = AZ_HAS(C, ui_theme) && C->ui_theme() == 1;
    t.text = C->color(AZ_COL_TEXT); t.bg = C->color(AZ_COL_BG);
    t.muted = t.day ? 0x505967 : 0xA5A2B0; t.cell = t.day ? 0xD5D9DF : 0x30303D; t.accent = t.day ? 0x006482 : CS_ACCENT;
    if (p->bg != CS_COL_AUTO) cs_fill(d, a, a.x, a.y, a.w, a.h, cs_resolve(p->bg, t.bg, &t));
    for (int k = 0; k < p->n; k++) cs_widget_paint(d, a, &p->wd[k], &t);
    if (p->border != CS_COL_NONE && p->bw > 0) {
        uint32_t c = cs_resolve(p->border, t.accent, &t);
        cs_fill(d, a, a.x, a.y, a.w, p->bw, c);
        if (!p->edge_top) {
            cs_fill(d, a, a.x, a.y + a.h - p->bw, a.w, p->bw, c);
            cs_fill(d, a, a.x, a.y, p->bw, a.h, c);
            cs_fill(d, a, a.x + a.w - p->bw, a.y, p->bw, a.h, c);
        }
    }
}

/* ---- tick ------------------------------------------------------------------------------ */
static unsigned cs_view_bit(int layout)
{
    return layout == GP_LAYOUT_4 ? CS_VIEW_4 : layout == GP_LAYOUT_24 ? CS_VIEW_24 : layout == GP_LAYOUT_2 ? CS_VIEW_2 : 0;
}

static void cs_reload(void *user)
{
    (void)user;
    __atomic_store_n(&cs_force_reload, 1, __ATOMIC_RELEASE);
}

static void cs_reset_phrase(void *user)
{
    int deck = (int)(intptr_t)user;
    gp_data_reset(deck);
    if (AZ_HAS(C, banner)) {
        char text[80]; snprintf(text, sizeof text, "Deck %d: phrase anchor requested", deck + 1);
        C->banner(MOD_ID, text, 1300);
    }
}

static void cs_take_layout(int *relayout)
{
    unsigned serial = __atomic_load_n(&cs_layout_serial, __ATOMIC_ACQUIRE);
    if (serial == cs_layout_seen || pthread_mutex_trylock(&cs_layout_mu) != 0) return;
    cs_ui = cs_shared;
    pthread_mutex_unlock(&cs_layout_mu);
    int first = cs_layout_seen == 0;
    cs_layout_seen = serial;
    *relayout = 1;
    for (int i = 0; i < CS_PANELS; i++) cs_at[i] = (az_rect){ -1, -1, 0, 0 };
    if (AZ_HAS(C, banner) && !first) {
        char text[112];
        int from = __atomic_load_n(&cs_from, __ATOMIC_ACQUIRE);
        if (cs_ui.errors) snprintf(text, sizeof text, "Custom Screen: %d panels, line %d not understood", cs_ui.n, cs_ui.first_error_line);
        else snprintf(text, sizeof text, "Custom Screen: %.40s (%d panels%s)", cs_ui.name[0] ? cs_ui.name : "layout", cs_ui.n, from ? "" : ", built-in");
        C->banner(MOD_ID, text, 1600);
    }
    C->log(MOD_ID, "layout %s: %d panels, %d errors (first on line %d)",
           cs_from == 2 ? "data/layout.txt" : cs_from == 1 ? "assets/layout.txt" : "built-in", cs_ui.n, cs_ui.errors, cs_ui.first_error_line);
}

static void cs_tick(void)
{
    long now = cs_now_ms();
    int relayout = 0;
    cs_take_layout(&relayout);
    int enabled = cs_show_row < 0 || C->row_value(cs_show_row) == 1;
    gp_data_enable(enabled);

    unsigned serial = __atomic_load_n(&cs_serial, __ATOMIC_ACQUIRE);
    if (serial != cs_seen_serial) { cs_seen_serial = serial; cs_last_frame = now; }
    unsigned bits = __atomic_load_n(&cs_select_bits, __ATOMIC_ACQUIRE);
    int fresh = gp_selection_fresh(serial, now, cs_last_frame);
    int tab = __atomic_load_n(&cs_tab_seen, __ATOMIC_RELAXED);
    if (tab != cs_tab_saved && AZ_HAS(C, cfg_set)) { cs_tab_saved = tab; C->cfg_set(MOD_ID, "layout", tab); }
    int layout = gp_layout_now(C->deck_count(), cs_two_deck_tab());
    if (layout != cs_layout_now) { cs_layout_now = layout; relayout = 1; }

    int dirty = relayout;
    unsigned fs = __atomic_load_n(&fv_serial, __ATOMIC_ACQUIRE);
    if (fs != fv_seen && pthread_mutex_trylock(&fv_mu) == 0) {
        fv_state ns = fv_shared;
        pthread_mutex_unlock(&fv_mu);
        fv_seen = fs;
        fv_rx_ms = now > 0 ? now : 1;
        ns.seq = 0;   /* a new sequence number alone is no reason to repaint */
        if (memcmp(&ns, &fv_ui, sizeof ns)) { fv_ui = ns; dirty = 1; }
    }
    int live = fv_rx_ms > 0 && now - fv_rx_ms < FV_STALE_MS;
    if (live != __atomic_load_n(&fv_live, __ATOMIC_RELAXED)) { __atomic_store_n(&fv_live, live, __ATOMIC_RELEASE); dirty = 1; }
    unsigned flash = __atomic_exchange_n(&fv_flash_bits, 0u, __ATOMIC_ACQ_REL);
    for (int act = 1; act < FV_ACTS; act++) {
        if (flash & (1u << act)) { fv_flash_until[act] = now + 300; dirty = 1; }
        else if (fv_flash_until[act] && fv_flash_until[act] <= now) { fv_flash_until[act] = 0; dirty = 1; }
    }
    for (int i = 0; i < 4; i++) {
        int s = fresh && gp_selected_deck(bits, i & 1) == i;
        if (s != cs_selected[i]) { cs_selected[i] = s; dirty = 1; }
    }
    if (now - cs_last_poll >= 125) {
        cs_last_poll = now;
        gp_readout r[4];
        for (int i = 0; i < 4; i++) {
            gp_data_read(i, &r[i]);
            if (!(r[i].loaded && r[i].playing)) cs_started[i] = 0;
            else if (!cs_started[i]) cs_started[i] = now > 0 ? now : 1;
        }
        for (int i = 0; i < 4; i++) {
            cs_view nv;
            memset(&nv, 0, sizeof nv);
            nv.remain = -1;
            if (r[i].loaded) {
                nv.loaded = 1; nv.playing = r[i].playing; nv.has_stems = r[i].has_stems; nv.stems_on = r[i].stems_on;
                nv.bpm10 = r[i].bpm > 0 && r[i].bpm < 1000 ? (int)(r[i].bpm * 10 + 0.5) : 0;
                nv.phrase_known = r[i].phrase_known; nv.bar = r[i].bar; nv.beat = r[i].beat;
                if (r[i].remaining_s >= 0) nv.remain = (int)(r[i].remaining_s + 0.5);
                nv.alert = gp_time_alert(r[i].playing, r[i].remaining_s);
                memcpy(nv.key, r[i].key, sizeof nv.key);
                memcpy(nv.title, r[i].title, sizeof nv.title);
                az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
                if (C->deck_info(i, &di) == 0 && di.loaded == 1) snprintf(nv.artist, sizeof nv.artist, "%s", di.artist);
                int ref = gp_reference_deck(i, cs_started);
                if (ref >= 0) {
                    double gap;
                    nv.match = gp_key_match(r[i].key, r[ref].key);
                    if (gp_bpm_gap(r[i].bpm, r[ref].bpm, &gap)) { nv.has_gap = 1; nv.gap10 = (int)(gap * 10 + (gap < 0 ? -0.5 : 0.5)); }
                }
            }
            if (memcmp(&nv, &cs_v[i], sizeof nv)) { cs_v[i] = nv; dirty = 1; }
        }
        time_t wall = time(NULL);
        int minute = (int)(wall / 60);
        if (minute != cs_minute) { cs_minute = minute; dirty = 1; }
    }

    unsigned bit = cs_view_bit(layout);
    int dirty_here[CS_PANELS];
    for (int slot = 0; slot < CS_PANELS; slot++) dirty_here[slot] = dirty;
    /* Waveforms: load analysis only while a shown panel has a wave or overview, and repaint
     * those panels while their deck plays (or when new analysis arrives). */
    unsigned wserial = __atomic_load_n(&cw_serial, __ATOMIC_ACQUIRE);
    int wave_new = wserial != cs_wave_seen, wanted = 0, moving[CS_PANELS] = { 0 }, nmoving = 0;
    cs_wave_seen = wserial;
    for (int slot = 0; slot < CS_PANELS; slot++) {
        const cs_panel *p = slot < cs_ui.n ? &cs_ui.p[slot] : NULL;
        if (!cs_surface[slot] || !enabled || !p || !(p->views & bit)) continue;
        for (int k = 0; k < p->n; k++)
            if (p->wd[k].type == CS_WAVE || p->wd[k].type == CS_OVERVIEW) {
                wanted = 1;
                if (!moving[slot] && cw_moving(cs_deck_of(p->wd[k].deck))) { moving[slot] = 1; nmoving++; }
            }
    }
    /* Scrolling panels share one frame budget, CS_WAVE_FPS repaints a second in all, and skip
     * the readout repaints in between (the next frame shows those too). */
    int frame = nmoving && now - cs_wave_frame >= 1000L * nmoving / CS_WAVE_FPS;
    if (frame) cs_wave_frame = now;
    int taps_moved = relayout;
    for (int slot = 0; slot < CS_PANELS; slot++) {
        if (!cs_surface[slot]) continue;
        const cs_panel *p = slot < cs_ui.n ? &cs_ui.p[slot] : NULL;
        int show = enabled && p && (p->views & bit);
        int changed = 0, waves = moving[slot] ? frame : wave_new && cs_has_wave(p);
        if (moving[slot] && !wave_new) dirty_here[slot] = 0;
        if (show) {
            az_rect r = { p->x, p->y, p->w, p->h };
            if (memcmp(&r, &cs_at[slot], sizeof r)) { cs_at[slot] = r; C->surface_place(cs_surface[slot], r); changed = 1; }
        }
        if (show != cs_visible[slot]) { cs_visible[slot] = show; C->surface_show(cs_surface[slot], show); changed = 1; taps_moved = 1; }
        if (show && (changed || dirty_here[slot] || waves)) C->surface_redraw(cs_surface[slot]);
    }
    cw_set_wanted(wanted);
    cw_log_flush();
    /* Only the deck screen: in browse or a menu no panel shows, so no button may take a touch. */
    int deck_view = !AZ_HAS(C, view) || C->view() == AZ_VIEW_DECK;
    static int was_deck_view = -1;
    if (deck_view != was_deck_view) { was_deck_view = deck_view; taps_moved = 1; }
    if (taps_moved) {
        if (deck_view) fv_publish_taps();
        else { int cur = __atomic_load_n(&fv_tap_cur, __ATOMIC_ACQUIRE); fv_tap_n[!cur] = 0; __atomic_store_n(&fv_tap_cur, !cur, __ATOMIC_RELEASE); }
    }
}

static int cs_init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_count) || !AZ_HAS(C, deck_info) || !AZ_HAS(C, deck_grid) ||
        !AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) ||
        !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, draw_text) || !AZ_HAS(C, fill_rect) ||
        !AZ_HAS(C, text_width) || !AZ_HAS(C, color) || !AZ_HAS(C, thread_start)) {
        C->log(MOD_ID, "this AZ-MODS lacks the surface or deck services (1.2.3 or newer)");
        return 2;
    }
    for (int i = 0; i < CS_PANELS; i++) {
        az_surface_v1 s = { .size = sizeof s, .layer = AZ_LAYER_OVER_EP147,
            .views = AZ_VIEW_BIT(AZ_VIEW_DECK), .on_paint = cs_paint, .user = (void *)(intptr_t)i };
        cs_surface[i] = C->surface_create(MOD_ID, &s);
        if (!cs_surface[i]) { C->log(MOD_ID, "surface %d refused (16 in all across mods)", i + 1); return 3; }
        cs_at[i] = (az_rect){ -1, -1, 0, 0 };
    }
    /* The folders on the USB, once: the worker only stats and reads these two files. */
    if (AZ_HAS(C, data_dir)) { const char *dd = C->data_dir(MOD_ID); if (dd) snprintf(cs_data_path, sizeof cs_data_path, "%s/layout.txt", dd); }
    if (AZ_HAS(C, assets_dir)) { const char *ad = C->assets_dir(MOD_ID); if (ad) snprintf(cs_assets_path, sizeof cs_assets_path, "%s/layout.txt", ad); }
    if (AZ_HAS(C, cfg_get)) cs_tab_seen = cs_tab_saved = C->cfg_get(MOD_ID, "layout", 0);
    if (cs_tab_seen != GP_LAYOUT_2 && cs_tab_seen != GP_LAYOUT_24 && cs_tab_seen != GP_LAYOUT_4) cs_tab_seen = cs_tab_saved = 0;
    if (AZ_HAS(C, touch_filter) && AZ_HAS(C, view)) C->touch_filter(MOD_ID, cs_touch, NULL);
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        az_row_v1 row = { .size = sizeof row, .key = "show", .label = "Custom Screen", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "Your own deck-screen panels from layout.txt (made in the Deck Screen Designer). Put a new layout in MODS/community-mods/custom-screen/data/layout.txt and it shows within a second." };
        cs_show_row = C->register_row(MOD_ID, &row);
        az_row_v1 reload = { .size = sizeof reload, .key = "reload", .label = "Reload layout", .kind = AZ_ROW_ACTION,
            .parent = "show", .on_activate = cs_reload,
            .help = "Reads layout.txt again now. It is also re-read by itself whenever the file changes." };
        C->register_row(MOD_ID, &reload);
        static const char *const views[] = { "Auto", "2 DECK", "2/4 DECK" };
        az_row_v1 view = { .size = sizeof view, .key = "two-deck", .label = "Two-deck view", .kind = AZ_ROW_CHOICE,
            .parent = "show", .choices = views, .n_choices = 3, .default_choice = 0,
            .help = "Auto follows the layout tab you last tapped. 4 DECK is always detected." };
        cs_view_row = C->register_row(MOD_ID, &view);
        static const char *const keys[] = { "reset1", "reset2", "reset3", "reset4" };
        static const char *const labels[] = { "Deck 1 phrase starts now", "Deck 2 phrase starts now", "Deck 3 phrase starts now", "Deck 4 phrase starts now" };
        for (int i = 0; i < 4; i++) {
            az_row_v1 action = { .size = sizeof action, .key = keys[i], .label = labels[i], .kind = AZ_ROW_ACTION,
                .parent = "show", .on_activate = cs_reset_phrase, .user = (void *)(intptr_t)i,
                .help = "Sets the eight-bar phrase counter's anchor. Press it on a downbeat." };
            C->register_row(MOD_ID, &action);
        }
    }
    int rc = gp_data_init(C, MOD_ID);
    if (rc) return 10 + rc;
    if (cw_init(C, MOD_ID) != 0) C->log(MOD_ID, "no library service: wave and overview widgets stay empty");
    if (cs_jobs_start(C, MOD_ID) != 0) C->log(MOD_ID, "no slow-work thread: wave and overview widgets stay empty");
    /* once per process: a second init (the mod switched off and on) keeps the first socket */
    if (__atomic_load_n(&fv_sock, __ATOMIC_ACQUIRE) < 0) __atomic_store_n(&fv_sock, fv_open(), __ATOMIC_RELEASE);
    if (__atomic_load_n(&fv_sock, __ATOMIC_ACQUIRE) < 0) C->log(MOD_ID, "UDP %d busy: FlowVJ widgets show FLOW not seen", FV_PORT);
    if (C->thread_start(MOD_ID, "screen layout", cs_file_worker, NULL) != 0) {
        /* No worker: the built-in layout, read now. */
        cs_publish(CS_DEFAULT_LAYOUT, strlen(CS_DEFAULT_LAYOUT), 0);
    }
    return 0;
}

static const char *const CS_TAGS[] = { "any", NULL };
static const az_mod_v1 CS_MOD = { .abi = AZ_MOD_ABI, .size = sizeof CS_MOD, .id = MOD_ID,
    .fw_tags = CS_TAGS, .init = cs_init, .tick = cs_tick, .on_frame = cs_led_frame };
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &CS_MOD; }
