/* SPDX-License-Identifier: GPL-3.0-only
 * Emulator-only native observation. No activation or input injection exists.
 * Only calls SDK services and originals of callbacks entered by native code.
 */
#include "azmod-mod.h"
#include <atomic>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

namespace uv3 {
static const char id[] = "usb-observer-v3";
static const char firmware[] = "ep147-9e9454e357389ece";
static const uintptr_t PARENT_SELECT = 0x1b296c0, EVENT_EXECUTE = 0x1ffb6c8;
static const uintptr_t POST_COMMIT_HELPER = 0x2014a88, POST_COMMIT_RETURN = 0x1ffbca8;
static const unsigned MAX_SCOPE_DEPTH = 8;
static const uintptr_t PARENT_VT = 0x2d5ab98, LISTENER_VT = 0x2d5af50;
static const uintptr_t CHILD_VT = 0x2e0b3e8, MODEL_VT = 0x2e0b900;
static const uintptr_t EVENT_VT = 0x3163ae8, BROWSER_CONTROL_VT = 0x317c028;
static const unsigned MAX_ROWS = 16, QUEUE_SIZE = 32;
enum Status { VALID = 1, BAD_PARENT, BAD_CHILD, BAD_MODEL, BAD_BOUNDS,
              BAD_EVENT, BAD_OWNER, RESULT_ONLY, NATIVE_FAILURE,
              COMMITTED_AT_ENTRY, COMMIT_MISMATCH, CHANGED_CONTEXT };
enum Kind { PARENT = 1, MEDIA_ENTRY, MEDIA_RESULT, COMMIT_ENTRY };
struct Media { uint32_t type; uint16_t id; };
struct Row { uint32_t kind; uint16_t media; uint8_t valid, focused, active; };
struct Record {
    uint64_t sequence, correlation;
    unsigned kind, status, tid;
    uintptr_t parent, child, event, browser;
    int mode, row_count, result;
    unsigned before_busy;
    Media requested, before;
    Row rows[MAX_ROWS];
};
static const az_core_v1 *core;
static unsigned pid;
static void *orig_parent, *orig_event, *orig_post_commit;
static std::atomic<int> state(0), live(1);
static std::atomic<unsigned> bad_thread(0), heartbeat(0), dropped(0);
static std::atomic<unsigned> write_index(0), read_index(0);
static Record queue[QUEUE_SIZE];
static uint64_t sequence; /* main message thread only */
static_assert(std::atomic<unsigned>::is_always_lock_free, "observer counters must be lock-free");
#ifdef UV3_TEST
static thread_local unsigned test_tid;
static thread_local uintptr_t test_post_commit_caller;
static bool test_prologues = true;
static std::atomic<unsigned> test_native_reads(0);
static void (*test_after_original)(void);
static unsigned current_tid() { return test_tid; }
#else
static unsigned current_tid() {
    static thread_local unsigned cached;
#ifdef __linux__
    if (!cached) cached = (unsigned)syscall(SYS_gettid);
#endif
    return cached;
}
#endif
static bool observing() {
    return state.load(std::memory_order_acquire) == 1 &&
        live.load(std::memory_order_relaxed) && !bad_thread.load(std::memory_order_acquire);
}
static bool message_thread() {
    unsigned tid = current_tid();
    if (tid && tid == pid) return true;
    bad_thread.store(tid ? tid : 1, std::memory_order_release);
    return false;
}
static bool address(uintptr_t p, size_t n, unsigned alignment = 8) {
    return p >= 0x10000 && p <= UINT64_C(0x0000ffffffffffff) - n &&
        !(p & (alignment - 1));
}
template<class T> static T read(uintptr_t p) {
#ifdef UV3_TEST
    test_native_reads.fetch_add(1, std::memory_order_relaxed);
#endif
    T value; memcpy(&value, (const void *)p, sizeof value); return value;
}
/* Numeric range checks do not establish mapping/lifetime. Every read below is
 * reached from a live callback argument or its verified owned object, never by
 * scanning memory or dereferencing a previously published diagnostic address. */
static Media media(uintptr_t p) {
    Media m = {}; m.type = read<uint32_t>(p); m.id = read<uint16_t>(p + 4); return m;
}
static void publish(Record &r) {
    r.sequence = ++sequence;
    r.tid = current_tid();
    unsigned w = write_index.load(std::memory_order_relaxed);
    unsigned next = (w + 1) % QUEUE_SIZE;
    if (next == read_index.load(std::memory_order_acquire)) {
        dropped.fetch_add(1, std::memory_order_relaxed); return;
    }
    queue[w] = r;
    write_index.store(next, std::memory_order_release);
}
static bool pop(Record &r) {
    unsigned p = read_index.load(std::memory_order_relaxed);
    if (p == write_index.load(std::memory_order_acquire)) return false;
    r = queue[p]; read_index.store((p + 1) % QUEUE_SIZE, std::memory_order_release);
    return true;
}
static Record parent_record(void *self) {
    Record r = {}; r.kind = PARENT; r.status = BAD_PARENT;
    uintptr_t p = (uintptr_t)self;
    if (!address(p, 0x3b0) || read<uintptr_t>(p) != PARENT_VT ||
        read<uintptr_t>(p + 0x198) != LISTENER_VT) return r;
    r.parent = p; r.mode = read<int32_t>(p + 0x1e8);
    uintptr_t c = read<uintptr_t>(p + 0x308);
    r.status = BAD_CHILD;
    if (!address(c, 0x158) || read<uintptr_t>(c) != CHILD_VT ||
        read<uintptr_t>(c + 0x60) != p + 0x198) return r;
    r.child = c;
    r.status = BAD_MODEL;
    uintptr_t m = c + 0x80;
    if (read<uintptr_t>(m) != MODEL_VT) return r;
    int n = read<int32_t>(m + 0x18), capacity = read<int32_t>(m + 0x10);
    uintptr_t rows = read<uintptr_t>(m + 8);
    r.status = BAD_BOUNDS;
    if (n < 0 || n > (int)MAX_ROWS || capacity < n || capacity > 1024 ||
        (n && !address(rows, (size_t)n * 0x28))) return r;
    r.row_count = n;
    for (int i = 0; i < n; ++i) {
        uintptr_t row = rows + (size_t)i * 0x28;
        r.rows[i].kind = read<uint32_t>(row + 4);
        r.rows[i].valid = read<uint8_t>(row + 8);
        r.rows[i].media = read<uint16_t>(row + 0xc);
        r.rows[i].focused = read<uint8_t>(row + 0x23);
        r.rows[i].active = read<uint8_t>(row + 0x24);
    }
    r.status = VALID; return r;
}
/* Event's shared_ptr at +0x58/+0x60 strongly owns BrowserData. Its exact
 * _Sp_counted_ptr_inplace vptr and data=control+0x10 identify the allocation;
 * BrowserData itself is not assumed polymorphic. No reference counts changed. */
static int32_t reference_count(uintptr_t p) {
#ifdef UV3_TEST
    test_native_reads.fetch_add(1, std::memory_order_relaxed);
#endif
    /* Other shared_ptr owners may change this count atomically. Reading it is
     * only a structural check; it does not acquire a reference or pin lifetime. */
    return __atomic_load_n((const int32_t *)p, __ATOMIC_RELAXED);
}
static bool owned_browser(uintptr_t e, uintptr_t &b, uintptr_t &control) {
    b = read<uintptr_t>(e + 0x58); control = read<uintptr_t>(e + 0x60);
    return address(control, 0x200) && b == control + 0x10 &&
        read<uintptr_t>(control) == BROWSER_CONTROL_VT &&
        reference_count(control + 8) > 0;
}
static void parent_hook(void *self) {
    if (observing() && message_thread()) {
        Record r = parent_record(self); publish(r);
    }
    ((void (*)(void *))__atomic_load_n(&orig_parent, __ATOMIC_ACQUIRE))(self);
    /* No post-call parent/child reads and no retained native object. */
}
/* Diagnostic pointer values identify the current native call only. They are
 * never dereferenced from this context. The additional callback may read only
 * its own live native argument, after equality and caller checks. */
struct EventScope;
static thread_local EventScope *active_scope;
struct EventScope {
    EventScope *const previous;
    const unsigned depth;
    const uint64_t entry_sequence;
    const uintptr_t event, browser;
    const Media requested;
    const bool valid;
    explicit EventScope(const Record &r) : previous(active_scope),
        depth(previous ? previous->depth + 1 : 1), entry_sequence(r.sequence),
        event(r.event), browser(r.browser), requested(r.requested),
        valid(r.status == VALID && depth <= MAX_SCOPE_DEPTH) { active_scope = this; }
    ~EventScope() { active_scope = previous; }
    EventScope(const EventScope &) = delete;
    EventScope &operator=(const EventScope &) = delete;
};
static bool same_media(const Media &a, const Media &b) {
    return a.type == b.type && a.id == b.id;
}
static Record commit_entry_record(void *self, const EventScope &scope) {
    Record r = {}; r.kind = COMMIT_ENTRY; r.status = BAD_EVENT;
    r.correlation = scope.entry_sequence; r.requested = scope.requested;
    const uintptr_t e = (uintptr_t)self;
    uintptr_t b = 0, control = 0;
    if (!address(e, 0xa0) || read<uintptr_t>(e) != EVENT_VT) return r;
    r.event = e; r.status = CHANGED_CONTEXT;
    if (!same_media(media(e + 0x98), scope.requested)) return r;
    r.status = BAD_OWNER;
    if (!owned_browser(e, b, control)) return r;
    r.status = CHANGED_CONTEXT;
    if (b != scope.browser) return r;
    r.browser = b; r.before = media(b + 0x1e0);
    r.before_busy = read<uint8_t>(b + 0x1ba);
    r.status = same_media(r.before, scope.requested) && !r.before_busy
        ? COMMITTED_AT_ENTRY : COMMIT_MISMATCH;
    return r;
}
static __attribute__((noinline)) int post_commit_hook(void *self, unsigned argument) {
    typedef int (*Fn)(void *, unsigned);
    Fn original = (Fn)__atomic_load_n(&orig_post_commit, __ATOMIC_ACQUIRE);
#ifdef UV3_TEST
    const uintptr_t caller = test_post_commit_caller;
#else
    const uintptr_t caller = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
#endif
    const EventScope *scope = active_scope;
    if (observing() && scope && scope->valid && caller == POST_COMMIT_RETURN &&
        (uintptr_t)self == scope->event && argument == 0 && message_thread()) {
        Record r = commit_entry_record(self, *scope); publish(r);
    }
    /* No native read after forwarding: this original may re-enter, destroy its
     * argument or throw. This snapshot never implies final transaction success. */
    return original(self, argument);
}
static int event_hook(void *self) {
    typedef int (*Fn)(void *);
    Fn original = (Fn)__atomic_load_n(&orig_event, __ATOMIC_ACQUIRE);
    if (!observing() || !message_thread()) return original(self);
    Record entry = {}; entry.kind = MEDIA_ENTRY; entry.status = BAD_EVENT;
    uintptr_t e = (uintptr_t)self, b = 0, control = 0;
    if (address(e, 0xa0) && read<uintptr_t>(e) == EVENT_VT) {
        entry.event = e; entry.requested = media(e + 0x98); entry.status = BAD_OWNER;
        if (owned_browser(e, b, control)) {
            entry.browser = b; entry.before = media(b + 0x1e0);
            entry.before_busy = read<uint8_t>(b + 0x1ba); entry.status = VALID;
        }
    }
    publish(entry);
    /* Once original returns, no native pointer is read again. A destructor
     * notification + flag recheck would not pin lifetime against another thread.
     * Return code 0/1 is evidence of a native result, NOT a verified commit. */
    EventScope scope(entry);
    int result = original(self);
#ifdef UV3_TEST
    if (test_after_original) test_after_original(); /* concurrent-free test seam */
#endif
    Record completion = {};
    completion.kind = MEDIA_RESULT;
    completion.correlation = entry.sequence;
    completion.result = result;
    completion.status = result == 0 || result == 1 ? RESULT_ONLY : NATIVE_FAILURE;
    publish(completion);
    return result;
}
static bool check_prologues() {
#ifdef UV3_TEST
    return test_prologues;
#else
    const struct { uintptr_t p; uint32_t w[4]; } checks[] = {
        {PARENT_SELECT, {0xa9ba7bfd,0xd0009181,0x91018021,0x9100c3e3}},
        {EVENT_EXECUTE, {0xa9b07bfd,0x910003fd,0xa90153f3,0xaa0003f3}},
        {POST_COMMIT_HELPER, {0xd103c3ff,0xa9017bfd,0x910043fd,0xa90253f3}},
        {0x1ffbc9c, {0x52800001,0xaa1303e0,0x94006379,0x35000280}}
    };
    for (unsigned i=0; i<sizeof checks/sizeof checks[0]; ++i)
        if (memcmp((const void *)checks[i].p, checks[i].w, sizeof checks[i].w)) return false;
    return read<uintptr_t>(PARENT_VT) == 0x1b2c9e0 &&
        read<uintptr_t>(LISTENER_VT) == 0x1b29458 &&
        read<uintptr_t>(EVENT_VT + 0x48) == EVENT_EXECUTE &&
        read<uintptr_t>(BROWSER_CONTROL_VT + 0x10) == 0x209e598;
#endif
}
static void log_record(const Record &r) {
    if (r.kind == PARENT) {
        core->log(id,"UV3 parent seq=%lu tid=%u status=%u parent=%lx child=%lx mode=%d rows=%d",
            (unsigned long)r.sequence,r.tid,r.status,(unsigned long)r.parent,(unsigned long)r.child,r.mode,r.row_count);
        for (int i=0;i<r.row_count;++i)
            core->log(id,"UV3 row seq=%lu index=%d kind=%u valid=%u media=%04x focus=%u active=%u",
                (unsigned long)r.sequence,i,r.rows[i].kind,r.rows[i].valid,r.rows[i].media,r.rows[i].focused,r.rows[i].active);
    } else if (r.kind == MEDIA_ENTRY) {
        core->log(id,"UV3 event-entry seq=%lu tid=%u status=%u event=%lx browser=%lx request=%u:%04x committed-at-entry=%u:%04x/%u",
            (unsigned long)r.sequence,r.tid,r.status,(unsigned long)r.event,(unsigned long)r.browser,
            r.requested.type,r.requested.id,r.before.type,r.before.id,r.before_busy);
    } else if (r.kind == COMMIT_ENTRY) {
        core->log(id,"UV3 commit-entry seq=%lu entry=%lu tid=%u status=%u request=%u:%04x committed-at-entry=%u:%04x/%u boundary-only=1",
            (unsigned long)r.sequence,(unsigned long)r.correlation,r.tid,r.status,
            r.requested.type,r.requested.id,r.before.type,r.before.id,r.before_busy);
    } else {
        core->log(id,"UV3 event-result seq=%lu entry=%lu tid=%u status=%u rc=%d post-read=none",
            (unsigned long)r.sequence,(unsigned long)r.correlation,r.tid,r.status,r.result);
    }
}
static void *worker(void *) {
    unsigned seen=0, quiet=0, logged=0, previous_dropped=0;
    for (;;) {
        struct timespec remain = {0,500000000};
        while (nanosleep(&remain,&remain)) {}
        int s=state.load(std::memory_order_acquire);
        if (s<0) return NULL;
        if (!s) continue;
        unsigned h=heartbeat.load(std::memory_order_relaxed);
        quiet=h==seen ? quiet+1 : 0; if (quiet>5) quiet=5;
        seen=h; live.store(quiet<5,std::memory_order_release);
        unsigned bad=bad_thread.load(std::memory_order_acquire);
        if (bad) { core->log(id,"UV3 inert: unexpected callback TID=%u; required PID=%u",bad,pid); return NULL; }
        unsigned lost=dropped.load(std::memory_order_relaxed);
        if (lost!=previous_dropped) { core->log(id,"UV3 queue dropped=%u",lost); previous_dropped=lost; }
        Record r;
        while (pop(r)) {
            if (logged++>=128) {
                live.store(0,std::memory_order_release);
                core->log(id,"UV3 observation budget reached; callbacks now only forward"); return NULL;
            }
            log_record(r);
        }
    }
}
static int init(const az_core_v1 *c) {
    const char *opt=getenv("AZ_USB_OBSERVER_V3_EMULATOR");
    if (!opt || strcmp(opt,"1")) return -9;
    if (!AZ_HAS(c,fw_tag) || !AZ_HAS(c,claim_hook) || !AZ_HAS(c,thread_start) ||
        !AZ_HAS(c,log) || !c->fw_tag() || strcmp(c->fw_tag(),firmware)) return -10;
#if !(defined(__linux__) && defined(__aarch64__)) && !defined(UV3_TEST)
    return -11;
#else
    if (!check_prologues()) return -12;
    core=c; pid=(unsigned)getpid();
    if (core->thread_start(id,"usb-observer-v3",worker,NULL)) return -13;
    /* Partial claim failure leaves every installed callback inert and forwarding.
     * No manual unpatch, retry claim or takeover is attempted. */
    struct { uintptr_t p; void *hook; void **original; } claims[] = {
        {PARENT_SELECT,(void *)parent_hook,&orig_parent},
        {EVENT_EXECUTE,(void *)event_hook,&orig_event},
        {POST_COMMIT_HELPER,(void *)post_commit_hook,&orig_post_commit}
    };
    for (unsigned i=0;i<sizeof claims/sizeof claims[0];++i) {
        if (core->claim_hook(id,claims[i].p,claims[i].hook,claims[i].original) ||
            !__atomic_load_n(claims[i].original,__ATOMIC_ACQUIRE)) {
            state.store(-1,std::memory_order_release);
            core->log(id,"UV3 disabled: claim %u refused; installed hooks only forward",i);
            return -20-(int)i;
        }
    }
    state.store(1,std::memory_order_release);
    core->log(id,"UV3 ready: manual native interactions only; PID=%u; no activation API",pid);
    return 0;
#endif
}
static void tick() { heartbeat.fetch_add(1,std::memory_order_relaxed); }
} // namespace uv3
extern "C" int uv3_init(const az_core_v1 *core) { return uv3::init(core); }
extern "C" void uv3_tick(void) { uv3::tick(); }
