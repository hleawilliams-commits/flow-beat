/* SPDX-License-Identifier: GPL-3.0-only
 * Observation-only native adapter. All native object reads run on EP147's
 * message thread, from a live callback or a destructor-tracked controller.
 * No browser activation, selection, media write, or input injection exists here.
 */
#define _GNU_SOURCE
#include "usb-backend.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#define USP_TIMER_ADDR ((uintptr_t)0x250b0e0)
#define USP_SOURCE_ADDR ((uintptr_t)0x1c32f40)
#define USP_DTOR_ADDR ((uintptr_t)0x1c32130)
#define USP_CTOR_ADDR ((uintptr_t)0x1c313c8)
#define USP_CONTROLLER_VT ((uintptr_t)0x2e0b3e8)
#define USP_MODEL_VT ((uintptr_t)0x2e0b900)
#define USP_MODEL_OFFSET 0x80u
#define USP_ROW_SIZE 0x28u

/* Exact verified ABIs; the caller at 0x1c33ad8 passes x0=self, x1=model,
 * w2=row. Constructor caller 0x1b2db28 passes x0..x5 and ignores x0 return.
 * C++ constructor/destructor and both callbacks return void. */
typedef void (*usp_one_fn)(void *);
typedef void (*usp_source_fn)(void *, void *, int);
typedef void (*usp_ctor_fn)(void *, void *, void *, void *, void *, void *);
static void *usp_orig_timer, *usp_orig_source, *usp_orig_dtor, *usp_orig_ctor;
static const az_core_v1 *usp_core;
static const char *usp_id;
static unsigned usp_pid;
static _Atomic int usp_state; /* 0 initializing, 1 all claims ready, -1 failed */
static _Atomic int usp_enabled = 1, usp_live, usp_observe_requested;
static _Atomic unsigned usp_heartbeat, usp_bad_thread;
static _Atomic unsigned usp_timer_tid, usp_source_tid, usp_ctor_tid, usp_dtor_tid;
static _Atomic unsigned usp_timer_calls, usp_source_calls, usp_ctor_calls, usp_dtor_calls;
static _Atomic uintptr_t usp_controller;
static _Atomic uint64_t usp_generation;
/* These fields are message-thread only. */
static int usp_clicked = -1, usp_from_ctor;
static uint64_t usp_serial;
static pthread_mutex_t usp_snapshot_mu = PTHREAD_MUTEX_INITIALIZER;
static usp_snapshot usp_published;

