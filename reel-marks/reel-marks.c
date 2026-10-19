/* SPDX-License-Identifier: GPL-3.0-only
 * Reel Marks: SHIFT + TAP marks a moment worth a clip. Each mark is appended to
 * MODS/community-mods/reel-marks/data/marks-<date>.csv on the USB with the time, the
 * minutes into the set and every playing deck's track, position and BPM. A Mac script
 * (tools/reel_cut.py) cuts clips around the marks from a phone video or REC recording.
 *
 * The combo is swallowed: TAP doesn't reach the deck while SHIFT is held (TAP alone still
 * works). Nothing else is changed.
 *
 * Threads: two input filters (ERP_IN for SHIFT, MIXER_IN for TAP) on bus threads set
 * words; the UI thread snapshots the decks and queues the line; a worker appends it to the
 * file (disk work never on the UI or bus threads).
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "azmod-mod.h"

#define MOD_ID "reel-marks"
static const az_core_v1 *C;

static int rm_shift;                 /* __atomic: SHIFT held on either deck side */
static unsigned rm_presses;          /* __atomic: SHIFT+TAP presses seen */
static int rm_tap_down;              /* bus-only: TAP held (edge detection) */
static char rm_data[320];            /* data_dir, from init */

#define RM_Q 16
static pthread_mutex_t rm_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rm_cv = PTHREAD_COND_INITIALIZER;
static char rm_q[RM_Q][900];
static int rm_qn;
static int rm_marks;                 /* UI: marks taken this session */
static int rm_row_on = -1;

/* ---- bus threads ---- */
static int rm_erp(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (bus != AZ_BUS_ERP_IN || !f || len < 0x12) return AZ_FRAME_OK;
    __atomic_store_n(&rm_shift, ((f[0x09] | f[0x11]) & 0x80) != 0, __ATOMIC_RELAXED);
    return AZ_FRAME_OK;
}
static int rm_mixer(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (bus != AZ_BUS_MIXER_IN || !f || len < 0x20) return AZ_FRAME_OK;
    int tap = (f[0x14] & 0x80) != 0;
    if (tap && __atomic_load_n(&rm_shift, __ATOMIC_RELAXED)) {
        if (!rm_tap_down) __atomic_add_fetch(&rm_presses, 1u, __ATOMIC_RELEASE);
        rm_tap_down = 1;
        f[0x14] &= (unsigned char)~0x80;              /* the deck never sees SHIFT+TAP */
        return AZ_FRAME_OK;
    }
    if (!tap) rm_tap_down = 0;
    return AZ_FRAME_OK;
}

/* ---- worker: append lines ---- */
static void *rm_worker(void *arg)
{
    (void)arg;
    for (;;) {
        char line[900], path[400];
        pthread_mutex_lock(&rm_mu);
        while (!rm_qn) pthread_cond_wait(&rm_cv, &rm_mu);
        snprintf(line, sizeof line, "%s", rm_q[0]);
        memmove(rm_q, rm_q + 1, sizeof rm_q[0] * (size_t)(rm_qn - 1));
        rm_qn--;
        pthread_mutex_unlock(&rm_mu);
        /* line starts with the file date "YYYYMMDD|" */
        char date[9]; memcpy(date, line, 8); date[8] = 0;
        snprintf(path, sizeof path, "%s/marks-%s.csv", rm_data, date);
        FILE *f = fopen(path, "ae");
        if (!f) continue;
        if (ftell(f) == 0)
            fputs("utc_epoch,local_time,set_minutes,deck,title,artist,content_id,position_s,bpm,playing\n", f);
        fputs(line + 9, f);
        fclose(f);
    }
    return NULL;
}

