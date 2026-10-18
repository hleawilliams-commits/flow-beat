/* SPDX-License-Identifier: GPL-3.0-only
 * cc -std=c11 -D_POSIX_C_SOURCE=200809L -I../../az-mods-sdk/include \
 *    -pthread grouped-panels/tests/test_data.c -lm -o /tmp/grouped-data-test
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "../data.h"

static az_deck_info gp_test_decks[4];
static double gp_test_phases[4];
static az_track gp_test_tracks[2][8];
static int gp_test_ntracks[2], gp_test_scan_calls[2], gp_test_cut;
static int gp_test_invalidate_during_scan, gp_test_threads;
static unsigned gp_test_checks;
#define GP_CHECK(test) do { assert(test); gp_test_checks++; } while (0)

static int gp_test_deck_info(int deck, az_deck_info *out)
{
    if (deck < 0 || deck > 3) return -1;
    *out = gp_test_decks[deck];
    return 0;
}
static int gp_test_grid(int deck, double *ph, double *bar)
{
    *ph = gp_test_phases[deck]; *bar = -1; return 0;
}
static int gp_test_library(const char *id, int stick, int pl,
                           int (*fn)(const az_track *, void *), void *arg)
{
    (void)id; (void)pl;
    gp_test_scan_calls[stick]++;
    for (int i = 0; i < gp_test_ntracks[stick]; i++) if (fn(&gp_test_tracks[stick][i], arg)) break;
    if (gp_test_invalidate_during_scan) {
        gp_deck_event(0, AZ_DECK_LOADED, NULL);
        gp_test_invalidate_during_scan = 0;
    }
    return gp_test_cut ? 1 : 0;
}
static int gp_test_events(const char *id, void (*fn)(int, int, void *), void *arg)
{
    (void)id; (void)fn; (void)arg; return 0;
}
static int gp_test_thread(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    (void)id; (void)name; (void)fn; (void)arg; gp_test_threads++; return 0;
}
static const az_core_v1 gp_test_core = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_core_v1),
    .deck_info = gp_test_deck_info, .deck_grid = gp_test_grid,
    .on_deck_event = gp_test_events, .lib_tracks = gp_test_library, .thread_start = gp_test_thread
};

static az_deck_info gp_test_loaded(uint32_t id)
{
    az_deck_info di = {0};
    di.size = sizeof di; di.loaded = 1; di.content_id = id;
    di.bpm = 120; di.pos_s = 10; di.len_s = -1;
    return di;
}
static az_track gp_test_track(uint32_t id, const char *title, const char *key)
{
    az_track t = {0};
    t.size = sizeof t; t.id = id; t.title = title; t.artist = "Artist"; t.key = key; t.len_s = 240;
    return t;
}
static void gp_test_clear(void)
{
    gp_data_enable(0);
    memset(gp_test_decks, 0, sizeof gp_test_decks);
    memset(gp_test_phases, 0, sizeof gp_test_phases);
    memset(gp_test_ntracks, 0, sizeof gp_test_ntracks);
    memset(gp_test_scan_calls, 0, sizeof gp_test_scan_calls);
    memset(gp_metadata, 0, sizeof gp_metadata);
    memset(gp_phrase, 0, sizeof gp_phrase);
    gp_test_cut = gp_test_invalidate_during_scan = 0;
    gp_data_enable(1);
}
static void gp_test_scan(void)
{
    struct gp_target targets[GP_DECKS];
    gp_get_targets(targets); gp_scan_metadata(targets);
}
static void gp_test_keys(void)
{
    static const char *const minor[] = {"G#m","D#m","A#m","Fm","Cm","Gm","Dm","Am","Em","Bm","Gbm","C#m"};
    static const char *const major[] = {"B","Gb","C#","G#","D#","A#","F","C","G","D","A","E"};
    for (int i = 0; i < 12; i++) {
        char want[8], got[8];
        snprintf(want, sizeof want, "%dA", i + 1); gp_camelot(minor[i], got); GP_CHECK(!strcmp(want, got));
        snprintf(want, sizeof want, "%dB", i + 1); gp_camelot(major[i], got); GP_CHECK(!strcmp(want, got));
        gp_camelot(want, got); GP_CHECK(!strcmp(want, got));
    }
    char got[8];
    gp_camelot("1m", got); GP_CHECK(!strcmp(got, "8A"));
    gp_camelot("12d", got); GP_CHECK(!strcmp(got, "7B"));
    gp_camelot(" c minor ", got); GP_CHECK(!strcmp(got, "5A"));
    gp_camelot("dmaj", got); GP_CHECK(!strcmp(got, "10B"));
    static const char *const bad[] = {"", "13A", "0B", "9C", "noise", "999999999999999999999999A", "Cdim"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) { gp_camelot(bad[i], got); GP_CHECK(!got[0]); }
}
static void gp_test_metadata(void)
{
    gp_test_clear(); gp_readout out;
    GP_CHECK(!gp_data_read(0, &out) && out.remaining_s == -1);
    GP_CHECK(!gp_data_read(-1, &out)); GP_CHECK(!gp_data_read(4, &out)); GP_CHECK(!gp_data_read(0, NULL));
    gp_test_decks[0] = gp_test_loaded(101);
    gp_test_tracks[0][0] = gp_test_track(101, "First Track", "Am"); gp_test_ntracks[0] = 1;
    gp_test_scan();
    GP_CHECK(gp_data_read(0, &out));
    GP_CHECK(out.loaded && !out.playing && out.bpm == 120);
    GP_CHECK(!strcmp(out.title, "First Track") && !strcmp(out.key, "8A"));
    GP_CHECK(out.remaining_s == 230 && !out.phrase_known);
    gp_test_decks[0].loaded = 0;
    GP_CHECK(!gp_data_read(0, &out) && !out.title[0] && !out.key[0] && out.remaining_s == -1);
    gp_test_decks[0].loaded = 1;
    gp_test_decks[0].content_id = 202; /* old metadata must never leak to a different ID */
    GP_CHECK(gp_data_read(0, &out) && !out.title[0] && !out.key[0] && out.remaining_s == -1);
    gp_test_decks[0].content_id = 101;
    gp_deck_event(0, AZ_DECK_LOADED, NULL); /* same ID, another load/USB */
    GP_CHECK(gp_data_read(0, &out) && !out.title[0] && !out.key[0]);
    gp_test_scan();
    GP_CHECK(gp_data_read(0, &out) && !strcmp(out.title, "First Track"));
    pthread_mutex_lock(&gp_meta_mu);
    GP_CHECK(gp_data_read(0, &out) && !out.title[0] && !out.key[0]); /* returns while lock held */
    pthread_mutex_unlock(&gp_meta_mu);
    strcpy(gp_test_decks[0].title, "SDK Title"); strcpy(gp_test_decks[0].key, "F#m");
    gp_test_decks[0].len_s = 45;
    GP_CHECK(gp_data_read(0, &out) && !strcmp(out.title, "SDK Title") && !strcmp(out.key, "11A") && out.remaining_s == 35);
    gp_test_decks[0].key[0] = 0;
    GP_CHECK(gp_data_read(0, &out) && !out.key[0]); /* SDK title disagrees: reject library key */
    gp_test_decks[0].title[0] = 0;
    gp_test_tracks[1][0] = gp_test_track(101, "Other Track", "Am"); gp_test_ntracks[1] = 1;
    gp_test_scan(); GP_CHECK(gp_data_read(0, &out) && !out.title[0] && !out.key[0]);
    gp_test_tracks[1][0] = gp_test_track(101, "First Track", "9A");
    gp_test_scan(); GP_CHECK(gp_data_read(0, &out) && !out.title[0] && !out.key[0]);
    gp_test_tracks[1][0] = gp_test_track(101, "First Track", "8A");
    gp_test_scan(); GP_CHECK(gp_data_read(0, &out) && !strcmp(out.key, "8A")); /* equivalent notation */
    char a[150], b[150]; memset(a, 'x', 149); memset(b, 'x', 149); a[149] = b[149] = 0; b[145] = 'y';
    gp_test_tracks[0][0].title = a; gp_test_tracks[1][0].title = b;
    gp_test_scan(); GP_CHECK(gp_data_read(0, &out) && !out.title[0]); /* clash beyond stored title length */
    gp_test_tracks[0][0].title = gp_test_tracks[1][0].title = "First Track";
    gp_test_cut = 1; gp_test_scan(); GP_CHECK(gp_data_read(0, &out) && !out.key[0]);
    gp_test_cut = 0; gp_test_invalidate_during_scan = 1;
    gp_test_scan(); GP_CHECK(gp_data_read(0, &out) && !out.key[0]); /* raced load invalidates completed scan */
    gp_test_scan(); gp_data_enable(0);
    GP_CHECK(!gp_data_read(0, &out) && !out.loaded && out.remaining_s == -1);
    gp_data_enable(1); GP_CHECK(gp_data_read(0, &out) && !out.key[0]);
    GP_CHECK(sizeof gp_metadata < 4096); /* Four slots, not a whole-library cache. */
}
static void gp_test_scan_limits(void)
{
    gp_test_clear(); struct gp_reader_state reader = {0};
    gp_test_decks[0] = gp_test_loaded(101);
    gp_test_tracks[0][0] = gp_test_track(101, "A", "Am"); gp_test_ntracks[0] = 1;
    gp_reader_once(&reader, 0); gp_reader_once(&reader, 749);
    GP_CHECK(gp_test_scan_calls[0] == 0);
    gp_reader_once(&reader, 750);
    GP_CHECK(gp_test_scan_calls[0] == 1 && gp_test_scan_calls[1] == 1);
    for (long long time = 1000; time < 20000; time += 250) {
        gp_test_decks[0].content_id++;
        gp_reader_once(&reader, time);
    }
    GP_CHECK(gp_test_scan_calls[0] == 1 && gp_test_scan_calls[1] == 1);
    gp_reader_once(&reader, 20749); GP_CHECK(gp_test_scan_calls[0] == 1);
    gp_reader_once(&reader, 20750); GP_CHECK(gp_test_scan_calls[0] == 2);
    gp_reader_once(&reader, 40750); GP_CHECK(gp_test_scan_calls[0] == 3); /* periodic USB refresh */
}
static void gp_test_phrases(void)
{
    struct gp_follow f = {0}; az_deck_info di = gp_test_loaded(101); di.pos_s = 0;
    gp_follow_step(&f, &di, 1, 1, 0, 0); GP_CHECK(!f.known && !f.anchored);
    di.playing = 1; gp_follow_step(&f, &di, 1, 1, 0, 0); GP_CHECK(f.known && f.u == 0);
    for (int step = 1; step <= 1600; step++) {
        di.pos_s = step * .01;
        double beat = step * .02;
        gp_follow_step(&f, &di, 1, 1, beat - floor(beat), 0);
    }
    GP_CHECK(f.known && fabs(f.u - 32) < .000001 && fabs(f.k - 2) < .000001);
    di.pos_s += 4; gp_follow_step(&f, &di, 1, 1, 0, 0);
    GP_CHECK(f.known && fabs(f.u - 40) < .000001); /* measured, on-grid jump */
    di.pos_s += .24; gp_follow_step(&f, &di, 1, 1, 0, 0); GP_CHECK(!f.known); /* off-grid jump */
    gp_follow_step(&f, &di, 1, 1, 0, 1); GP_CHECK(f.known && f.u == 0);
    gp_follow_step(&f, &di, 2, 1, 0, 0); GP_CHECK(f.known && f.k == 0); /* same-ID reload resets measurement */
    di.pos_s += 8; gp_follow_step(&f, &di, 2, 1, 0, 0); GP_CHECK(!f.known); /* long jump before measured speed */
    gp_follow_step(&f, &di, 2, 1, .1, 1); GP_CHECK(f.known && fabs(f.u - .1) < .000001);
    di.loaded = 0; gp_follow_step(&f, &di, 2, 0, 0, 0); GP_CHECK(!f.known && !f.anchored);
    di = gp_test_loaded(202); di.playing = 1; di.pos_s = 1;
    gp_follow_step(&f, &di, 3, 1, .9, 0); GP_CHECK(f.known && fabs(f.u + .1) < .000001);
    gp_follow_step(&f, &di, 3, 1, NAN, 0); GP_CHECK(!f.known);

    gp_test_clear(); struct gp_follow all[4] = {{0}}; gp_readout out;
    for (int deck = 0; deck < 4; deck++) { gp_test_decks[deck] = gp_test_loaded(101 + deck); gp_test_decks[deck].playing = deck != 1; }
    gp_test_phases[2] = .9; gp_counter_once(all);
    for (int deck = 0; deck < 4; deck++) {
        GP_CHECK(gp_data_read(deck, &out));
        GP_CHECK(out.phrase_known == (deck != 1));
        if (deck == 2) GP_CHECK(out.bar == 8 && out.beat == 4);
        else if (deck != 1) GP_CHECK(out.bar == 1 && out.beat == 1);
    }
    gp_data_reset(2); gp_counter_once(all);
    GP_CHECK(gp_data_read(2, &out) && out.phrase_known);
    gp_deck_event(0, AZ_DECK_EJECTED, NULL);
    GP_CHECK(gp_data_read(0, &out) && !out.phrase_known);
    pthread_mutex_lock(&gp_phrase_mu);
    GP_CHECK(gp_data_read(3, &out) && !out.phrase_known);
    pthread_mutex_unlock(&gp_phrase_mu);
}
static void *gp_test_counter_loop(void *arg)
{
    (void)arg; struct gp_follow f[4] = {{0}};
    for (int i = 0; i < 2000; i++) gp_counter_once(f);
    return NULL;
}
static void *gp_test_reader_loop(void *arg)
{
    (void)arg; struct gp_reader_state reader = {0};
    for (int i = 0; i < 2000; i++) gp_reader_once(&reader, i * 250LL);
    return NULL;
}
static void gp_test_concurrency(void)
{
    gp_test_clear();
    for (int d = 0; d < 4; d++) {
        gp_test_decks[d] = gp_test_loaded(d + 1);
        gp_test_decks[d].playing = 1;
        gp_test_tracks[0][d] = gp_test_track(d + 1, "Track", "8A");
    }
    gp_test_ntracks[0] = 4;
    pthread_t counter, reader;
    GP_CHECK(!pthread_create(&counter, NULL, gp_test_counter_loop, NULL));
    GP_CHECK(!pthread_create(&reader, NULL, gp_test_reader_loop, NULL));
    for (int i = 0; i < 4000; i++) {
        gp_readout out; int d = i % 4;
        if (i % 29 == 0) gp_deck_event(d, AZ_DECK_LOADED, NULL);
        if (i % 17 == 0) gp_data_reset(d);
        if (i % 31 == 0) { gp_data_enable(0); gp_data_enable(1); }
        gp_data_read(d, &out);
        GP_CHECK(out.bar >= 0 && out.bar <= 8 && out.beat >= 0 && out.beat <= 4);
    }
    GP_CHECK(!pthread_join(counter, NULL)); GP_CHECK(!pthread_join(reader, NULL));
}
int main(void)
{
    GP_CHECK(gp_data_init(NULL, "grouped-panels") != 0);
    GP_CHECK(!gp_data_init(&gp_test_core, "grouped-panels"));
    GP_CHECK(gp_test_threads == 2);
    gp_test_keys(); gp_test_metadata(); gp_test_scan_limits(); gp_test_phrases(); gp_test_concurrency();
    printf("Grouped panels data: %u checks passed\n", gp_test_checks);
    return 0;
}
