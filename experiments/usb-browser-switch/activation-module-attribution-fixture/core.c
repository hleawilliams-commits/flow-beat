/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "core.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#if defined(DP_CONTROL_RA_HOST_SUBSTITUTE) && !defined(DP_FIXTURE)
#error "RA substitution is an explicit offline control"
#endif
_Static_assert(RC_ID_BYTES == CC_ID_BYTES && CC_ID_BYTES == 33, "complete SDK ID");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "single bounded session handle");

/* The selected exact helpers use only id/lo/hi and g_cm_n. Mutable lifecycle
 * fields are separate from the once-published identity/range subobjects.
 * Every write to g_cm/g_cm_n uses cm->writer. New fault readers and writer-held
 * storage exclusion read separate immutable ma_modules, never lifecycle fields. */
typedef cc_module_snapshot cm_mod;
static cm_mod g_cm[CC_MODULES];
static int g_cm_n;
static pthread_mutex_t g_cm_mu = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static int g_sdk_in_init, g_cm_loading = -1;
static uintptr_t g_cm_text_lo, g_cm_text_hi;
static _Atomic unsigned active_session;
static _Atomic unsigned loader_started, runner_active;
static _Atomic bool terminal_incomplete;
typedef struct {
    unsigned index;
    rc_owner owner;
} cc_session;
static cc_session sessions[CC_MODULES];
typedef struct {
    bool valid;
    unsigned binding;
    rc_owner owner;
    const cc_session *session;
} cc_permit;
static cc_permit bound_permit;
static bool cc_storage_overlaps_modules(rc_span value);
static bool ma_storage_overlaps(rc_span value);
static bool cc_metadata_owner_valid(const rc_metadata *value);
static bool cc_permit_matches(const rc_metadata *value);

/* Explicitly transformed receipt support; owns the only common writer and the
 * actual fixed originals, resource journals and installer publication cut. */
#include "receipt-body.inc"
/* Three byte-identical retained SDK bodies, not a copied claim/loader. */
#include "pinned-community.inc"
#define CM_RA() ((uintptr_t) __builtin_extract_return_addr(__builtin_return_address(0)))

static bool cc_storage_overlaps_modules(rc_span value) {
    return ma_storage_overlaps(value);
}
static bool cc_metadata_owner_valid(const rc_metadata *value) {
    return bound_permit.valid && value->binding == bound_permit.binding &&
        value->owner.kind == bound_permit.owner.kind &&
        value->owner.process_id == bound_permit.owner.process_id &&
        memcmp(value->owner.id, bound_permit.owner.id, RC_ID_BYTES) == 0;
}
static bool cc_permit_matches(const rc_metadata *value) {
    if (!bound_permit.valid || value->binding != bound_permit.binding ||
        value->owner.kind != bound_permit.owner.kind ||
        value->owner.process_id != bound_permit.owner.process_id) return false;
    if (bound_permit.owner.kind == RC_OWNER_CORE)
        return bound_permit.session == NULL;
    const cc_session *s = bound_permit.session;
    /* Only this private pointer, bound after locked admission, is dereferenced.
     * No captured handle refresh, second mutable epoch veto, or loader lookup.
     * Complete ID equality was checked in invisible metadata preparation; the
     * baseline does not mutate metadata again before the fixed cut. */
    return s && s->index < CC_MODULES && s == &sessions[s->index] &&
        s->owner.kind == bound_permit.owner.kind &&
        s->owner.process_id == bound_permit.owner.process_id &&
        memcmp(s->owner.id, bound_permit.owner.id, RC_ID_BYTES) == 0;
}
static int cc_lock(bool community, unsigned index) {
    (void)index;
    int error = pthread_mutex_lock(community ? &g_cm_mu : &writer);
    if (error) atomic_store_explicit(&terminal_incomplete, true, memory_order_release);
    return error;
}
static int cc_unlock(bool community) {
    int error = pthread_mutex_unlock(community ? &g_cm_mu : &writer);
    if (error) atomic_store_explicit(&terminal_incomplete, true, memory_order_release);
    return error;
}
static bool cc_both(unsigned index) {
    if (cc_lock(true, index)) return false;
    if (cc_lock(false, index)) {
        (void)cc_unlock(true);
        return false;
    }
    return true;
}
static bool cc_release_both(void) {
    int w = cc_unlock(false);
    int c = cc_unlock(true);
    return !w && !c;
}
/* Separate immutable module reader and exact modeled fault consumer. */
#include "attribution.inc"