#ifdef USP_TEST
static unsigned usp_test_tid;
static unsigned usp_current_tid(void) { return usp_test_tid; }
static int usp_test_bad_prologue;
#else
static unsigned usp_current_tid(void) {
    /* One syscall on each observed thread, not on every native timer. */
    static _Thread_local unsigned cached;
    if (!cached) {
#if defined(__linux__)
        cached = (unsigned)syscall(SYS_gettid);
#endif
    }
    return cached;
}
#endif
static int usp_addr_ok(uintptr_t p, size_t n, unsigned alignment) {
    return p >= 0x10000 && p <= UINT64_C(0x0000ffffffffffff) - n &&
           !(p & (alignment - 1u));
}
/* Caller must already own the object lifetime. This is not a pointer scanner. */
static uintptr_t usp_pointer(uintptr_t p) {
    uintptr_t v; memcpy(&v, (const void *)p, sizeof v); return v;
}
static int32_t usp_i32(uintptr_t p) {
    int32_t v; memcpy(&v, (const void *)p, sizeof v); return v;
}
static int usp_on_message_thread(_Atomic unsigned *record) {
    unsigned tid = usp_current_tid();
    atomic_store_explicit(record, tid, memory_order_relaxed);
    if (tid && tid == usp_pid) return 1;
    atomic_store_explicit(&usp_bad_thread, tid ? tid : 1, memory_order_release);
    atomic_store_explicit(&usp_controller, 0, memory_order_release);
    return 0;
}
static int usp_observing(void) {
    return atomic_load_explicit(&usp_state, memory_order_acquire) == 1 &&
           atomic_load_explicit(&usp_enabled, memory_order_relaxed) &&
           atomic_load_explicit(&usp_live, memory_order_relaxed) &&
           !atomic_load_explicit(&usp_bad_thread, memory_order_acquire);
}
static void usp_copy_label(char dst[USP_LABEL_SIZE], uintptr_t p) {
    dst[0] = 0;
    if (!usp_addr_ok(p, 1, 1)) return;
    /* juce::String owns a NUL-terminated UTF-8 allocation. Stop at its NUL. */
    for (unsigned i = 0; i < USP_LABEL_SIZE - 1; ++i) {
        unsigned char ch = ((const unsigned char *)p)[i];
        if (!ch) { dst[i] = 0; return; }
        if (ch < 32 || ch == 127) { dst[0] = 0; return; }
        dst[i] = (char)ch;
    }
    dst[USP_LABEL_SIZE - 1] = 0;
    /* Drop any incomplete trailing UTF-8 code point. */
    unsigned end = USP_LABEL_SIZE - 1, start = end;
    while (start && (((unsigned char)dst[start - 1] & 0xc0) == 0x80)) --start;
    if (start && (unsigned char)dst[start - 1] >= 0xc0) {
        unsigned char c = (unsigned char)dst[start - 1];
        unsigned expected = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (end - (start - 1) < expected) dst[start - 1] = 0;
    }
}
static void usp_capture(void *self, void *model, int row, int from_ctor) {
    uintptr_t p = (uintptr_t)self;
    if (!usp_addr_ok(p, 0x158, 8) || usp_pointer(p) != USP_CONTROLLER_VT ||
        (uintptr_t)model != p + USP_MODEL_OFFSET ||
        usp_pointer((uintptr_t)model) != USP_MODEL_VT) return;
    uintptr_t old = atomic_load_explicit(&usp_controller, memory_order_relaxed);
    if (old != p) {
        atomic_fetch_add_explicit(&usp_generation, 1, memory_order_relaxed);
        usp_clicked = -1;
        usp_from_ctor = from_ctor;
    }
    atomic_store_explicit(&usp_controller, p, memory_order_release);
    if (row >= 0) usp_clicked = row;
    atomic_store_explicit(&usp_observe_requested, 1, memory_order_release);
}
/* Native-message-thread only; all reads stay within controller-owned live data.
 * Refuse the entire table on unexpected bounds instead of silently truncating. */
