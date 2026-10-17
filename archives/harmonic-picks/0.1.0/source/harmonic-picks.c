/* SPDX-License-Identifier: GPL-3.0-only
 *
 * harmonic-picks: a page in the Mod Menu with the tracks on your sticks that fit what is
 * playing now.
 *
 * Mod Menu > Community Mods > Installed > Harmonic Picks. The page takes the deck that started
 * playing last (or deck 1 when nothing plays) and lists up to 40 tracks from the sticks'
 * rekordbox libraries with a Camelot-compatible key (same, a neighbour, the relative key)
 * and an original tempo within 6% (half and double time count), closest first. Acapellas
 * (by title) are marked VOX. Turn the browse knob to scroll; press it to switch between all
 * tracks and acapellas only. The list follows the decks while the page is open.
 *
 * The libraries are read once on the mod's own thread, which also does the ranking; the UI
 * thread only hands over the playing track and draws the result. No hooks, controls, LEDs,
 * audio, files or network. "firmware": ["any"].
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "harmonic-picks"
#define NDECK 4
#define MAX_TRACKS 20000
#define NPICKS 40
#define POLL_MS 500
#define READER_SLEEP_MS 100
#define MAX_GAP 6.0

static const az_core_v1 *C;

/* ---- key notation ----------------------------------------------------------------------
 * rekordbox exports the key in whatever notation its settings chose: classic ("Am", "F#m",
 * "Db"), Camelot ("8A") or Open Key ("1m"). Everything is mapped onto the Camelot wheel and
 * written back out in the notation the DJ picked. A string that fits no notation is shown
 * as it is. */
struct keyname { const char *camelot; const char *minor; const char *major; const char *minor_alt; const char *major_alt; };
static const struct keyname WHEEL[12] = {
    /* n   nA minor   nB major   alt spellings */
    { "1",  "Abm", "B",  "G#m", NULL  },
    { "2",  "Ebm", "F#", "D#m", "Gb"  },
    { "3",  "Bbm", "Db", "A#m", "C#"  },
    { "4",  "Fm",  "Ab", NULL,  "G#"  },
    { "5",  "Cm",  "Eb", NULL,  "D#"  },
    { "6",  "Gm",  "Bb", NULL,  "A#"  },
    { "7",  "Dm",  "F",  NULL,  NULL  },
    { "8",  "Am",  "C",  NULL,  NULL  },
    { "9",  "Em",  "G",  NULL,  NULL  },
    { "10", "Bm",  "D",  NULL,  NULL  },
    { "11", "F#m", "A",  "Gbm", NULL  },
    { "12", "Dbm", "E",  "C#m", NULL  },
};

/* Parse into wheel index 0..11 and minor (1) / major (0). 0 = not understood. */
static int parse_key(const char *s, int *idx, int *minor)
{
    char b[16];
    size_t n = 0;
    for (const char *p = s; *p && n < sizeof b - 1; p++)
        if (*p != ' ') b[n++] = *p;
    b[n] = 0;
    if (n == 0) return 0;
    /* Camelot "8A" / "12B", Open Key "1m" / "12d" */
    if (b[0] >= '0' && b[0] <= '9') {
        int num = 0; size_t i = 0;
        while (b[i] >= '0' && b[i] <= '9') num = num * 10 + (b[i++] - '0');
        if (num < 1 || num > 12 || b[i] == 0 || b[i + 1] != 0) return 0;
        char c = b[i];
        if (c == 'A' || c == 'a') { *idx = num - 1; *minor = 1; return 1; }
        if (c == 'B' || c == 'b') { *idx = num - 1; *minor = 0; return 1; }
        if (c == 'm' || c == 'd') {                 /* Open Key: 1 = Camelot 8 */
            *idx = (num + 6) % 12; *minor = (c == 'm'); return 1;
        }
        return 0;
    }
    /* classic: root, accidental, "m" / "min" / "maj" / nothing */
    char root[4] = { 0 }; size_t i = 0;
    if (!((b[0] >= 'A' && b[0] <= 'G') || (b[0] >= 'a' && b[0] <= 'g'))) return 0;
    root[0] = (char)((b[0] >= 'a') ? b[0] - 32 : b[0]); i = 1;
    if (b[i] == '#' || b[i] == 'b') root[1] = b[i++];
    int is_minor = 0;
    if (b[i] == 0) is_minor = 0;
    else if (strcmp(b + i, "m") == 0 || strcmp(b + i, "min") == 0 || strcmp(b + i, "minor") == 0) is_minor = 1;
    else if (strcmp(b + i, "maj") == 0 || strcmp(b + i, "major") == 0 || strcmp(b + i, "M") == 0) is_minor = 0;
    else return 0;
    char want[8];
    snprintf(want, sizeof want, "%s%s", root, is_minor ? "m" : "");
    for (int k = 0; k < 12; k++) {
        const struct keyname *w = &WHEEL[k];
        if (is_minor) {
            if (strcmp(want, w->minor) == 0 || (w->minor_alt && strcmp(want, w->minor_alt) == 0)) { *idx = k; *minor = 1; return 1; }
        } else {
            if (strcmp(want, w->major) == 0 || (w->major_alt && strcmp(want, w->major_alt) == 0)) { *idx = k; *minor = 0; return 1; }
        }
    }
    return 0;
}


