/* SPDX-License-Identifier: GPL-3.0-only
 * Grouped deck readouts. No audio/control writes. Call init last: SDK workers persist.
 * The UI never scans a library or waits for a mutex. Only the four loaded tracks are
 * retained. The 10 ms phrase worker is independent of the slower library reader.
 */
#ifndef GP_DATA_H
#define GP_DATA_H
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "azmod-mod.h"

#define GP_DECKS 4
#define GP_SCAN_INTERVAL_MS 20000
#define GP_SCAN_SETTLE_MS 750

typedef struct gp_readout {
    int loaded, playing;
    int has_stems, stems_on; /* SDK: stems found for the track / stems engine playing it */
    double bpm;
    char key[8], title[128]; /* Original analyzed key in Camelot; never Key Shift. */
    int phrase_known, bar, beat; /* Anchored counter, not song-phrase analysis. */
    double remaining_s; /* Track seconds, -1 when unavailable. */
} gp_readout;

static const az_core_v1 *gp_core;
static const char *gp_mod_id;
static int gp_enabled, gp_reader_ok, gp_counter_ok;
static unsigned gp_epoch[GP_DECKS];
static int gp_reset[GP_DECKS];

/* Each load event invalidates cached metadata, including a reload with the same
 * content ID from the other USB. The SDK does not identify a deck's source stick. */
static void gp_deck_event(int deck, int what, void *user)
{
    (void)user;
    if (deck < 0 || deck >= GP_DECKS) return;
    if (what == AZ_DECK_LOADED || what == AZ_DECK_EJECTED)
        __atomic_add_fetch(&gp_epoch[deck], 1u, __ATOMIC_ACQ_REL);
}

static int gp_snapshot(int deck, az_deck_info *di)
{
    memset(di, 0, sizeof *di);
    di->size = sizeof *di;
    di->len_s = -1;
    di->playing = -1;
    if (!gp_core || deck < 0 || deck >= GP_DECKS || gp_core->deck_info(deck, di) != 0)
        return 0;
    di->title[sizeof di->title - 1] = 0;
    di->key[sizeof di->key - 1] = 0;
    return di->loaded == 1;
}

static void gp_sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

/* Same Camelot/Open Key/classic mapping as key-next-to-bpm. Unrecognized keys
 * remain unknown, instead of making an arbitrary string look like Camelot. */
static void gp_camelot(const char *in, char out[8])
{
    static const char *const gp_minor[12] = {
        "Abm", "Ebm", "Bbm", "Fm", "Cm", "Gm", "Dm", "Am", "Em", "Bm", "F#m", "Dbm"
    };
    static const char *const gp_major[12] = {
        "B", "F#", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E"
    };
    static const char *const gp_minor_alt[12] = {
        "G#m", "D#m", "A#m", "", "", "", "", "", "", "", "Gbm", "C#m"
    };
    static const char *const gp_major_alt[12] = {
        "", "Gb", "C#", "G#", "D#", "A#", "", "", "", "", "", ""
    };
    out[0] = 0;
    if (!in) return;
    char b[24]; size_t n = 0;
    for (; *in; in++) {
        if (*in == ' ') continue;
        if (n == sizeof b - 1) return;
        b[n++] = *in;
    }
    b[n] = 0;
    if (!n) return;
    if (b[0] >= '0' && b[0] <= '9') {
        unsigned num = 0; size_t i = 0;
        while (b[i] >= '0' && b[i] <= '9') {
            num = num * 10u + (unsigned)(b[i++] - '0');
            if (num > 12) return;
        }
        if (!num || !b[i] || b[i + 1]) return;
        if (b[i] == 'A' || b[i] == 'a' || b[i] == 'B' || b[i] == 'b')
            snprintf(out, 8, "%u%c", num, b[i] == 'a' || b[i] == 'A' ? 'A' : 'B');
        else if (b[i] == 'm' || b[i] == 'd')
            snprintf(out, 8, "%u%c", (num + 6) % 12 + 1, b[i] == 'm' ? 'A' : 'B');
        return;
    }
    if (b[0] >= 'a' && b[0] <= 'g') b[0] -= 'a' - 'A';
    if (b[0] < 'A' || b[0] > 'G') return;
    size_t i = 1;
    if (b[i] == '#' || b[i] == 'b') i++;
    int minor = 0;
    if (!b[i] || !strcmp(b + i, "M") || !strcmp(b + i, "maj") || !strcmp(b + i, "major")) minor = 0;
    else if (!strcmp(b + i, "m") || !strcmp(b + i, "min") || !strcmp(b + i, "minor")) minor = 1;
    else return;
    b[i] = 0;
    if (minor) { b[i++] = 'm'; b[i] = 0; }
    for (int k = 0; k < 12; k++) {
        const char *primary = minor ? gp_minor[k] : gp_major[k];
        const char *alt = minor ? gp_minor_alt[k] : gp_major_alt[k];
        if (!strcmp(b, primary) || (*alt && !strcmp(b, alt))) {
            snprintf(out, 8, "%d%c", k + 1, minor ? 'A' : 'B');
            return;
        }
    }
}