/* ---- UI ---- */
static void rm_csv(char *out, size_t n, const char *s)   /* quote a CSV field */
{
    size_t o = 0;
    if (o + 1 < n) out[o++] = '"';
    for (; s && *s && o + 3 < n; s++) { if (*s == '"') out[o++] = '"'; if (*s != '\n' && *s != '\r') out[o++] = *s; }
    if (o + 1 < n) out[o++] = '"';
    out[o] = 0;
}
static unsigned rm_seen;
static long rm_set_start;            /* first time anything played this session */
static void rm_take(void)
{
    time_t now = time(NULL);
    struct tm lt; localtime_r(&now, &lt);   /* the deck's own clock/zone; utc_epoch is authoritative */
    char date[16], ltime[16];
    strftime(date, sizeof date, "%Y%m%d", &lt);
    strftime(ltime, sizeof ltime, "%H:%M:%S", &lt);
    double set_min = rm_set_start ? (now - rm_set_start) / 60.0 : 0;
    char block[900]; size_t o = 0;
    char names[120] = "";
    o += (size_t)snprintf(block + o, sizeof block - o, "%s|", date);
    int rows = 0;
    for (int d = 0; d < 4; d++) {
        az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di; di.playing = -1;
        if (C->deck_info(d, &di) != 0 || di.loaded != 1) continue;
        di.title[sizeof di.title - 1] = di.artist[sizeof di.artist - 1] = 0;
        char t[200], a[200];
        rm_csv(t, sizeof t, di.title); rm_csv(a, sizeof a, di.artist);
        int w = snprintf(block + o, sizeof block - o, "%ld,%s,%.2f,%d,%s,%s,%u,%.1f,%.2f,%d\n",
                         (long)now, ltime, set_min, d + 1, t, a, di.content_id, di.pos_s, di.bpm, di.playing == 1);
        if (w < 0 || (size_t)w >= sizeof block - o) break;
        o += (size_t)w; rows++;
        if (di.playing == 1) {
            size_t l = strlen(names);
            snprintf(names + l, sizeof names - l, "%s%.40s", l ? " / " : "", di.title);
        }
    }
    if (!rows) o += (size_t)snprintf(block + o, sizeof block - o, "%ld,%s,%.2f,0,\"\",\"\",0,0,0,0\n", (long)now, ltime, set_min);
    pthread_mutex_lock(&rm_mu);
    if (rm_qn < RM_Q) { snprintf(rm_q[rm_qn++], sizeof rm_q[0], "%s", block); pthread_cond_signal(&rm_cv); }
    pthread_mutex_unlock(&rm_mu);
    rm_marks++;
    if (AZ_HAS(C, banner)) {
        char b[110];
        snprintf(b, sizeof b, "Marked #%d%s%.80s", rm_marks, names[0] ? " - " : "", names);
        C->banner(MOD_ID, b, 1500);
    }
}
static void rm_tick(void)
{
    int on = rm_row_on < 0 || C->row_value(rm_row_on) == 1;
    if (!rm_set_start)
        for (int d = 0; d < 4; d++) {
            az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
            if (C->deck_info(d, &di) == 0 && di.loaded == 1 && di.playing == 1) { rm_set_start = (long)time(NULL); break; }
        }
    unsigned p = __atomic_load_n(&rm_presses, __ATOMIC_ACQUIRE);
    if (p != rm_seen) { rm_seen = p; if (on && rm_data[0]) rm_take(); }
}
static void rm_draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->draw_header(d, "Reel Marks");
    int y = a.y + (AZ_HAS(C, row_height) ? C->row_height(d) : 48) + 14, x = a.x + 20;
    char l[160];
    const char *lines[4] = {"Press SHIFT + TAP (Beat FX) to mark a moment worth a clip.", l,
                            "Marks are saved on the USB: MODS/community-mods/reel-marks/data/",
                            "On the Mac: python3 tools/reel_cut.py marks.csv video.mov"};
    snprintf(l, sizeof l, "%d marks this session%s", rm_marks, rm_data[0] ? "" : "  (USB not found: marks can't be saved)");
    for (int i = 0; i < 4 && y + 30 <= a.y + a.h; i++) {
        int px = 20;
        while (px > 10 && AZ_HAS(C, text_width) && C->text_width(lines[i], px) > a.w - 40) px -= 2;
        if (AZ_HAS(C, text_width) && C->text_width(lines[i], px) > a.w - 40) continue;
        C->draw_text(d, x, y, lines[i], px, C->color(i == 1 ? AZ_COL_HI : AZ_COL_TEXT));
        y += 34;
    }
}
static int rm_input(const az_event *ev, void *user) { (void)user; return ev && ev->kind == AZ_EV_BACK ? 0 : 1; }

static int rm_init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    if (__atomic_load_n(&C, __ATOMIC_ACQUIRE) != core) __atomic_store_n(&C, core, __ATOMIC_RELEASE);
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, thread_start) || !AZ_HAS(C, data_dir)) return 2;
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        const az_row_v1 on = {.size = sizeof on, .key = "on", .label = "Reel marks", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "SHIFT + TAP marks a moment: time and the playing tracks go to a file on the USB, for cutting reel clips on the Mac. TAP alone works as usual."};
        rm_row_on = C->register_row(MOD_ID, &on);
    }
    if (AZ_HAS(C, register_screen)) {
        static const az_screen_v1 scr = {.size = sizeof scr, .on_draw = rm_draw, .on_input = rm_input};
        C->register_screen(MOD_ID, &scr);
    }
    static int started;
    if (!started) {
        const char *dd = C->data_dir(MOD_ID);
        if (dd) snprintf(rm_data, sizeof rm_data, "%s", dd);
        if (AZ_HAS(C, frame_filter)) {
            C->frame_filter(MOD_ID, 1u << AZ_BUS_ERP_IN, rm_erp, NULL);
            C->frame_filter(MOD_ID, 1u << AZ_BUS_MIXER_IN, rm_mixer, NULL);
        }
        if (C->thread_start(MOD_ID, "reel-marks", rm_worker, NULL) != 0) return 3;
        started = 1;
    }
    return 0;
}
static const char *const TAGS[] = {"any", NULL};
static const az_mod_v1 MOD = {.abi = AZ_MOD_ABI, .size = sizeof MOD, .id = MOD_ID, .fw_tags = TAGS,
                              .init = rm_init, .tick = rm_tick};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