unsigned dp_active_session(void) {
    return atomic_load_explicit(&active_session, memory_order_acquire);
}
cc_status dp_core_initialize(uintptr_t text_lo, uintptr_t text_hi) {
    if (!cc_both(CC_MODULES)) return CC_LOCK;
    cc_status result = CC_ALREADY;
    if (!initialize_attempted) {
        initialize_attempted = true;
        result = text_hi > text_lo ? CC_OK : CC_INVALID;
        for (unsigned i = 0; result == CC_OK && i < RC_BINDINGS; ++i)
            if (!bindings[i].target || (bindings[i].target & 3) ||
                !bindings[i].replacement || ((uintptr_t)bindings[i].original & 7) ||
                __atomic_load_n(bindings[i].original, __ATOMIC_ACQUIRE) != NULL)
                result = CC_INVALID;
        if (result == CC_OK) {
            g_cm_text_lo = text_lo;
            g_cm_text_hi = text_hi;
            azm_hooks_go_late();
            initialized = true;
        }
    }
    (void)&azm_hooks_dump;
    (void)&azm_patch_vtable; /* compiled retained helper, never called */
    if (!cc_release_both()) result = CC_LOCK;
    return result;
}
static const az_core_v1 dp_core_table = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_core_v1), .claim_hook = cc_claim_hook
};
/* Host binding identities remain the exact same immutable process-image objects.
 * This fixture-only export is outside the SDK table and supplies no setter. */
