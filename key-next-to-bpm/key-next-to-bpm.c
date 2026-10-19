/* SPDX-License-Identifier: GPL-3.0-only
 *
 * key-next-to-bpm: the loaded track's musical key on the deck screen, next to the BPM.
 *
 * How it works:
 *   - tick() (the core's UI thread, a few times a second) reads each deck's snapshot with
 *     deck_info: loaded, the rekordbox content id and, when the core fills it, the key.
 *   - When the core does not hand over the key (AZ-MODS 1.2.3 leaves it empty), the key comes
 *     from the stick's rekordbox library: a reader thread (thread_start) lists every track of
 *     each stick once with lib_tracks and keeps an id -> key table. The UI thread only looks
 *     ids up in that table under a short mutex; it never touches the stick.
 *   - One small surface per deck, shown on the deck view only, draws the key in the theme's
 *     colors. Position, size and notation (as exported, Camelot, classic) are Mod Menu rows.
 *   - Unknown means nothing drawn: no key in the library, an ambiguous deck pair in the
 *     2/4 DECK view, or a core without the services.
 *
 * Nothing here patches the deck's code, changes a control, LED or the audio, writes files
 * or uses the network. "firmware": ["any"].
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "key-next-to-bpm"
#define NDECK 4
#define MAX_TRACKS 20000      /* the core's own cut for one lib_tracks call */
#define POLL_MS 250           /* how often tick re-reads the decks */
#define RESCAN_MIN_S 20       /* at most one library scan this often */
#define READER_SLEEP_MS 250

/* ---- deck screen geometry (1280 x 800), from the AZ-MODS core's own notes -------------
 * 2 DECK and 2/4 DECK: the two waveform rows are 198 px tall at y 81 and 286 (y 130 and 308,
 * 176 tall, while the official Phase Meter's layout is on). Only x 212..1050 of a row shows
 * between the left deck panel and the Beat FX panel. 4 DECK: the rows are not documented
 * anywhere; ROW4_Y below was measured on the emulator's screen. The "Nudge" row fine-tunes. */
#define ROW_X0 212
#define ROW_X1 1050
static const int ROW2_Y[2] = { 81, 286 };
static const int ROW2_PHASE_Y[2] = { 130, 308 };
/* 4 DECK, measured in the emulator: four 112 px lanes stacked 3-1-2-4 from the top, by deck
 * index (deck 1 is the second lane). */
static const int ROW4_Y[4] = { 203, 325, 92, 447 };
#define TOP_Y 46   /* "Screen top" position: the status strip above the waveforms */

static const az_core_v1 *C;
static az_surface *badge[NDECK];
static int r_show = -1, r_notation = -1, r_size = -1, r_pos = -1, r_nudge = -1;
static int r_phase_official = -1;   /* the official Phase Meter row, read-only */

