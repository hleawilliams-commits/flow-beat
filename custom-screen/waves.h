/* SPDX-License-Identifier: GPL-3.0-only
 * 4-Deck Waveforms for Custom Screen: the `wave` widget (a zoomed, scrolling waveform with the
 * beat grid, bar lines and a centre playhead) and the `overview` widget (the whole track with
 * the played part dimmed). The rekordbox waveform comes from the loaded track's ANLZ files on
 * the USB, found the way flow-beat 0.4.0 finds them: content id -> lib_tracks path -> the PPTH
 * index of PIONEER/USBANLZ. All of that runs as a job on the slow-work thread (jobs.h); the
 * painter only reads what the job published, under a per-deck lock it never waits for.
 */
#ifndef CW_WAVES_H
#define CW_WAVES_H
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "azmod-mod.h"
#include "anlz.h"

#define CW_DECKS 4
#define CW_IDX_MAX 30000
#define CW_RETRY_MS 30000      /* a track with no analysis is looked up again this often */
#define CW_REINDEX_MS 60000    /* a lookup miss rebuilds the stick's index at most this often */

enum { CW_EMPTY = 0, CW_LOADING, CW_MISSING, CW_READY };

static const az_core_v1 *cw_core;
static const char *cw_mod_id;
static int cw_wanted;                 /* atomic: a visible layout has a wave or overview widget */
static unsigned cw_serial;            /* atomic: bumped on every publish, so tick repaints */

/* What the painter sees, one per deck. The job swaps t under mu; the painter only trylocks. */
static struct { pthread_mutex_t mu; cw_track *t; int status; } cw_deck[CW_DECKS] = {
    { PTHREAD_MUTEX_INITIALIZER, NULL, 0 }, { PTHREAD_MUTEX_INITIALIZER, NULL, 0 },
    { PTHREAD_MUTEX_INITIALIZER, NULL, 0 }, { PTHREAD_MUTEX_INITIALIZER, NULL, 0 } };