static void usp_observe(void) {
    usp_snapshot s;
    memset(&s, 0, sizeof s);
    s.serial = ++usp_serial;
    s.generation = atomic_load_explicit(&usp_generation, memory_order_relaxed);
    s.process_id = usp_pid;
    s.timer_tid = atomic_load_explicit(&usp_timer_tid, memory_order_relaxed);
    s.source_tid = atomic_load_explicit(&usp_source_tid, memory_order_relaxed);
    s.ctor_tid = atomic_load_explicit(&usp_ctor_tid, memory_order_relaxed);
    s.destructor_tid = atomic_load_explicit(&usp_dtor_tid, memory_order_relaxed);
    s.timer_calls = atomic_load_explicit(&usp_timer_calls, memory_order_relaxed);
    s.source_calls = atomic_load_explicit(&usp_source_calls, memory_order_relaxed);
    s.ctor_calls = atomic_load_explicit(&usp_ctor_calls, memory_order_relaxed);
    s.destructor_calls = atomic_load_explicit(&usp_dtor_calls, memory_order_relaxed);
    s.last_clicked_row = usp_clicked;
    s.captured_by_ctor = usp_from_ctor;
    uintptr_t p = atomic_load_explicit(&usp_controller, memory_order_acquire);
    if (atomic_load_explicit(&usp_bad_thread, memory_order_acquire)) {
        s.status = USP_WRONG_THREAD;
    } else if (!usp_observing()) {
        s.status = USP_IDLE;
    } else if (!p) {
        s.status = USP_NO_CONTROLLER;
    } else if (!usp_addr_ok(p, 0x158, 8) || usp_pointer(p) != USP_CONTROLLER_VT) {
        s.status = USP_BAD_CONTROLLER;
        atomic_store_explicit(&usp_controller, 0, memory_order_release);
    } else {
        s.controller = p;
        s.model = p + USP_MODEL_OFFSET;
        s.widget = usp_pointer(p + 0x38);
        if (usp_pointer(s.model) != USP_MODEL_VT) {
            s.status = USP_BAD_MODEL;
        } else {
            uintptr_t rows = usp_pointer(s.model + 8);
            int n = usp_i32(s.model + 0x18), capacity = usp_i32(s.model + 0x10);
            if (n < 0 || n > USP_MAX_ROWS || capacity < n || capacity > 1024 ||
                (n && !usp_addr_ok(rows, (size_t)n * USP_ROW_SIZE, 8))) {
                s.status = USP_BAD_BOUNDS;
            } else {
                s.status = USP_ROWS_VALID;
                s.row_count = n;
                for (int i = 0; i < n; ++i) {
                    uintptr_t r = rows + (size_t)i * USP_ROW_SIZE;
                    usp_row *out = &s.rows[i];
                    memcpy(&out->kind, (void *)(r + 4), sizeof out->kind);
                    out->media_valid = *(const uint8_t *)(r + 8);
                    memcpy(&out->media_id, (void *)(r + 0xc), sizeof out->media_id);
                    /* +0xe/+0xf are undefined row padding: never copy them. */
                    out->focused = *(const uint8_t *)(r + 0x23);
                    out->source_active = *(const uint8_t *)(r + 0x24);
                    usp_copy_label(out->label, usp_pointer(r + 0x10));
                }
            }
        }
    }
    if (pthread_mutex_trylock(&usp_snapshot_mu) == 0) {
        usp_published = s;
        pthread_mutex_unlock(&usp_snapshot_mu);
    } else {
        atomic_store_explicit(&usp_observe_requested, 1, memory_order_release);
    }
}
static void usp_timer_hook(void *self) {
    /* SDK publishes original before making this hook reachable. Always chain. */
    usp_one_fn original = (usp_one_fn)__atomic_load_n(&usp_orig_timer, __ATOMIC_ACQUIRE);
    original(self);
    if (atomic_load_explicit(&usp_state, memory_order_acquire) != 1) return;
    atomic_fetch_add_explicit(&usp_timer_calls, 1, memory_order_relaxed);
    if (!usp_on_message_thread(&usp_timer_tid)) return;
    if (atomic_exchange_explicit(&usp_observe_requested, 0, memory_order_acq_rel))
        usp_observe();
}
static void usp_source_hook(void *self, void *model, int row) {
    if (usp_observing() && usp_on_message_thread(&usp_source_tid)) {
        atomic_fetch_add_explicit(&usp_source_calls, 1, memory_order_relaxed);
        usp_capture(self, model, row, 0);
    }
    usp_source_fn original = (usp_source_fn)__atomic_load_n(&usp_orig_source, __ATOMIC_ACQUIRE);
    original(self, model, row);
    /* No dereference after original: it may have destroyed the controller. */
}
static void usp_ctor_hook(void *self, void *widget, void *a2, void *a3, void *a4, void *a5) {
    usp_ctor_fn original = (usp_ctor_fn)__atomic_load_n(&usp_orig_ctor, __ATOMIC_ACQUIRE);
    original(self, widget, a2, a3, a4, a5);
    if (usp_observing() && usp_on_message_thread(&usp_ctor_tid)) {
        atomic_fetch_add_explicit(&usp_ctor_calls, 1, memory_order_relaxed);
        usp_capture(self, (char *)self + USP_MODEL_OFFSET, -1, 1);
    }
}
static void usp_dtor_hook(void *self) {
    /* Invalidate before native destruction even if observation has been disabled. */
    uintptr_t expected = (uintptr_t)self;
    if (atomic_compare_exchange_strong_explicit(&usp_controller, &expected, 0,
            memory_order_acq_rel, memory_order_relaxed)) {
        atomic_fetch_add_explicit(&usp_generation, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&usp_dtor_calls, 1, memory_order_relaxed);
        (void)usp_on_message_thread(&usp_dtor_tid);
        atomic_store_explicit(&usp_observe_requested, 1, memory_order_release);
    }
    usp_one_fn original = (usp_one_fn)__atomic_load_n(&usp_orig_dtor, __ATOMIC_ACQUIRE);
    original(self);
}
int usp_backend_snapshot(usp_snapshot *out) {
    if (!out || atomic_load_explicit(&usp_state, memory_order_acquire) != 1)
        return USP_NOT_READY;
    if (pthread_mutex_trylock(&usp_snapshot_mu)) return USP_BUSY;
    *out = usp_published;
    pthread_mutex_unlock(&usp_snapshot_mu);
    /* Never expose the last valid rows after a lifetime/thread/enable guard has
     * invalidated them, even if no later native timer is able to publish. */
    int invalid = 0;
    if (atomic_load_explicit(&usp_bad_thread, memory_order_acquire)) {
        out->status = USP_WRONG_THREAD; invalid = 1;
    } else if (!atomic_load_explicit(&usp_enabled, memory_order_acquire) ||
               !atomic_load_explicit(&usp_live, memory_order_acquire)) {
        out->status = USP_IDLE; invalid = 1;
    } else if (out->generation != atomic_load_explicit(&usp_generation, memory_order_acquire) ||
               out->controller != atomic_load_explicit(&usp_controller, memory_order_acquire)) {
        out->status = USP_NO_CONTROLLER; invalid = 1;
    }
    if (invalid) {
        out->controller = out->model = out->widget = 0;
        out->row_count = 0;
        memset(out->rows, 0, sizeof out->rows);
    }
    return USP_OK;
}
void usp_backend_tick(void) {
    atomic_fetch_add_explicit(&usp_heartbeat, 1, memory_order_relaxed);
}
void usp_backend_enable(int enabled) {
    if (atomic_exchange_explicit(&usp_enabled, !!enabled, memory_order_acq_rel) != !!enabled)
        atomic_fetch_add_explicit(&usp_generation, 1, memory_order_release);
    atomic_store_explicit(&usp_observe_requested, 1, memory_order_release);
}
int usp_backend_request_observation(void) {
    if (atomic_load_explicit(&usp_state, memory_order_acquire) != 1) return USP_NOT_READY;
    atomic_store_explicit(&usp_observe_requested, 1, memory_order_release);
    return USP_OK;
}
int usp_backend_request_port(int port) {
    if (port != 1 && port != 2) return USP_BAD_PORT;
    return USP_OBSERVATION_ONLY; /* deliberately no native activation implementation */
}
static int usp_same_content(const usp_snapshot *a, const usp_snapshot *b) {
    return a->generation == b->generation && a->controller == b->controller &&
           a->status == b->status && a->row_count == b->row_count &&
           a->last_clicked_row == b->last_clicked_row &&
           a->timer_tid == b->timer_tid && a->source_tid == b->source_tid &&
           a->ctor_tid == b->ctor_tid && a->destructor_tid == b->destructor_tid &&
           !memcmp(a->rows, b->rows, sizeof a->rows);
}
static void *usp_worker(void *unused) {
    (void)unused;
    unsigned seen_heartbeat = 0, quiet = 0, batches = 0;
    usp_snapshot last;
    memset(&last, 0, sizeof last);
    last.status = -1;
    for (;;) {
        struct timespec remain = {0, 500000000};
        while (nanosleep(&remain, &remain) != 0) {}
        int state = atomic_load_explicit(&usp_state, memory_order_acquire);
        if (state < 0) return NULL;
        if (!state) continue;
        unsigned heartbeat = atomic_load_explicit(&usp_heartbeat, memory_order_relaxed);
        if (heartbeat == seen_heartbeat) ++quiet; else quiet = 0;
        seen_heartbeat = heartbeat;
        if (quiet > 5) quiet = 5;
        int live = quiet < 5;
        int old_live = atomic_exchange_explicit(&usp_live, live, memory_order_acq_rel);
        if (old_live != live)
            atomic_fetch_add_explicit(&usp_generation, 1, memory_order_release);
        if ((live && atomic_load_explicit(&usp_enabled, memory_order_relaxed)) || old_live != live)
            atomic_store_explicit(&usp_observe_requested, 1, memory_order_release);
        usp_snapshot now;
        if (usp_backend_snapshot(&now) || !now.serial || usp_same_content(&last, &now)) continue;
        last = now;
        if (batches++ >= 128) continue; /* bounded diagnostic volume per process */
        usp_core->log(usp_id,
            "OBS serial=%lu gen=%lu status=%d ctrl=%lx rows=%d click=%d ctor=%d pid=%u tids(timer/source/ctor/dtor)=%u/%u/%u/%u calls=%u/%u/%u/%u",
            (unsigned long)now.serial, (unsigned long)now.generation, now.status,
            (unsigned long)now.controller, now.row_count, now.last_clicked_row, now.captured_by_ctor,
            now.process_id, now.timer_tid, now.source_tid, now.ctor_tid, now.destructor_tid,
            now.timer_calls, now.source_calls, now.ctor_calls, now.destructor_calls);
        for (int i = 0; i < now.row_count; ++i) {
            const usp_row *r = &now.rows[i];
            usp_core->log(usp_id, "ROW index=%d kind=%u media_valid=%u media=%04x focus=%u active=%u label=\"%s\"",
                i, r->kind, r->media_valid, r->media_id, r->focused, r->source_active, r->label);
        }
    }
}
static int usp_check_prologues(void) {
#ifdef USP_TEST
    return !usp_test_bad_prologue;
#else
    static const struct { uintptr_t addr; uint32_t words[4]; } expected[] = {
        {USP_TIMER_ADDR, {0xa9bb7bfd, 0x910003fd, 0xa90153f3, 0xb000d9d3}},
        {USP_SOURCE_ADDR,{0xa9b37bfd, 0x52800001, 0x910003fd, 0xa90153f3}},
        {USP_DTOR_ADDR,  {0xa9bc7bfd, 0xb0008ec8, 0xb0008ec7, 0x910003fd}},
        {USP_CTOR_ADDR,  {0xa9b77bfd, 0xd0008ecb, 0xd0008eca, 0x910003fd}}
    };
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; ++i)
        if (memcmp((void *)expected[i].addr, expected[i].words, sizeof expected[i].words)) return 0;
    return 1;
