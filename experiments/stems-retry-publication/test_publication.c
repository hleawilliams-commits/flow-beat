/* SPDX-License-Identifier: GPL-3.0-only */
/* Deterministic production-path reproduction, not a replacement implementation.
 * Codec/offset work is represented by worker barriers. The complete production
 * pbread_hook and literal offset gate-release block are compiled unchanged. */
#include <math.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include CONSTANTS_INC
#define BLOCK 2048
#define FRAMES 4096
#include TYPES_INC

/* Fixture storage. Production types above and all functions below are extracted
 * from the pinned upstream revision plus the two preceding experiments. */
static deck_stems g_dk[NDECK];
static _Atomic unsigned g_deck_trackgen[NDECK];
static _Atomic int g_enable = 1, g_gate_hold[NDECK], g_load_stale[NDECK];
static _Atomic int g_stem_loading[NDECK], g_env_ready[NDECK];
static _Atomic unsigned g_stem_present[NDECK], g_ofs_epoch[NDECK];
static _Atomic long g_stem_dec[NDECK][NSTEM];
static pthread_mutex_t g_adv_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic long long g_retry_bytes[NDECK], g_play_obs[NDECK];
static _Atomic unsigned long long g_play_activity[NDECK];
static unsigned long long g_retry_activity[NDECK];
static int g_retry_still[NDECK];
static long g_retry_after[NDECK], g_now;
static long long g_available;
static long ms_now(void) { return g_now; }
static long long mem_available_bytes(void) { return g_available; }
static void env_progress(int deck) { (void)deck; }
static void logln(const char *fmt, ...) { (void)fmt; }
/* Free is deliberately a harmless fixture; stale-load tests exercise drop/gate,
 * not allocator lifetime or loader_thread's real join/free implementation. */
static void free_stems(int d) { g_dk[d].stem_frames = 0; }
static _Atomic int g_sgain_k[NDECK][NSTEM], g_allsum;
static int g_orig_prev[NDECK];
static unsigned g_em_prev[NDECK];

/* pbread's unrelated diagnostics/routing dependencies. TLS is explicitly set
 * to each tested deck, so no native-address traversal is involved. */
static _Atomic unsigned g_pbread_hits;
static _Atomic uintptr_t g_track_vtable, g_target_pbself;
struct pbcand { uintptr_t self; long long lastpos; int advances, seen; };
static struct pbcand g_pbcand[8];
struct pbobs { void *self; Pos32 in, out; long long cnt; float s0L,s0R,sNL,sNR; };
static struct pbobs g_pbring[128];
static _Atomic int g_pbobs_on, g_inject_on;
static _Atomic unsigned g_pbw;
static _Thread_local int g_tls_deck;
static _Atomic long g_pbread_tls_set, g_pbread_tls_unset, g_pbr_in_fgpr, g_pbr_no_fgpr;
static int g_fgpr_depth;
static uintptr_t g_fgpr_ctx;
struct ctxrow { _Atomic long calls; };
static struct ctxrow *ctxrow_of(uintptr_t self) { (void)self; return NULL; }
struct tidrow { _Atomic long calls, tls_set, inj; };
static struct tidrow g_pbr_tid[1];
static struct tidrow *tidrow(struct tidrow *p, int tid) { (void)tid; return p; }
static int az_tid(void) { return 1; }
static int pb_deck_of(uintptr_t self) { (void)self; return -1; }
static _Atomic int g_autokey_on, g_key_manual, g_ak_bits[NDECK], g_ak_anyshift, g_keyratio_bits;
struct pbcls { uintptr_t self; long long pos; float e; };
static struct pbcls g_clsring[128];
static _Atomic unsigned g_clsw;
static _Atomic int g_ofs_state[NDECK];
static _Atomic long long g_ofs_base[NDECK];
static float g_ofs_cap[NDECK][OFS_N];
static unsigned char g_ofs_cov[NDECK][OFS_N];
static _Atomic uintptr_t g_evict_pb[NDECK], g_ra_caller;
static _Atomic long long g_inj_start, g_play_S[NDECK], g_gap_min, g_gap_max, g_gap_absmin;
static _Atomic long long g_inj_cnt, g_inj_adv;
static _Atomic int g_testtone_on;