struct gp_follow {
    int anchored, known;
    uint32_t id;
    unsigned epoch;
    double u, last_pos, k, k_beats, k_secs;
};
struct gp_phrase_value { uint32_t id; unsigned epoch; int known, beat; };
static struct gp_phrase_value gp_phrase[GP_DECKS];
static pthread_mutex_t gp_phrase_mu = PTHREAD_MUTEX_INITIALIZER;

static double gp_wrap_half(double x)
{
    while (x > 0.5) x -= 1.0;
    while (x <= -0.5) x += 1.0;
    return x;
}

/* Phrase Coach's counter algorithm: first play anchors the nearest beat, then
 * integrate beat phase. Long early jumps or off-grid jumps lose confidence until
 * an explicit reset/new load. It cannot discover a track's real phrase downbeat. */
static void gp_follow_step(struct gp_follow *f, const az_deck_info *di,
                           unsigned epoch, int grid_ok, double ph, int reset)
{
    int ok = di->loaded == 1 && grid_ok && isfinite(ph) && ph >= 0 && ph <= 1 &&
             isfinite(di->pos_s) && di->pos_s >= 0;
    /* one bad reading of the same track (no grid this tick) keeps the count: only a new
     * load or eject starts it again. The next good tick sees the gap as a jump. */
    if (!ok && f->anchored && di->loaded == 1 && f->id == di->content_id && f->epoch == epoch) return;
    if (!ok || f->id != di->content_id || f->epoch != epoch) {
        memset(f, 0, sizeof *f);
        f->id = ok ? di->content_id : 0;
        f->epoch = epoch;
        if (!ok) return;
    }
    if (ph >= 1) ph = 0;
    if (!f->anchored) {
        if (di->playing != 1) return;
        f->u = ph < 0.5 ? ph : ph - 1;
        f->anchored = f->known = 1;
        f->last_pos = di->pos_s;
    } else {
        double dpos = di->pos_s - f->last_pos;
        double dph = gp_wrap_half(ph - (f->u - floor(f->u)));
        double bpm = isfinite(di->bpm) && di->bpm > 0 ? di->bpm : 0;
        double lim = bpm > 30 ? 0.4 * 60 / bpm : 0.2;
        if (lim > 0.2) lim = 0.2;
        if (fabs(dpos) <= lim) {
            f->u += dph;
            if (dpos > 0.002) {
                f->k_beats += dph;
                f->k_secs += dpos;
                if (f->k_secs > 4) f->k = f->k_beats / f->k_secs;
            }
        } else {
            double k = f->k > 0 ? f->k : bpm / 60;
            double guess = f->u + dpos * k;
            double whole = floor(guess - ph + 0.5);
            double miss = fabs(guess - (whole + ph));
            f->u = whole + ph;
            if (miss > 0.33 || k <= 0 || (f->k <= 0 && fabs(dpos * k) > 6)) f->known = 0;
        }
        f->last_pos = di->pos_s;
    }
    if (!isfinite(f->u)) { f->known = 0; f->u = 0; }
    if (reset) { f->u -= floor(f->u + 0.5); f->known = 1; }
}

