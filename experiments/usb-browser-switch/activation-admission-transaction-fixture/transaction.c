#include "transaction.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if !defined(TX_FIXTURE) && (defined(TX_CONTROL_SKIP_EPOCH_RECHECK) || \
    defined(TX_CONTROL_OMIT_HOOK_ACCOUNTING) || defined(TX_CONTROL_ALLOW_LATE_FALLBACK) || \
    defined(TX_CONTROL_ACCEPT_BAD_PERMIT) || defined(TX_CONTROL_ACCEPT_FAILED_PROVENANCE) || \
    defined(TX_CONTROL_MODULE_PREFIX_SCAN) || defined(TX_CONTROL_ORIGINAL_BEFORE_METADATA))
#error "Negative controls are fixture-only"
#endif

_Static_assert(TX_SLOTS <= 512u, "bounded inline metadata capacity");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "publication flag must always be lock-free");
_Static_assert(sizeof(uintptr_t) == sizeof(unsigned long) && ATOMIC_LONG_LOCK_FREE == 2,
               "fixture integer slots require always-lock-free uintptr_t");

typedef struct {
    atomic_uint published;
    tx_module_snapshot value;
} module_slot;
typedef struct {
    atomic_uint published;
    bool reserved;
    tx_owner owner;
    tx_span target;
    tx_layout layout;
    uint64_t attempt;
    atomic_uintptr_t original, redirect;
} emission_slot;

struct tx_system {
    pthread_mutex_t cm, writer;
    bool cm_held, writer_held, initialized, destroying;
    uint64_t process_id, instance;
    bool loader_started, late, current_init_open;
    uint32_t current_init_index;
    uint64_t next_epoch, current_init_epoch;
    size_t attempts, reserved_slots;
    module_slot modules[TX_MODULES];
    emission_slot slots[TX_SLOTS];
    tx_receipt receipts[TX_ATTEMPTS];
#ifdef TX_FIXTURE
    tx_test_hook hook;
    void *hook_context;
    bool hook_set;
#endif
};

#ifdef TX_FIXTURE
#define HOOK(s, point, kind, raw, receipt) do { \
    if ((s)->hook != NULL) \
        (s)->hook((point), (kind), (raw), (receipt), (s)->hook_context); \
} while (0)
#else
#define HOOK(s, point, kind, raw, receipt) ((void)0)
#endif

static bool usable(const tx_system *s) {
    return s != NULL && s->initialized && !s->destroying;
}

/* Only fixture builds observe contention; neither path changes mutex ownership. */
static int lock_one(tx_system *s, pthread_mutex_t *mutex, unsigned kind) {
#ifdef TX_FIXTURE
    int raw = pthread_mutex_trylock(mutex);
    if (raw == 0) return 0;
    if (raw != EBUSY) return raw;
    HOOK(s, TX_TEST_LOCK_BUSY, (tx_lock_kind)kind, raw, NULL);
#else
    (void)s;
    (void)kind;
#endif
    return pthread_mutex_lock(mutex);
}

static tx_error lock_both(tx_system *s) {
    if (lock_one(s, &s->cm, 1u) != 0) return TX_SYNC;
    if (lock_one(s, &s->writer, 2u) != 0) {
        (void)pthread_mutex_unlock(&s->cm);
        return TX_SYNC;
    }
    return TX_OK;
}
static tx_error unlock_both(tx_system *s, tx_error result) {
    int writer_result = pthread_mutex_unlock(&s->writer);
    int cm_result = pthread_mutex_unlock(&s->cm);
    return writer_result == 0 && cm_result == 0 ? result : TX_SYNC;
}
static tx_error unlock_writer(tx_system *s, tx_error result) {
    return pthread_mutex_unlock(&s->writer) == 0 ? result : TX_SYNC;
}