static Pos32 original_read(void *self, Float2 *dst, const Pos32 *pos, long long cnt) {
    (void)self;
    Pos32 out = *pos;
    long long start;
    memcpy(&start, pos->b + POS_SAMPLE_OFF, 8);
    start += cnt;
    memcpy(out.b + POS_SAMPLE_OFF, &start, 8);
    for (long long i = 0; i < cnt; i++) { dst[i].L = 0.75f; dst[i].R = -0.75f; }
    return out;
}
static Pos32 (*g_orig_pbread)(void *, Float2 *, const Pos32 *, long long) = original_read;
/* Native layout fixture for the extracted hook_pbi prefix. The values are host
 * offsets into these safe objects, not firmware addresses. */
typedef struct { int slot; long *position; } NativeDeck;
typedef struct { int nch, ns; float **channels; } NativeBuffer;
#define OFF_POS_PTR offsetof(NativeDeck, position)
#define OFF_POS 0
#define OFF_NUMCH offsetof(NativeBuffer, nch)
#define OFF_NUMSAMP offsetof(NativeBuffer, ns)
#define OFF_CHANS offsetof(NativeBuffer, channels)
#define MAX_DECKS NDECK
static const int STEM_SLOT[NDECK] = {0, 1, 2, 3};
static int deck_slot(void *self) { return ((NativeDeck *)self)->slot; }
static int ap_deck_of(void *self) { return deck_slot(self) + 1; }
static void deck_track_rt(int deck, void *self) { (void)deck; (void)self; }
static struct tidrow g_pbi_tid[1];
static _Atomic int g_arr_init, g_arr_rank[NDECK], g_arr_next, g_slot_id[NDECK];
static void native_resume_render(void *self, void *buf, void *midi);
static void (*g_orig)(void *, void *, void *) = native_resume_render;
#include PRODUCTION_INC

static int passed, failed;
#define CHECK(label, condition) do { \
    if (condition) passed++; \
    else { failed++; fprintf(stderr, "FAIL: %s\n", label); } \
} while (0)
static float pcm[NDECK][NSTEM][FRAMES * 2];
static Float2 output[BLOCK];
static const char *modes[] = {"all-on unity", "muted vocal", "gain 0.5", "missing drum", "forced sum"};

static void reset(int deck, int mode, int old_original) {
    memset(g_dk, 0, sizeof g_dk);
    g_now = 0; g_available = 900LL << 20;
    atomic_store(&g_target_pbself, 1); /* skip unneeded candidate discovery */
    atomic_store(&g_inject_on, 1);
    atomic_store(&g_enable, 1);
    atomic_store(&g_allsum, mode == 4);
    atomic_store(&g_clsw, 0);
    atomic_store(&g_keyratio_bits, 0); /* disable unrelated content matching */
    g_tls_deck = deck;
    for (int d = 0; d < NDECK; d++) {
        atomic_store(&g_deck_trackgen[d], 10);
        atomic_store(&g_retry_bytes[d], 300LL << 20);
        atomic_store(&g_play_obs[d], -1);
        atomic_store(&g_play_activity[d], 0);
        atomic_store(&g_stem_loading[d], 0);
        atomic_store(&g_gate_hold[d], 0);
        atomic_store(&g_load_stale[d], 0);
        atomic_store(&g_ofs_epoch[d], 0);
        atomic_store(&g_env_ready[d], 0);
        g_retry_still[d] = 0; g_retry_after[d] = 2000; g_retry_activity[d] = 0;
        g_orig_prev[d] = old_original;
        g_em_prev[d] = mode == 1 ? 0x0e : 0x0f;
        atomic_store(&g_dk[d].ctrl, 0x80 | g_em_prev[d]);
        atomic_store(&g_stem_present[d], mode == 3 ? 0x07 : 0x0f);
        for (int k = 0; k < NSTEM; k++) {
            atomic_store(&g_sgain_k[d][k], mode == 2 && k == 1 ? 500 : 1000);
            atomic_store(&g_stem_dec[d][k], 0);
            for (int f = 0; f < FRAMES; f++) {
                pcm[d][k][2*f] = (float)(k+1) * 0.03f;
                pcm[d][k][2*f+1] = -(float)(k+1) * 0.02f;
            }
            g_dk[d].stem[k] = mode == 3 && k == 3 ? NULL : pcm[d][k];
        }
    }
}

