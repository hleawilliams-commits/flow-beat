/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "receipt.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#if !defined(__aarch64__) || !defined(__linux__)
#error "This offline installer fixture requires Linux AArch64"
#endif
#if !defined(RC_FIXTURE) && (defined(RC_CONTROL_ORIGINAL_BEFORE_METADATA) || \
 defined(RC_CONTROL_LITERAL_AS_INSTRUCTION) || defined(RC_CONTROL_ERASE_CLEANUP_FAILURE) || \
 defined(RC_CONTROL_TOKEN_EVIDENCE_ALIAS) || defined(RC_CONTROL_WARNING_AS_REFUSAL))
#error "Controls require the authored fixture configuration"
#endif
_Static_assert(sizeof(void *) == 8, "AArch64 pointer slots");
_Static_assert(sizeof(uintptr_t) == sizeof(size_t) && sizeof(long) <= sizeof(uintptr_t),
               "selected Linux LP64 arithmetic");
_Static_assert(__atomic_always_lock_free(8, 0), "plain original pointer publication");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "bounded immutable metadata publication");

_Alignas(8) void *rc_originals[RC_BINDINGS] = {NULL};
static const rc_binding bindings[RC_BINDINGS] = {
    {(uintptr_t)&rc_target0, (void *)&rc_replacement0, &rc_originals[0]},
    {(uintptr_t)&rc_target1, (void *)&rc_replacement1, &rc_originals[1]},
    {(uintptr_t)&rc_target2, (void *)&rc_replacement2, &rc_originals[2]},
    {(uintptr_t)&rc_target0, (void *)&rc_replacement3, &rc_originals[3]}
};
typedef struct {
    _Atomic unsigned published;
    rc_metadata immutable;
    rc_attempt attempt;
} rc_record;
static rc_record records[RC_BINDINGS];
static pthread_mutex_t cm = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t writer = PTHREAD_MUTEX_INITIALIZER;
static bool initialized, initialize_attempted, epoch_created, epoch_live;
static rc_module module_value;
static uint64_t module_hooks;
static rc_attempt *rc_active;
static rc_protection *rc_current_protection;
static rc_query *rc_current_query;
static unsigned active_binding;
static uint64_t active_faults;

#ifdef RC_FIXTURE
#define CHECKPOINT(p,b,a) rc_fixture_checkpoint((p),(b),(a))
#else
#define CHECKPOINT(p,b,a) ((void)0)
#endif
static bool fault(uint64_t bit) {
#ifdef RC_FIXTURE
    return (active_faults & bit) != 0;
#else
    (void)bit;
    return false;
#endif
}
/* Internal numeric values agree with the explicitly fixture-only header bits. */
enum { F_MAP=1, F_SEAL=2, F_IUNLOCK=4, F_TUNLOCK=8, F_IRELOCK=16,
       F_TRELOCK=32, F_CLEANUP=64, F_REJECT=128, F_IMAP=256 };
