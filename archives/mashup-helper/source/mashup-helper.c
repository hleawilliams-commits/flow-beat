/* SPDX-License-Identifier: GPL-3.0-only
 *
 * mashup-helper: do the two decks you are mixing go together?
 *
 * One line at the top of the deck view for the two decks in play (the two that started
 * playing last; with fewer playing, the loaded ones fill in):
 *
 *   1+2  8A 9A  MATCH  pitch 1.6%
 *
 *   SAME / MATCH  green: same key, a neighbour on the Camelot wheel, or the relative key
 *   BOOST         amber: two steps round the wheel (an energy lift; works for short blends)
 *   CLASH         red:   anything else
 *   pitch         how far apart the two tracks' original tempos are (half and double time
 *                 count), so you know how much one has to be pitched to meet the other
 *
 * Keys and original tempos come from the stick's rekordbox library, read once on the mod's
 * own thread (the same reader as Key next to BPM); the UI thread only looks ids up. No hooks,
 * controls, LEDs, audio, files or network. "firmware": ["any"].
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "mashup-helper"
#define NDECK 4
#define MAX_TRACKS 20000
#define POLL_MS 250
#define RESCAN_MIN_S 20
#define READER_SLEEP_MS 250
#define PX 20
#define AREA_X0 212
static const int LINE_Y[2] = { 46, 752 };

static const az_core_v1 *C;
static az_surface *surf;
static int r_show = -1, r_pos = -1, r_pitch = -1;

/* ---- the key table: written by the reader thread, read by the UI thread ----------------- */
struct kt { uint32_t id; int stick; char key[8]; double bpm; };
static struct kt g_tab[MAX_TRACKS];
static int g_ntab;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_want_scan = 1;     /* atomics: UI -> reader (a key was missing), reader -> UI */
static int g_scan_done;
static int g_scan_count;
static int g_reader_ok;

/* The reader's scratch, so the mutex is held only for the copy. */
static struct kt g_scratch[MAX_TRACKS];
static int g_nscratch;

static int on_track(const az_track *t, void *user)
{
    int stick = *(int *)user;
    if (g_nscratch >= MAX_TRACKS) return 1;
    if (!t || !t->key || !t->key[0]) return 0;      /* no key in the library: nothing to show */
    struct kt *k = &g_scratch[g_nscratch++];
    k->id = t->id;
    k->stick = stick;
    snprintf(k->key, sizeof k->key, "%s", t->key);
    k->bpm = t->bpm;
    return 0;
}

static void scan_library(void)
{
    g_nscratch = 0;
    for (int stick = 0; stick < 2 && g_nscratch < MAX_TRACKS; stick++) {
        int s = stick;
        int r = C->lib_tracks(MOD_ID, stick, 0, on_track, &s);  /* 0 = every track */
        if (r < 0) continue;                                    /* no library in that port */
    }
    pthread_mutex_lock(&g_mu);
    memcpy(g_tab, g_scratch, sizeof(struct kt) * (size_t)g_nscratch);
    g_ntab = g_nscratch;
    pthread_mutex_unlock(&g_mu);
    __atomic_store_n(&g_scan_count, g_nscratch, __ATOMIC_RELAXED);
    __atomic_store_n(&g_scan_done, 1, __ATOMIC_RELEASE);   /* tick logs it, on the UI thread */
}

