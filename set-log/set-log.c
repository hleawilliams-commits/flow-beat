/* SPDX-License-Identifier: GPL-3.0-only
 *
 * set-log: your tracklist, written to the USB stick while you play.
 *
 * A track counts as played once it has played for 30 seconds in one go (so cueing and
 * previewing do not count), once per load. Each played track becomes one line in a CSV in the
 * mod's data folder on the stick, MODS/community-mods/set-log/data/:
 *
 *   set-<date>-<time>.csv   one file per set: a new set starts after a reboot, after
 *                           "Start a new set" in the Mod Menu, or after 30 minutes with
 *                           nothing playing
 *   n,set_time,clock,deck,artist,title,bpm,length,rekordbox_id
 *
 * set_time is the time since the set's first track (from the deck's monotonic clock), clock is
 * the deck's wall clock (only as right as the deck's own clock). The UI thread only notices
 * plays and queues lines; a thread of the mod's own writes them (open, append, close, so an
 * eject is never held up). No hooks, controls, LEDs, audio or network. "firmware": ["any"].
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "set-log"
#define NDECK 4
#define POLL_MS 250
#define PLAYED_MS 30000
#define NEW_SET_IDLE_MS (30 * 60 * 1000L)
#define QLEN 64

static const az_core_v1 *C;
static int r_on = -1, r_banner = -1;

/* ---- the queue: UI thread -> writer thread ---------------------------------------------- */
struct line { char text[800]; int new_set; time_t wall; };
static struct line g_q[QLEN];
static int g_qhead, g_qtail;            /* under g_mu */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_written, g_failed;         /* atomics: writer -> UI, for the log */

static void csv_field(char *out, size_t n, const char *s)
{
    size_t o = 0;
    if (o < n - 1) out[o++] = '"';
    for (const char *p = s; *p && o < n - 3; p++) {
        if (*p == '"') out[o++] = '"';
        if (*p == '\n' || *p == '\r') { out[o++] = ' '; continue; }
        out[o++] = *p;
    }
    out[o++] = '"';
    out[o] = 0;
}