static void gp_counter_once(struct gp_follow f[GP_DECKS])
{
    for (int deck = 0; deck < GP_DECKS; deck++) {
        az_deck_info di;
        unsigned epoch = __atomic_load_n(&gp_epoch[deck], __ATOMIC_ACQUIRE);
        int loaded = gp_snapshot(deck, &di);
        double ph = -1, bar = -1;
        int grid_ok = loaded && gp_core->deck_grid(deck, &ph, &bar) == 0;
        if (!loaded) di.loaded = 0;
        int reset = __atomic_exchange_n(&gp_reset[deck], 0, __ATOMIC_ACQ_REL);
        gp_follow_step(&f[deck], &di, epoch, grid_ok, ph, reset);
        struct gp_phrase_value value = { f[deck].id, epoch, f[deck].known, 0 };
        if (value.known) {
            double beat = fmod(floor(f[deck].u), 64.0);   /* 16 bars */
            value.beat = (int)(beat < 0 ? beat + 64 : beat);
        }
        pthread_mutex_lock(&gp_phrase_mu);
        gp_phrase[deck] = value;
        pthread_mutex_unlock(&gp_phrase_mu);
    }
}

static void *gp_counter_worker(void *arg)
{
    (void)arg;
    struct gp_follow f[GP_DECKS];
    memset(f, 0, sizeof f);
    for (;;) {
        if (!__atomic_load_n(&gp_enabled, __ATOMIC_ACQUIRE)) { gp_sleep_ms(250); continue; }
        gp_counter_once(f);
        gp_sleep_ms(10);
    }
    return NULL;
}

struct gp_target { int loaded; uint32_t id; unsigned epoch; };
struct gp_meta {
    struct gp_target target;
    int found, clash;
    char title[128], key[8];
    uint64_t title_hash, artist_hash;
    double len_s;
};
static struct gp_meta gp_metadata[GP_DECKS];
static pthread_mutex_t gp_meta_mu = PTHREAD_MUTEX_INITIALIZER;

static int gp_same_target(struct gp_target a, struct gp_target b)
{
    return a.loaded == b.loaded && a.id == b.id && a.epoch == b.epoch;
}

static uint64_t gp_string_hash(const char *s)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    if (s) for (; *s; s++) { hash ^= (unsigned char)*s; hash *= UINT64_C(1099511628211); }
    return hash;
}

static void gp_get_targets(struct gp_target target[GP_DECKS])
{
    for (int deck = 0; deck < GP_DECKS; deck++) {
        az_deck_info di;
        target[deck].epoch = __atomic_load_n(&gp_epoch[deck], __ATOMIC_ACQUIRE);
        target[deck].loaded = gp_snapshot(deck, &di);
        target[deck].id = target[deck].loaded ? di.content_id : 0;
    }
}

static int gp_collect_track(const az_track *track, void *user)
{
    struct gp_meta *result = user;
    if (!__atomic_load_n(&gp_enabled, __ATOMIC_ACQUIRE)) return 1;
    if (!track) return 0;
    for (int deck = 0; deck < GP_DECKS; deck++) {
        struct gp_meta *m = &result[deck];
        if (!m->target.loaded || !m->target.id || m->target.id != track->id) continue;
        char key[8]; gp_camelot(track->key, key);
        uint64_t title_hash = gp_string_hash(track->title);
        uint64_t artist_hash = gp_string_hash(track->artist);
        if (m->found) {
            if (title_hash != m->title_hash || artist_hash != m->artist_hash || strcmp(m->key, key))
                m->clash = 1;
        } else {
            m->found = 1;
            m->title_hash = title_hash;
            m->artist_hash = artist_hash;
            snprintf(m->title, sizeof m->title, "%s", track->title ? track->title : "");
            snprintf(m->key, sizeof m->key, "%s", key);
            m->len_s = isfinite(track->len_s) && track->len_s > 0 ? track->len_s : -1;
        }
    }
    return 0;
}