/* ---- the key table: written by the reader thread, read by the UI thread ----------------- */
struct kt { uint32_t id; int stick; char key[8]; };
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
static int lookup(uint32_t id, char *out, int n)
{
    int found = 0, clash = 0;
    char first[8] = "";
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_ntab; i++) {
        if (g_tab[i].id != id) continue;
        if (!found) { snprintf(first, sizeof first, "%s", g_tab[i].key); found = 1; }
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

static void format_key(const char *in, int notation, char *out, int n)
{
    int idx, minor;
    if (notation == NOTE_AS_IS || !parse_key(in, &idx, &minor)) {
        snprintf(out, (size_t)n, "%s", in);
        return;
    }
    if (notation == NOTE_CAMELOT) snprintf(out, (size_t)n, "%s%s", WHEEL[idx].camelot, minor ? "A" : "B");
    else snprintf(out, (size_t)n, "%s", minor ? WHEEL[idx].minor : WHEEL[idx].major);
}

/* ---- what is shown (UI thread only) ------------------------------------------------- */
static struct {
    int loaded;
    uint32_t id;
    char key[8];        /* the raw key, "" = unknown */
    char text[32];      /* what the badge draws, "" = hidden */
    az_rect where;
    int shown;
} g_d[NDECK];
static int g_count;         /* decks in the view, 2 or 4, 0 = unknown */
static int g_combined;      /* 2/4 DECK with decks 3/4 in use: one labelled badge */
static long last_poll_ms = -POLL_MS;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int text_px(void)
{
    int size = r_size >= 0 ? C->row_value(r_size) : 1;
    return size == 0 ? 18 : size == 2 ? 34 : 26;
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
    int deck = (int)(intptr_t)user;
    if (deck < 0 || deck >= NDECK || !g_d[deck].text[0]) return;
    int px = text_px();
    C->draw_text(d, a.x + 6, a.y + 2, g_d[deck].text, px, C->color(AZ_COL_TEXT));
    C->fill_rect(d, a.x, a.y + a.h - 2, a.w, 2, C->color(AZ_COL_HI));
}

static int input(const az_event *ev, void *user)
{
    (void)ev; (void)user;
    return 0;   /* taps go through to the deck */
}

/* Where deck d's badge sits in the current view. */
static az_rect place_for(int d, int w, int h)
{
    int pos = r_pos >= 0 ? C->row_value(r_pos) : 0;
    int nudge_i = r_nudge >= 0 ? C->row_value(r_nudge) : 0;
    static const int NUDGE[4] = { 0, -24, 24, 48 };
    int nudge = NUDGE[nudge_i & 3];
    int phase_on = r_phase_official >= 0 && C->row_value(r_phase_official) == 1;
    int y;
    if (g_combined || pos == 2) {
        /* one strip at the screen top, badges side by side */
        return (az_rect){ ROW_X0 + 4 + d * (w + 8), TOP_Y + nudge, w, h };
    }
    if (g_count == 4) y = ROW4_Y[d & 3];
    else y = phase_on ? ROW2_PHASE_Y[d & 1] : ROW2_Y[d & 1];
    y += 2 + nudge;
    int x = pos == 1 ? ROW_X0 + 4 : ROW_X1 - w - 4;
    return (az_rect){ x, y, w, h };
}

static void refresh(void)
{
    int show = r_show < 0 || C->row_value(r_show) == 1;
    int notation = r_notation >= 0 ? C->row_value(r_notation) : NOTE_AS_IS;
    int px = text_px();
    int h = px * 3 / 2 + 6;

    for (int d = 0; d < NDECK; d++) {
        char text[32] = "";
        int visible = 0;
        if (show && g_d[d].loaded && g_d[d].key[0]) {
            char k[16];
            format_key(g_d[d].key, notation, k, sizeof k);
            if (g_combined) snprintf(text, sizeof text, "%d %s", d + 1, k);
            else snprintf(text, sizeof text, "%s", k);
            /* in the 2-row view only the two rows that show get a badge */
            visible = g_combined || g_count == 4 || d < 2;
        }
        if (!visible) text[0] = 0;
        int w = text[0] ? est_width(text, px) + 12 : 0;
        az_rect r = text[0] ? place_for(d, w, h) : (az_rect){ 0, 0, 0, 0 };
        int changed = strcmp(text, g_d[d].text) != 0 || memcmp(&r, &g_d[d].where, sizeof r) != 0;
        snprintf(g_d[d].text, sizeof g_d[d].text, "%s", text);
        g_d[d].where = r;
        if (!badge[d]) continue;
        if (text[0]) {
            if (changed) C->surface_place(badge[d], r);
            if (!g_d[d].shown) { C->surface_show(badge[d], 1); g_d[d].shown = 1; }
            if (changed) C->surface_redraw(badge[d]);
        } else if (g_d[d].shown) {
            C->surface_show(badge[d], 0);
            g_d[d].shown = 0;
        }
    }
}

static void tick(void)
{
    long now = now_ms();
    int table_changed = __atomic_exchange_n(&g_scan_done, 0, __ATOMIC_ACQ_REL);
    if (!table_changed && now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;
    if (table_changed)
        C->log(MOD_ID, "library scan: %d keyed tracks", __atomic_load_n(&g_scan_count, __ATOMIC_RELAXED));

    int n = C->deck_count();
    g_count = (n == 2 || n == 4) ? n : 0;
    int missing = 0, high_in_use = 0;

    for (int d = 0; d < NDECK; d++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        if (C->deck_info(d, &di) != 0 || di.loaded != 1) {
            g_d[d].loaded = 0; g_d[d].id = 0; g_d[d].key[0] = 0;
            continue;
        }
        if (d >= 2) high_in_use = 1;
        int new_track = !g_d[d].loaded || g_d[d].id != di.content_id;
        g_d[d].loaded = 1;
        g_d[d].id = di.content_id;
        if (di.key[0]) {                                   /* a core that fills it: use it */
            snprintf(g_d[d].key, sizeof g_d[d].key, "%s", di.key);
        } else if (new_track || table_changed || !g_d[d].key[0]) {
            g_d[d].key[0] = 0;
            if (di.content_id && g_reader_ok) {
                int r = lookup(di.content_id, g_d[d].key, sizeof g_d[d].key);
                if (r == 0) missing = 1;                   /* not in the table yet */
                if (r < 0) g_d[d].key[0] = 0;              /* two sticks disagree: unknown */
            }
        }
    }
    /* 2/4 DECK shows decks 1/2 or 3/4 and the core does not say which: with decks 3/4 in
     * use, every badge carries its deck number and they sit in one strip, never on a row. */
    g_combined = (g_count == 2 && high_in_use);
    if (missing) __atomic_store_n(&g_want_scan, 1, __ATOMIC_RELEASE);
    refresh();
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_count) || !AZ_HAS(C, deck_info) || !AZ_HAS(C, surface_create) ||
        !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) || !AZ_HAS(C, surface_redraw) ||
        !AZ_HAS(C, draw_text) || !AZ_HAS(C, fill_rect) || !AZ_HAS(C, color)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck or surface services (1.2.3 or newer)");
        return 2;
    }

    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
    static const char *const NOTATIONS[] = { "Raw", "8A", "Am" };
    static const char *const SIZES[] = { "S", "M", "L" };
    static const char *const POSITIONS[] = { "Right", "Left", "Top" };
    static const char *const NUDGES[] = { "0", "Up", "Dn", "Dn2" };
    az_row_v1 row;
    memset(&row, 0, sizeof row);
    row.size = sizeof row;
    row.key = "show"; row.label = "Key next to BPM"; row.kind = AZ_ROW_TOGGLE;
    row.flags = AZ_ROW_DEFAULT_ON;
    row.help = "The loaded track's key on each deck, from the stick's rekordbox library.";
    r_show = C->register_row(MOD_ID, &row);
    memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "show";
    row.key = "notation"; row.label = "Notation"; row.kind = AZ_ROW_CHOICE;
    row.choices = NOTATIONS; row.n_choices = 3; row.default_choice = 0;
    row.help = "Raw = as exported, 8A = Camelot, Am = classic.";
    r_notation = C->register_row(MOD_ID, &row);
    row.key = "size"; row.label = "Size"; row.choices = SIZES; row.n_choices = 3; row.default_choice = 1;
    row.help = "Text size of the key.";
    r_size = C->register_row(MOD_ID, &row);
    row.key = "position"; row.label = "Position"; row.choices = POSITIONS; row.n_choices = 3; row.default_choice = 0;
    row.help = "Right or left of the waveform, or screen top.";
    r_pos = C->register_row(MOD_ID, &row);
    row.key = "nudge"; row.label = "Nudge"; row.choices = NUDGES; row.n_choices = 4; row.default_choice = 0;
    row.help = "Move the key up or down to line up with BPM.";
    r_nudge = C->register_row(MOD_ID, &row);
    if (AZ_HAS(C, row_lookup)) r_phase_official = C->row_lookup("phase-meter");
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: defaults (on, as exported, medium)");
    }

    for (int d = 0; d < NDECK; d++) {
        az_surface_v1 desc;
        memset(&desc, 0, sizeof desc);
        desc.size = sizeof desc;
        desc.layer = AZ_LAYER_OVER_EP147;
        desc.views = AZ_VIEW_BIT(AZ_VIEW_DECK);
        desc.on_paint = paint;
        desc.on_input = input;
        desc.user = (void *)(intptr_t)d;
        badge[d] = C->surface_create(MOD_ID, &desc);
        /* The core shares 16 surfaces between every mod. With several mods on, the last
         * ones can be refused: keep the badges we got (decks 1 and 2 first) and run. */
        if (!badge[d]) { C->log(MOD_ID, "surface %d refused: other mods hold the rest", d + 1); break; }
    }
    if (!badge[0]) return 3;

    /* The library reader, last: a thread outlives a refused init. Without it the mod still
     * shows a key on a core that fills deck_info.key. */
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