/* ---- the library table (reader thread writes it once, then only the reader reads it) ---- */
struct tr { uint32_t id; signed char idx, minor, vox, stick; float bpm; char title[56]; char artist[36]; };
static struct tr g_lib[MAX_TRACKS];
static int g_nlib;

static int has_word(const char *s, const char *w)
{
    size_t n = strlen(w);
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] && (s[i] | 32) == w[i]) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int on_track(const az_track *t, void *user)
{
    int stick = *(int *)user;
    if (g_nlib >= MAX_TRACKS) return 1;
    int idx, minor;
    if (!t || !t->key || !parse_key(t->key, &idx, &minor) || t->bpm <= 0) return 0;
    struct tr *r = &g_lib[g_nlib++];
    r->id = t->id;
    r->stick = (signed char)stick;
    r->idx = (signed char)idx;
    r->minor = (signed char)minor;
    r->bpm = (float)t->bpm;
    snprintf(r->title, sizeof r->title, "%s", t->title ? t->title : "");
    snprintf(r->artist, sizeof r->artist, "%s", t->artist ? t->artist : "");
    r->vox = has_word(r->title, "acap") || has_word(r->title, "a cap") || has_word(r->title, "vocal");
    return 0;
}

/* ---- seed and result: UI thread <-> reader thread, under g_mu ---------------------------- */
struct pick { uint32_t id; signed char idx, minor, vox, step; float bpm, gap; char title[56]; char artist[36]; };
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_seed_id;          /* the playing track; 0 = none */
static int g_seed_idx = -1, g_seed_minor, g_seed_vox_only, g_seed_gen;
static float g_seed_bpm;
static struct pick g_res[NPICKS];
static int g_nres, g_res_gen = -1, g_lib_ready;
static int g_new_result;            /* atomic: reader -> UI */

static float gap_pct(float a, float b)
{
    float best = 1e9f;
    static const float F[3] = { 0.5f, 1.0f, 2.0f };
    for (int i = 0; i < 3; i++) {
        float d = b * F[i] - a;
        if (d < 0) d = -d;
        d = d / a * 100.0f;
        if (d < best) best = d;
    }
    return best;
}

/* 0 same, 1 neighbour or relative, -1 no fit */
static int key_step(int ia, int ma, int ib, int mb)
{
    int dist = (ia - ib + 12) % 12;
    if (dist > 6) dist = 12 - dist;
    if (ma == mb) return dist == 0 ? 0 : dist == 1 ? 1 : -1;
    return dist == 0 ? 1 : -1;
}