static bool id_copy(char out[TX_ID_MAX + 1u], const char *id, size_t n) {
    if (id == NULL || n == 0 || n > TX_ID_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return false;
        out[i] = (char)c;
    }
    out[n] = '\0';
    return true;
}
static bool same_owner(const tx_owner *a, const tx_owner *b) {
    return a->kind == b->kind && a->process_id == b->process_id &&
           a->module_index == b->module_index &&
           memcmp(a->id, b->id, sizeof(a->id)) == 0;
}
static bool overlap(tx_span a, tx_span b) {
    return a.begin < a.end && b.begin < b.end &&
           a.begin < b.end && b.begin < a.end;
}
static bool contains(tx_span range, uintptr_t address) {
    return address >= range.begin && address < range.end;
}
static bool end_checked(uintptr_t base, uintptr_t size, uintptr_t *end) {
    if (size > UINTPTR_MAX - base) return false;
    *end = base + size;
    return true;
}
static bool blobs_touch(const tx_layout *a, const tx_layout *b) {
    return overlap(a->island_blob, b->island_blob) ||
           overlap(a->island_blob, b->trampoline_blob) ||
           overlap(a->trampoline_blob, b->island_blob) ||
           overlap(a->trampoline_blob, b->trampoline_blob);
}
static bool blob_touches(const tx_layout *a, tx_span b) {
    return overlap(a->island_blob, b) || overlap(a->trampoline_blob, b);
}

static tx_error layout_for(const tx_plan *p, tx_layout *layout) {
    memset(layout, 0, sizeof(*layout));
    if ((p->ncopy != 1u && p->ncopy != 4u) || p->expanded_adrps > p->ncopy ||
        p->trampoline == 0 || (p->trampoline & 3u) != 0 ||
        (p->ncopy == 1u && (p->island == 0 || (p->island & 3u) != 0)) ||
        (p->ncopy == 4u && p->island != 0)) return TX_LAYOUT;
    layout->ncopy = p->ncopy;
    layout->expanded_adrps = p->expanded_adrps;
    layout->trampoline_instructions.begin = p->trampoline;
    /* At most 16 relocated words plus the two-word absolute branch. */
    uintptr_t bytes = 4u * (p->ncopy + 3u * p->expanded_adrps + 2u);
    if (!end_checked(p->trampoline, bytes, &layout->trampoline_instructions.end) ||
        !end_checked(layout->trampoline_instructions.end, 8u, &layout->trampoline_blob.end))
        return TX_LAYOUT;
    layout->trampoline_blob.begin = p->trampoline;
    if (p->ncopy == 1u) {
        layout->island_instructions.begin = p->island;
        layout->island_blob.begin = p->island;
        if (!end_checked(p->island, 8u, &layout->island_instructions.end) ||
            !end_checked(p->island, 16u, &layout->island_blob.end))
            return TX_LAYOUT;
        if (overlap(layout->island_blob, layout->trampoline_blob)) return TX_STORAGE_CONFLICT;
    }
    return TX_OK;
}

tx_error tx_create(uint64_t process_id, uint64_t instance, tx_system **out) {
    if (out == NULL) return TX_INVALID;
    *out = NULL;
    if (process_id == 0 || instance == 0) return TX_INVALID;
    tx_system *s = calloc(1u, sizeof(*s));
    if (s == NULL) return TX_NOMEM;
    *out = s; /* Retain actual allocation before fallible pthread initialization. */
    s->process_id = process_id;
    s->instance = instance;
    s->current_init_index = TX_NO_INDEX;
    for (size_t i = 0; i < TX_MODULES; ++i) atomic_init(&s->modules[i].published, 0u);
    for (size_t i = 0; i < TX_SLOTS; ++i) {
        atomic_init(&s->slots[i].published, 0u);
        atomic_init(&s->slots[i].original, 0u);
        atomic_init(&s->slots[i].redirect, 0u);
    }
    if (pthread_mutex_init(&s->cm, NULL) != 0) return TX_SYNC;
    s->cm_held = true;
    if (pthread_mutex_init(&s->writer, NULL) != 0) return TX_SYNC;
    s->writer_held = true;
    s->initialized = true;
    return TX_OK;
}

tx_error tx_destroy(tx_system **slot) {
    if (slot == NULL || *slot == NULL) return TX_INVALID;
    tx_system *s = *slot;
    s->destroying = true; /* External quiescence: no reader or operation may remain. */
    if (s->writer_held) {
        if (pthread_mutex_destroy(&s->writer) != 0) return TX_SYNC;
        s->writer_held = false;
    }
    if (s->cm_held) {
        if (pthread_mutex_destroy(&s->cm) != 0) return TX_SYNC;
        s->cm_held = false;
    }
    *slot = NULL;
    free(s);
    return TX_OK;
}