static int changed_frames(int deck) {
    g_tls_deck = deck;
    Pos32 pos;
    for (int i = 0; i < 32; i++) pos.b[i] = (unsigned char)(0x80 + i);
    long long start = 64;
    memcpy(pos.b + POS_SAMPLE_OFF, &start, 8);
    Pos32 out = pbread_hook(NULL, output, &pos, BLOCK);
    Pos32 expected = pos;
    long long end = start + BLOCK;
    memcpy(expected.b + POS_SAMPLE_OFF, &end, 8);
    CHECK("entire native return Position retained", memcmp(&out, &expected, sizeof out) == 0);
    int changed = 0;
    for (int i = 0; i < BLOCK; i++)
        changed += output[i].L != 0.75f || output[i].R != -0.75f;
    return changed;
}

static unsigned long long before_render_activity;
static int changed_during_render;
static void native_resume_render(void *self, void *buf, void *midi) {
    (void)buf; (void)midi;
    int deck = deck_slot(self);
    CHECK("first resumed render precedes updated observation", stem_retry_play_activity(deck) == before_render_activity);
    changed_during_render = changed_frames(deck);
    CHECK("pbread still precedes updated observation", stem_retry_play_activity(deck) == before_render_activity);
}

static void delayed_observation(int deck) {
    reset(deck, 1, 0); /* muted vocal: avoid attributing native seam to recovery */
    stem_play_observe(deck, 0);
    before_render_activity = stem_retry_play_activity(deck);
    atomic_store(&g_gate_hold[deck], 1);
    for (int k = 0; k < NSTEM; k++) atomic_store(&g_stem_dec[deck][k], FRAMES);
    advance_stem_frames(deck);
    CHECK("unpublished recovery retains native before delayed render", changed_frames(deck) == 0);
    /* A candidate worker-side unchanged-motion snapshot would still pass here.
     * This comparison is a documented hypothetical guard, not production code. */
    CHECK("candidate snapshot cannot yet detect resumed render", stem_retry_play_activity(deck) == before_render_activity);
    offset_release_block(deck);
    long resumed_position = 882;
    NativeDeck native = {deck, &resumed_position};
    float sample = 0, *channel = &sample;
    NativeBuffer buffer = {1, 1, &channel};
    changed_during_render = -1;
    hook_pbi_observation_prefix(&native, &buffer, NULL);
    CHECK("CONTROL: first resumed native render injected before observation", changed_during_render == BLOCK);
    CHECK("actual hook prefix eventually observes motion", (unsigned)stem_retry_play_activity(deck) != (unsigned)before_render_activity);
    CHECK("actual hook prefix records resumed position", atomic_load(&g_play_obs[deck]) == resumed_position);
    CHECK("actual hook prefix releases TLS deck", g_tls_deck == -1);
    printf("deck %d | delayed observation | first resumed changed=%d before activity publish\n", deck + 1, changed_during_render);
}

/* A condition-variable rendezvous provides deterministic actual worker/main
 * interleaving without unsynchronised reads of upstream's plain stem_frames.
 * It is harness scheduling only: no lock is inserted into production code. */
