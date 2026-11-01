/* SPDX-License-Identifier: GPL-3.0-only
 *
 * harmonic-picks: a page in the Mod Menu with the tracks on your sticks that fit what is
 * playing now, and a press to load one.
 *
 * Mod Menu > Harmonic picks (the mod's own row in the main list). The page takes the deck
 * that started playing last (or the first loaded deck) and lists up to 40 tracks from the
 * WHOLE rekordbox library on both sticks, not one playlist: a Camelot-compatible key (same,
 * a neighbour, the relative key) and an original tempo within the BPM range row (3, 6 or
 * 10%; half and double time count), closest first. Acapellas (by title) are marked VOX.
 *
 * Turn the browse knob to move the cursor. Press on a pick: the mod puts the deck's own
 * browser on that track (SOURCE, PLAYLIST, knob and click, from a playlist the track is in,
 * like Crate Remote's iPad Load) and presses LOAD for a stopped deck: the other deck of the
 * pair (1<->2, 3<->4), else any stopped deck. It never loads over a playing deck, and
 * touching any control stops it. Press on the top row to switch between all tracks and
 * acapellas only. "Press loads a pick" off = the press only shows the track in the browser.
 *
 * Threads: the libraries, playlist membership and ranking are read on the mod's own thread;
 * the UI thread hands over the seed / load requests and draws; the mixer bus thread runs the
 * navigator script (nav.h). "firmware": ["any"].
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"
#include "nav.h"

#define MOD_ID "harmonic-picks"
#define NDECK 4
#define MAX_TRACKS 20000
#define NPICKS 40
#define POLL_MS 500
#define READER_SLEEP_MS 100

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
struct pick { uint32_t id; signed char idx, minor, vox, step, stick; float bpm, gap; char title[56]; char artist[36]; };
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_seed_id;          /* the playing track; 0 = none */
static int g_seed_idx = -1, g_seed_minor, g_seed_vox_only, g_seed_gen;
static float g_seed_bpm, g_seed_range = 6.0f;
static struct pick g_res[NPICKS];
static int g_nres, g_res_gen = -1, g_lib_ready;
static int g_new_result;            /* atomic: reader -> UI */

/* A load request (UI -> reader) and its playlist path (reader -> UI), under g_mu. */
struct loadreq { int stick, deck; uint32_t id; char title[56]; };
struct loadres { int ok, stick, deck, depth, path[8]; uint32_t id; char title[56], where[160]; };
static struct loadreq g_lreq;
static int g_lreq_gen;
static struct loadres g_lres;
static int g_lres_new;              /* atomic: reader -> UI */

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

static void rank(uint32_t seed_id, int sidx, int smin, float sbpm, float range, int vox_only,
                 struct pick *out, int *nout)
{
    int n = 0;
    for (int i = 0; i < g_nlib; i++) {
        const struct tr *t = &g_lib[i];
        if (t->id == seed_id || (vox_only && !t->vox)) continue;
        int step = key_step(sidx, smin, t->idx, t->minor);
        if (step < 0) continue;
        float g = gap_pct(sbpm, t->bpm);
        if (g > range) continue;
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
        p->id = t->id; p->idx = t->idx; p->minor = t->minor; p->vox = t->vox; p->stick = t->stick;
        p->step = (signed char)step; p->bpm = t->bpm; p->gap = g;
        memcpy(p->title, t->title, sizeof p->title);
        memcpy(p->artist, t->artist, sizeof p->artist);
    }
    *nout = n;
}

/* ---- playlists, for finding a pick in the deck's browser (reader thread only) ------------
 * The deck's browser can only be steered through the PLAYLIST tree, so a pick is reached
 * through a playlist it is in (the first 250 rows, at most 7 folders deep). Read on the
 * first load request per stick, then kept. */
struct pl { int id, parent, folder; char name[48]; uint32_t *mem; int nmem; };
struct plset { int state; struct pl *p; int np, cap; };   /* state 0 unread, 1 ok, -1 none */
static struct plset g_pls[2];