#ifdef TX_FIXTURE
tx_error tx_set_test_hook(tx_system *s, tx_test_hook hook, void *context) {
    if (!usable(s)) return TX_NOT_READY;
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    if (s->hook_set || s->loader_started || s->attempts != 0)
        return unlock_both(s, TX_ALREADY_STARTED);
    s->hook = hook;
    s->hook_context = context;
    s->hook_set = true;
    return unlock_both(s, TX_OK);
}
#endif

tx_error tx_loader_begin(tx_system *s) {
    if (!usable(s)) return TX_NOT_READY;
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    if (s->loader_started) return unlock_both(s, TX_ALREADY_STARTED);
    s->loader_started = true;
    return unlock_both(s, TX_OK);
}

tx_error tx_record_module(tx_system *s, uint32_t index, const char *id,
                          size_t id_length, tx_provenance provenance) {
    if (!usable(s)) return TX_NOT_READY;
    if (index >= TX_MODULES) return TX_INVALID;
    tx_owner owner = {0};
    owner.kind = TX_COMMUNITY;
    owner.process_id = s->process_id;
    owner.module_index = index;
    if (!id_copy(owner.id, id, id_length)) return TX_INVALID;
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    if (!s->loader_started) return unlock_both(s, TX_NOT_READY);
    module_slot *m = &s->modules[index];
    if (m->value.recorded) return unlock_both(s, TX_ALREADY_RECORDED);
    for (size_t i = 0; i < TX_MODULES; ++i) {
        if (s->modules[i].value.recorded &&
            strcmp(s->modules[i].value.owner.id, owner.id) == 0)
            return unlock_both(s, TX_DUPLICATE_ID);
    }
    m->value.recorded = true;
    m->value.owner = owner;
    m->value.provenance = provenance; /* Retain even failed source/provenance facts. */
    bool valid = provenance.handle_succeeded && provenance.handle_cookie != 0 &&
                 provenance.link_map_succeeded && provenance.intended_object_matched &&
                 provenance.lo < provenance.hi && provenance.lo >= provenance.base;
#ifdef TX_CONTROL_ACCEPT_FAILED_PROVENANCE
    valid = provenance.lo < provenance.hi && provenance.lo >= provenance.base;
#endif
    if (!valid) return unlock_both(s, TX_PROVENANCE);
    HOOK(s, TX_TEST_BEFORE_MODULE_PUBLISH, TX_LOCK_WRITER, 0, NULL);
    m->value.published = true;
    atomic_store_explicit(&m->published, 1u, memory_order_release);
    return unlock_both(s, TX_OK);
}

tx_error tx_record_descriptor(tx_system *s, uint32_t index,
                              bool accepted, bool inline_capability) {
    if (!usable(s)) return TX_NOT_READY;
    if (index >= TX_MODULES) return TX_INVALID;
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    module_slot *m = &s->modules[index];
    if (atomic_load_explicit(&m->published, memory_order_acquire) == 0u)
        return unlock_both(s, TX_PROVENANCE);
    if (m->value.descriptor_recorded) return unlock_both(s, TX_ALREADY_RECORDED);
    m->value.descriptor_recorded = true;
    m->value.descriptor_accepted = accepted;
    m->value.inline_capability = inline_capability;
    return unlock_both(s, accepted ? TX_OK : TX_DESCRIPTOR);
}

tx_error tx_open_init(tx_system *s, uint32_t index, tx_epoch *out) {
    if (!usable(s)) return TX_NOT_READY;
    if (index >= TX_MODULES || out == NULL) return TX_INVALID;
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    module_slot *m = &s->modules[index];
    if (atomic_load_explicit(&m->published, memory_order_acquire) == 0u)
        return unlock_both(s, TX_PROVENANCE);
    if (!m->value.descriptor_recorded || !m->value.descriptor_accepted)
        return unlock_both(s, TX_DESCRIPTOR);
    if (m->value.epoch != 0) return unlock_both(s, TX_ALREADY_STARTED);
    if (s->current_init_open) return unlock_both(s, TX_INIT_BUSY);
    if (s->next_epoch == UINT64_MAX) return unlock_both(s, TX_EPOCH_EXHAUSTED);
    ++s->next_epoch;
    m->value.epoch = s->next_epoch;
    m->value.init_open = true;
    s->current_init_open = true;
    s->current_init_index = index;
    s->current_init_epoch = m->value.epoch;
    *out = (tx_epoch){s, s->instance, m->value.epoch, index};
    return unlock_both(s, TX_OK);
}