static void begin(rc_raw *r, bool injected) {
    memset(r, 0, sizeof(*r));
    r->attempted = true;
    r->injected = injected;
    r->underlying_called = !injected;
}
static void returned(rc_raw *r, intptr_t value, bool failed, int error) {
    r->result = value;
    r->errno_valid = failed;
    r->error = failed ? error : 0;
    r->returned = true;
}
static int take_lock(pthread_mutex_t *mutex, unsigned binding, bool community) {
#ifdef RC_FIXTURE
    int rc = pthread_mutex_trylock(mutex);
    if (rc == EBUSY) {
        CHECKPOINT(community ? RC_TEST_CM_BUSY : RC_TEST_WRITER_BUSY, binding, NULL);
        return pthread_mutex_lock(mutex);
    }
    return rc;
#else
    (void)binding; (void)community;
    return pthread_mutex_lock(mutex);
#endif
}
static int protection_mutex(pthread_mutex_t *mutex, rc_raw *r, bool release) {
    begin(r, false);
    int value = release ? pthread_mutex_unlock(mutex) : pthread_mutex_lock(mutex);
    /* pthread returns an error number directly, not errno. */
    returned(r, value, false, 0);
    return value;
}
static long page_size(rc_raw *r) {
    begin(r, false);
    errno = 0;
    long value = sysconf(_SC_PAGESIZE);
    int error = errno;
    returned(r, value, value == -1 && error != 0, error);
    return value;
}
bool rc_checked_pages(uintptr_t address, size_t length, long size,
                      uintptr_t *base, size_t *rounded_length) {
    if (!base || !rounded_length || !address || !length || size <= 0)
        return false;
    uintptr_t pg = (uintptr_t)size;
    if ((pg & (pg - 1)) != 0 ||
        length > UINTPTR_MAX - address) return false;
    uintptr_t end = address + length;
    if (end > UINTPTR_MAX - (pg - 1)) return false;
    uintptr_t low = address & ~(pg - 1);
    uintptr_t high = (end + pg - 1) & ~(pg - 1);
    if (high <= low) return false;
    *base = low;
    *rounded_length = (size_t)(high - low);
    return true;
}
static int rc_open_maps(const char *path, int flags) {
    rc_raw *r = rc_current_query ? &rc_current_query->open : NULL;
    if (r) begin(r, false);
    int value = open(path, flags);
    int error = errno;
    if (r) returned(r, value, value == -1, error);
    return value;
}
static ssize_t rc_read_maps(int fd, void *buffer, size_t length) {
    rc_raw *r = NULL;
    if (rc_current_query) {
        if (rc_current_query->read_count == RC_READS) {
            rc_current_query->read_capacity_refused = true;
            errno = EOVERFLOW;
            return -1; /* Bounded successor refusal before another actual read. */
        }
        r = &rc_current_query->reads[rc_current_query->read_count++];
        begin(r, false);
    }
    ssize_t value = read(fd, buffer, length);
    int error = errno;
    if (r) returned(r, value, value == -1, error);
    return value;
}
static int rc_close_maps(int fd) {
    rc_raw *r = rc_current_query ? &rc_current_query->close : NULL;
    if (r) begin(r, false);
    int value = close(fd);
    int error = errno;
    if (r) returned(r, value, value == -1, error);
    return value;
}
static void *rc_map_call(void *hint, size_t length, int bits, rc_mapping_kind kind) {
    if (!rc_active || rc_active->mapping_count >= RC_MAPS) {
        if (rc_active) rc_active->status = RC_CAPACITY;
        errno = EOVERFLOW;
        return MAP_FAILED;
    }
    rc_mapping *m = &rc_active->mappings[rc_active->mapping_count++];
    m->kind = kind; m->hint = (uintptr_t)hint; m->length = length; m->protection = bits;
    bool injected = fault(kind == RC_MAP_ISLAND ? F_IMAP : F_MAP);
    begin(&m->allocation, injected);
    void *value;
    if (injected) { errno = ENOMEM; value = MAP_FAILED; }
    else value = mmap(hint, length, bits, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int error = errno;
    returned(&m->allocation, (intptr_t)value, value == MAP_FAILED, error);
    if (value != MAP_FAILED) {
        m->address = (uintptr_t)value; m->held = true;
        if (kind == RC_MAP_TRAMPOLINE) rc_active->emission.trampoline = (uintptr_t)value;
    }
    return value;
}
static rc_mapping *mapping_for(uintptr_t address) {
    if (!rc_active) return NULL;
    for (unsigned i = 0; i < rc_active->mapping_count; ++i)
        if (rc_active->mappings[i].held && rc_active->mappings[i].address == address)
            return &rc_active->mappings[i];
    return NULL;
}
static int rc_unmap_call(void *address, size_t length) {
    rc_mapping *m = mapping_for((uintptr_t)address);
    /* Internal misuse fails closed; never unmap an unowned or already attempted object. */
    if (!m || m->length != length || m->cleanup.attempted) { errno = EINVAL; return -1; }
    bool injected = fault(F_CLEANUP);
    begin(&m->cleanup, injected);
    int value;
    if (injected) { errno = EIO; value = -1; }
    else value = munmap(address, length);
    int error = errno;
    returned(&m->cleanup, value, value == -1, error);
    if (value == 0) m->held = false;
    else m->cleanup_uncertain = true;
#ifdef RC_CONTROL_ERASE_CLEANUP_FAILURE
    if (value != 0) m->held = false; /* Diagnostic-only safe negative control. */
#endif
    return value;
}
static bool rc_accept_candidate(void *address) {
    rc_mapping *m = mapping_for((uintptr_t)address);
    if (!m) return false;
    if (fault(F_REJECT)) { m->placement_rejected = true; return false; }
    m->accepted_pool = true;
    return true;
}
static uint32_t *rc_slot_observed(unsigned pool, unsigned slot, uint32_t *address) {
    rc_active->pool_index = pool;
    rc_active->pool_slot = slot;
    rc_active->island_slot_burned = true;
    rc_active->pool_base = (uintptr_t)address - (uintptr_t)slot * 16;
    rc_active->pool_length = 65536;
    rc_active->emission.island = (uintptr_t)address;
    rc_active->emission.island_instructions = (rc_span){(uintptr_t)address, (uintptr_t)address + 8};
    rc_active->emission.island_storage = (rc_span){(uintptr_t)address, (uintptr_t)address + 16};
    return address;
}
static int rc_seal_call(void *address, size_t length, int bits) {
    bool injected = fault(F_SEAL);
    begin(&rc_active->seal, injected);
    int value;
    if (injected) { errno = EIO; value = -1; }
    else value = mprotect(address, length, bits);
    int error = errno;
    returned(&rc_active->seal, value, value == -1, error);
    return value;
}
static bool protection_fault(rc_protection *p, bool relock) {
    if (!rc_active) return false;
    bool island = p == &rc_active->island_protection;
    return fault(relock ? (island ? F_IRELOCK : F_TRELOCK) :
                         (island ? F_IUNLOCK : F_TUNLOCK));
}
static int rc_protect_call(void *address, size_t length, int bits, rc_raw *r, bool inject) {
    begin(r, inject);
    int value;
    if (inject) { errno = EIO; value = -1; }
    else value = mprotect(address, length, bits);
    int error = errno;
    returned(r, value, value == -1, error);
    return value;
}
static bool span(uintptr_t start, size_t length, rc_span *out) {
    if (!start || length > UINTPTR_MAX - start) return false;
    out->begin = start; out->end = start + length; return out->end > out->begin;
}
static bool overlap(rc_span a, rc_span b) {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
/* Conservative maximum late-route emitted footprint, checked before the first
 * trampoline write. Allocation custody is already retained. Actual out and
 * precise endpoints are separately validated before island publication. */
static bool rc_prewrite_trampoline(uint32_t *trampoline) {
    rc_mapping *mapping = mapping_for((uintptr_t)trampoline);
    rc_span blob, allocation;
    if (!mapping || mapping->kind != RC_MAP_TRAMPOLINE || !mapping->held ||
        mapping->length != 4096 || !mapping->allocation.returned ||
        !span(mapping->address, mapping->length, &allocation) ||
        !span(mapping->address, 32, &blob) || blob.end > allocation.end ||
        overlap(blob, rc_active->emission.island_storage)) return false;
    for (unsigned i = 0; i < RC_BINDINGS; ++i) {
        rc_span target, original;
        if (!span(bindings[i].target, 16, &target) ||
            !span((uintptr_t)bindings[i].original, sizeof(void *), &original) ||
            overlap(blob, target) || overlap(blob, original)) return false;
        if (i != active_binding && records[i].attempt.admitted &&
            (overlap(blob, records[i].attempt.emission.island_storage) ||
             overlap(blob, records[i].attempt.emission.trampoline_storage))) return false;
    }
    if (epoch_created && overlap(blob, (rc_span){module_value.lo, module_value.hi}))
        return false;
    rc_active->trampoline_prewrite_validated = true;
    return true;
}
static bool rc_prepare_metadata(uintptr_t target, void *replacement, void **original,
                                uint32_t *island, uint32_t *trampoline, int ncopy, int out) {
    rc_metadata *m = &rc_active->emission;
    m->ncopy = (unsigned)ncopy; m->out = (unsigned)out;
    m->island = (uintptr_t)island; m->trampoline = (uintptr_t)trampoline;
    m->continuation = target + (uintptr_t)ncopy * 4;
    if (ncopy != 1 || (out != 1 && out != 4) ||
        replacement != bindings[active_binding].replacement ||
        original != bindings[active_binding].original ||
        __atomic_load_n(original, __ATOMIC_ACQUIRE) != NULL ||
        !span(target, 16, &m->target_exclusion) ||
        !span(m->island, 8, &m->island_instructions) ||
        !span(m->island, 16, &m->island_storage) ||
        !span(m->trampoline, (size_t)(out + 2) * 4, &m->trampoline_instructions) ||
        !span(m->trampoline, (size_t)(out + 4) * 4, &m->trampoline_storage) ||
        overlap(m->island_storage, m->trampoline_storage)) return false;
    rc_span blobs[2] = {m->island_storage, m->trampoline_storage};
    for (unsigned k = 0; k < 2; ++k) {
        for (unsigned i = 0; i < RC_BINDINGS; ++i) {
            rc_span t, o;
            if (!span(bindings[i].target, 16, &t) ||
                !span((uintptr_t)bindings[i].original, sizeof(void *), &o) ||
                overlap(blobs[k], t) || overlap(blobs[k], o)) return false;
            if (i != active_binding && records[i].attempt.admitted &&
                (overlap(blobs[k], records[i].attempt.emission.island_storage) ||
                 overlap(blobs[k], records[i].attempt.emission.trampoline_storage))) return false;
        }
        if (epoch_created && overlap(blobs[k], (rc_span){module_value.lo, module_value.hi}))
            return false;
    }
#ifdef RC_CONTROL_LITERAL_AS_INSTRUCTION
    m->trampoline_instructions.end = m->trampoline_storage.end;
#endif
    rc_active->prepared = true;
    records[active_binding].immutable = *m; /* Still invisible to lookup. */
    return true;
}
static bool rc_validate_permit(uintptr_t target, void *replacement, void **original) {
    const rc_metadata *m = &records[active_binding].immutable;
    return rc_active->prepared && m->binding == active_binding &&
        m->target == target && m->replacement == (uintptr_t)replacement &&
        m->original_slot == (uintptr_t)original &&
        m->owner.kind == rc_active->emission.owner.kind &&
        m->owner.process_id == rc_active->emission.owner.process_id &&
        __atomic_load_n(original, __ATOMIC_ACQUIRE) == NULL;
}
static void dbg(const char *format, ...) { (void)format; }
/* Exact pinned inputs are transformed offline by transform-installer.py.
 * Full diffs/ledger distinguish instrumentation from byte-identical helpers. */
#include "inline-receipt.inc"

bool rc_binding_get(unsigned index, rc_binding *out) {
    if (index >= RC_BINDINGS || !out) return false;
    *out = bindings[index]; return true;
}
rc_status rc_initialize(void) {
    if (take_lock(&cm, RC_BINDINGS, true) != 0) return RC_LOCK;
    if (take_lock(&writer, RC_BINDINGS, false) != 0) {
        (void)pthread_mutex_unlock(&cm); return RC_LOCK;
    }
    rc_status result = RC_ALREADY;
    if (!initialize_attempted) {
        initialize_attempted = true;
        result = RC_OK;
        for (unsigned i = 0; i < RC_BINDINGS; ++i)
            if (!bindings[i].target || (bindings[i].target & 3) ||
                !bindings[i].replacement || ((uintptr_t)bindings[i].original & 7) ||
                __atomic_load_n(bindings[i].original, __ATOMIC_ACQUIRE) != NULL)
                result = RC_INVALID;
        if (result == RC_OK) {
            azm_hooks_go_late();
            initialized = true;
        }
    }
    /* These original static entry points remain compiled, never invoked. */
    (void)&azm_hooks_dump; (void)&azm_patch_vtable;
    (void)pthread_mutex_unlock(&writer); (void)pthread_mutex_unlock(&cm);
    return result;
}
rc_status rc_open_epoch(const rc_module *input, rc_epoch *out) {
    if (!input || !out) return RC_INVALID;
    rc_module value = *input;
    if (!value.process_id || !value.id[0] ||
        !memchr(value.id, '\0', sizeof(value.id)) || value.hi <= value.lo)
        return RC_INVALID;
    if (take_lock(&cm, RC_BINDINGS, true) != 0) return RC_LOCK;
    if (take_lock(&writer, RC_BINDINGS, false) != 0) {
        (void)pthread_mutex_unlock(&cm); return RC_LOCK;
    }
    rc_status result = !initialized ? RC_NOT_READY : epoch_created ? RC_ALREADY : RC_OK;
    if (result == RC_OK) {
        module_value = value; epoch_created = true; epoch_live = true;
        out->generation = 1;
    }
    (void)pthread_mutex_unlock(&writer); (void)pthread_mutex_unlock(&cm);
    return result;
}
rc_status rc_close_epoch(const rc_epoch *input, uint64_t *nhooks) {
    if (!input || !nhooks) return RC_INVALID;
    rc_epoch token = *input;
    if (take_lock(&cm, RC_BINDINGS, true) != 0) return RC_LOCK;
    if (take_lock(&writer, RC_BINDINGS, false) != 0) {
        (void)pthread_mutex_unlock(&cm); return RC_LOCK;
    }
    rc_status result = epoch_created && token.generation == 1 ? RC_OK : RC_EPOCH;
    if (result == RC_OK) { epoch_live = false; *nhooks = module_hooks; }
    (void)pthread_mutex_unlock(&writer); (void)pthread_mutex_unlock(&cm);
    return result;
}
static rc_status install(bool community, const rc_epoch *token, unsigned index,
                         const rc_request *input, rc_attempt *out) {
    if (!input || !out || index >= RC_BINDINGS) return RC_INVALID;
    rc_request request = *input;
    rc_epoch epoch = {0};
    if (community) { if (!token) return RC_INVALID; epoch = *token; }
    memset(out, 0, sizeof(*out));
    CHECKPOINT(RC_TEST_BEFORE_ADMISSION, index, NULL);
    if (community && take_lock(&cm, index, true) != 0) return RC_LOCK;
    if (take_lock(&writer, index, false) != 0) {
        if (community) (void)pthread_mutex_unlock(&cm);
        return RC_LOCK;
    }
    rc_status status = RC_OK;
    if (!initialized) status = RC_NOT_READY;
    else if (community && (!epoch_live || epoch.generation != 1)) status = RC_EPOCH;
    else if (request.original != bindings[index].original ||
             request.replacement != bindings[index].replacement) status = RC_INVALID;
    else if (records[index].attempt.admitted) status = RC_ALREADY;
    else {
        rc_span desired;
        if (!span(bindings[index].target, 16, &desired)) status = RC_INVALID;
        for (unsigned i = 0; status == RC_OK && i < RC_BINDINGS; ++i)
            if (records[i].attempt.admitted &&
                overlap(desired, records[i].attempt.emission.target_exclusion)) status = RC_CONFLICT;
        if (status == RC_OK &&
            __atomic_load_n(bindings[index].original, __ATOMIC_ACQUIRE) != NULL) status = RC_INVALID;
        if (status == RC_OK && community &&
            ((uintptr_t)bindings[index].replacement < module_value.lo ||
             (uintptr_t)bindings[index].replacement >= module_value.hi)) status = RC_INVALID;
    }
    if (status == RC_OK) {
        rc_active = &records[index].attempt; active_binding = index;
        active_faults = 0;
#ifdef RC_FIXTURE
        active_faults = request.faults;
#endif
        rc_active->admitted = true;
        rc_active->status = RC_POLICY;
        rc_metadata *m = &rc_active->emission;
        m->binding = index; m->target = bindings[index].target;
        m->replacement = (uintptr_t)bindings[index].replacement;
        m->original_slot = (uintptr_t)bindings[index].original;
        (void)span(m->target, 16, &m->target_exclusion);
        m->owner.kind = community ? RC_OWNER_COMMUNITY : RC_OWNER_CORE;
        m->owner.process_id = community ? module_value.process_id : 1;
        if (community) memcpy(m->owner.id, module_value.id, RC_ID_BYTES);
        CHECKPOINT(RC_TEST_ADMITTED, index, rc_active);
        long pg = page_size(&rc_active->initial_page_size);
        if (pg != 4096) rc_active->status = RC_POLICY;
        else {
            g_hook_owner = community ? m->owner.id : "Core"; /* Diagnostic only. */
            rc_active->diagnostic_before = (unsigned)__atomic_load_n(&g_azm_rec_n, __ATOMIC_ACQUIRE);
            rc_active->installer_result = azm_install_hook_at(
                m->target, bindings[index].replacement, bindings[index].original, "receipt-owned-target");
            rc_active->diagnostic_after = (unsigned)__atomic_load_n(&g_azm_rec_n, __ATOMIC_ACQUIRE);
            rc_active->diagnostic_lost = (unsigned)__atomic_load_n(&g_azm_rec_lost, __ATOMIC_ACQUIRE);
            if (rc_active->redirect_published && community) {
                ++module_hooks; rc_active->nhooks_accounted = true;
            }
        }
        status = rc_active->status;
        *out = *rc_active;
        rc_active = NULL; rc_current_protection = NULL; rc_current_query = NULL;
        active_faults = 0;
    } else out->status = status;
    (void)pthread_mutex_unlock(&writer);
    if (community) (void)pthread_mutex_unlock(&cm);
    return status;
}
rc_status rc_install_core(unsigned binding, const rc_request *request, rc_attempt *out) {
    return install(false, NULL, binding, request, out);
}
rc_status rc_install_community(const rc_epoch *epoch, unsigned binding,
                               const rc_request *request, rc_attempt *out) {
    return install(true, epoch, binding, request, out);
}
bool rc_snapshot(unsigned binding, rc_attempt *out) {
    if (binding >= RC_BINDINGS || !out || take_lock(&writer, binding, false) != 0) return false;
    bool present = records[binding].attempt.admitted;
    if (present) *out = records[binding].attempt;
    (void)pthread_mutex_unlock(&writer);
    return present;
}
bool rc_lookup(uintptr_t pc, rc_metadata *out) {
    if (!out) return false;
    for (unsigned i = 0; i < RC_BINDINGS; ++i) {
        if (atomic_load_explicit(&records[i].published, memory_order_acquire) == 0) continue;
        const rc_metadata *m = &records[i].immutable;
        if ((pc >= m->island_instructions.begin && pc < m->island_instructions.end) ||
            (pc >= m->trampoline_instructions.begin && pc < m->trampoline_instructions.end)) {
            *out = *m; return true;
        }
    }
    return false;
}
int rc_query_protection(uintptr_t address, size_t length) {
    if (!address || !length || length > UINTPTR_MAX - address ||
        take_lock(&writer, RC_BINDINGS, false) != 0) return -1;
    rc_current_protection = NULL; rc_current_query = NULL;
    int result = azprot_query(address, address + length);
    (void)pthread_mutex_unlock(&writer);
    return result;
}
#ifdef RC_FIXTURE
void rc_fixture_saturate_diagnostics(void) {
    if (take_lock(&writer, RC_BINDINGS, false) != 0) return;
    __atomic_store_n(&g_azm_rec_n, AZM_REC_MAX, __ATOMIC_RELEASE);
    (void)pthread_mutex_unlock(&writer);
}
#endif