static void *reader(void *arg)
{
    (void)arg;
    /* time is counted in sleeps, so the thread never touches a clock */
    int since_scan = RESCAN_MIN_S * 1000 / READER_SLEEP_MS;
    for (;;) {
        if (since_scan < RESCAN_MIN_S * 1000 / READER_SLEEP_MS) since_scan++;
        if (__atomic_load_n(&g_want_scan, __ATOMIC_ACQUIRE) &&
            since_scan >= RESCAN_MIN_S * 1000 / READER_SLEEP_MS) {
            __atomic_store_n(&g_want_scan, 0, __ATOMIC_RELAXED);
            since_scan = 0;
            scan_library();
        }
        struct timespec ts = { 0, READER_SLEEP_MS * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* The key for a content id, from the table. 1 = found (copied into out), 0 = not there,
 * -1 = the id is on both sticks with different keys (unknown: show nothing). */
static int lookup(uint32_t id, char *out, int n, double *bpm)
{
    int found = 0, clash = 0;
    char first[8] = "";
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_ntab; i++) {
        if (g_tab[i].id != id) continue;
        if (!found) { snprintf(first, sizeof first, "%s", g_tab[i].key); *bpm = g_tab[i].bpm; found = 1; }
        else if (strcmp(first, g_tab[i].key) != 0) clash = 1;
    }
    pthread_mutex_unlock(&g_mu);
    if (!found) return 0;
    if (clash) return -1;
    snprintf(out, (size_t)n, "%s", first);
    return 1;
}

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
enum { NOTE_AS_IS = 0, NOTE_CAMELOT, NOTE_CLASSIC };

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


/* ---- what is shown (UI thread only) ------------------------------------------------- */
static struct { int loaded, playing; uint32_t id; char key[8]; double bpm; long started; } g_d[NDECK];
static long last_poll_ms = -POLL_MS;
static char g_text[64];
static uint32_t g_color;
static az_rect g_where;
static int g_shown;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Steps between two keys on the Camelot wheel: 0 same, 1 neighbour or relative, 2 two steps
 * on the same ring, 3 anything else. -1 = a key not understood. */
static int wheel_steps(const char *a, const char *b, int *ia, int *ma, int *ib, int *mb)
{
    if (!parse_key(a, ia, ma) || !parse_key(b, ib, mb)) return -1;
    int dist = (*ia - *ib + 12) % 12;
    if (dist > 6) dist = 12 - dist;
    if (*ma == *mb) return dist == 0 ? 0 : dist == 1 ? 1 : dist == 2 ? 2 : 3;
    return dist == 0 ? 1 : 3;     /* the relative major / minor */
}

static double pitch_gap(double a, double b)
{
    if (a <= 0 || b <= 0) return -1;
    double best = 1e9;
    static const double F[3] = { 0.5, 1.0, 2.0 };
    for (int i = 0; i < 3; i++) {
        double g = ((b * F[i] - a) < 0 ? (a - b * F[i]) : (b * F[i] - a)) / a * 100.0;
        if (g < best) best = g;
    }
    return best;
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
    C->fill_rect(d, a.x, a.y + a.h - 3, a.w, 3, g_color);
    C->draw_text(d, a.x + 6, a.y + 1, g_text, PX, 0xFFFFFF);
}

static int input(const az_event *ev, void *user)
{
    (void)ev; (void)user;
    return 0;
}

static void tick(void)
{
    long now = now_ms();
    int table_changed = __atomic_exchange_n(&g_scan_done, 0, __ATOMIC_ACQ_REL);
    if (!table_changed && now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;
    if (table_changed)
        C->log(MOD_ID, "library scan: %d keyed tracks", __atomic_load_n(&g_scan_count, __ATOMIC_RELAXED));

    int missing = 0;
    for (int d = 0; d < NDECK; d++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        if (C->deck_info(d, &di) != 0 || di.loaded != 1) {
            memset(&g_d[d], 0, sizeof g_d[d]);
            continue;
        }
        int new_track = !g_d[d].loaded || g_d[d].id != di.content_id;
        int playing = di.playing == 1;
        if (playing && !g_d[d].playing) g_d[d].started = now;
        g_d[d].playing = playing;
        g_d[d].loaded = 1;
        g_d[d].id = di.content_id;
        if (new_track || table_changed || !g_d[d].key[0]) {
            g_d[d].key[0] = 0;
            g_d[d].bpm = 0;
            if (di.content_id && g_reader_ok) {
                int r = lookup(di.content_id, g_d[d].key, sizeof g_d[d].key, &g_d[d].bpm);
                if (r == 0) missing = 1;
                if (r < 0) g_d[d].key[0] = 0;
            }
            if (di.key[0]) snprintf(g_d[d].key, sizeof g_d[d].key, "%s", di.key);
        }
    }
    if (missing) __atomic_store_n(&g_want_scan, 1, __ATOMIC_RELEASE);

    /* the pair: playing decks first, latest start first, then loaded decks in order */
    int order[NDECK], n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int d = 0; d < NDECK; d++) {
            if (!g_d[d].loaded || (pass == 0) != (g_d[d].playing == 1)) continue;
            int i = n++;
            order[i] = d;
            while (pass == 0 && i > 0 && g_d[order[i - 1]].started < g_d[d].started) {
                order[i] = order[i - 1]; order[i - 1] = d; i--;
            }
        }

    int show = r_show < 0 || C->row_value(r_show) == 1;
    int pos = r_pos >= 0 ? C->row_value(r_pos) : 0;
    int want_pitch = r_pitch < 0 || C->row_value(r_pitch) == 1;
    char text[64] = "";
    uint32_t color = 0x808080;
    if (show && n >= 2) {
        int a = order[0] < order[1] ? order[0] : order[1];
        int b = order[0] < order[1] ? order[1] : order[0];
        if (g_d[a].key[0] && g_d[b].key[0]) {
            int ia, ma, ib, mb;
            int s = wheel_steps(g_d[a].key, g_d[b].key, &ia, &ma, &ib, &mb);
            if (s >= 0) {
                static const char *const WORD[4] = { "SAME", "MATCH", "BOOST", "CLASH" };
                static const uint32_t COL[4] = { 0x3CCB5A, 0x3CCB5A, 0xF2C230, 0xFF4A3D };
                snprintf(text, sizeof text, "%d+%d  %s%s %s%s  %s", a + 1, b + 1,
                         WHEEL[ia].camelot, ma ? "A" : "B", WHEEL[ib].camelot, mb ? "A" : "B", WORD[s]);
                color = COL[s];
                double g = pitch_gap(g_d[a].bpm, g_d[b].bpm);
                if (want_pitch && g >= 0) {
                    size_t l = strlen(text);
                    snprintf(text + l, sizeof text - l, "  pitch %.1f%%", g);
                }
            }
        }
    }

    int w = text[0] ? est_width(text, PX) + 12 : 0;
    az_rect r = { 0, 0, 0, 0 };
    if (text[0]) r = (az_rect){ AREA_X0, LINE_Y[pos & 1], w, PX * 3 / 2 + 4 };
    int changed = strcmp(text, g_text) != 0 || color != g_color || memcmp(&r, &g_where, sizeof r) != 0;
    snprintf(g_text, sizeof g_text, "%s", text);
    g_color = color;
    if (surf) {
        if (text[0]) {
            if (memcmp(&r, &g_where, sizeof r) != 0) C->surface_place(surf, r);
            if (!g_shown) { C->surface_show(surf, 1); g_shown = 1; }
            if (changed) C->surface_redraw(surf);
        } else if (g_shown) {
            C->surface_show(surf, 0);
            g_shown = 0;
        }
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
        static const char *const POS[] = { "Screen top", "Bottom" };
        az_row_v1 row;
        memset(&row, 0, sizeof row);
        row.size = sizeof row;
        row.key = "show"; row.label = "Mashup helper"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Do the two decks in play go together? Key match and pitch gap.";
        r_show = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "pitch"; row.label = "Pitch gap"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "How far apart the two tracks' original tempos are.";
        r_pitch = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
        row.key = "position"; row.label = "Position"; row.kind = AZ_ROW_CHOICE;
        row.choices = POS; row.n_choices = 2; row.default_choice = 0;
        row.help = "Where the line sits.";
        r_pos = C->register_row(MOD_ID, &row);
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: defaults (on, pitch on, top)");
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
    if (AZ_HAS(C, lib_tracks) && AZ_HAS(C, thread_start)) {
        if (C->thread_start(MOD_ID, "key reader", reader, NULL) == 0) g_reader_ok = 1;
        else C->log(MOD_ID, "thread_start refused: keys only from deck_info");
    } else {
        C->log(MOD_ID, "no library service: keys only from deck_info");
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
