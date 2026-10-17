/* SPDX-License-Identifier: GPL-3.0-only
 * Real UI/frame threads, using the registered callback. Scheduling gates are
 * compiled only into this host test, never the shared library. */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int test_clock(clockid_t id, struct timespec *t);
static void before_claim(uint64_t token);
static void after_claim(uint64_t token);
#define GP_SHORTCUT_GETTIME test_clock
#define GP_SHORTCUT_BEFORE_CLAIM(token) before_claim(token)
#define GP_SHORTCUT_AFTER_CLAIM(token) after_claim(token)
#include "../shortcut.h"

static atomic_long mock_now;
static atomic_int clock_calls, view_calls, stop_worker, worker_calls;
static int checks, fallbacks, success;
static az_frame_fn registered;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static int gate_stage, gate_reached, gate_released;
#define CHECK(x) do { assert(x); checks++; } while (0)
static int test_clock(clockid_t id, struct timespec *t)
{
    assert(id == CLOCK_MONOTONIC);
    atomic_fetch_add(&clock_calls, 1);
    long now = atomic_load(&mock_now);
    if (now < 0) return -1;
    t->tv_sec = now / 1000; t->tv_nsec = (now % 1000) * 1000000;
    return 0;
}
static int view(void) { atomic_fetch_add(&view_calls, 1); return AZ_VIEW_DECK; }
static void banner(const char *id, const char *s, int ms)
{
    assert(!strcmp(id, "mods-shortcut"));
    if (!strcmp(s, "MODS timed out. Use SOURCE > MOD MENU")) { assert(ms == 3000); fallbacks++; }
    else { assert(!strcmp(s, "Tap MOD MENU in the source list")); success++; }
}
static int register_filter(const char *id, unsigned mask, az_frame_fn fn, void *user)
{
    assert(!strcmp(id, "mods-shortcut") && mask == (1u << AZ_BUS_MIXER_IN) && !user);
    registered = fn; return 0;
}
static az_core_v1 core = {.size = sizeof core, .view = view, .banner = banner, .frame_filter = register_filter};
static uint64_t pending(void) { return __atomic_load_n(&gp_shortcut_request_word, __ATOMIC_ACQUIRE); }
static void gate(int stage)
{
    pthread_mutex_lock(&gate_lock);
    if (gate_stage == stage) {
        gate_reached = 1; pthread_cond_broadcast(&gate_cond);
        while (!gate_released) pthread_cond_wait(&gate_cond, &gate_lock);
    }
    pthread_mutex_unlock(&gate_lock);
}
static void before_claim(uint64_t token) { assert(token); gate(1); }
static void after_claim(uint64_t token) { assert(token); gate(2); }
static void arm_gate(int stage)
{
    pthread_mutex_lock(&gate_lock);
    gate_stage = stage; gate_reached = gate_released = 0;
    pthread_mutex_unlock(&gate_lock);
}
static void await_gate(void)
{
    pthread_mutex_lock(&gate_lock);
    while (!gate_reached) pthread_cond_wait(&gate_cond, &gate_lock);
    pthread_mutex_unlock(&gate_lock);
}
static void release_gate(void)
{
    pthread_mutex_lock(&gate_lock); gate_released = 1; pthread_cond_broadcast(&gate_cond);
    pthread_mutex_unlock(&gate_lock);
}
static void reset(void)
{
    gp_shortcut_request_word = gp_shortcut_completion = gp_shortcut_pulse_token = 0;
    gp_shortcut_until = gp_shortcut_notice_token = gp_shortcut_notice_until = 0;
    gp_shortcut_notice = gp_shortcut_knob_known = 0; gp_shortcut_last_request = -10000;
    atomic_store(&clock_calls, 0); atomic_store(&view_calls, 0); atomic_store(&mock_now, 100);
    fallbacks = success = 0; arm_gate(0);
}
static void idle(unsigned char *f) { memset(f, 0, 152); f[6] = 8; f[34] = 7; }
static void *one_frame(void *arg)
{
    unsigned char *f = arg;
    assert(registered(AZ_BUS_MIXER_IN, f, 152, NULL) == AZ_FRAME_OK);
    return NULL;
}
static void *many_frames(void *arg)
{
    (void)arg; unsigned char f[152];
    while (!atomic_load(&stop_worker)) {
        idle(f); registered(AZ_BUS_MIXER_IN, f, 152, NULL);
        /* The filter must never mutate anything but the two documented bytes. */
        assert(f[34] == 7 && f[31] == 0 && f[33] == 0);
        assert((f[32] == 0 && f[6] == 8) || (f[32] == 128 && f[6] == 0));
        atomic_fetch_add(&worker_calls, 1); sched_yield();
    }
    return NULL;
}
int main(void)
{
    CHECK(gp_shortcut_init(&core, "mods-shortcut"));
    unsigned char f[152], before[152]; pthread_t thread;
    reset(); idle(f);
    for (int i = 0; i < 10000; i++) { f[34] = (unsigned char)i; registered(AZ_BUS_MIXER_IN, f, 152, NULL); }
    registered(AZ_BUS_DECK_IN, f, 152, NULL); registered(AZ_BUS_MIXER_IN, f, 97, NULL);
    registered(AZ_BUS_MIXER_IN, NULL, 152, NULL);
    CHECK(atomic_load(&clock_calls) == 0 && atomic_load(&view_calls) == 0);
    CHECK(gp_shortcut_knob_known && gp_shortcut_knob == (unsigned char)9999);
    CHECK(gp_shortcut_request(100));
    int views_after_request = atomic_load(&view_calls);
    registered(AZ_BUS_DECK_IN, f, 152, NULL); registered(AZ_BUS_MIXER_IN, f, 97, NULL);
    registered(AZ_BUS_MIXER_IN, NULL, 152, NULL);
    CHECK(atomic_load(&clock_calls) == 0 && atomic_load(&view_calls) == views_after_request);
    f[34]++; memcpy(before, f, 152);
    registered(AZ_BUS_MIXER_IN, f, 152, NULL);
    CHECK(!pending() && !memcmp(f, before, 152)); CHECK(atomic_load(&clock_calls) == 1);

    /* Callback loaded old token but did not claim: timeout/new request wins. */
    reset(); CHECK(gp_shortcut_request(100)); arm_gate(1); idle(f); memcpy(before, f, 152);
    CHECK(!pthread_create(&thread, NULL, one_frame, f)); await_gate();
    gp_shortcut_tick_at(1101); CHECK(!pending() && fallbacks == 1);
    CHECK(gp_shortcut_request(1200)); uint64_t newer = pending();
    atomic_store(&mock_now, 1201); release_gate(); CHECK(!pthread_join(thread, NULL));
    CHECK(pending() == newer && !memcmp(f, before, 152));
    CHECK(atomic_load(&clock_calls) == 0); /* stale claim never reads time */
    gp_shortcut_tick_at(1201); CHECK(fallbacks == 1 && gp_shortcut_notice);
    arm_gate(0); registered(AZ_BUS_MIXER_IN, f, 152, NULL);
    CHECK(f[32] == 128 && gp_shortcut_pulse_token == newer);

    /* Callback owns BUSY: UI may report timeout, but never steal ownership. */
    reset(); CHECK(gp_shortcut_request(100)); arm_gate(2); idle(f); memcpy(before, f, 152);
    CHECK(!pthread_create(&thread, NULL, one_frame, f)); await_gate();
    uint64_t claimed = pending(); CHECK(claimed & GP_SHORTCUT_BUSY);
    gp_shortcut_tick_at(1101); CHECK(pending() == claimed && fallbacks == 1);
    CHECK(!gp_shortcut_request(1200));
    atomic_store(&mock_now, 1201); release_gate(); CHECK(!pthread_join(thread, NULL));
    CHECK(!pending() && !memcmp(f, before, 152)); /* fresh post-claim time expires it */
    gp_shortcut_tick_at(1202); CHECK(fallbacks == 1); CHECK(gp_shortcut_request(1300));
    gp_shortcut_tick_at(1301); CHECK(gp_shortcut_notice && fallbacks == 1); /* old completion ignored */

    /* Official core stops invoking a quarantined filter: no callbacks needed
     * for UI timeout, fallback, or safe acceptance of another request. */
    reset(); CHECK(gp_shortcut_request(100)); gp_shortcut_tick_at(1101);
    CHECK(!pending() && fallbacks == 1); CHECK(gp_shortcut_request(1200));
    gp_shortcut_tick_at(2201); CHECK(!pending() && fallbacks == 2);
    idle(f); memcpy(before, f, 152); registered(AZ_BUS_MIXER_IN, f, 152, NULL);
    CHECK(!memcmp(f, before, 152) && atomic_load(&clock_calls) == 0);

    /* TSan exercises actual simultaneous producer/consumer operations, not just
     * a sequential model. Atomic fixture fields keep fixture races out of it. */
    reset(); atomic_store(&stop_worker, 0); atomic_store(&worker_calls, 0);
    CHECK(!pthread_create(&thread, NULL, many_frames, NULL));
    for (long i = 0; i < 20000; i++) {
        long now = 10000 + i * 1001;
        atomic_store(&mock_now, now); gp_shortcut_tick_at(now); gp_shortcut_request(now);
        sched_yield();
    }
    atomic_store(&stop_worker, 1); CHECK(!pthread_join(thread, NULL));
    CHECK(atomic_load(&worker_calls) > 0);
    atomic_store(&mock_now, 30000000); gp_shortcut_tick(); CHECK(!pending());
    printf("PASS: %d MODS concurrent/idle assertions; %d simultaneous frame callbacks\n", checks, atomic_load(&worker_calls));
    return 0;
}