static pthread_mutex_t step_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t step_cond = PTHREAD_COND_INITIALIZER;
static int step;
static void reach(int n) {
    pthread_mutex_lock(&step_lock);
    step = n;
    pthread_cond_broadcast(&step_cond);
    pthread_mutex_unlock(&step_lock);
}
static void await_step(int n) {
    pthread_mutex_lock(&step_lock);
    while (step < n) pthread_cond_wait(&step_cond, &step_lock);
    pthread_mutex_unlock(&step_lock);
}
static void *decode_worker(void *arg) {
    int deck = *(int *)arg;
    for (int k = 0; k < NSTEM; k++)
        if (g_dk[deck].stem[k]) atomic_store(&g_stem_dec[deck][k], FRAMES);
    advance_stem_frames(deck); /* decode chunks cannot publish under offset hold */
    reach(1);
    await_step(2); /* the DJ resumes (or the flusher replaces/ejects the track) */
    offset_release_block(deck); /* exact production offset completion */
    reach(3);
    return NULL;
}

static void reproduction(int deck, int mode, int old_original, int track_changed) {
    reset(deck, mode, old_original);
    int eligible = 0;
    for (int n = 0; n < 40 && !eligible; n++) {
        stem_play_observe(deck, 0);
        g_now += 200;
        eligible = memory_retry_ready(deck);
    }
    CHECK("fresh stillness permits retry", eligible);
    unsigned generation = atomic_load(&g_deck_trackgen[deck]);
    atomic_store(&g_retry_bytes[deck], 0);
    atomic_store(&g_stem_loading[deck], 1);
    atomic_store(&g_gate_hold[deck], 1);
    step = 0;
    pthread_t worker;
    if (pthread_create(&worker, NULL, decode_worker, &deck) != 0) abort();
    await_step(1);
    CHECK("offset hold preserves empty publication", g_dk[deck].stem_frames == 0);
    CHECK("before offset release native audio remains", changed_frames(deck) == 0);
    unsigned long long before = stem_retry_play_activity(deck);
    stem_play_observe(deck, 441);
    stem_play_observe(deck, 882);
    CHECK("resume motion is observed before publication", (unsigned)stem_retry_play_activity(deck) != (unsigned)before);
    if (track_changed) {
        deck_track_changed(deck);
        drop_stems(deck); /* exact flusher stale-load guard */
    }
    reach(2);
    await_step(3);
    if (pthread_join(worker, NULL) != 0) abort();
    if (track_changed) {
        CHECK("replacement/eject bumps generation", atomic_load(&g_deck_trackgen[deck]) == generation + 1);
        CHECK("stale recovery remains held", atomic_load(&g_gate_hold[deck]) == 1);
        CHECK("stale recovery cannot publish PCM", g_dk[deck].stem_frames == 0);
        CHECK("stale recovery retains native PCM", changed_frames(deck) == 0);
        return;
    }
    CHECK("same track generation is preserved", atomic_load(&g_deck_trackgen[deck]) == generation);
    CHECK("CONTROL: existing code publishes after observed resume", g_dk[deck].stem_frames == FRAMES);
    CHECK("CONTROL: existing offset completion opens the gate", atomic_load(&g_gate_hold[deck]) == 0);
    int first = changed_frames(deck), second = changed_frames(deck);
    int expected_first = mode == 0 ? (old_original ? 0 : XF_ORIG) : BLOCK;
    int expected_second = mode == 0 ? 0 : BLOCK;
    CHECK("first resumed pbread PCM matches actual mode/seam", first == expected_first);
    CHECK("following resumed pbread PCM matches actual mode", second == expected_second);
    printf("deck %d | %-13s | previous=%s | frames=%ld | first changed=%d | next changed=%d\n",
           deck + 1, modes[mode], old_original ? "original" : "sum", g_dk[deck].stem_frames, first, second);
}

int main(void) {
    for (int d = 0; d < NDECK; d++) {
        for (int mode = 0; mode < 5; mode++)
            for (int old_original = 0; old_original < 2; old_original++)
                reproduction(d, mode, old_original, 0);
        reproduction(d, 1, 0, 1);
        delayed_observation(d);
    }
    printf("%d passed, %d failed (existing resume gap intentionally reproduced)\n", passed, failed);
    return failed ? 1 : 0;
}