static const dp_fixture_input dp_inputs[RC_BINDINGS] = {
    {"mod-a", (uintptr_t)&rc_target0, (void *)&rc_replacement0, &rc_originals[0]},
    {"mod-b", (uintptr_t)&rc_target1, (void *)&rc_replacement1, &rc_originals[1]},
    {"mod-c", (uintptr_t)&rc_target2, (void *)&rc_replacement2, &rc_originals[2]},
    {"mod-d", (uintptr_t)&rc_target0, (void *)&rc_replacement3, &rc_originals[3]}
};
const dp_fixture_input *dp_fixture_input_get(unsigned index) {
    return index < RC_BINDINGS ? &dp_inputs[index] : NULL;
}
static bool registration_attempted[CC_MODULES], registration_closed;
cc_status dp_core_reserve(unsigned count) {
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&loader_started, &expected, 1u,
            memory_order_acq_rel, memory_order_acquire)) return CC_ALREADY;
    if (count == 0 || count > CC_MODULES) return CC_INVALID;
    if (!cc_both(CC_MODULES)) return CC_LOCK;
    cc_status result = initialized ? CC_OK : CC_NOT_READY;
    if (atomic_load_explicit(&terminal_incomplete, memory_order_acquire)) result = CC_INCOMPLETE;
    if (result == CC_OK) g_cm_n = (int)count; /* Reserved holes stay fixed. */
    if (!cc_release_both()) result = CC_LOCK;
    return result;
}
cc_status ma_root_register(const ma_root_input *input) {
    if (!input) return CC_INVALID;
    ma_root_input request = *input;
    const unsigned index = request.index;
    char copied_id[33];
    if (index >= CC_MODULES || !request.process_id ||
        !ma_copy_id(request.id, copied_id)) return CC_INVALID;
#ifndef MA_FIXTURE
    if (request.dry) return CC_INVALID;
#endif
    if (!cc_both(index)) return CC_LOCK;
    cc_status result = CC_OK;
    if (!initialized || index >= (unsigned)g_cm_n) result = CC_NOT_READY;
    else if (atomic_load_explicit(&terminal_incomplete, memory_order_acquire)) result = CC_INCOMPLETE;
    else if (registration_attempted[index]) result = CC_ALREADY;
    else if (registration_closed) result = CC_NOT_READY;
    else if (atomic_load_explicit(&runner_active, memory_order_acquire) ||
             dp_active_session() || g_sdk_in_init) result = CC_BUSY;
    else {
        registration_attempted[index] = true;
        for (unsigned i = 0; i < (unsigned)g_cm_n; ++i)
            if (g_cm[i].present && !strcmp(g_cm[i].id, copied_id)) result = CC_INVALID;
        bool consumed = request.consumed_success;
#ifdef MA_CONTROL_RAW_QUERY
        if (request.dry) consumed = true; /* One explicitly dry consumption veto. */
#endif
        if (result == CC_OK && (!request.raw_valid || !consumed || !request.held))
            result = CC_CUSTODY_REFUSED;
        if (result == CC_OK && (request.hi <= request.lo ||
            request.fault_hi <= request.fault_lo || request.fault_lo < request.lo ||
            request.fault_hi > request.hi)) result = CC_RANGE_REFUSED;
        if (result == CC_OK && !ma_stage_locked(index, copied_id, request.process_id,
                request.fault_lo, request.fault_hi, false)) result = CC_ALREADY;
        if (result == CC_OK) {
            bool publish = true;
#ifdef MA_CONTROL_OMIT_DESCRIPTOR_PUBLICATION
            publish = index != 0;
#endif
            if (publish && !ma_commit_locked(index)) result = CC_INCOMPLETE;
        }
        /* Positive immutable ownership precedes capability/lifecycle results.
         * A dry query selector ends here and never creates an init record. */
        if (result == CC_OK && !request.dry) {
            cm_mod *c = &g_cm[index];
            c->present = true; c->index = index; c->process_id = request.process_id;
            memcpy(c->id, copied_id, CC_ID_BYTES);
            c->handle_retained = request.held;
            c->firmware_any = request.firmware_any;
            c->hooks_ok = request.hooks && !request.firmware_any;
            c->phase = CC_REGISTERED;
            if (request.hooks && request.firmware_any) c->load_status = CC_HOOKS_REFUSED;
            else {
                c->lo = request.lo; c->hi = request.hi;
                c->range_published = true; c->load_status = CC_OK;
            }
            if (!c->range_published) c->phase = CC_RANGE_FAILED;
            result = c->load_status;
        }
    }
    if (!cc_release_both()) result = CC_LOCK;
    return result;
}
bool cc_module_get(unsigned index, cc_module_snapshot *out) {
    if (!out || index >= CC_MODULES || !cc_both(index)) return false;
    bool found = index < (unsigned)g_cm_n && g_cm[index].present;
    if (found) *out = g_cm[index];
    bool released = cc_release_both();
    return found && released;
}
static cc_status cc_close_locked(unsigned index) {
    cm_mod *c = &g_cm[index];
    if (!c->session_used || !c->session_open || g_cm_loading != (int)index ||
        !g_sdk_in_init || dp_active_session() != index + 1u) {
        atomic_store_explicit(&terminal_incomplete, true, memory_order_release);
        return CC_INCOMPLETE;
    }
    /* Admitted claims already accounted while holding the same cm->writer. */
    atomic_store_explicit(&active_session, 0u, memory_order_release);
    c->session_open = false;
    g_sdk_in_init = 0;
    g_cm_loading = -1;
    return atomic_load_explicit(&terminal_incomplete, memory_order_acquire)
        ? CC_INCOMPLETE : CC_OK;
}
cc_status dp_core_run(unsigned index, dp_entry_fn entry,
    dp_descriptor_validator validator, void *context, cc_module_snapshot *out) {
    if (!out || index >= CC_MODULES) return CC_INVALID;
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&runner_active, &expected, 1u,
            memory_order_acq_rel, memory_order_acquire)) return CC_BUSY;
    if (!cc_both(index)) return CC_LOCK; /* unresolved lock failure stays terminal */
    cc_status result = CC_OK;
    if (!initialized) result = CC_NOT_READY;
    else if (atomic_load_explicit(&terminal_incomplete, memory_order_acquire))
        result = CC_INCOMPLETE;
    else if (index >= (unsigned)g_cm_n || !g_cm[index].present) result = CC_INVALID;
    else if (!g_cm[index].range_published) result = g_cm[index].load_status;
    else if (g_cm[index].entry_attempted || g_cm[index].session_used) result = CC_ALREADY;
    else if (g_sdk_in_init || dp_active_session()) result = CC_BUSY;
    if (result != CC_OK) {
        if (index < (unsigned)g_cm_n) *out = g_cm[index];
        bool released = cc_release_both();
        atomic_store_explicit(&runner_active, 0u, memory_order_release);
        return released ? result : CC_LOCK;
    }
    cm_mod *c = &g_cm[index];
    registration_closed = true; /* The fixed load phase never reopens. */
    c->entry_attempted = true;
    if (!cc_release_both()) return CC_LOCK;
    /* Actual SDK descriptor entry and custody validator, with no locks held. */
    dp_lifecycle_event(index, 3u);
    const az_mod_v1 *raw_descriptor = entry ? entry() : NULL;
    bool custody_ok = raw_descriptor && validator &&
        validator(index, raw_descriptor, context) == 1;
    az_mod_v1 descriptor = {0};
    if (custody_ok) descriptor = *raw_descriptor;
    char descriptor_id[CC_ID_BYTES] = {0};
    size_t descriptor_length = custody_ok && descriptor.id
        ? strnlen(descriptor.id, CC_ID_BYTES) : CC_ID_BYTES;
    bool id_valid = descriptor_length < CC_ID_BYTES;
    if (id_valid) memcpy(descriptor_id, descriptor.id, descriptor_length + 1);
    bool tag_ok = false, tag_any = false;
    if (custody_ok && descriptor.fw_tags) {
        /* Validator proves each bounded array/string access before this loop.
         * Index is checked before access, correcting the old SDK loop order. */
        for (unsigned i = 0; i < 32u; ++i) {
            const char *tag = descriptor.fw_tags[i];
            if (!tag) break;
            if (!strcmp(tag, "fixture-fw")) tag_ok = true;
            else if (!strcmp(tag, "any")) tag_any = true;
        }
    }
    if (!cc_both(index)) return CC_LOCK;
    c->entry_returned = true;
    if (!custody_ok || !id_valid || strcmp(descriptor_id, c->id) ||
        descriptor.abi != AZ_MOD_ABI ||
        descriptor.size < (int)(offsetof(az_mod_v1, on_frame) + sizeof descriptor.on_frame) ||
        !(tag_ok || (tag_any && c->firmware_any)) || !descriptor.init) {
        c->phase = CC_DESCRIPTOR_FAILED;
        c->load_status = CC_DESCRIPTOR_REFUSED;
        *out = *c;
        bool released = cc_release_both();
        atomic_store_explicit(&runner_active, 0u, memory_order_release);
        return released ? CC_DESCRIPTOR_REFUSED : CC_LOCK;
    }
    if (atomic_load_explicit(&terminal_incomplete, memory_order_acquire)) {
        *out = *c;
        (void)cc_release_both();
        return CC_INCOMPLETE;
    }
    c->descriptor_accepted = true;
    c->callbacks_retained = true;
    c->session_used = true; c->session_open = true;
    c->phase = CC_INITIALIZING;
    sessions[index].index = index;
    sessions[index].owner.kind = RC_OWNER_COMMUNITY;
    sessions[index].owner.process_id = c->process_id;
    memcpy(sessions[index].owner.id, c->id, CC_ID_BYTES);
    g_cm_loading = (int)index;
    g_sdk_in_init = 1;
    atomic_store_explicit(&active_session, index + 1u, memory_order_release);
    c->init_attempted = true;
    if (!cc_release_both()) return CC_LOCK;
    dp_lifecycle_event(index, 4u); /* session already opened; locks released */
    dp_lifecycle_event(index, 5u);
    int init_result = descriptor.init(&dp_core_table);
    if (!cc_both(index)) return CC_LOCK;
    c->init_returned = true;
    c->init_result = init_result;
    result = cc_close_locked(index);
    if (result == CC_OK) {
        if (init_result) {
            c->callbacks_retained = false;
            c->phase = CC_FAILED;
            c->load_status = CC_INIT_FAILED;
#ifdef MA_CONTROL_CLEAR_INIT
            if (index == 0) atomic_store_explicit(&ma_modules[index].committed, 0u,
                                                memory_order_release);
#endif
            result = CC_INIT_FAILED;
        } else {
            c->phase = CC_LOADED;
            c->load_status = CC_OK;
        }
    } else {
        c->phase = CC_FAILED;
        c->callbacks_retained = false;
        c->load_status = CC_INCOMPLETE;
    }
    *out = *c;
    bool released = cc_release_both();
    atomic_store_explicit(&runner_active, 0u, memory_order_release);
    if (released) dp_lifecycle_event(index, 6u);
    return released ? result : CC_LOCK;
}

