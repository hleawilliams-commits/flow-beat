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
#include <limits.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

extern "C" {
void *az_dispatch_original;
void az_dispatch_entry(void);
}
namespace edo {
static const char id[] = "encoder-dispatch-observer";
static const char firmware[] = "ep147-9e9454e357389ece";
static const uintptr_t ENTRY=0x1adeee0, SLOT=0x2d61d98;
static const unsigned QUEUE_SIZE=32, BUDGET=128, DIAGNOSTIC_BUDGET=8;
struct Record { uint64_t sequence; uintptr_t x0, x1, x2, caller; uint32_t w3,w4,tid; };
static const az_core_v1 *core;
static unsigned pid;
static std::atomic<int> state(0), live(1);
static std::atomic<unsigned> heartbeat(0), dropped(0), producer_busy(0), nested_dropped(0);
static std::atomic<unsigned> write_index(0), read_index(0);
static Record queue[QUEUE_SIZE];
static uint64_t sequence; /* one producer: native process main thread */
enum { OFF_EMPTY, OFF_WRITING, OFF_READY, OFF_CONSUMED };
static std::atomic<unsigned> off_state(OFF_EMPTY), off_skipped(0), tid_failures(0);
static Record off_record; /* one successful writer per process, one worker consumer */
static_assert(std::atomic<unsigned>::is_always_lock_free,"lock-free counters required");
static_assert(std::atomic<int>::is_always_lock_free,"lock-free state required");
static unsigned normalize_tid(long raw) noexcept {
    return raw>0 && (unsigned long)raw<=UINT_MAX ? (unsigned)raw : 0;
}
#ifdef AZDISPATCH_TEST
static thread_local long test_tid;
static thread_local unsigned test_tid_calls;
static thread_local int test_tid_errno, test_helper_errno;
static void (*test_before_off_copy)(void);
static void (*test_inside_producer)(void);
static bool test_prologues=true;
static unsigned current_tid() {
    ++test_tid_calls;
    if (test_tid_errno) errno=test_tid_errno;
    return normalize_tid(test_tid);
}
#else
static unsigned current_tid() {
    static thread_local unsigned cached;
#ifdef __linux__
    if (!cached) cached=normalize_tid(syscall(SYS_gettid));
#endif
    return cached;
}
#endif
static void observe(uintptr_t x0, uintptr_t x1, uintptr_t x2, uint32_t w3, uint32_t w4,
                    uintptr_t caller) noexcept {
    if (state.load(std::memory_order_acquire)!=1 ||
        !live.load(std::memory_order_acquire)) return;
    unsigned tid=current_tid();
    if (!tid) { tid_failures.fetch_add(1,std::memory_order_relaxed); return; }
    if (tid!=pid) {
        unsigned expected=OFF_EMPTY;
        if (!off_state.compare_exchange_strong(expected,OFF_WRITING,std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
            off_skipped.fetch_add(1,std::memory_order_relaxed); return;
        }
#ifdef AZDISPATCH_TEST
        if (test_before_off_copy) test_before_off_copy();
#endif
        // This independent one-shot record has no global/main-thread sequence meaning.
        off_record={0,x0,x1,x2,caller,w3,w4,tid};
        off_state.store(OFF_READY,std::memory_order_release);
        return;
    }
    unsigned expected=0;
    if (!producer_busy.compare_exchange_strong(expected,1,std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) {
        nested_dropped.fetch_add(1,std::memory_order_relaxed); return;
    }
    struct ReleaseProducer { ~ReleaseProducer() { producer_busy.store(0,std::memory_order_release); } } release;
#ifdef AZDISPATCH_TEST
    if (test_inside_producer) test_inside_producer();
#endif
    // Five opaque input scalar copies: deliberately unmapped x0/x1/x2 are safe here.
    Record r={++sequence,x0,x1,x2,caller,w3,w4,tid};
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
static bool pop_off(Record &r) {
    if (off_state.load(std::memory_order_acquire)!=OFF_READY) return false;
    r=off_record;
    // Never reset to EMPTY: no later writer can race this copy or replace its identity.
    off_state.store(OFF_CONSUMED,std::memory_order_release);
    return true;
}
static bool check_prologues() {
#ifdef AZDISPATCH_TEST
    return test_prologues;
#else
    // Exact immutable firmware text/table only, never a native object read.
    const uint32_t expected[]={0xa9b37bfd,0x910003fd,0xa90153f3,0xd0012853};
    uintptr_t target=0;
    memcpy(&target,(const void *)SLOT,sizeof target);
    return target==ENTRY && !memcmp((const void *)ENTRY,expected,sizeof expected);
#endif
}
struct WorkerState {
    unsigned seen=0, quiet=0, logged=0, lost=0, skipped=0, failed=0, nested=0, diagnostics=0;
};
static bool log_record(WorkerState &w, const Record &r, bool off) {
    if (w.logged>=BUDGET) {
        live.store(0,std::memory_order_release);
        core->log(id,"EDO observation budget reached; native pass-through remains"); return false;
    }
    ++w.logged;
    core->log(id,"EDO entry lane=%s main-seq=%lu tid=%u x0=%lx x1=%lx x2=%lx w3=%08x w4=%08x caller=%lx entry-only=1",
        off?"off-thread-once":"main",(unsigned long)r.sequence,r.tid,
        (unsigned long)r.x0,(unsigned long)r.x1,(unsigned long)r.x2,r.w3,r.w4,
        (unsigned long)r.caller);
    return true;
}
static bool worker_cycle(WorkerState &w) {
    int s=state.load(std::memory_order_acquire);
    if (s<0) return false;
    if (!s) return true;
    unsigned h=heartbeat.load(std::memory_order_relaxed);
    w.quiet=h==w.seen ? (w.quiet<5?w.quiet+1:5) : 0;
    w.seen=h; live.store(w.quiet<5,std::memory_order_release);
    unsigned lost=dropped.load(std::memory_order_relaxed);
    unsigned skipped=off_skipped.load(std::memory_order_relaxed);
    unsigned failed=tid_failures.load(std::memory_order_relaxed);
    unsigned nested=nested_dropped.load(std::memory_order_relaxed);
    if (lost!=w.lost || skipped!=w.skipped || failed!=w.failed || nested!=w.nested) {
        if (w.diagnostics<DIAGNOSTIC_BUDGET) {
            ++w.diagnostics;
            core->log(id,"EDO diagnostics queue-dropped=%u off-thread-skipped=%u tid-failed=%u nested-dropped=%u",
                      lost,skipped,failed,nested);
        }
        w.lost=lost; w.skipped=skipped; w.failed=failed; w.nested=nested;
    }
    Record r;
    if (pop_off(r) && !log_record(w,r,true)) return false;
    while (pop(r)) if (!log_record(w,r,false)) return false;
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
    const char *opt=getenv("AZ_ENCODER_DISPATCH_OBSERVER_EMULATOR");
    if (!opt || strcmp(opt,"1")) return -9;
    if (!c || c->abi!=AZ_MOD_ABI || c->size<0 || !AZ_HAS(c,fw_tag) ||
        !AZ_HAS(c,claim_hook) || !AZ_HAS(c,thread_start) || !AZ_HAS(c,log)) return -10;
    const char *tag=c->fw_tag();
    if (!tag || strcmp(tag,firmware)) return -10;
#if !(defined(__linux__) && defined(__aarch64__)) && !defined(AZDISPATCH_TEST)
    return -11;
#else
    if (!check_prologues()) return -12;
    core=c; pid=(unsigned)getpid();
    if (core->thread_start(id,"encoder-dispatch-observer",worker,NULL)) return -13;
    // Core publishes the original before its atomic instruction redirect. Late
    // claim is island-only; absent island or hook conflict is a refused install.
    if (core->claim_hook(id,ENTRY,(void *)az_dispatch_entry,&az_dispatch_original) ||
        !__atomic_load_n(&az_dispatch_original,__ATOMIC_ACQUIRE)) {
        state.store(-1,std::memory_order_release);
        core->log(id,"EDO disabled: hook refused; no retry or takeover"); return -20;
    }
    state.store(1,std::memory_order_release);
    core->log(id,"EDO ready PID=%u; native entry only; no input consumption",pid);
    return 0;
#endif
}
} // namespace edo
extern "C" void az_dispatch_observe_entry(uintptr_t x0, uintptr_t x1, uintptr_t x2,
    uint32_t w3, uint32_t w4, uintptr_t original_lr) noexcept {
    const int saved_errno=errno;
#ifdef AZDISPATCH_TEST
    if (edo::test_helper_errno) errno=edo::test_helper_errno;
#endif
    edo::observe(x0,x1,x2,w3,w4,original_lr);
    errno=saved_errno;
}
extern "C" int dispatch_observer_init(const az_core_v1 *core) { return edo::init(core); }
extern "C" void dispatch_observer_tick(void) { edo::heartbeat.fetch_add(1,std::memory_order_relaxed); }