static bool epoch_identity(const tx_system *s, const tx_epoch *token) {
    return token != NULL && token->system == s && token->instance == s->instance &&
           token->module_index < TX_MODULES && token->epoch != 0 &&
           s->modules[token->module_index].value.epoch == token->epoch;
}
static bool epoch_open(const tx_system *s, const tx_epoch *token) {
    return epoch_identity(s, token) && s->current_init_open &&
           s->current_init_index == token->module_index &&
           s->current_init_epoch == token->epoch &&
           s->modules[token->module_index].value.init_open;
}

tx_error tx_close_init(tx_system *s, const tx_epoch *token, int init_result) {
    if (!usable(s)) return TX_NOT_READY;
    tx_epoch token_value = {0};
    if (token != NULL) token_value = *token;
    HOOK(s, TX_TEST_BEFORE_CLOSE, TX_LOCK_CM, 0, NULL);
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    if (!epoch_open(s, &token_value)) return unlock_both(s, TX_EPOCH_CLOSED);
    module_slot *m = &s->modules[token_value.module_index];
    m->value.init_open = false;
    m->value.init_finished = true;
    m->value.init_result = init_result;
    s->current_init_open = false;
    HOOK(s, TX_TEST_CLOSED, TX_LOCK_CM, 0, NULL);
    return unlock_both(s, TX_OK);
}

tx_error tx_go_late(tx_system *s) {
    if (!usable(s)) return TX_NOT_READY;
    HOOK(s, TX_TEST_BEFORE_LATE, TX_LOCK_WRITER, 0, NULL);
    if (lock_one(s, &s->writer, 2u) != 0) return TX_SYNC;
    s->late = true;
    HOOK(s, TX_TEST_LATE, TX_LOCK_WRITER, 0, NULL);
    return unlock_writer(s, TX_OK);
}

static tx_error storage_admission(const tx_system *s, size_t self,
                                  tx_span target, const tx_layout *layout) {
    if (blob_touches(layout, target)) return TX_STORAGE_CONFLICT;
    for (size_t i = 0; i < TX_SLOTS; ++i) {
        const emission_slot *other = &s->slots[i];
        if (i == self || !other->reserved) continue;
        if (overlap(target, other->target) || blob_touches(&other->layout, target))
            return TX_TARGET_CONFLICT;
        if (blob_touches(layout, other->target) || blobs_touch(layout, &other->layout))
            return TX_STORAGE_CONFLICT;
    }
    for (size_t i = 0; i < TX_MODULES; ++i) {
        const module_slot *m = &s->modules[i];
        if (atomic_load_explicit(&m->published, memory_order_acquire) != 0u) {
            tx_span range = {m->value.provenance.lo, m->value.provenance.hi};
            if (blob_touches(layout, range)) return TX_MODULE_CONFLICT;
        }
    }
    return TX_OK;
}

typedef struct {
    const tx_system *system;
    uint64_t instance, attempt, epoch;
    uint32_t slot;
    tx_owner owner;
    uintptr_t target, replacement, original;
    bool consumed;
} preparation_permit;

static bool valid_permit(const tx_system *s, const preparation_permit *p,
                          const tx_receipt *r, const tx_epoch *token) {
    if (p->system != s || p->instance != s->instance || p->slot >= TX_SLOTS ||
        p->slot != r->slot || p->attempt != r->attempt || p->consumed ||
        p->target != r->target || p->replacement != r->replacement ||
        p->original != r->layout.trampoline_instructions.begin ||
        !same_owner(&p->owner, &r->owner) || p->epoch != r->epoch)
        return false;
    const emission_slot *slot = &s->slots[p->slot];
    if (!slot->reserved || slot->attempt != p->attempt ||
        atomic_load_explicit(&slot->published, memory_order_acquire) != 0u ||
        slot->target.begin != p->target || !same_owner(&slot->owner, &p->owner))
        return false;
    if (r->owner.kind == TX_COMMUNITY) {
#ifdef TX_CONTROL_SKIP_EPOCH_RECHECK
        return epoch_identity(s, token);
#else
        return epoch_open(s, token);
#endif
    }
    return r->owner.kind == TX_CORE;
}