/* Writer-held internal entry: no second cm/epoch/counter and no recursive
 * public installer call. Identity matches before any supplied orig dereference. */
static rc_status cc_install_locked(unsigned index, const rc_request *request,
                                   const rc_owner *owner, const cc_session *session,
                                   rc_attempt *out) {
    memset(out, 0, sizeof(*out));
    rc_status status = RC_OK;
    if (!initialized) status = RC_NOT_READY;
    else if (atomic_load_explicit(&terminal_incomplete, memory_order_acquire)) status = RC_LOCK;
    else if (index >= RC_BINDINGS ||
             request->original != bindings[index].original ||
             request->replacement != bindings[index].replacement) status = RC_INVALID;
    else if (records[index].attempt.admitted) status = RC_ALREADY;
    else {
        rc_span desired;
        if (!span(bindings[index].target, 16, &desired)) status = RC_INVALID;
        for (unsigned i = 0; status == RC_OK && i < RC_BINDINGS; ++i)
            if (records[i].attempt.admitted &&
                overlap(desired, records[i].attempt.emission.target_exclusion)) status = RC_CONFLICT;
        if (status == RC_OK &&
            __atomic_load_n(bindings[index].original, __ATOMIC_ACQUIRE) != NULL)
            status = RC_INVALID;
    }
    if (status != RC_OK) { out->status = status; return status; }
    bound_permit = (cc_permit){true, index, *owner, session};
    rc_active = &records[index].attempt;
    active_binding = index;
    active_faults = 0;
#ifdef RC_FIXTURE
    active_faults = request->faults;
#endif
    rc_active->admitted = true;
    rc_active->status = RC_POLICY;
    rc_metadata *m = &rc_active->emission;
    m->binding = index; m->target = bindings[index].target;
    m->replacement = (uintptr_t)bindings[index].replacement;
    m->original_slot = (uintptr_t)bindings[index].original;
    (void)span(m->target, 16, &m->target_exclusion);
    m->owner = *owner;
    CHECKPOINT(RC_TEST_ADMITTED, index, rc_active);
    long pg = page_size(&rc_active->initial_page_size);
    if (pg == 4096) {
        g_hook_owner = owner->kind == RC_OWNER_COMMUNITY ? owner->id : "Core";
        rc_active->diagnostic_before = (unsigned)__atomic_load_n(&g_azm_rec_n, __ATOMIC_ACQUIRE);
        rc_active->installer_result = azm_install_hook_at(
            m->target, bindings[index].replacement, bindings[index].original, "claim-receipt-owned-target");
        rc_active->diagnostic_after = (unsigned)__atomic_load_n(&g_azm_rec_n, __ATOMIC_ACQUIRE);
        rc_active->diagnostic_lost = (unsigned)__atomic_load_n(&g_azm_rec_lost, __ATOMIC_ACQUIRE);
        g_hook_owner = "AZ-MODS";
    }
    status = rc_active->status;
    *out = *rc_active;
    bound_permit.valid = false;
    rc_active = NULL; rc_current_protection = NULL; rc_current_query = NULL;
    active_faults = 0;
    return status;
}
rc_status cc_install_core(unsigned index, const rc_request *input, rc_attempt *out) {
    if (!input || !out || index >= RC_BINDINGS) return RC_INVALID;
    rc_request request = *input;
    if (cc_lock(false, index)) return RC_LOCK;
    const rc_owner owner = {RC_OWNER_CORE, 1, {0}};
    rc_status result = cc_install_locked(index, &request, &owner, NULL, out);
    if (cc_unlock(false)) return RC_LOCK;
    return result;
}
static int cc_claim_worker(const char *input_id, uintptr_t target, void *replacement,
                           void **original, uintptr_t ra, unsigned captured,
                           uint64_t faults, cc_claim_result *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->sdk_result = -1;
    out->module_index = CC_MODULES;
    out->captured_session = captured;
    char id[CC_ID_BYTES] = {0};
    size_t id_length = input_id ? strnlen(input_id, CC_ID_BYTES) : CC_ID_BYTES;
    bool id_valid = id_length < CC_ID_BYTES;
    if (id_valid) memcpy(id, input_id, id_length + 1);
    if (!cc_both(CC_MODULES)) { out->status = CC_LOCK; return -1; }
    cm_mod *c = id_valid ? cm_find(id) : NULL;
    cc_status status = CC_OK;
    unsigned index = c ? (unsigned)(c - g_cm) : CC_MODULES;
    unsigned current = dp_active_session();
    out->resolved_session = current;
    out->module_index = index;
    if (!initialized) status = CC_NOT_READY;
    else if (atomic_load_explicit(&terminal_incomplete, memory_order_acquire)) status = CC_INCOMPLETE;
    else if (!c) status = CC_UNKNOWN_ID;
    else if (!cm_called_by(c, ra)) status = CC_CALLER_REFUSED;
    else if (!g_sdk_in_init || index != (unsigned)g_cm_loading) status = CC_INIT_REFUSED;
    else if (!c->hooks_ok) status = CC_HOOKS_REFUSED;
    else if (target < g_cm_text_lo || target >= g_cm_text_hi) status = CC_TARGET_REFUSED;
    else if (captured == 0 || captured != current || captured != index + 1u)
        status = CC_SESSION_REFUSED;
    else if (c->nhooks >= RC_BINDINGS) status = CC_INSTALLER_REFUSED;
    unsigned binding = RC_BINDINGS;
    if (status == CC_OK) {
        for (unsigned i = 0; i < RC_BINDINGS; ++i)
            if (target == bindings[i].target && replacement == bindings[i].replacement &&
                original == bindings[i].original) { binding = i; break; }
        if (binding == RC_BINDINGS) status = CC_CUSTODY_REFUSED;
    }
    if (c) out->nhooks_before = c->nhooks;
    if (status == CC_OK) {
        rc_request request = {0};
        request.original = original; request.replacement = replacement;
#ifdef RC_FIXTURE
        request.faults = faults;
#else
        (void)faults;
#endif
        /* Build from the admitted current immutable session, never refresh an
         * entry token or add a second mutable epoch authority inside installer. */
        const cc_session *session = &sessions[index];
        (void)cc_install_locked(binding, &request, &session->owner, session, &out->receipt);
        out->installer_called = out->receipt.installer_entered;
        if (out->receipt.redirect_published) {
            ++c->nhooks;
            records[binding].attempt.nhooks_accounted = true;
            out->receipt.nhooks_accounted = true;
            out->accounted = true;
            if (out->receipt.metadata_published && out->receipt.original_published &&
                out->receipt.installer_result == 1 &&
                (out->receipt.status == RC_OK || out->receipt.status == RC_WARNINGS)) {
                out->sdk_result = 0;
                status = CC_OK;
            } else {
                status = CC_INCOMPLETE;
                out->incomplete = true;
                atomic_store_explicit(&terminal_incomplete, true, memory_order_release);
            }
        } else {
            status = CC_INSTALLER_REFUSED;
            if (out->receipt.metadata_published || out->receipt.original_published ||
                out->receipt.installer_result == 1) {
                status = CC_INCOMPLETE;
                out->incomplete = true;
                atomic_store_explicit(&terminal_incomplete, true, memory_order_release);
            }
        }
    }
    if (c) out->nhooks_after = c->nhooks;
    out->status = status;
    if (!cc_release_both()) {
        out->status = CC_LOCK; out->sdk_result = -1; out->incomplete = true;
    }
    return out->sdk_result;
}
/* Process-lifetime diagnostic slots are independent of installer authority.
 * Publication cannot turn a returned install into refusal when diagnostics fill.
 * They copy bits/results only after the worker returns; no native pointee reads. */
