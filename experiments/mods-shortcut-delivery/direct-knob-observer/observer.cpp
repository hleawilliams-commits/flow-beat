/* SPDX-License-Identifier: GPL-3.0-only
 * Entry-only emulator observer. No native pointer is dereferenced or retained
 * for later use. All native execution is tail-forwarded by abi/entry.S.
 */
#include "azmod-mod.h"
#include <atomic>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

extern "C" {
void *az_knob_original;
void az_knob_entry(void);
}
namespace ko {
static const char id[] = "waveform-knob-observer";
static const char firmware[] = "ep147-9e9454e357389ece";
static const uintptr_t ENTRY=0x1b3ab60, VTABLE=0x2d61ae0;
static const unsigned QUEUE_SIZE=32, BUDGET=128;
struct Record { uint64_t sequence; uintptr_t x0, x1, caller; uint32_t w2,w3,tid; };
static const az_core_v1 *core;
static unsigned pid;
static std::atomic<int> state(0), live(1);
static std::atomic<unsigned> bad_thread(0), heartbeat(0), dropped(0);
static std::atomic<unsigned> write_index(0), read_index(0);
static Record queue[QUEUE_SIZE];
static uint64_t sequence; /* one producer: native process main thread */
static_assert(std::atomic<unsigned>::is_always_lock_free,"lock-free counters required");
#ifdef AZKNOB_TEST
static thread_local unsigned test_tid;
static bool test_prologues=true;
static unsigned current_tid() { return test_tid; }
#else
static unsigned current_tid() {
    static thread_local unsigned cached;
#ifdef __linux__
    if (!cached) cached=(unsigned)syscall(SYS_gettid);
#endif
    return cached;
}
#endif
static void observe(uintptr_t x0, uintptr_t x1, uint32_t w2, uint32_t w3,
                    uintptr_t caller) noexcept {
    if (state.load(std::memory_order_acquire)!=1 ||
        !live.load(std::memory_order_acquire) || bad_thread.load(std::memory_order_acquire)) return;
    unsigned tid=current_tid();
    if (!tid || tid!=pid) { bad_thread.store(tid?tid:1,std::memory_order_release); return; }
    // Opaque scalar copies only: even deliberately unmapped x0/x1 are safe here.
    Record r={++sequence,x0,x1,caller,w2,w3,tid};
    unsigned w=write_index.load(std::memory_order_relaxed), next=(w+1)%QUEUE_SIZE;
    if (next==read_index.load(std::memory_order_acquire)) {
        dropped.fetch_add(1,std::memory_order_relaxed); return;
    }
    queue[w]=r;
    write_index.store(next,std::memory_order_release);
}
static bool pop(Record &r) {
    unsigned p=read_index.load(std::memory_order_relaxed);
    if (p==write_index.load(std::memory_order_acquire)) return false;
    r=queue[p]; read_index.store((p+1)%QUEUE_SIZE,std::memory_order_release); return true;
}
static bool check_prologues() {
#ifdef AZKNOB_TEST
    return test_prologues;
#else
    // Exact immutable firmware text/table only, never a native object read.
    const uint32_t expected[]={0xf9421c04,0x39430084,0x36080064,0x52800000};
    uintptr_t target=0;
    memcpy(&target,(const void *)(VTABLE+0x198),sizeof target);
    return target==ENTRY && !memcmp((const void *)ENTRY,expected,sizeof expected);
#endif
}
struct WorkerState { unsigned seen=0, quiet=0, logged=0, lost=0; };
static bool worker_cycle(WorkerState &w) {
    int s=state.load(std::memory_order_acquire);
    if (s<0) return false;
    if (!s) return true;
    unsigned h=heartbeat.load(std::memory_order_relaxed);
    w.quiet=h==w.seen ? (w.quiet<5?w.quiet+1:5) : 0;
    w.seen=h; live.store(w.quiet<5,std::memory_order_release);
    unsigned bad=bad_thread.load(std::memory_order_acquire);
    if (bad) {
        live.store(0,std::memory_order_release);
        core->log(id,"WKO inert: unexpected TID=%u required PID=%u",bad,pid); return false;
    }
    unsigned lost=dropped.load(std::memory_order_relaxed);
    if (lost!=w.lost) { core->log(id,"WKO queue dropped=%u",lost); w.lost=lost; }
    Record r;
    while (pop(r)) {
        if (w.logged>=BUDGET) {
            live.store(0,std::memory_order_release);
            core->log(id,"WKO observation budget reached; native pass-through remains"); return false;
        }
        ++w.logged;
        core->log(id,"WKO entry seq=%lu tid=%u x0=%lx x1=%lx w2=%08x w3=%08x caller=%lx entry-only=1",
            (unsigned long)r.sequence,r.tid,(unsigned long)r.x0,(unsigned long)r.x1,
            r.w2,r.w3,(unsigned long)r.caller);
    }
    return true;
}
static void *worker(void *) {
    WorkerState w;
    for (;;) {
        struct timespec remaining={0,500000000};
        while (nanosleep(&remaining,&remaining)) {
            if (errno!=EINTR) { live.store(0,std::memory_order_release); return NULL; }
        }
        if (!worker_cycle(w)) return NULL;
    }
}
static int init(const az_core_v1 *c) {
    const char *opt=getenv("AZ_WAVEFORM_KNOB_OBSERVER_EMULATOR");
    if (!opt || strcmp(opt,"1")) return -9;
    if (!AZ_HAS(c,fw_tag) || !AZ_HAS(c,claim_hook) || !AZ_HAS(c,thread_start) ||
        !AZ_HAS(c,log) || !c->fw_tag() || strcmp(c->fw_tag(),firmware)) return -10;
#if !(defined(__linux__) && defined(__aarch64__)) && !defined(AZKNOB_TEST)
    return -11;
#else
    if (!check_prologues()) return -12;
    core=c; pid=(unsigned)getpid();
    if (core->thread_start(id,"waveform-knob-observer",worker,NULL)) return -13;
    // Core publishes the original before its atomic instruction redirect. Late
    // claim is island-only; absent island or hook conflict is a refused install.
    if (core->claim_hook(id,ENTRY,(void *)az_knob_entry,&az_knob_original) ||
        !__atomic_load_n(&az_knob_original,__ATOMIC_ACQUIRE)) {
        state.store(-1,std::memory_order_release);
        core->log(id,"WKO disabled: hook refused; no retry or takeover"); return -20;
    }
    state.store(1,std::memory_order_release);
    core->log(id,"WKO ready PID=%u; native entry only; no input consumption",pid);
    return 0;
#endif
}
} // namespace ko
extern "C" void az_knob_observe_entry(uintptr_t x0, uintptr_t x1, uint32_t w2,
    uint32_t w3, uintptr_t original_lr) noexcept { ko::observe(x0,x1,w2,w3,original_lr); }
extern "C" int knob_observer_init(const az_core_v1 *core) { return ko::init(core); }
extern "C" void knob_observer_tick(void) { ko::heartbeat.fetch_add(1,std::memory_order_relaxed); }