static void gp_scan_metadata(const struct gp_target target[GP_DECKS])
{
    struct gp_meta result[GP_DECKS];
    memset(result, 0, sizeof result);
    for (int deck = 0; deck < GP_DECKS; deck++) result[deck].target = target[deck];
    int complete = 1;
    for (int stick = 0; stick < 2; stick++) {
        if (!__atomic_load_n(&gp_enabled, __ATOMIC_ACQUIRE)) return;
        int r = gp_core->lib_tracks(gp_mod_id, stick, 0, gp_collect_track, result);
        /* A truncated list could hide a conflicting ID. -1 also means no USB. */
        if (r == 1) complete = 0;
    }
    if (!complete) for (int deck = 0; deck < GP_DECKS; deck++) result[deck].clash = 1;
    pthread_mutex_lock(&gp_meta_mu);
    memcpy(gp_metadata, result, sizeof result);
    pthread_mutex_unlock(&gp_meta_mu);
}

struct gp_reader_state {
    struct gp_target target[GP_DECKS];
    long long last_scan, changed_at;
    int have_scan, have_targets;
};

/* All targets settle for 750 ms before a scan. Even continuous track changes
 * cannot cause more than one scan per stick per 20 seconds. Periodic refreshes
 * also clear missing metadata when a USB is removed. */
static void gp_reader_once(struct gp_reader_state *state, long long now)
{
    struct gp_target target[GP_DECKS];
    gp_get_targets(target);
    int changed = !state->have_targets, any = 0;
    for (int deck = 0; deck < GP_DECKS; deck++) {
        if (!gp_same_target(target[deck], state->target[deck])) changed = 1;
        if (target[deck].loaded && target[deck].id) any = 1;
    }
    if (changed) {
        memcpy(state->target, target, sizeof target);
        state->have_targets = 1;
        state->changed_at = now;
    }
    if (!any || now - state->changed_at < GP_SCAN_SETTLE_MS ||
        (state->have_scan && now - state->last_scan < GP_SCAN_INTERVAL_MS)) return;
    state->last_scan = now;
    state->have_scan = 1;
    gp_scan_metadata(target);
}

static void *gp_reader_worker(void *arg)
{
    (void)arg;
    struct gp_reader_state state;
    memset(&state, 0, sizeof state);
    long long elapsed_ms = 0;
    for (;;) {
        if (__atomic_load_n(&gp_enabled, __ATOMIC_ACQUIRE)) gp_reader_once(&state, elapsed_ms);
        gp_sleep_ms(250);
        /* Count completed sleeps, not scan duration: the real interval can only
         * be longer than 20 seconds. No clock service runs on this worker. */
        elapsed_ms += 250;
    }
    return NULL;
}

static void gp_data_enable(int enabled)
{
    enabled = !!enabled;
    if (__atomic_exchange_n(&gp_enabled, enabled, __ATOMIC_ACQ_REL) == enabled) return;
    for (int deck = 0; deck < GP_DECKS; deck++) {
        __atomic_add_fetch(&gp_epoch[deck], 1u, __ATOMIC_ACQ_REL);
        __atomic_store_n(&gp_reset[deck], 0, __ATOMIC_RELEASE);
    }
}