typedef struct { _Atomic unsigned committed; dp_claim_observation value; } dp_observation_slot;
static dp_observation_slot dp_observations[DP_CLAIM_OBSERVATIONS];
static _Atomic unsigned dp_observation_next, dp_observation_dropped;
unsigned dp_claim_count(void) {
    unsigned n = atomic_load_explicit(&dp_observation_next, memory_order_acquire);
    return n > DP_CLAIM_OBSERVATIONS ? DP_CLAIM_OBSERVATIONS : n;
}
unsigned dp_claim_dropped(void) {
    return atomic_load_explicit(&dp_observation_dropped, memory_order_acquire);
}
bool dp_claim_get(unsigned sequence, dp_claim_observation *out) {
    if (!out || sequence >= DP_CLAIM_OBSERVATIONS ||
        !atomic_load_explicit(&dp_observations[sequence].committed, memory_order_acquire)) return false;
    *out = dp_observations[sequence].value;
    return true;
}
#ifdef DP_CONTROL_RA_HOST_SUBSTITUTE
__attribute__((noinline)) static void dp_host_ra_marker(void) { __asm__ volatile("" ::: "memory"); }
#endif
__attribute__((noinline))
int cc_claim_hook(const char *id, uintptr_t target, void *replacement, void **original) {
    uintptr_t raw_ra = CM_RA();
    unsigned captured = atomic_load_explicit(&active_session, memory_order_acquire);
    uintptr_t used_ra = raw_ra;
#ifdef DP_CONTROL_RA_HOST_SUBSTITUTE
    used_ra = (uintptr_t)&dp_host_ra_marker;
#endif
    cc_claim_result result;
    int returned_result = cc_claim_worker(id, target, replacement, original,
        used_ra, captured, 0, &result);
    unsigned slot = atomic_load_explicit(&dp_observation_next, memory_order_relaxed);
    while (slot < DP_CLAIM_OBSERVATIONS &&
           !atomic_compare_exchange_weak_explicit(&dp_observation_next, &slot, slot + 1u,
               memory_order_relaxed, memory_order_relaxed)) { }
    if (slot < DP_CLAIM_OBSERVATIONS) {
        dp_claim_observation *v = &dp_observations[slot].value;
        v->raw_ra = raw_ra; v->used_ra = used_ra; v->id_pointer = (uintptr_t)id;
        v->target = target; v->replacement = (uintptr_t)replacement;
        v->original = (uintptr_t)original; v->captured_session = captured;
        v->returned_sdk_result = returned_result; v->claim = result;
        atomic_store_explicit(&dp_observations[slot].committed, 1u, memory_order_release);
    } else atomic_fetch_add_explicit(&dp_observation_dropped, 1u, memory_order_relaxed);
    return returned_result;
}