static int on_pl(int pl_id, int parent, const char *name, int folder, void *user)
{
    struct plset *s = user;
    if (s->np == s->cap) {
        int c = s->cap ? s->cap * 2 : 128;
        struct pl *n = realloc(s->p, sizeof *n * (size_t)c);
        if (!n) return 1;
        s->p = n; s->cap = c;
    }
    struct pl *x = &s->p[s->np++];
    memset(x, 0, sizeof *x);
    x->id = pl_id; x->parent = parent; x->folder = folder;
    snprintf(x->name, sizeof x->name, "%s", name ? name : "");
    return 0;
}
struct ids { uint32_t *v; int n, cap; };
static int on_member(const az_track *t, void *user)
{
    struct ids *a = user;
    if (a->n >= 250) return 1;                 /* the navigator reaches the first 250 rows */
    if (a->n == a->cap) {
        int c = a->cap ? a->cap * 2 : 64;
        uint32_t *n = realloc(a->v, sizeof *n * (size_t)c);
        if (!n) return 1;
        a->v = n; a->cap = c;
    }
    a->v[a->n++] = t->id;
    return 0;
}
static struct plset *pls_get(int stick)
{
    struct plset *s = &g_pls[stick];
    if (s->state) return s;
    if (!AZ_HAS(C, lib_playlists) || C->lib_playlists(MOD_ID, stick, on_pl, s) < 0) { s->state = -1; return s; }
    for (int i = 0; i < s->np; i++) {
        if (s->p[i].folder) continue;
        struct ids a = { 0 };
        C->lib_tracks(MOD_ID, stick, s->p[i].id, on_member, &a);
        s->p[i].mem = a.v; s->p[i].nmem = a.n;
    }
    s->state = 1;
    return s;
}
static int pl_index(const struct plset *s, int id)
{
    for (int i = 0; i < s->np; i++) if (s->p[i].id == id) return i;
    return -1;
}
/* Rows from the top of the PLAYLIST tree down to playlist index pi, siblings in the DJ's
 * order (as lib_playlists reports them). 0 = not reachable. Same rules as Crate Remote. */
static int tree_path(const struct plset *s, int pi, int *path, int *depth, char *where, size_t wn)
{
    int chain[8], n = 0;
    for (int cur = pi; cur >= 0; cur = pl_index(s, s->p[cur].parent)) {
        if (n == 7) return 0;
        for (int k = 0; k < n; k++) if (chain[k] == cur) return 0;
        chain[n++] = cur;
        if (s->p[cur].parent == 0) break;
    }
    *depth = 0; where[0] = 0;
    for (int k = n - 1; k >= 0; k--) {
        int me = chain[k], parent = s->p[me].parent, row = 0;
        int top = pl_index(s, parent) < 0;
        for (int j = 0; j < s->np && j != me; j++)
            if (s->p[j].parent == parent || (top && pl_index(s, s->p[j].parent) < 0)) row++;
        if (row > 249) return 0;
        path[(*depth)++] = row;
        size_t l = strlen(where);
        snprintf(where + l, wn - l, "%s%s", l ? " / " : "", s->p[me].name);
    }
    return 1;
}
/* The shortest path (fewest folders, then the earliest row). 1 found, 0 in no reachable
 * playlist, -1 no playlists on that stick. */
static int find_path(int stick, uint32_t id, struct loadres *r)
{
    struct plset *s = pls_get(stick);
    if (s->state != 1) return -1;
    int best = -1, bestrow = 0, bestdepth = 99;
    for (int i = 0; i < s->np; i++) {
        for (int row = 0; row < s->p[i].nmem; row++) {
            if (s->p[i].mem[row] != id) continue;
            int path[8], depth; char where[160];
            if (tree_path(s, i, path, &depth, where, sizeof where) && depth <= 7 &&
                (best < 0 || depth < bestdepth || (depth == bestdepth && row < bestrow))) {
                best = i; bestrow = row; bestdepth = depth;
                memcpy(r->path, path, sizeof path);
                r->path[depth] = row; r->depth = depth + 1;
                snprintf(r->where, sizeof r->where, "%.140s / #%d", where, row + 1);
            }
            break;
        }
    }
    return best >= 0;
}

static struct pick g_scratch[NPICKS];