static void gp_data_reset(int deck)
{
    if (deck >= 0 && deck < GP_DECKS && __atomic_load_n(&gp_enabled, __ATOMIC_ACQUIRE))
        __atomic_store_n(&gp_reset[deck], 1, __ATOMIC_RELEASE);
}

/* No library work, allocation or blocking lock on the UI. A contended lock yields
 * unknown for that part of this read rather than a stale previous-track label. */
static int gp_data_read(int deck, gp_readout *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    out->remaining_s = -1;
    if (deck < 0 || deck >= GP_DECKS || !__atomic_load_n(&gp_enabled, __ATOMIC_ACQUIRE)) return 0;
    unsigned epoch = __atomic_load_n(&gp_epoch[deck], __ATOMIC_ACQUIRE);
    az_deck_info di;
    if (!gp_snapshot(deck, &di)) return 0;
    out->loaded = 1;
    out->playing = di.playing == 1;
    out->has_stems = di.has_stems == 1;
    out->stems_on = di.stems_on == 1;
    out->bpm = isfinite(di.bpm) && di.bpm > 0 ? di.bpm : 0;
    snprintf(out->title, sizeof out->title, "%s", di.title);
    gp_camelot(di.key, out->key);
    double len_s = di.len_s;
    struct gp_target target = { 1, di.content_id, epoch };
    if (gp_reader_ok && pthread_mutex_trylock(&gp_meta_mu) == 0) {
        const struct gp_meta *m = &gp_metadata[deck];
        if (gp_same_target(target, m->target) && m->found && !m->clash &&
            (!di.title[0] || m->title_hash == gp_string_hash(di.title))) {
            if (!out->title[0]) snprintf(out->title, sizeof out->title, "%s", m->title);
            if (!out->key[0]) snprintf(out->key, sizeof out->key, "%s", m->key);
            if (!isfinite(len_s) || len_s <= 0) len_s = m->len_s;
        }
        pthread_mutex_unlock(&gp_meta_mu);
    }
    if (isfinite(len_s) && len_s > 0 && isfinite(di.pos_s) && di.pos_s >= 0)
        out->remaining_s = fmax(0, len_s - di.pos_s);
    /* a real lock: the worker holds it for a struct copy, and a missed trylock showed "?" */
    if (gp_counter_ok && pthread_mutex_lock(&gp_phrase_mu) == 0) {
        struct gp_phrase_value p = gp_phrase[deck];
        pthread_mutex_unlock(&gp_phrase_mu);
        if (p.id == di.content_id && p.epoch == epoch && p.known) {
            out->phrase_known = 1;
            out->bar = p.beat / 4 + 1;
            out->beat = p.beat % 4 + 1;
        }
    }
    /* A load/eject racing this read must not publish any prior track's fields. */
    if (__atomic_load_n(&gp_epoch[deck], __ATOMIC_ACQUIRE) != epoch) {
        memset(out, 0, sizeof *out);
        out->remaining_s = -1;
        return 0;
    }
    return 1;
}

/* Start disabled; the owner's tick explicitly enables data while the mod is on.
 * A refused metadata worker leaves SDK-only title/key available. Registering the
 * event listener is required before enabling library fallback for same-ID loads. */
static int gp_data_init(const az_core_v1 *core, const char *id)
{
    if (!core || !id || !AZ_HAS(core, deck_info) || !AZ_HAS(core, deck_grid) ||
        !AZ_HAS(core, thread_start)) return 1;
    gp_core = core;
    gp_mod_id = id;
    int events_ok = AZ_HAS(core, on_deck_event) && core->on_deck_event(id, gp_deck_event, NULL) == 0;
    if (core->thread_start(id, "screen phrase", gp_counter_worker, NULL) != 0) return 2;
    gp_counter_ok = 1;
    if (events_ok && AZ_HAS(core, lib_tracks) &&
        core->thread_start(id, "screen metadata", gp_reader_worker, NULL) == 0) gp_reader_ok = 1;
    return 0;
}
#endif