static void *writer(void *arg)
{
    (void)arg;
    char path[512] = "";
    for (;;) {
        struct line l;
        int have = 0;
        pthread_mutex_lock(&g_mu);
        if (g_qtail != g_qhead) { l = g_q[g_qtail]; g_qtail = (g_qtail + 1) % QLEN; have = 1; }
        pthread_mutex_unlock(&g_mu);
        if (!have) {
            struct timespec ts = { 0, 500 * 1000000L };
            nanosleep(&ts, NULL);
            continue;
        }
        const char *dir = C->data_dir(MOD_ID);
        if (!dir) { __atomic_add_fetch(&g_failed, 1, __ATOMIC_RELAXED); continue; }
        int fresh = 0;
        if (l.new_set || !path[0]) {
            struct tm tm;
            char stamp[32] = "unknown";
            if (localtime_r(&l.wall, &tm)) strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
            snprintf(path, sizeof path, "%s/set-%s.csv", dir, stamp);
            fresh = 1;
        }
        FILE *f = fopen(path, "a");
        if (!f) { __atomic_add_fetch(&g_failed, 1, __ATOMIC_RELAXED); continue; }
        if (fresh) fputs("n,set_time,clock,deck,artist,title,bpm,length,rekordbox_id\n", f);
        fputs(l.text, f);
        fputc('\n', f);
        if (fclose(f) == 0) __atomic_add_fetch(&g_written, 1, __ATOMIC_RELAXED);
        else __atomic_add_fetch(&g_failed, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}

static void enqueue(const char *text, int new_set, time_t wall)
{
    pthread_mutex_lock(&g_mu);
    int next = (g_qhead + 1) % QLEN;
    if (next != g_qtail) {
        snprintf(g_q[g_qhead].text, sizeof g_q[g_qhead].text, "%s", text);
        g_q[g_qhead].new_set = new_set;
        g_q[g_qhead].wall = wall;
        g_qhead = next;
    }
    pthread_mutex_unlock(&g_mu);
}

/* ---- noticing plays (UI thread) ------------------------------------------------------- */
static struct { uint32_t id; int loaded; long play_since; int logged; char title[128]; } g_d[NDECK];
static long last_poll_ms = -POLL_MS;
static long set_start_ms = -1, last_play_ms = -1;
static int g_n, g_writer_ok, g_new_set_asked = 1, g_last_written, g_last_failed;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void new_set(void *user)
{
    (void)user;
    g_new_set_asked = 1;
    if (AZ_HAS(C, banner)) C->banner(MOD_ID, "Set log: the next track starts a new set file", 2500);
}

static void log_track(int d, const az_deck_info *di, long now)
{
    int starts_set = g_new_set_asked || set_start_ms < 0 ||
                     (last_play_ms >= 0 && now - last_play_ms > NEW_SET_IDLE_MS);
    if (starts_set) { set_start_ms = now; g_n = 0; g_new_set_asked = 0; }
    g_n++;
    long st = (now - set_start_ms) / 1000;
    time_t wall = time(NULL);
    struct tm tm;
    char clock_s[16] = "";
    if (localtime_r(&wall, &tm)) strftime(clock_s, sizeof clock_s, "%H:%M:%S", &tm);
    char artist[300], title[300];
    csv_field(artist, sizeof artist, di->artist);
    csv_field(title, sizeof title, di->title);
    char text[800];
    snprintf(text, sizeof text, "%d,%ld:%02ld:%02ld,%s,%d,%s,%s,%.2f,%ld,%u", g_n, st / 3600,
             (st / 60) % 60, st % 60, clock_s, d + 1, artist, title, di->bpm,
             di->len_s > 0 ? (long)(di->len_s + 0.5) : -1L, di->content_id);
    enqueue(text, starts_set, wall);
    if (r_banner >= 0 && C->row_value(r_banner) == 1 && AZ_HAS(C, banner)) {
        char b[112];
        snprintf(b, sizeof b, "Logged #%d: %.90s", g_n, di->title);
        C->banner(MOD_ID, b, 1500);
    }
}

/* A load or eject restarts that deck's 30 s count at once, without waiting for the poll. */
static void deck_event(int deck, int what, void *user)
{
    (void)user;
    if (deck < 0 || deck >= NDECK) return;
    if (what == AZ_DECK_LOADED || what == AZ_DECK_EJECTED) memset(&g_d[deck], 0, sizeof g_d[deck]);
    else if (what == AZ_DECK_STOP) g_d[deck].play_since = 0;
}

static void tick(void)
{
    long now = now_ms();
    if (now - last_poll_ms < POLL_MS) return;
    last_poll_ms = now;

    int w = __atomic_load_n(&g_written, __ATOMIC_RELAXED);
    int f = __atomic_load_n(&g_failed, __ATOMIC_RELAXED);
    if (f != g_last_failed) C->log(MOD_ID, "%d line(s) could not be written (stick out?)", f - g_last_failed);
    g_last_written = w;
    g_last_failed = f;

    int on = r_on < 0 || C->row_value(r_on) == 1;
    for (int d = 0; d < NDECK; d++) {
        az_deck_info di;
        memset(&di, 0, sizeof di);
        di.size = (int)sizeof di;
        if (C->deck_info(d, &di) != 0 || di.loaded != 1) { memset(&g_d[d], 0, sizeof g_d[d]); continue; }
        if (!g_d[d].loaded || di.content_id != g_d[d].id || strcmp(di.title, g_d[d].title) != 0) {
            g_d[d].loaded = 1;
            g_d[d].id = di.content_id;
            snprintf(g_d[d].title, sizeof g_d[d].title, "%s", di.title);
            g_d[d].logged = 0;
            g_d[d].play_since = 0;
        }
        if (di.playing == 1) {
            last_play_ms = now;
            if (!g_d[d].play_since) g_d[d].play_since = now;
            if (on && g_writer_ok && !g_d[d].logged && now - g_d[d].play_since >= PLAYED_MS) {
                g_d[d].logged = 1;
                log_track(d, &di, now);
            }
        } else {
            g_d[d].play_since = 0;
        }
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, data_dir) || !AZ_HAS(C, thread_start)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck, file or thread services (1.2.3 or newer)");
        return 2;
    }
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        az_row_v1 row;
        memset(&row, 0, sizeof row);
        row.size = sizeof row;
        row.key = "on"; row.label = "Set log"; row.kind = AZ_ROW_TOGGLE;
        row.flags = AZ_ROW_DEFAULT_ON;
        row.help = "Writes every track you play (30 s or more) to the USB.";
        r_on = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "on";
        row.key = "banner"; row.label = "Show when logged"; row.kind = AZ_ROW_TOGGLE;
        row.help = "A short note on screen each time a track is logged.";
        r_banner = C->register_row(MOD_ID, &row);
        memset(&row, 0, sizeof row); row.size = sizeof row; row.parent = "on";
        row.key = "newset"; row.label = "Start a new set"; row.kind = AZ_ROW_ACTION;
        row.help = "The next track starts a new file.";
        row.on_activate = new_set;
        C->register_row(MOD_ID, &row);
    } else {
        C->log(MOD_ID, "no Mod Menu rows on this core: always on");
    }
    if (AZ_HAS(C, on_deck_event) && C->on_deck_event(MOD_ID, deck_event, NULL) != 0)
        C->log(MOD_ID, "deck events refused: plays are noticed by polling only");
    if (C->thread_start(MOD_ID, "set-log writer", writer, NULL) == 0) g_writer_ok = 1;
    else C->log(MOD_ID, "thread_start refused: nothing will be written");
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