static long cw_now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static uint32_t cw_fnv(const char *a, const char *b)
{
    uint32_t h = 2166136261u;
    for (const char *p = a; p && *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    h = (h ^ 0xffu) * 16777619u;
    for (const char *p = b; p && *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    return h;
}

/* Log lines from the job wait here for the UI thread (cw_log_flush from tick): the job thread
 * never calls the core's log itself, like data.h's workers. Lines beyond 8 are dropped. */
static pthread_mutex_t cw_log_mu = PTHREAD_MUTEX_INITIALIZER;
static char cw_log_q[8][120];
static int cw_log_n;

static void cw_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void cw_logf(const char *fmt, ...)
{
    pthread_mutex_lock(&cw_log_mu);
    if (cw_log_n < 8) {
        va_list ap; va_start(ap, fmt);
        vsnprintf(cw_log_q[cw_log_n++], sizeof cw_log_q[0], fmt, ap);
        va_end(ap);
    }
    pthread_mutex_unlock(&cw_log_mu);
}

static void cw_log_flush(void)
{
    if (pthread_mutex_trylock(&cw_log_mu) != 0) return;
    for (int i = 0; i < cw_log_n; i++) cw_core->log(cw_mod_id, "%s", cw_log_q[i]);
    cw_log_n = 0;
    pthread_mutex_unlock(&cw_log_mu);
}

static void cw_publish(int deck, cw_track *t, int status)
{
    pthread_mutex_lock(&cw_deck[deck].mu);
    cw_track *old = cw_deck[deck].t;
    cw_deck[deck].t = t;
    __atomic_store_n(&cw_deck[deck].status, status, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&cw_deck[deck].mu);
    if (old != t) cw_track_free(old);
    __atomic_add_fetch(&cw_serial, 1u, __ATOMIC_ACQ_REL);
}

/* ---- the stick's ANLZ index: hash of the PPTH path -> folder Pxxx/xxxxxxxx --------------- */
typedef struct { uint32_t h, leaf; uint16_t top; } cw_ent;
typedef struct { char root[256]; long mtime; long built_ms; cw_ent *e; int n; } cw_index;
static cw_index cw_idx[2];

static int cw_hex(const char *s, int n, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return 0;
        v = v << 4 | (uint32_t)d;
    }
    if (s[n]) return 0;
    *out = v;
    return 1;
}

static void cw_index_build(cw_index *x, const char *root, long now)
{
    char base[320], p1[340], f[400], path8[1024];
    snprintf(base, sizeof base, "%s/PIONEER/USBANLZ", root);
    struct stat st;
    if (stat(base, &st) != 0) return;
    if (!x->e) x->e = (cw_ent *)malloc(CW_IDX_MAX * sizeof *x->e);
    if (!x->e) return;
    snprintf(x->root, sizeof x->root, "%s", root);
    x->mtime = (long)st.st_mtime; x->built_ms = now; x->n = 0;
    DIR *d1 = opendir(base);
    if (!d1) return;
    uint8_t hdr[2048];
    struct dirent *a;
    while ((a = readdir(d1)) && x->n < CW_IDX_MAX) {
        uint32_t top;
        if (a->d_name[0] != 'P' || !cw_hex(a->d_name + 1, 3, &top)) continue;
        snprintf(p1, sizeof p1, "%.300s/P%03X", base, (unsigned)top);
        DIR *d2 = opendir(p1);
        if (!d2) continue;
        struct dirent *b;
        while ((b = readdir(d2)) && x->n < CW_IDX_MAX) {
            uint32_t leaf;
            if (!cw_hex(b->d_name, 8, &leaf)) continue;
            snprintf(f, sizeof f, "%.340s/%08X/ANLZ0000.DAT", p1, (unsigned)leaf);
            FILE *fp = fopen(f, "rbe");
            if (!fp) continue;
            size_t got = fread(hdr, 1, sizeof hdr, fp);
            fclose(fp);
            if (!cw_ppth(hdr, got, path8, (int)sizeof path8)) continue;
            x->e[x->n].h = cw_fnv(path8, "");
            x->e[x->n].top = (uint16_t)top; x->e[x->n].leaf = leaf;
            x->n++;
        }
        closedir(d2);
    }
    closedir(d1);
    cw_logf("waveforms: indexed %d analysed tracks on %.60s", x->n, root);
}

/* lib_tracks callback: the loaded track by id, and title when the deck knows it. */
typedef struct { uint32_t id; const char *title; char path[1024]; int found; } cw_find;
static int cw_find_track(const az_track *t, void *user)
{
    cw_find *f = (cw_find *)user;
    if (!t || t->id != f->id || !t->path) return 0;
    if (f->title && f->title[0] && t->title && strcmp(t->title, f->title)) return 0;
    snprintf(f->path, sizeof f->path, "%s", t->path);
    f->found = 1;
    return 1;
}

static uint8_t *cw_slurp(const char *file, size_t *n)
{
    *n = 0;
    FILE *fp = fopen(file, "rbe");
    if (!fp) return NULL;
    uint8_t *d = NULL;
    if (fseek(fp, 0, SEEK_END) == 0) {
        long size = ftell(fp);
        if (size >= 28 && size <= CW_FILE_MAX && fseek(fp, 0, SEEK_SET) == 0 && (d = (uint8_t *)malloc((size_t)size))) {
            if (fread(d, 1, (size_t)size, fp) == (size_t)size) *n = (size_t)size;
            else { free(d); d = NULL; }
        }
    }
    fclose(fp);
    return d;
}

static int cw_lookup(cw_index *x, uint32_t h, uint16_t *top, uint32_t *leaf)
{
    for (int i = 0; i < x->n; i++) if (x->e[i].h == h) { *top = x->e[i].top; *leaf = x->e[i].leaf; return 1; }
    return 0;
}

/* The analysis of the track on a deck, or NULL. Disk and library work: the job thread only. */
static cw_track *cw_load(const az_deck_info *di, long now)
{
    cw_find f;
    memset(&f, 0, sizeof f);
    f.id = di->content_id; f.title = di->title;
    int stick = -1;
    for (int s = 0; s < 2 && stick < 0; s++)
        if (cw_core->lib_tracks(cw_mod_id, s, 0, cw_find_track, &f) >= 0 && f.found) stick = s;
    if (stick < 0) return NULL;
    /* the mount point: the longest prefix of the audio path that holds PIONEER/USBANLZ */
    char root[256] = "", probe[320];
    for (size_t i = strlen(f.path); i > 1; i--) {
        if (f.path[i] != '/' || i >= sizeof root) continue;
        memcpy(root, f.path, i); root[i] = 0;
        snprintf(probe, sizeof probe, "%s/PIONEER/USBANLZ", root);
        struct stat st;
        if (stat(probe, &st) == 0) break;
        root[0] = 0;
    }
    if (!root[0]) return NULL;
    const char *rel = f.path + strlen(root);
    cw_index *x = &cw_idx[stick];
    struct stat st;
    snprintf(probe, sizeof probe, "%s/PIONEER/USBANLZ", root);
    int stale = !x->e || strcmp(x->root, root) || (stat(probe, &st) == 0 && (long)st.st_mtime != x->mtime);
    if (stale) cw_index_build(x, root, now);
    uint32_t h = cw_fnv(rel, ""), leaf = 0;
    uint16_t top = 0;
    int hit = cw_lookup(x, h, &top, &leaf);
    if (!hit && !stale && now - x->built_ms >= CW_REINDEX_MS) {   /* analysed since the last index */
        cw_index_build(x, root, now);
        hit = cw_lookup(x, h, &top, &leaf);
    }
    if (!hit) return NULL;
    char file[400], path8[1024];
    size_t dn = 0, en = 0;
    snprintf(file, sizeof file, "%s/PIONEER/USBANLZ/P%03X/%08X/ANLZ0000.DAT", root, top, leaf);
    uint8_t *dat = cw_slurp(file, &dn);
    /* a hash can collide: the DAT's own PPTH must name this track */
    if (!dat || !cw_ppth(dat, dn, path8, (int)sizeof path8) || strcmp(path8, rel)) { free(dat); return NULL; }
    snprintf(file, sizeof file, "%s/PIONEER/USBANLZ/P%03X/%08X/ANLZ0000.EXT", root, top, leaf);
    uint8_t *ext = cw_slurp(file, &en);
    cw_track *t = cw_parse(dat, dn, ext, en);
    free(dat); free(ext);
    return t;
}

/* ---- the job: five times a second, one load at most per deck change --------------------- */
static struct { uint32_t sig; long tried_ms; } cw_job_state[CW_DECKS];

static void cw_job(void *user)
{
    (void)user;
    int wanted = __atomic_load_n(&cw_wanted, __ATOMIC_ACQUIRE);
    /* Count job periods instead of reading a clock: no clock service runs on this thread
     * (as in data.h's workers). Retries can only come later than planned, never sooner. */
    static long elapsed;
    long now = elapsed += CS_JOBS_PERIOD_MS;
    for (int d = 0; d < CW_DECKS; d++) {
        az_deck_info di;
        memset(&di, 0, sizeof di); di.size = (int)sizeof di;
        int loaded = cw_core->deck_info(d, &di) == 0 && di.loaded == 1;
        di.title[sizeof di.title - 1] = 0; di.artist[sizeof di.artist - 1] = 0;
        if (!loaded || !wanted) {
            /* nothing to show: free the memory, and look again when it is wanted */
            if (cw_job_state[d].sig || cw_deck[d].t || cw_deck[d].status != CW_EMPTY) cw_publish(d, NULL, CW_EMPTY);
            cw_job_state[d].sig = 0;
            continue;
        }
        uint32_t sig = cw_fnv(di.title, di.artist) ^ di.content_id;
        if (!sig) sig = 1;
        int status = __atomic_load_n(&cw_deck[d].status, __ATOMIC_ACQUIRE);
        int retry = status == CW_MISSING && now - cw_job_state[d].tried_ms >= CW_RETRY_MS;
        if (sig == cw_job_state[d].sig && !retry) continue;
        if (sig != cw_job_state[d].sig) cw_publish(d, NULL, CW_LOADING);
        cw_job_state[d].sig = sig; cw_job_state[d].tried_ms = now;
        cw_track *t = di.content_id ? cw_load(&di, now) : NULL;
        /* the deck may have changed while the files were read */
        az_deck_info again;
        memset(&again, 0, sizeof again); again.size = (int)sizeof again;
        if (cw_core->deck_info(d, &again) != 0 || again.content_id != di.content_id) { cw_track_free(t); cw_job_state[d].sig = 0; continue; }
        if (!t && status != CW_MISSING) cw_logf("waveforms: deck %d: no rekordbox waveform found for id %u", d + 1, (unsigned)di.content_id);
        if (t) cw_logf("waveforms: deck %d: %u entries (%s), %u beats", d + 1, (unsigned)t->n, t->colour ? "colour" : "PWV3", (unsigned)t->nbeats);
        cw_publish(d, t, t ? CW_READY : CW_MISSING);
    }
}

static void cw_set_wanted(int on) { __atomic_store_n(&cw_wanted, !!on, __ATOMIC_RELEASE); }

static int cw_init(const az_core_v1 *core, const char *id)
{
    static int added;   /* an init after the first keeps the job it registered */
    cw_core = core; cw_mod_id = id;
    if (!AZ_HAS(core, lib_tracks) || !AZ_HAS(core, deck_info)) return -1;
    if (added) return 0;
    added = 1;
    return cs_job_add(cw_job, NULL);
}

/* ---- painting (overlay thread) ------------------------------------------------------------ */
typedef struct { uint32_t text, muted, grid, bg; int day; } cw_style;

/* where each deck's playhead was last seen, to scroll smoothly between deck_info updates */
static struct { double pos; long at; int valid; } cw_motion[CW_DECKS];

static double cw_position(int deck, const az_deck_info *di, const cw_track *t, long now)
{
    double pos = di->pos_s;
    if (!isfinite(pos) || pos < 0) return -1;
    if (!cw_motion[deck].valid || pos != cw_motion[deck].pos || di->playing != 1) {
        cw_motion[deck].pos = pos; cw_motion[deck].at = now; cw_motion[deck].valid = 1;
        return pos;
    }
    long dt = now - cw_motion[deck].at;
    if (dt > 250) dt = 250;           /* never run far ahead of what the deck reported */
    double native = cw_tempo_at(t, pos), rate = 1;
    if (native > 0 && isfinite(di->bpm) && di->bpm > 0) rate = di->bpm / native;
    if (rate < 0.5 || rate > 2) rate = 1;
    return pos + dt / 1000.0 * rate;
}

static void cw_fill(az_draw *d, az_rect a, int x, int y, int w, int h, uint32_t c)
{
    int x0 = x > a.x ? x : a.x, y0 = y > a.y ? y : a.y;
    int x1 = x + w < a.x + a.w ? x + w : a.x + a.w, y1 = y + h < a.y + a.h ? y + h : a.y + a.h;
    if (x1 > x0 && y1 > y0) cw_core->fill_rect(d, x0, y0, x1 - x0, y1 - y0, c);
}

static uint32_t cw_dim(uint32_t c, int pct)
{
    uint32_t r = (c >> 16 & 255) * (uint32_t)pct / 100, g = (c >> 8 & 255) * (uint32_t)pct / 100, b = (c & 255) * (uint32_t)pct / 100;
    return r << 16 | g << 8 | b;
}

static void cw_note(az_draw *d, az_rect a, int x, int y, int w, int h, const char *s, uint32_t c)
{
    int px = h >= 30 ? 14 : 10;
    if (cw_core->text_width(s, px) > w - 12) return;
    int ty = y + (h - px * 3 / 2) / 2;
    if (ty < a.y) ty = a.y;
    cw_core->draw_text(d, x + 8, ty, s, px, c);
}

/* Heights are drawn against the track's own peak, as rekordbox does, so a quiet master still
 * fills the strip; never more than 2.5x, so near-silence stays small. */
static int cw_scale(const cw_track *t) { return t->peak < 12 ? 12 : t->peak > 31 ? 31 : t->peak; }

/* Takes the deck's track for painting; 0 = draw the status line instead (in *status). */
static int cw_take(int deck, cw_track **t, int *status)
{
    *t = NULL;
    *status = __atomic_load_n(&cw_deck[deck].status, __ATOMIC_ACQUIRE);
    if (*status != CW_READY) return 0;
    if (pthread_mutex_trylock(&cw_deck[deck].mu) != 0) { *status = CW_LOADING; return 0; }
    *t = cw_deck[deck].t;
    if (!*t) { pthread_mutex_unlock(&cw_deck[deck].mu); *status = CW_LOADING; return 0; }
    return 1;
}

static const char *cw_status_text(int status, int loaded)
{
    if (!loaded) return "No track loaded";
    if (status == CW_MISSING) return "No rekordbox waveform for this track";
    return "Reading waveform...";
}

/* The zoomed waveform: `beats` beats across the widget, the playhead in the middle.
 * mono = 0 draws rekordbox's colours, else that colour. */
static void cw_paint_wave(az_draw *d, az_rect a, int x, int y, int w, int h, int deck, int beats, int grid,
                          uint32_t mono, const cw_style *st)
{
    if (deck < 0 || deck >= CW_DECKS || w < 8 || h < 8) return;
    az_deck_info di;
    memset(&di, 0, sizeof di); di.size = (int)sizeof di;
    int loaded = cw_core->deck_info(deck, &di) == 0 && di.loaded == 1;
    cw_track *t; int status;
    if (!loaded || !cw_take(deck, &t, &status)) {
        cw_fill(d, a, x, y + h / 2, w, 1, st->grid);
        cw_note(d, a, x, y, w, h, cw_status_text(loaded ? __atomic_load_n(&cw_deck[deck].status, __ATOMIC_ACQUIRE) : CW_EMPTY, loaded), st->muted);
        return;
    }
    long now = cw_now_ms();
    double pos = cw_position(deck, &di, t, now);
    if (pos < 0) pos = 0;
    double bpm = cw_tempo_at(t, pos);
    if (bpm <= 0) bpm = isfinite(di.bpm) && di.bpm > 0 ? di.bpm : 120;
    if (beats < 2) beats = 2;
    double span = beats * 60.0 / bpm;                  /* track seconds across the widget */
    double px_s = w / span, ent_px = CW_RATE / px_s;   /* pixels a second, entries a pixel */
    int cx = x + w / 2, cy = y + h / 2, half = h / 2 - 1;
    double left = pos - (w / 2) / px_s;                /* track time at the left edge */

    if (grid && t->nbeats) {
        uint32_t i = cw_beat_at(t, left * 1000.0);
        for (; i < t->nbeats; i++) {
            double bx = x + (t->beat_ms[i] / 1000.0 - left) * px_s;
            if (bx >= x + w) break;
            int bar = t->beat_num[i] == 1;
            cw_fill(d, a, (int)bx, y, 1, h, bar ? cw_dim(st->text, 30) : st->grid);
        }
    }
    /* columns, merged while neither colour nor height changes */
    int run_x = x, run_h = -1; uint32_t run_c = 0;
    for (int c = 0; c <= w; c++) {
        int hh = 0; uint32_t col = 0;
        if (c < w) {
            double e0 = (left + c / px_s) * CW_RATE;
            uint16_t e = cw_peak(t, e0, e0 + (ent_px > 1 ? ent_px : 1));
            hh = cw_height(e) * half / cw_scale(t);
            col = mono ? mono : cw_rgb(e);
        }
        if (c == w || hh != run_h || col != run_c) {
            if (run_h > 0) cw_fill(d, a, run_x, cy - run_h, x + c - run_x, run_h * 2, run_c);
            run_x = x + c; run_h = hh; run_c = col;
        }
    }
    if (grid && t->nbeats) {
        uint32_t i = cw_beat_at(t, left * 1000.0);
        int tick = h >= 60 ? 8 : 4;
        for (; i < t->nbeats; i++) {
            double bx = x + (t->beat_ms[i] / 1000.0 - left) * px_s;
            if (bx >= x + w) break;
            int bar = t->beat_num[i] == 1;
            uint32_t c = bar ? st->text : st->muted;
            int tw = bar ? 2 : 1, th = bar ? tick + tick / 2 : tick;
            cw_fill(d, a, (int)bx, y, tw, th, c);
            cw_fill(d, a, (int)bx, y + h - th, tw, th, c);
        }
    }
    pthread_mutex_unlock(&cw_deck[deck].mu);
    cw_fill(d, a, cx - 1, y, 2, h, st->day ? 0xD0103A : 0xFFFFFF);
}

/* The whole track, bottom-anchored like a CDJ's overview; the played part dimmed. */
static void cw_paint_overview(az_draw *d, az_rect a, int x, int y, int w, int h, int deck, uint32_t mono, const cw_style *st)
{
    if (deck < 0 || deck >= CW_DECKS || w < 8 || h < 4) return;
    az_deck_info di;
    memset(&di, 0, sizeof di); di.size = (int)sizeof di;
    int loaded = cw_core->deck_info(deck, &di) == 0 && di.loaded == 1;
    cw_track *t; int status;
    if (!loaded || !cw_take(deck, &t, &status)) {
        cw_fill(d, a, x, y + h - 1, w, 1, st->grid);
        if (h >= 16) cw_note(d, a, x, y, w, h, cw_status_text(loaded ? __atomic_load_n(&cw_deck[deck].status, __ATOMIC_ACQUIRE) : CW_EMPTY, loaded), st->muted);
        return;
    }
    double len_s = (double)t->n / CW_RATE;
    double pos = cw_position(deck, &di, t, cw_now_ms());
    int px = pos >= 0 && len_s > 0 ? x + (int)(pos / len_s * w) : -1;
    int run_x = x, run_h = -1; uint32_t run_c = 0;
    for (int c = 0; c <= w; c++) {
        int hh = 0; uint32_t col = 0;
        if (c < w) {
            int b0 = c * CW_OV / w, b1 = (c + 1) * CW_OV / w;
            if (b1 <= b0) b1 = b0 + 1;
            uint16_t best = 0;
            for (int b = b0; b < b1 && b < CW_OV; b++) if (cw_height(t->ov[b]) > cw_height(best)) best = t->ov[b];
            hh = cw_height(best) * h / cw_scale(t);
            col = mono ? mono : cw_rgb(best);
            if (px >= 0 && x + c < px) col = cw_dim(col, 45);
        }
        if (c == w || hh != run_h || col != run_c) {
            if (run_h > 0) cw_fill(d, a, run_x, y + h - run_h, x + c - run_x, run_h, run_c);
            run_x = x + c; run_h = hh; run_c = col;
        }
    }
    pthread_mutex_unlock(&cw_deck[deck].mu);
    if (px >= 0) cw_fill(d, a, px - 1, y, 2, h, st->day ? 0xD0103A : 0xFFFFFF);
}

/* For tick: is any of these decks playing with a waveform up? Then scroll at the tick rate. */
static int cw_moving(int deck)
{
    if (deck < 0 || deck >= CW_DECKS || __atomic_load_n(&cw_deck[deck].status, __ATOMIC_ACQUIRE) != CW_READY) return 0;
    az_deck_info di;
    memset(&di, 0, sizeof di); di.size = (int)sizeof di;
    return cw_core->deck_info(deck, &di) == 0 && di.loaded == 1 && di.playing == 1;
}
#endif