static void cleanup_mapping(const tx_plan *plan, tx_receipt *receipt) {
    if (!receipt->resources.mapping_held) return;
    receipt->resources.cleanup_attempted = true;
    receipt->resources.cleanup = plan->cleanup;
    if (plan->cleanup.result == 0) receipt->resources.mapping_held = false;
}
static void relock_record(tx_protection_receipt *receipt, tx_raw result) {
    receipt->relock_attempted = true;
    receipt->relock = result;
    receipt->final_bits_known = result.result == 0;
    if (receipt->final_bits_known) receipt->final_bits = receipt->saved_bits;
}

/* Caller owns writer throughout, and also cm for Community. The permit is never
 * exposed. Reservations and all attempt/resource receipts remain in this owner. */
static tx_error transaction(tx_system *s, const tx_epoch *token,
                             uintptr_t caller_ra, const tx_plan *plan,
                             tx_receipt *out) {
    if (s->attempts == TX_ATTEMPTS) return TX_ATTEMPT_CAPACITY;
    tx_receipt *r = &s->receipts[s->attempts];
    memset(r, 0, sizeof(*r));
    r->attempt = ++s->attempts;
    r->slot = TX_NO_INDEX;
    r->target = plan->target;
    r->replacement = plan->replacement;
    r->owner.kind = token == NULL ? TX_CORE : TX_COMMUNITY;
    r->owner.process_id = s->process_id;
    r->owner.module_index = TX_NO_INDEX;
    tx_error error = TX_OK;
    if (token != NULL) {
#ifdef TX_CONTROL_SKIP_EPOCH_RECHECK
        bool admitted_epoch = epoch_identity(s, token);
#else
        bool admitted_epoch = epoch_open(s, token);
#endif
        if (!admitted_epoch) { error = TX_EPOCH_CLOSED; goto done; }
        module_slot *m = &s->modules[token->module_index];
        r->owner = m->value.owner;
        r->epoch = token->epoch;
        if (!m->value.descriptor_accepted || !m->value.inline_capability) {
            error = TX_DESCRIPTOR; goto done;
        }
        tx_span caller_envelope = {m->value.provenance.lo, m->value.provenance.hi};
        if (!contains(caller_envelope, caller_ra)) { error = TX_CALLER; goto done; }
        if (m->value.nhooks == UINT64_MAX) { error = TX_CAPACITY; goto done; }
    }
    r->admitted = true;
    r->late_at_admission = s->late;
    HOOK(s, TX_TEST_ADMITTED, TX_LOCK_WRITER, 0, r);
    if (plan->target == 0 || (plan->target & 3u) != 0 ||
        plan->replacement == 0 || plan->island_saved_bits > 7u ||
        plan->target_saved_bits > 7u) { error = TX_INVALID; goto done; }
    tx_span target = {plan->target, 0u};
    if (!end_checked(plan->target, 16u, &target.end)) { error = TX_INVALID; goto done; }
#ifndef TX_CONTROL_ALLOW_LATE_FALLBACK
    if (plan->ncopy == 4u && s->late) { error = TX_LATE; goto done; }
#endif
    error = layout_for(plan, &r->layout);
    if (error != TX_OK) goto done;
    error = storage_admission(s, TX_SLOTS, target, &r->layout);
    if (error != TX_OK) goto done;
    if (s->reserved_slots == TX_SLOTS) { error = TX_CAPACITY; goto done; }
    size_t index = s->reserved_slots++;
    emission_slot *slot = &s->slots[index];
    slot->reserved = true;
    slot->owner = r->owner;
    slot->target = target;
    slot->layout = r->layout;
    slot->attempt = r->attempt;
    r->slot = (uint32_t)index;
    r->reserved = true;
    HOOK(s, TX_TEST_RESERVED, TX_LOCK_WRITER, 0, r);
    r->resources.island_slot_burned = plan->ncopy == 1u;
    r->resources.allocation_attempted = true;
    r->resources.allocation = plan->allocation;
    if (plan->allocation.result != 0) { error = TX_ALLOCATION; goto done; }
    r->resources.mapping_held = true;
    r->resources.seal_attempted = true;
    r->resources.seal = plan->seal;
    if (plan->seal.result != 0) {
        cleanup_mapping(plan, r); error = TX_SEAL; goto done;
    }
    if (plan->ncopy == 1u) {
        r->island_protection.unlock_attempted = true;
        r->island_protection.unlock = plan->island_unlock;
        r->island_protection.saved_bits = plan->island_saved_bits;
        if (plan->island_unlock.result != 0) {
            cleanup_mapping(plan, r); error = TX_ISLAND_UNLOCK; goto done;
        }
        relock_record(&r->island_protection, plan->island_relock);
    }
    r->prepared = true;
    preparation_permit permit = {s, s->instance, r->attempt, r->epoch,
        r->slot, r->owner, r->target, r->replacement,
        r->layout.trampoline_instructions.begin, false};
#ifdef TX_FIXTURE
    switch (plan->permit_fault) {
        case TX_PERMIT_NORMAL: break;
        case TX_PERMIT_WRONG_SYSTEM: permit.system = NULL; break;
        case TX_PERMIT_WRONG_SLOT: permit.slot = TX_SLOTS; break;
        case TX_PERMIT_WRONG_OWNER: permit.owner.process_id = 0; break;
        case TX_PERMIT_CONSUMED: permit.consumed = true; break;
        default: permit.consumed = true; break;
    }
#endif
    bool permit_ok = valid_permit(s, &permit, r, token);
#ifdef TX_CONTROL_ACCEPT_BAD_PERMIT
    permit_ok = true;
#endif
    if (!permit_ok) { cleanup_mapping(plan, r); error = TX_PERMIT; goto done; }
    error = storage_admission(s, index, target, &r->layout);
    if (error != TX_OK) { cleanup_mapping(plan, r); goto done; }
    r->permit_validated = true;
    HOOK(s, TX_TEST_BEFORE_UNLOCK, TX_LOCK_WRITER, 0, r);
    r->target_protection.unlock_attempted = true;
    r->target_protection.unlock = plan->target_unlock;
    r->target_protection.saved_bits = plan->target_saved_bits;
    if (plan->target_unlock.result != 0) {
        cleanup_mapping(plan, r); error = TX_TARGET_UNLOCK; goto done;
    }

    /* Publication cut: only stores/loads of already-bound fixed storage.
     * No cfg hook, allocation, lock, provider, validation or failure branch. */
    permit.consumed = true;
#ifdef TX_CONTROL_ORIGINAL_BEFORE_METADATA
    atomic_store_explicit(&slot->original, plan->trampoline, memory_order_relaxed);
    r->original_published = true;
    r->original_order = 1u;
    r->original_saw_metadata =
        atomic_load_explicit(&slot->published, memory_order_acquire) != 0u;
    atomic_store_explicit(&slot->published, 1u, memory_order_release);
    r->metadata_published = true;
    r->metadata_order = 2u;
#else
    atomic_store_explicit(&slot->published, 1u, memory_order_release);
    r->metadata_published = true;
    r->metadata_order = 1u;
    atomic_store_explicit(&slot->original, plan->trampoline, memory_order_relaxed);
    r->original_published = true;
    r->original_order = 2u;
    r->original_saw_metadata =
        atomic_load_explicit(&slot->published, memory_order_acquire) != 0u;
#endif
    atomic_store_explicit(&slot->redirect, plan->replacement, memory_order_release);
    r->redirect_published = true;
    r->redirect_order = 3u;
    r->redirect_saw_original =
        atomic_load_explicit(&slot->original, memory_order_relaxed) == plan->trampoline;
    r->original_value = plan->trampoline;
    r->redirect_value = plan->replacement;
    /* End cut. Protection warnings never erase the above publication. */
    HOOK(s, TX_TEST_AFTER_CUT, TX_LOCK_WRITER, 0, r);
    relock_record(&r->target_protection, plan->target_relock);
    if (token != NULL) {
#ifndef TX_CONTROL_OMIT_HOOK_ACCOUNTING
        ++s->modules[token->module_index].value.nhooks;
        r->nhooks_accounted = true;
#endif
    }
done:
    r->result = error;
    *out = *r;
    return error;
}