#endif
}
int usp_backend_init(const az_core_v1 *core, const char *mod_id) {
    if (!AZ_HAS(core, fw_tag) || !AZ_HAS(core, claim_hook) ||
        !AZ_HAS(core, thread_start) || !AZ_HAS(core, log) || !mod_id ||
        !core->fw_tag() || strcmp(core->fw_tag(), USP_FW_TAG)) return -10;
#if !(defined(__linux__) && defined(__aarch64__)) && !defined(USP_TEST)
    return -11; /* host harness cannot call or inspect fixed native addresses */
#else
    if (!usp_check_prologues()) return -12;
    usp_core = core;
    usp_id = mod_id;
    usp_pid = (unsigned)getpid();
    if (core->thread_start(mod_id, "usb-observe", usp_worker, NULL)) return -13;
    /* All fallible setup is done. SDK atomically publishes each trampoline before
     * installing its branch. A later claim refusal leaves earlier hooks inert and
     * forwarding; no manual unpatch or second owner is attempted. */
    struct { uintptr_t addr; void *hook; void **orig; } claims[] = {
        {USP_DTOR_ADDR, (void *)usp_dtor_hook, &usp_orig_dtor},
        {USP_SOURCE_ADDR, (void *)usp_source_hook, &usp_orig_source},
        {USP_CTOR_ADDR, (void *)usp_ctor_hook, &usp_orig_ctor},
        {USP_TIMER_ADDR, (void *)usp_timer_hook, &usp_orig_timer}
    };
    for (unsigned i = 0; i < sizeof claims / sizeof claims[0]; ++i) {
        if (core->claim_hook(mod_id, claims[i].addr, claims[i].hook, claims[i].orig) ||
            !__atomic_load_n(claims[i].orig, __ATOMIC_ACQUIRE)) {
            atomic_store_explicit(&usp_state, -1, memory_order_release);
            core->log(mod_id, "OBS disabled: claim %u failed; earlier hooks only forward until restart", i);
            return -20 - (int)i;
        }
    }
    atomic_store_explicit(&usp_live, 1, memory_order_release);
    atomic_store_explicit(&usp_observe_requested, 1, memory_order_release);
    atomic_store_explicit(&usp_state, 1, memory_order_release);
    core->log(mod_id, "OBS ready; firmware=%s; no activation implemented; waiting for live controller", USP_FW_TAG);
    return 0;
#endif
}
