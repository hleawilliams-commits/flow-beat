/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercises the extracted recovery predicate and audio observation publisher.
 * Looped audio may return to the same position between flusher ticks. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define NDECK 4
#define AZSTEM_MEM_FLOOR_MB 400
static _Atomic long long g_retry_bytes[NDECK], g_play_obs[NDECK];
static _Atomic int g_stem_loading[NDECK];
static int g_retry_still[NDECK];
static long g_retry_after[NDECK], g_now;
static long long g_available;
static long ms_now(void) { return g_now; }
static long long mem_available_bytes(void) { return g_available; }
#ifdef BASELINE
static long long g_retry_pos[NDECK];
static long long azstem_play_obs(int d) { return atomic_load(&g_play_obs[d]); }
static void stem_play_observe(int d, long long pos) { atomic_store(&g_play_obs[d], pos); }
#else
static unsigned long long g_retry_activity[NDECK];
static _Atomic unsigned long long g_play_activity[NDECK];
#include OBSERVER_INC
#endif
#include RETRY_INC
static int passed, failed;
#define CHECK(label, condition) do { \
    if (condition) { passed++; } else { failed++; fprintf(stderr, "FAIL deck %d: %s\n", d+1, label); } \
} while (0)
static void reset(int d) {
    g_now = 0; g_available = 900LL << 20;
    for (int i=0;i<NDECK;i++) {
        atomic_store(&g_retry_bytes[i], i==d ? 300LL<<20 : 0);
        atomic_store(&g_play_obs[i], -1);
        atomic_store(&g_stem_loading[i], 0);
        g_retry_still[i] = 0; g_retry_after[i] = 2000;
#ifdef BASELINE
        g_retry_pos[i] = -1;
#else
        g_retry_activity[i] = 0; atomic_store(&g_play_activity[i], 0);
#endif
    }
}
static int tick(int d) { g_now += 200; return memory_retry_ready(d); }
int main(void) {
    for (int d=0;d<NDECK;d++) {
        int eligible;
        reset(d); eligible=0;
        for (int n=0;n<40;n++) {
            /* Many moving observations, same position at every 200 ms sample. */
            for (int k=1;k<20;k++) stem_play_observe(d,k*441);
            stem_play_observe(d,0);
            eligible |= tick(d);
        }
        CHECK("moving loop cannot look paused at sampling boundary", !eligible);
        reset(d); stem_play_observe(d,100); eligible=0;
        for (int n=0;n<40;n++) eligible |= tick(d);
        CHECK("stale playhead without fresh audio observations does not retry", !eligible);
        reset(d); eligible=0;
        for (int n=0;n<40;n++) eligible |= tick(d);
        CHECK("unknown playhead does not retry", !eligible);
        reset(d); eligible=0;
        for (int n=0;n<30;n++) { stem_play_observe(d,n*8820); eligible |= tick(d); }
        CHECK("ordinary moving track does not retry", !eligible);
        for (int n=0;n<5;n++) { stem_play_observe(d,123); eligible |= tick(d); }
        CHECK("brief stillness does not retry", !eligible);
        for (int n=0;n<30;n++) { stem_play_observe(d,123); eligible |= tick(d); }
        CHECK("fresh still observations eventually allow recovery", eligible);
        reset(d); g_available=(700LL<<20)-1; eligible=0;
        for (int n=0;n<40;n++) { stem_play_observe(d,0); eligible |= tick(d); }
        CHECK("reserve preserved to the byte", !eligible);
        g_available=700LL<<20;
        for (int n=0;n<20;n++) { stem_play_observe(d,0); eligible |= tick(d); }
        CHECK("exact reserve boundary permits recovery", eligible);
        reset(d); g_available=-1; eligible=0;
        for (int n=0;n<40;n++) { stem_play_observe(d,0); eligible |= tick(d); }
        CHECK("unreadable memory does not retry", !eligible);
        reset(d); atomic_store(&g_stem_loading[(d+1)%NDECK],1); eligible=0;
        for (int n=0;n<40;n++) { stem_play_observe(d,0); eligible |= tick(d); }
        CHECK("other decode blocks recovery", !eligible);
        reset(d); atomic_store(&g_retry_bytes[d],0); eligible=0;
        for (int n=0;n<40;n++) { stem_play_observe(d,0); eligible |= tick(d); }
        CHECK("non-refused track does not retry", !eligible);
#ifndef BASELINE
        reset(d);
        atomic_store(&g_play_activity[d],0x00000007ffffffffULL);
        stem_play_observe(d,1);
        CHECK("motion counter wrap still records fresh activity", stem_retry_play_activity(d)!=0x00000007ffffffffULL);
        CHECK("invalid activity deck rejected", stem_retry_play_activity(-1)==0 && stem_retry_play_activity(NDECK)==0);
#endif
    }
    printf("%d passed, %d failed\n",passed,failed);
    return failed ? 1 : 0;
}