tx_error tx_install_community(tx_system *s, const tx_epoch *token,
                              uintptr_t caller_ra, const tx_plan *plan,
                              tx_receipt *out) {
    if (!usable(s)) return TX_NOT_READY;
    if (token == NULL || plan == NULL || out == NULL) return TX_INVALID;
    /* Caller owns stable input storage during these value copies. Neither
     * public input is read again, including after fixture callbacks/permit. */
    tx_epoch token_value = *token;
    tx_plan plan_value = *plan;
    /* This checkpoint precedes every current system epoch/admission read. */
    HOOK(s, TX_TEST_BEFORE_ADMISSION, TX_LOCK_CM, 0, NULL);
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    e = transaction(s, &token_value, caller_ra, &plan_value, out);
    return unlock_both(s, e);
}

tx_error tx_install_core(tx_system *s, const tx_plan *plan, tx_receipt *out) {
    if (!usable(s)) return TX_NOT_READY;
    if (plan == NULL || out == NULL) return TX_INVALID;
    tx_plan plan_value = *plan;
    HOOK(s, TX_TEST_BEFORE_ADMISSION, TX_LOCK_WRITER, 0, NULL);
    if (lock_one(s, &s->writer, 2u) != 0) return TX_SYNC;
    tx_error e = transaction(s, NULL, 0u, &plan_value, out);
    return unlock_writer(s, e);
}