static void rank(uint32_t seed_id, int sidx, int smin, float sbpm, int vox_only, struct pick *out, int *nout)
{
    int n = 0;
    for (int i = 0; i < g_nlib; i++) {
        const struct tr *t = &g_lib[i];
        if (t->id == seed_id || (vox_only && !t->vox)) continue;
        int step = key_step(sidx, smin, t->idx, t->minor);
        if (step < 0) continue;
        float g = gap_pct(sbpm, t->bpm);
        if (g > MAX_GAP) continue;
        float score = g + step * 2.0f;
        /* insertion into the sorted top NPICKS; the same title and artist only once */
        int dup = 0;
        for (int j = 0; j < n; j++)
            if (strcmp(out[j].title, t->title) == 0 && strcmp(out[j].artist, t->artist) == 0) { dup = 1; break; }
        if (dup) continue;
        int pos = n;
        while (pos > 0 && out[pos - 1].gap + out[pos - 1].step * 2.0f > score) pos--;
        if (pos >= NPICKS) continue;
        if (n < NPICKS) n++;
        memmove(&out[pos + 1], &out[pos], sizeof out[0] * (size_t)(n - 1 - pos));
        struct pick *p = &out[pos];
        p->id = t->id; p->idx = t->idx; p->minor = t->minor; p->vox = t->vox;
        p->step = (signed char)step; p->bpm = t->bpm; p->gap = g;
        memcpy(p->title, t->title, sizeof p->title);
        memcpy(p->artist, t->artist, sizeof p->artist);
    }
    *nout = n;
}

static struct pick g_scratch[NPICKS];