static void *reader(void *arg)
{
    (void)arg;
    for (int stick = 0; stick < 2 && g_nlib < MAX_TRACKS; stick++) {
        int s = stick;
        C->lib_tracks(MOD_ID, stick, 0, on_track, &s);      /* 0 = every track on the stick */
    }
    pthread_mutex_lock(&g_mu);
    g_lib_ready = g_nlib;
    pthread_mutex_unlock(&g_mu);
    __atomic_store_n(&g_new_result, 1, __ATOMIC_RELEASE);
    int done_gen = -1, done_lgen = 0;
    for (;;) {
        pthread_mutex_lock(&g_mu);
        int gen = g_seed_gen, sidx = g_seed_idx, smin = g_seed_minor, vox = g_seed_vox_only;
        uint32_t sid = g_seed_id;
        float sbpm = g_seed_bpm, range = g_seed_range;
        int lgen = g_lreq_gen;
        struct loadreq lq = g_lreq;
        pthread_mutex_unlock(&g_mu);
        if (gen != done_gen) {
            int n = 0;
            if (sidx >= 0 && sbpm > 0) rank(sid, sidx, smin, sbpm, range, vox, g_scratch, &n);
            pthread_mutex_lock(&g_mu);
            memcpy(g_res, g_scratch, sizeof g_res[0] * (size_t)n);
            g_nres = n;
            g_res_gen = gen;
            pthread_mutex_unlock(&g_mu);
            done_gen = gen;
            __atomic_store_n(&g_new_result, 1, __ATOMIC_RELEASE);
        }
        if (lgen != done_lgen) {
            struct loadres r;
            memset(&r, 0, sizeof r);
            r.stick = lq.stick; r.deck = lq.deck; r.id = lq.id;
            memcpy(r.title, lq.title, sizeof r.title);
            r.ok = (lq.stick == 0 || lq.stick == 1) ? find_path(lq.stick, lq.id, &r) : -1;
            pthread_mutex_lock(&g_mu);
            g_lres = r;
            pthread_mutex_unlock(&g_mu);
            done_lgen = lgen;
            __atomic_store_n(&g_lres_new, 1, __ATOMIC_RELEASE);
        }
        struct timespec ts = { 0, READER_SLEEP_MS * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- deck navigator glue (mixer bus thread) ---------------------------------------------- */
static int g_filter_ok;
static int deck_busy(int deck)
{
    az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di; di.playing = -1;
    if (!AZ_HAS(C, deck_info) || C->deck_info(deck, &di) != 0) return 1;   /* unknown: refuse */
    return di.loaded == 1 && di.playing != 0;                               /* playing or unknown */
}
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static int g_source_touched = 1;    /* atomic: the DJ pressed SOURCE (or we never switched) */
static int on_mixer(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (bus != AZ_BUS_MIXER_IN || !f || len < 98) return AZ_FRAME_OK;
    if (f[32] & 0x80) __atomic_store_n(&g_source_touched, 1, __ATOMIC_RELAXED);
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
    if (st != NAV_ARMED && st != NAV_RUNNING && !nav_off) { nav_real_last = f[34]; return AZ_FRAME_OK; }
    nav_frame(f, len, now_ms(), C->view(), deck_busy);
    return AZ_FRAME_OK;
}

/* ---- the page (UI thread) ---------------------------------------------------------------- */
static int g_page_on, g_scroll, g_cursor, g_vox_only, g_reader_ok;
static int g_row_load = -1, g_row_range = -1;
static const float RANGES[3] = { 3.0f, 6.0f, 10.0f };
static float g_range = 6.0f;
static int g_seed_deck = -1;
static uint32_t g_ui_seed_id;
static long last_poll_ms = -POLL_MS, g_page_seen_ms = -100000;
static int started_seq[NDECK], was_playing[NDECK], seq_counter;
/* what on_draw shows: a copy taken on the UI thread */
static struct pick g_show[NPICKS];
static int g_nshow, g_show_lib;
static char g_seed_text[96], g_status[160];
/* navigator bookkeeping (UI thread) */
static struct loadres g_active;
static int g_nav_seen = -1, g_last_stick = -1, g_verify_left, g_waiting_path;
static long g_armed_at, g_settle_until;

static void status(const char *s)
{
    snprintf(g_status, sizeof g_status, "%s", s);
    if (AZ_HAS(C, request_redraw)) C->request_redraw();
}

static void set_seed(void)
{
    /* the deck that started playing last, else the first loaded deck */
    int best = -1;
    az_deck_info pick_di;
    memset(&pick_di, 0, sizeof pick_di);
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
    float range = g_row_range >= 0 ? RANGES[C->row_value(g_row_range) % 3] : 6.0f;
    uint32_t id = best >= 0 ? pick_di.content_id : 0;
    if (best == g_seed_deck && id == g_ui_seed_id && range == g_range) return;
    g_seed_deck = best;
    g_ui_seed_id = id;
    g_range = range;
    g_scroll = 0;
    g_cursor = 0;
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
    g_seed_vox_only = g_vox_only; g_seed_range = range;
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
    if (g_cursor > g_nshow) g_cursor = g_nshow;
}

/* The deck a press loads: the seed deck's partner (1<->2, 3<->4), else any other stopped
 * deck in view. -1 = every other deck plays. */
static int target_deck(void)
{
    int n = AZ_HAS(C, deck_count) ? C->deck_count() : 2;
    if (n != 4) n = 2;
    int seed = g_seed_deck >= 0 && g_seed_deck < n ? g_seed_deck : 0;
    int partner = seed ^ 1;
    if (!deck_busy(partner)) return partner;
    for (int d = 0; d < n; d++) if (d != seed && d != partner && !deck_busy(d)) return d;
    if (g_seed_deck < 0 && !deck_busy(seed)) return seed;
    return -1;
}

static void request_load(const struct pick *p)
{
    if (!g_filter_ok) { status("This AZ-MODS can't steer the browser: find it by hand"); return; }
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
    if (st == NAV_ARMED || st == NAV_RUNNING || g_waiting_path) { status("Still finding the last pick..."); return; }
    int loads = g_row_load < 0 || C->row_value(g_row_load) == 1;
    int deck = -1;
    if (loads) {
        deck = target_deck();
        if (deck < 0) { status("Every other deck is playing: stop one first"); return; }
    }
    pthread_mutex_lock(&g_mu);
    g_lreq.stick = p->stick; g_lreq.deck = deck; g_lreq.id = p->id;
    memcpy(g_lreq.title, p->title, sizeof g_lreq.title);
    g_lreq_gen++;
    pthread_mutex_unlock(&g_mu);
    g_waiting_path = 1;
    char m[160];
    snprintf(m, sizeof m, "Looking up %.60s in your playlists...", p->title);
    status(m);
}

static void nav_tick(long now)
{
    char m[200];
    if (g_waiting_path && now >= g_settle_until && __atomic_exchange_n(&g_lres_new, 0, __ATOMIC_ACQ_REL)) {
        struct loadres r;
        pthread_mutex_lock(&g_mu);
        r = g_lres;
        pthread_mutex_unlock(&g_mu);
        g_waiting_path = 0;
        nav_script sc;
        int sel = r.stick != g_last_stick || __atomic_load_n(&g_source_touched, __ATOMIC_RELAXED);
        if (r.ok < 0) status("Can't read that USB's playlists");
        else if (r.ok == 0) {
            snprintf(m, sizeof m, "%.60s is in no playlist the deck can reach: search for it", r.title);
            status(m);
        } else if (r.deck >= 0 && deck_busy(r.deck)) {
            snprintf(m, sizeof m, "Deck %d is playing: not loaded", r.deck + 1);
            status(m);
        } else if (!nav_build(&sc, sel, r.stick, r.path, r.depth, r.deck) || !nav_arm(&sc)) {
            status("The browser is busy; try again");
        } else {
            g_active = r; g_armed_at = now; g_verify_left = 0;
            if (sel) { __atomic_store_n(&g_source_touched, 0, __ATOMIC_RELAXED); g_last_stick = -1; }
            snprintf(m, sizeof m, "Finding %.50s on USB %d: %.80s", r.title, r.stick + 1, r.where);
            status(m);
        }
    }
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
    if (st == NAV_ARMED && now - g_armed_at > 4000) {
        int expect = NAV_ARMED;
        if (__atomic_compare_exchange_n(&nav_state, &expect, NAV_IDLE, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            status("Hands on the controls: press again in a moment");
    }
    if (st != g_nav_seen) {
        g_nav_seen = st;
        if (st == NAV_DONE) {
            g_last_stick = g_active.stick;
            if (g_active.deck >= 0) {
                g_verify_left = 40;
                snprintf(m, sizeof m, "Loading on Deck %d...", g_active.deck + 1);
                status(m);
            } else if (C->view() != AZ_VIEW_BROWSE) {
                status("The screen changed before it landed: press again");
            } else {
                status("Highlighted in the browser: press LOAD");
                if (AZ_HAS(C, banner)) C->banner(MOD_ID, "Harmonic pick highlighted: press LOAD", 2500);
            }
        } else if (st == NAV_CANCELLED) {
            status("Stopped: you used the controls");
        } else if (st == NAV_REFUSED) {
            snprintf(m, sizeof m, "Deck %d started playing: not loaded", __atomic_load_n(&nav_refused_deck, __ATOMIC_RELAXED));
            status(m);
        }
    }
    /* After a load, confirm the deck reports the track we meant (about 8 s of ticks). */
    if (g_verify_left > 0) {
        az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
        if (C->deck_info(g_active.deck, &di) == 0 && di.loaded == 1 && di.content_id == g_active.id) {
            g_verify_left = 0; g_settle_until = now + 3500;
            snprintf(m, sizeof m, "Loaded on Deck %d: %.60s", g_active.deck + 1, g_active.title);
            status(m);
            if (AZ_HAS(C, banner)) C->banner(MOD_ID, m, 2000);
        } else if (--g_verify_left == 0) {
            g_settle_until = now + 3500;
            snprintf(m, sizeof m, "Check Deck %d: it didn't report %.60s (playlist sorted differently?)",
                     g_active.deck + 1, g_active.title);
            status(m);
        }
    }
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
    char buf[200];
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
    C->draw_header(d, g_vox_only ? "Harmonic picks: acapellas, whole library" : "Harmonic picks: whole library");
    int y = a.y + rh + 6;
    put(d, a, a.x + 16, y, g_seed_text, 20, hi);
    y += 32;
    if (g_status[0]) { put(d, a, a.x + 16, y, g_status, 16, dim); }
    y += 28;
    if (!g_reader_ok) { put(d, a, a.x + 16, y, "No library service on this AZ-MODS", 20, dim); return; }
    if (!g_show_lib) { put(d, a, a.x + 16, y, "Reading the library...", 20, dim); return; }
    int line = 34;
    /* row 0: the all / acapellas switch; rows 1..n: the picks */
    int nrows = g_nshow + 1;
    int fit = (a.y + a.h - y - 24) / line;
    if (fit < 1) fit = 1;
    if (g_cursor < g_scroll) g_scroll = g_cursor;
    if (g_cursor >= g_scroll + fit) g_scroll = g_cursor - fit + 1;
    if (g_scroll > nrows - fit) g_scroll = nrows - fit;
    if (g_scroll < 0) g_scroll = 0;
    for (int r = g_scroll; r < nrows && r < g_scroll + fit; r++) {
        int sel = r == g_cursor;
        if (sel && AZ_HAS(C, fill_rect) && a.w > 12 && y - 2 >= a.y && y + line - 4 <= a.y + a.h)
            C->fill_rect(d, a.x + 6, y - 2, a.w - 12, line - 2, C->color(AZ_COL_HI) & 0x3F3F3F);
        if (sel) put(d, a, a.x + 4, y, ">", 20, hi);
        if (r == 0) {
            char t[96];
            snprintf(t, sizeof t, "%s   (press to show %s)", g_vox_only ? "Acapellas only" : "All tracks",
                     g_vox_only ? "all tracks" : "acapellas only");
            put(d, a, a.x + 24, y, t, 18, sel ? text : dim);
            y += line;
            continue;
        }
        const struct pick *p = &g_show[r - 1];
        char k[8], b[16], g[16], name[100];
        snprintf(k, sizeof k, "%s%s", WHEEL[p->idx].camelot, p->minor ? "A" : "B");
        snprintf(b, sizeof b, "%.1f", p->bpm);
        snprintf(g, sizeof g, "%.1f%%", p->gap);
        snprintf(name, sizeof name, "%s%s%s", p->artist, p->artist[0] ? " - " : "", p->title);
        put(d, a, a.x + 24, y, k, 20, p->step == 0 ? 0x3CCB5A : text);
        put(d, a, a.x + 84, y, b, 20, text);
        put(d, a, a.x + 164, y, g, 20, dim);
        int nx = a.x + 244;
        if (p->vox) { put(d, a, nx, y, "VOX", 20, 0xF2C230); nx += 56; }
        put(d, a, nx, y, name, 20, text);
        y += line;
    }
    if (g_nshow == 0) {
        char t[48];
        snprintf(t, sizeof t, "Nothing fits within %.0f%%", g_range);
        put(d, a, a.x + 24, y, t, 20, dim);
    }
    int loads = g_row_load < 0 || C->row_value(g_row_load) == 1;
    char foot[96];
    snprintf(foot, sizeof foot, "%d picks within %.0f%%   knob: move   press: %s", g_nshow, g_range,
             loads ? "load on a stopped deck" : "show in browser");
    put(d, a, a.x + 16, a.y + a.h - 28, foot, 14, dim);
}

static int input(const az_event *ev, void *user)
{
    (void)user;
    if (!ev) return 0;
    if (ev->kind == AZ_EV_BACK) { g_page_on = 0; return 0; }
    if (ev->kind == AZ_EV_KNOB) {
        g_cursor += ev->dir;
        if (g_cursor > g_nshow) g_cursor = g_nshow;
        if (g_cursor < 0) g_cursor = 0;
    } else if (ev->kind == AZ_EV_CLICK) {
        if (g_cursor == 0) {
            g_vox_only = !g_vox_only;
            g_ui_seed_id = (uint32_t)-1;
            g_seed_deck = -2;
            set_seed();
        } else if (g_cursor - 1 < g_nshow) {
            request_load(&g_show[g_cursor - 1]);
        }
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
    if (g_filter_ok) nav_tick(now);
    /* the page counts as open while it is being drawn; it is drawn when shown */
    if (g_page_on && now - g_page_seen_ms > 5000) g_page_on = 0;
    if (!g_page_on || now - last_poll_ms < POLL_MS) return;
    /* while a pick is being found, keep the list still under the cursor */
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
    if (g_waiting_path || st == NAV_ARMED || st == NAV_RUNNING || g_verify_left > 0) return;
    last_poll_ms = now;
    uint32_t before = g_ui_seed_id;
    int before_deck = g_seed_deck;
    set_seed();
    if ((before != g_ui_seed_id || before_deck != g_seed_deck) && AZ_HAS(C, request_redraw)) C->request_redraw();
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    /* A Reload calls init again while the reader (started once) keeps using C. */
    if (__atomic_load_n(&C, __ATOMIC_ACQUIRE) != core) __atomic_store_n(&C, core, __ATOMIC_RELEASE);
    if (!AZ_HAS(C, register_screen) || !AZ_HAS(C, deck_info) || !AZ_HAS(C, draw_text) ||
        !AZ_HAS(C, draw_header) || !AZ_HAS(C, color)) {
        C->log(MOD_ID, "this AZ-MODS lacks the screen or deck services (1.2.3 or newer)");
        return 2;
    }
    static const az_screen_v1 scr = { sizeof(az_screen_v1), draw, input, on_enter, NULL };
    if (C->register_screen(MOD_ID, &scr) != 0) { C->log(MOD_ID, "screen refused"); return 3; }
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        az_row_v1 open = { .size = sizeof open, .key = "open", .label = "Harmonic picks", .kind = AZ_ROW_SCREEN,
            .help = "Tracks from your whole USB library (both sticks, not just one playlist) in a key that mixes "
                    "with the deck that started last, within the BPM range. Press one to load it." };
        C->register_row(MOD_ID, &open);
        az_row_v1 load = { .size = sizeof load, .key = "load", .label = "Press loads a pick", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "On: pressing a pick finds it in a playlist it is in (SOURCE, PLAYLIST, knob, click, done for "
                    "you) and presses LOAD for a stopped deck, the other deck of the pair first. Never a playing "
                    "deck; touching any control stops it. Off: it only highlights the track in the browser." };
        g_row_load = C->register_row(MOD_ID, &load);
        static const char *const RANGE_NAMES[3] = { "3%", "6%", "10%" };
        az_row_v1 range = { .size = sizeof range, .key = "range", .label = "BPM range", .kind = AZ_ROW_CHOICE,
            .choices = RANGE_NAMES, .n_choices = 3, .default_choice = 1,
            .help = "How far a pick's BPM may be from the playing track. Half and double time count." };
        g_row_range = C->register_row(MOD_ID, &range);
    }
    snprintf(g_seed_text, sizeof g_seed_text, "Load a track to see picks");
    static int started;
    if (!started) {
        if (AZ_HAS(C, frame_filter) && AZ_HAS(C, view))
            g_filter_ok = C->frame_filter(MOD_ID, 1u << AZ_BUS_MIXER_IN, on_mixer, NULL) == 0;
        if (AZ_HAS(C, lib_tracks) && AZ_HAS(C, thread_start)) {
            if (C->thread_start(MOD_ID, "picks reader", reader, NULL) == 0) g_reader_ok = 1;
            else C->log(MOD_ID, "thread_start refused: no picks");
        } else {
            C->log(MOD_ID, "no library service: no picks");
        }
        started = 1;
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