tx_match tx_lookup(const tx_system *s, uintptr_t address) {
    tx_match found = {0};
    if (!usable(s)) return found;
    for (size_t i = 0; i < TX_MODULES; ++i) {
        const module_slot *m = &s->modules[i];
        unsigned published = atomic_load_explicit(&m->published, memory_order_acquire);
#ifdef TX_CONTROL_MODULE_PREFIX_SCAN
        if (published == 0u) break;
#endif
        if (published != 0u) {
            tx_span envelope = {m->value.provenance.lo, m->value.provenance.hi};
            if (contains(envelope, address)) {
                found.source = TX_MODULE_MATCH;
                found.owner = m->value.owner;
                return found;
            }
        }
    }
    for (size_t i = 0; i < TX_SLOTS; ++i) {
        const emission_slot *slot = &s->slots[i];
        if (atomic_load_explicit(&slot->published, memory_order_acquire) != 0u &&
            (contains(slot->layout.island_instructions, address) ||
             contains(slot->layout.trampoline_instructions, address))) {
            found.source = TX_GENERATED_MATCH;
            found.owner = slot->owner;
            found.target = slot->target.begin;
            return found;
        }
    }
    return found;
}

tx_error tx_inspect(tx_system *s, tx_snapshot *out) {
    if (!usable(s)) return TX_NOT_READY;
    if (out == NULL) return TX_INVALID;
    tx_error e = lock_both(s);
    if (e != TX_OK) return e;
    memset(out, 0, sizeof(*out));
    out->initialized = true;
    out->loader_started = s->loader_started;
    out->late = s->late;
    out->process_id = s->process_id;
    out->instance = s->instance;
    out->next_epoch = s->next_epoch;
    out->current_init_open = s->current_init_open;
    out->current_init_index = s->current_init_index;
    out->current_init_epoch = s->current_init_epoch;
    out->attempts = s->attempts;
    out->reserved_slots = s->reserved_slots;
    for (size_t i = 0; i < TX_MODULES; ++i) out->modules[i] = s->modules[i].value;
    for (size_t i = 0; i < TX_SLOTS; ++i) {
        const emission_slot *slot = &s->slots[i];
        tx_slot_snapshot *copy = &out->slots[i];
        copy->reserved = slot->reserved;
        copy->published = atomic_load_explicit(&slot->published, memory_order_acquire) != 0u;
        copy->owner = slot->owner;
        copy->target = slot->target;
        copy->layout = slot->layout;
        copy->original_value = atomic_load_explicit(&slot->original, memory_order_relaxed);
        copy->redirect_value = atomic_load_explicit(&slot->redirect, memory_order_relaxed);
    }
    memcpy(out->receipts, s->receipts, sizeof(out->receipts));
    return unlock_both(s, TX_OK);
}