static void *reader(void *arg)
{
    (void)arg;
    for (int stick = 0; stick < 2 && g_nlib < MAX_TRACKS; stick++) {
        int s = stick;
        C->lib_tracks(MOD_ID, stick, 0, on_track, &s);
    }
    pthread_mutex_lock(&g_mu);
    g_lib_ready = g_nlib;
    pthread_mutex_unlock(&g_mu);
    __atomic_store_n(&g_new_result, 1, __ATOMIC_RELEASE);
    int done_gen = -1;
    for (;;) {
        pthread_mutex_lock(&g_mu);
        int gen = g_seed_gen, sidx = g_seed_idx, smin = g_seed_minor, vox = g_seed_vox_only;
        uint32_t sid = g_seed_id;
        float sbpm = g_seed_bpm;
        pthread_mutex_unlock(&g_mu);
        if (gen != done_gen) {
            int n = 0;
            if (sidx >= 0 && sbpm > 0) rank(sid, sidx, smin, sbpm, vox, g_scratch, &n);
            pthread_mutex_lock(&g_mu);
            memcpy(g_res, g_scratch, sizeof g_res[0] * (size_t)n);
            g_nres = n;
            g_res_gen = gen;
            pthread_mutex_unlock(&g_mu);
            done_gen = gen;
            __atomic_store_n(&g_new_result, 1, __ATOMIC_RELEASE);
        }
        struct timespec ts = { 0, READER_SLEEP_MS * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- the page (UI thread) ---------------------------------------------------------------- */
static int g_page_on, g_scroll, g_vox_only, g_reader_ok;
static int g_seed_deck = -1;
static uint32_t g_ui_seed_id;
static long last_poll_ms = -POLL_MS, g_page_seen_ms = -100000;
static int started_seq[NDECK], was_playing[NDECK], seq_counter;
/* what on_draw shows: a copy taken on the UI thread */
static struct pick g_show[NPICKS];
static int g_nshow, g_show_lib;
static char g_seed_text[96];

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void set_seed(void)
{
    /* the deck that started playing last, else the first loaded deck */
    int best = -1;
    az_deck_info pick_di;
    for (int d = 0; d < NDECK; d++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        int ok = C->deck_info(d, &di) == 0 && di.loaded == 1;
        int playing = ok && di.playing == 1;
        if (playing && !was_playing[d]) started_seq[d] = ++seq_counter;
        if (!playing) started_seq[d] = 0;
        was_playing[d] = playing;
        if (!ok) continue;
        int better = best < 0 || started_seq[d] > started_seq[best];
        if (better) { best = d; pick_di = di; }
    }
    uint32_t id = best >= 0 ? pick_di.content_id : 0;
    if (best == g_seed_deck && id == g_ui_seed_id) return;
    g_seed_deck = best;
    g_ui_seed_id = id;
    g_scroll = 0;
    /* the seed's key and original tempo: look the id up in the table under the lock */
    int idx = -1, minor = 0;
    float bpm = 0;
    char title[56] = "";
    pthread_mutex_lock(&g_mu);
    int ready = g_lib_ready;
    pthread_mutex_unlock(&g_mu);
    if (id && ready) {
        for (int i = 0; i < ready; i++)
            if (g_lib[i].id == id) { idx = g_lib[i].idx; minor = g_lib[i].minor; bpm = g_lib[i].bpm; memcpy(title, g_lib[i].title, sizeof title); break; }
    }
    if (idx < 0 && best >= 0 && pick_di.key[0] && parse_key(pick_di.key, &idx, &minor)) bpm = (float)pick_di.bpm;
    if (best < 0) snprintf(g_seed_text, sizeof g_seed_text, "Load a track to see picks");
    else if (idx < 0) snprintf(g_seed_text, sizeof g_seed_text, "Deck %d: no key for this track", best + 1);
    else snprintf(g_seed_text, sizeof g_seed_text, "Deck %d  %s%s  %.1f BPM  %.40s", best + 1,
                  WHEEL[idx].camelot, minor ? "A" : "B", bpm, title);
    pthread_mutex_lock(&g_mu);
    g_seed_id = id; g_seed_idx = idx; g_seed_minor = minor; g_seed_bpm = bpm;
    g_seed_vox_only = g_vox_only;
    g_seed_gen++;
    pthread_mutex_unlock(&g_mu);
}

static void take_result(void)
{
    pthread_mutex_lock(&g_mu);
    memcpy(g_show, g_res, sizeof g_show[0] * (size_t)g_nres);
    g_nshow = g_nres;
    g_show_lib = g_lib_ready;
    pthread_mutex_unlock(&g_mu);
}

static void on_enter(void *user)
{
    (void)user;
    g_page_on = 1;
    g_page_seen_ms = now_ms();
    g_ui_seed_id = (uint32_t)-1;     /* force a fresh seed */
    g_seed_deck = -2;
    set_seed();
    if (AZ_HAS(C, request_redraw)) C->request_redraw();
}

/* draw text only if it fits inside the page area, cutting it short to the area width */
static int put(az_draw *d, az_rect a, int x, int y, const char *s, int px, uint32_t col)
{
    int h = px * 3 / 2;
    if (x >= a.x + a.w || y < a.y || y + h > a.y + a.h) return 0;
    char buf[160];
    snprintf(buf, sizeof buf, "%s", s);
    int room = a.x + a.w - x;
    size_t n = strlen(buf);
    while (n > 0 && (int)(n * px * 64 / 100 + 2) > room) buf[--n] = 0;
    if (n == 0) return 0;
    C->draw_text(d, x, y, buf, px, col);
    return 1;
}

static void draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    g_page_seen_ms = now_ms();
    int rh = AZ_HAS(C, row_height) ? C->row_height(d) : 50;
    if (rh < 30) rh = 30;
    uint32_t text = C->color(AZ_COL_TEXT), dim = C->color(AZ_COL_TEXT_DIM), hi = C->color(AZ_COL_HI);
    C->draw_header(d, g_vox_only ? "Harmonic picks: acapellas" : "Harmonic picks");
    int y = a.y + rh + 6;
    put(d, a, a.x + 16, y, g_seed_text, 20, hi);
    y += 36;
    if (!g_reader_ok) { put(d, a, a.x + 16, y, "No library service on this AZ-MODS", 20, dim); return; }
    if (!g_show_lib) { put(d, a, a.x + 16, y, "Reading the library...", 20, dim); return; }
    if (g_nshow == 0) { put(d, a, a.x + 16, y, "Nothing fits within 6%", 20, dim); return; }
    int line = 34;
    int fit = (a.y + a.h - y - 24) / line;
    if (fit < 1) fit = 1;
    if (g_scroll > g_nshow - fit) g_scroll = g_nshow - fit;
    if (g_scroll < 0) g_scroll = 0;
    for (int i = g_scroll; i < g_nshow && i < g_scroll + fit; i++) {
        const struct pick *p = &g_show[i];
        char k[8], b[16], g[16], name[100];
        snprintf(k, sizeof k, "%s%s", WHEEL[p->idx].camelot, p->minor ? "A" : "B");
        snprintf(b, sizeof b, "%.1f", p->bpm);
        snprintf(g, sizeof g, "%.1f%%", p->gap);
        snprintf(name, sizeof name, "%s%s%s", p->artist, p->artist[0] ? " - " : "", p->title);
        put(d, a, a.x + 16, y, k, 20, p->step == 0 ? 0x3CCB5A : text);
        put(d, a, a.x + 76, y, b, 20, text);
        put(d, a, a.x + 156, y, g, 20, dim);
        int nx = a.x + 236;
        if (p->vox) { put(d, a, nx, y, "VOX", 20, 0xF2C230); nx += 56; }
        put(d, a, nx, y, name, 20, text);
        y += line;
    }
    char foot[64];
    snprintf(foot, sizeof foot, "%d-%d of %d   knob: scroll   press: %s", g_scroll + 1,
             g_scroll + fit < g_nshow ? g_scroll + fit : g_nshow, g_nshow, g_vox_only ? "all tracks" : "acapellas only");
    put(d, a, a.x + 16, a.y + a.h - 28, foot, 14, dim);
}

static int input(const az_event *ev, void *user)
{
    (void)user;
    if (!ev) return 0;
    if (ev->kind == AZ_EV_BACK) { g_page_on = 0; return 0; }
    if (ev->kind == AZ_EV_KNOB) { g_scroll += ev->dir; if (g_scroll < 0) g_scroll = 0; }
    else if (ev->kind == AZ_EV_CLICK) {
        g_vox_only = !g_vox_only;
        g_ui_seed_id = (uint32_t)-1;
        g_seed_deck = -2;
        set_seed();
    } else return 0;
    if (AZ_HAS(C, request_redraw)) C->request_redraw();
    return 1;
}

static void tick(void)
{
    long now = now_ms();
    if (__atomic_exchange_n(&g_new_result, 0, __ATOMIC_ACQ_REL)) {
        take_result();
        if (g_page_on && AZ_HAS(C, request_redraw)) C->request_redraw();
    }
    /* the page counts as open while it is being drawn; it is drawn when shown */
    if (g_page_on && now - g_page_seen_ms > 5000) g_page_on = 0;
    if (!g_page_on || now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;
    uint32_t before = g_ui_seed_id;
    int before_deck = g_seed_deck;
    set_seed();
    if ((before != g_ui_seed_id || before_deck != g_seed_deck) && AZ_HAS(C, request_redraw)) C->request_redraw();
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, register_screen) || !AZ_HAS(C, deck_info) || !AZ_HAS(C, draw_text) ||
        !AZ_HAS(C, draw_header) || !AZ_HAS(C, color)) {
        C->log(MOD_ID, "this AZ-MODS lacks the screen or deck services (1.2.3 or newer)");
        return 2;
    }
    static const az_screen_v1 scr = { sizeof(az_screen_v1), draw, input, on_enter, NULL };
    if (C->register_screen(MOD_ID, &scr) != 0) { C->log(MOD_ID, "screen refused"); return 3; }
    snprintf(g_seed_text, sizeof g_seed_text, "Load a track to see picks");
    if (AZ_HAS(C, lib_tracks) && AZ_HAS(C, thread_start)) {
        if (C->thread_start(MOD_ID, "picks reader", reader, NULL) == 0) g_reader_ok = 1;
        else C->log(MOD_ID, "thread_start refused: no picks");
    } else {
        C->log(MOD_ID, "no library service: no picks");
    }
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
