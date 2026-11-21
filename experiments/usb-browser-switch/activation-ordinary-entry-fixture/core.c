/* SPDX-License-Identifier: GPL-3.0-only */
/* Separately versioned ordinary-entry witness with pre-entry authored setup.
 * No frozen consumer, SDK admission, installer, DSO, native frame/file provider,
 * signal or callback is invoked. */
#include "core.h"
#include <stdatomic.h>
#include <string.h>

#if defined(MA_CONTROL_STOP_AT_HOLE) || defined(MA_CONTROL_RAW_QUERY) || \
    defined(MA_CONTROL_OMIT_DESCRIPTOR_PUBLICATION) || defined(MA_CONTROL_CLEAR_INIT) || \
    defined(MA_CONTROL_EXEC_ONLY) || defined(MA_CONTROL_GENERATED_FIRST) || \
    defined(MA_CONTROL_OMIT_PROCESS) || defined(MA_CONTROL_OMIT_ID_COPY) || defined(RC_FIXTURE)
#error "predecessor controls and installer fixtures are excluded"
#endif
_Static_assert(AC_ID_BYTES == RC_ID_BYTES && MA_MODULES == 4 && RC_BINDINGS == 4,
               "fixed retained identity and registry bounds");
/* This generated include contains only the exact retained layouts, storage and
 * four readers. The original instruction emission/publication machinery is absent. */
#include "readers.inc"

/* Addressable evidence anchor only: it is never called and grants no authority.
 * Keep the exact static reader observable in standalone compiler artifacts. */
int (*const ac_generated_reader_evidence)(uintptr_t) = ma_generated_index;

static _Atomic unsigned ac_state_word = ATOMIC_VAR_INIT(AC_VIRGIN);
static ac_private ac_work;
static ac_result ac_final;
static const ac_request *ac_request_value;
typedef struct { uintptr_t address; uint64_t value; int64_t returned_bytes; } ac_cell;
static ac_cell ac_cells[AC_MODEL_CELLS];
static unsigned ac_cell_count;
#ifdef AC_FIXTURE
static ac_auxiliary ac_aux;
static bool ac_snapshot_refused;
#endif
#ifdef AO_WITNESS_SETUP
static bool ac_module_staged[MA_MODULES], ac_module_used[MA_MODULES];
static bool ac_generated_staged[RC_BINDINGS], ac_generated_used[RC_BINDINGS];
#endif

static size_t ac_id_length(const char id[AC_ID_BYTES]) {
    size_t length = 0;
    while (length < AC_ID_BYTES && id[length] != 0) {
        unsigned char c = (unsigned char)id[length];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
        ++length;
    }
    if (length == 0 || length == AC_ID_BYTES) return 0;
    for (size_t i = length; i < AC_ID_BYTES; ++i)
        if (id[i] != 0) return 0;
    return length;
}
static void ac_owner_value(ac_owner *out, const ac_owner *source) {
    memset(out, 0, sizeof *out);
    out->kind = source->kind;
    if (source->kind == AC_COMMUNITY) {
        out->index = source->index;
        out->process_id = source->process_id;
        memcpy(out->id, source->id, AC_ID_BYTES);
    }
}
static void ac_halt(void) {
    ac_work.working.violation = true;
    ac_work.phase = AC_HALTED;
}
static ac_select_status ac_selection_error(ac_selector *s, ac_select_error error) {
    s->error = error;
    s->phase = AC_SELECT_ERROR;
    s->status = AC_RETURNED_INVARIANT_ERROR;
    return s->status;
}
static ac_select_status ac_no_owner(ac_selector *s) {
    memset(&s->value, 0, sizeof s->value);
    s->phase = AC_SELECT_DONE;
    s->status = AC_RETURNED_VALUE;
    return s->status;
}
static ac_select_call *ac_trace(ac_selector *s, ac_select_phase operation, uintptr_t address) {
    if (s->trace_count >= AC_SELECT_CALLS) {
        (void)ac_selection_error(s, AC_ERROR_TRACE_CAPACITY);
        return NULL;
    }
    ac_select_call *call = &s->trace[s->trace_count++];
    memset(call, 0, sizeof *call);
    call->operation = operation;
    call->address = address;
    return call;
}
static ac_select_status ac_frame_begin(ac_selector *s) {
    if (s->frame_count >= 16 || s->fp < 0x10000 || (s->fp & 7)) return ac_no_owner(s);
    if (s->fp > UINTPTR_MAX - 8) {
        s->address_bound = true; /* New authored input rule, not a frozen consumer claim. */
        return ac_no_owner(s);
    }
    s->phase = AC_SELECT_NFP;
    return AC_PENDING;
}
static ac_select_status ac_choose(ac_selector *s, int index) {
    s->chosen_index = index;
#ifdef AC_CONTROL_BINDING_AS_SLOT
    /* Dedicated generated binding0 -> module3 case; slot0 is a valid decoy.
     * All subsequent consistency checks intentionally use this altered index. */
    s->chosen_index = 0;
#endif
    if (s->chosen_index < 0 || (unsigned)s->chosen_index >= MA_MODULES)
        return ac_selection_error(s, AC_ERROR_INDEX);
    s->phase = AC_SELECT_SNAPSHOT;
    return AC_PENDING;
}
static ac_select_status ac_probe(ac_selector *s) {
    if (s->probe_count >= 18) return ac_selection_error(s, AC_ERROR_TRACE_CAPACITY);
    ac_select_phase operation = s->phase;
    uintptr_t address = operation == AC_SELECT_PC ? s->pc :
        operation == AC_SELECT_LR ? s->lr : s->ra;
    ac_select_call *call = ac_trace(s, operation, address);
    if (!call) return s->status;
    call->owner_index = ma_owner_index(address); /* Complete unchanged reader call. */
    ++s->probe_count;
    if (operation == AC_SELECT_FRAME_OWNER) ++s->frame_count;
    if (call->owner_index >= 0) return ac_choose(s, call->owner_index);
#ifdef AC_CONTROL_STOP_ON_CORE_PC
    if (operation == AC_SELECT_PC) {
        if (ac_aux.core_query_count >= AC_SCRATCH_RUNS)
            return ac_selection_error(s, AC_ERROR_TRACE_CAPACITY);
        ac_core_query *q = &ac_aux.core_queries[ac_aux.core_query_count++];
        rc_metadata metadata;
        memset(q, 0, sizeof *q);
        q->address = address;
        q->found = rc_lookup(address, &metadata); /* Explicit control-only diagnostic. */
        q->kind = q->found ? metadata.owner.kind : RC_OWNER_NONE;
        if (q->found && q->kind == RC_OWNER_CORE) return ac_no_owner(s);
    }
#endif
    if (operation == AC_SELECT_PC) {
        s->phase = AC_SELECT_LR;
        return AC_PENDING;
    }
    if (operation == AC_SELECT_LR) return ac_frame_begin(s);
    /* Ownership was checked before this nonincreasing-frame break. */
    if (s->nfp <= s->fp) return ac_no_owner(s);
    s->fp = s->nfp;
    return ac_frame_begin(s);
}
static ac_select_status ac_cell_read(ac_selector *s) {
    if (s->cell_count >= 32) return ac_selection_error(s, AC_ERROR_TRACE_CAPACITY);
    ac_select_phase operation = s->phase;
    uintptr_t address = operation == AC_SELECT_NFP ? s->fp : s->fp + 8;
    ac_select_call *call = ac_trace(s, operation, address);
    if (!call) return s->status;
    ++s->cell_count;
    /* An absent authored key returns zero bytes. No native memory is read. */
    for (unsigned i = 0; i < ac_cell_count; ++i) {
        if (ac_cells[i].address != address) continue;
        call->bytes_returned = ac_cells[i].returned_bytes;
        if (call->bytes_returned == 8) call->cell_value = ac_cells[i].value;
        break;
    }
    call->actual_success = call->bytes_returned == 8;
    call->consumed_success = call->actual_success;
    if (!call->consumed_success) return ac_no_owner(s);
    if (operation == AC_SELECT_NFP) {
        s->nfp = (uintptr_t)call->cell_value;
        s->phase = AC_SELECT_RA;
    } else {
        s->ra = (uintptr_t)call->cell_value;
        s->phase = AC_SELECT_FRAME_OWNER;
    }
    return AC_PENDING;
}
static ac_select_status ac_snapshot_selected(ac_selector *s, bool allow_refusal) {
    if (s->snapshot_count != 0) return ac_selection_error(s, AC_ERROR_TRACE_CAPACITY);
    ac_select_call *call = ac_trace(s, AC_SELECT_SNAPSHOT, (uintptr_t)s->chosen_index);
    if (!call) return s->status;
    call->owner_index = s->chosen_index;
    call->actual_success = ma_module_snapshot((unsigned)s->chosen_index, &call->snapshot);
    ++s->snapshot_count;
    call->consumed_success = call->actual_success;
#ifdef AC_FIXTURE
    if (allow_refusal && ac_snapshot_refused) {
        call->substituted = true;
        call->consumed_success = false; /* Actual bool/tuple above remain honest. */
    }
#else
    (void)allow_refusal;
#endif
    if (!call->consumed_success) return ac_selection_error(s, AC_ERROR_SNAPSHOT);
    const ma_module_value *m = &call->snapshot;
    if (!m->committed || m->index != (unsigned)s->chosen_index ||
        !m->process_id || !ac_id_length(m->id))
        return ac_selection_error(s, AC_ERROR_TUPLE);
    memset(&s->value, 0, sizeof s->value);
    s->value.kind = AC_COMMUNITY;
    s->value.index = m->index;
    s->value.process_id = m->process_id;
#ifdef AC_CONTROL_NARROW_PROCESS
    s->value.process_id = (uint32_t)m->process_id;
#endif
    memcpy(s->value.id, m->id, AC_ID_BYTES);
    s->phase = AC_SELECT_DONE;
    s->status = AC_RETURNED_VALUE;
    return s->status;
}
static ac_select_status ac_select_step(ac_selector *s, const ac_request *request, bool allow_refusal) {
    if (!s->initialized) {
        s->initialized = true;
        s->chosen_index = -1;
        s->phase = AC_SELECT_PC;
        if (!request) return ac_selection_error(s, AC_ERROR_REQUEST);
        s->pc = request->pc; s->lr = request->lr; s->fp = request->fp;
    }
    if (s->status != AC_PENDING) return s->status;
    switch (s->phase) {
    case AC_SELECT_PC:
    case AC_SELECT_LR:
    case AC_SELECT_FRAME_OWNER: return ac_probe(s);
    case AC_SELECT_NFP:
    case AC_SELECT_RA: return ac_cell_read(s);
    case AC_SELECT_SNAPSHOT: return ac_snapshot_selected(s, allow_refusal);
    default: return ac_selection_error(s, AC_ERROR_TUPLE);
    }
}
__attribute__((noinline, noclone))
static ac_select_status ac_select_values(ac_selector *s, const ac_request *request,
                                         bool complete, bool allow_refusal) {
    ac_select_status status;
    do { status = ac_select_step(s, request, allow_refusal); }
    while (complete && status == AC_PENDING);
    return status;
}
#if defined(AC_CONTROL_SELECT_BEFORE_CAS) || defined(AC_CONTROL_RESELECT_AT_RECORD) || \
    defined(AC_CONTROL_REFRESH_NEGATIVE)
static ac_selector *ac_fresh_scratch(const ac_request *request) {
    if (ac_aux.scratch_count >= AC_SCRATCH_RUNS) {
        ac_aux.violation = true;
        return NULL;
    }
    ac_selector *s = &ac_aux.scratch[ac_aux.scratch_count++];
    /* Slots are initially zero and never reused. Scratch has no admission right. */
    (void)ac_select_values(s, request, true, false);
    return s;
}
#endif

__attribute__((noinline, noclone))
static bool ac_admit_internal(const ac_request *request) {
    unsigned expected = AC_VIRGIN;
    bool admitted = atomic_compare_exchange_strong_explicit(
        &ac_state_word, &expected, AC_ACTIVE_INCOMPLETE,
        memory_order_acq_rel, memory_order_acquire);
    if (!admitted) return false;
    memset(&ac_work, 0, sizeof ac_work);
    ac_work.phase = AC_READY_ENTER;
    ac_work.next_kind = AC_SELECTION;
    ac_request_value = request; /* No baseline request field was read before CAS. */
    return true;
}
static ac_request_result ac_begin_internal(const ac_request *request) {
#ifdef AC_CONTROL_SELECT_BEFORE_CAS
    ac_selector *early = ac_fresh_scratch(request);
    if (!early || early->status != AC_RETURNED_VALUE) return AC_INCOMPLETE;
#endif
    if (!ac_admit_internal(request)) return AC_SUPPRESSED;
#ifdef AC_CONTROL_SECOND_ADMISSION
    /* The inner failed CAS returns before this hook, so it cannot recurse. */
    return ac_run(request);
#else
    return AC_ADMITTED;
#endif
}
static bool ac_enter(void) {
    if (ac_work.working.event_count >= AC_EVENT_CAPACITY) { ac_halt(); return false; }
    unsigned index = ac_work.working.event_count++;
    ac_work.current_event = index;
    ac_event *event = &ac_work.working.events[index];
    memset(event, 0, sizeof *event);
    event->kind = ac_work.next_kind;
    event->entered = true;
    ac_work.raw_oracle.available = false;
    if (event->kind == AC_WRITE) {
        size_t length = ac_id_length(ac_work.working.owner.id);
        if (!length) { ac_halt(); return false; }
        event->requested_length = length;
        memcpy(event->payload, ac_work.working.owner.id, length);
    }
    ac_work.phase = AC_ENTERED;
    return true;
}
static bool ac_receive(bool one_call) {
    ac_owner selected;
    if (ac_work.next_kind == AC_SELECTION) {
        ac_select_status status = ac_select_values(&ac_work.selector, ac_request_value,
                                                   !one_call, true);
        if (status == AC_PENDING) return true; /* Do not touch raw latch or sequence. */
        if (status == AC_RETURNED_INVARIANT_ERROR) {
            ac_work.adapter_error_returned = true;
            ac_work.adapter_error = ac_work.selector.error;
            ac_halt();
            return false;
        }
        ac_owner_value(&selected, &ac_work.selector.value);
    } else if (!ac_request_value) {
        ac_halt(); return false;
    }
    ac_raw_oracle *raw = &ac_work.raw_oracle;
    unsigned sequence = raw->sequence + 1u;
    memset(raw, 0, sizeof *raw);
    raw->sequence = sequence;
    raw->kind = ac_work.next_kind;
    switch (raw->kind) {
    case AC_SELECTION: ac_owner_value(&raw->selection, &selected); break;
    case AC_UNBLAMED: break; /* Opaque authored ordinary return only. */
    case AC_OPEN: raw->io = ac_request_value->open; break;
    case AC_WRITE: raw->io = ac_request_value->write; break;
    case AC_FSYNC: raw->io = ac_request_value->sync; break;
    case AC_CLOSE: raw->io = ac_request_value->close; break;
    default: ac_halt(); return false;
    }
    raw->available = true;
    ac_work.phase = AC_RAW_AVAILABLE;
    return true;
}
static bool ac_record(void) {
    ac_event *event = &ac_work.working.events[ac_work.current_event];
    const ac_raw_oracle *raw = &ac_work.raw_oracle;
    if (!raw->available || raw->kind != event->kind) { ac_halt(); return false; }
    if (event->kind == AC_SELECTION) {
        const ac_owner *selected = &raw->selection;
#if defined(AC_CONTROL_RESELECT_AT_RECORD) || defined(AC_CONTROL_REFRESH_NEGATIVE)
        bool reselect = true;
#ifdef AC_CONTROL_REFRESH_NEGATIVE
        reselect = raw->selection.kind == AC_NO_COMMUNITY;
#endif
        if (reselect) {
            ac_selector *fresh = ac_fresh_scratch(ac_request_value);
            if (!fresh || fresh->status != AC_RETURNED_VALUE) { ac_halt(); return false; }
            selected = &fresh->value; /* Honest old raw value remains untouched. */
        }
#endif
        ac_owner_value(&ac_work.working.owner, selected);
        ac_work.working.selection_known = true;
    } else if (event->kind != AC_UNBLAMED) {
        int64_t value = raw->io.raw;
        bool valid = event->kind == AC_OPEN ? value >= -1 :
            event->kind == AC_WRITE ? value >= -1 &&
                (value < 0 || (uint64_t)value <= event->requested_length) :
                (value == 0 || value == -1);
        if (!valid) { ac_halt(); return false; }
        event->raw = value;
        event->errno_valid = value == -1;
        event->error_number = event->errno_valid ? raw->io.failure_errno : 0;
        if (value == -1) ac_work.working.known_failure = true;
        if (event->kind == AC_WRITE && value >= 0 &&
            (uint64_t)value < event->requested_length) ac_work.working.short_write = true;
    }
    event->returned_recorded = true;
    ac_work.phase = AC_RECORDED;
    return true;
}
static void ac_next(void) {
    const ac_event *event = &ac_work.working.events[ac_work.current_event];
    switch (event->kind) {
    case AC_SELECTION:
        ac_work.next_kind = ac_work.working.owner.kind == AC_COMMUNITY ? AC_OPEN : AC_UNBLAMED;
        break;
    case AC_UNBLAMED:
    case AC_CLOSE: ac_work.phase = AC_READY_COMPLETE; return;
    case AC_OPEN:
        if (event->raw == -1) { ac_work.phase = AC_READY_COMPLETE; return; }
        ac_work.next_kind = AC_WRITE; break;
    case AC_WRITE: ac_work.next_kind = AC_FSYNC; break;
    case AC_FSYNC: ac_work.next_kind = AC_CLOSE; break;
    default: ac_halt(); return;
    }
    ac_work.phase = AC_READY_ENTER;
}
__attribute__((noinline, noclone))
static void ac_publish_internal(void) {
    ac_final = ac_work.working;
    ac_work.phase = AC_PUBLISHED;
    atomic_store_explicit(&ac_state_word, AC_ORDINARY_RETURNED, memory_order_release);
}
static bool ac_step_internal(bool one_call) {
    if (atomic_load_explicit(&ac_state_word, memory_order_relaxed) != AC_ACTIVE_INCOMPLETE)
        return false; /* Driver eligibility, never a second admission. */
    switch (ac_work.phase) {
    case AC_READY_ENTER: return ac_enter();
    case AC_ENTERED: return ac_receive(one_call);
    case AC_RAW_AVAILABLE: return ac_record();
    case AC_RECORDED: ac_next(); return ac_work.phase != AC_HALTED;
    case AC_READY_COMPLETE: ac_publish_internal(); return true;
    default: return false;
    }
}
ac_request_result ac_run(const ac_request *request) {
    ac_request_result result = ac_begin_internal(request);
    if (result != AC_ADMITTED) return result;
#ifdef AO_CONTROL_OMIT_DRIVER
    return AC_INCOMPLETE;
#endif
#ifdef AO_CONTROL_STOP_AFTER_SELECTION_RAW
    while (ac_step_internal(false)) {
        if (ac_work.phase == AC_RAW_AVAILABLE && ac_work.raw_oracle.kind == AC_SELECTION)
            break;
    }
#else
    while (ac_step_internal(false)) { /* Bounded authored progress, not a CAS retry loop. */ }
#endif
#ifdef AO_CONTROL_WRONG_PUBLISHED_RETURN
    return ac_work.phase == AC_PUBLISHED ? AC_INCOMPLETE : AC_INCOMPLETE;
#elif defined(AO_CONTROL_WRONG_HALTED_RETURN)
    return ac_work.phase == AC_PUBLISHED ? AC_RETURNED : AC_RETURNED;
#else
    return ac_work.phase == AC_PUBLISHED ? AC_RETURNED : AC_INCOMPLETE;
#endif
}
__attribute__((noinline))
void ac_observe(ac_snapshot *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    unsigned state = atomic_load_explicit(&ac_state_word, memory_order_acquire);
    out->state = (ac_state)state;
    if (state == AC_ORDINARY_RETURNED) {
        out->has_payload = true;
        out->result = ac_final;
    }
#ifdef AC_CONTROL_EXPOSE_RAW_SELECTION
    else if (state == AC_ACTIVE_INCOMPLETE && ac_work.raw_oracle.available &&
             ac_work.raw_oracle.kind == AC_SELECTION) {
        out->has_payload = true;
        out->result.selection_known = true;
        ac_owner_value(&out->result.owner, &ac_work.raw_oracle.selection);
    }
#endif
}

#ifdef AC_FIXTURE
ac_request_result ac_fixture_begin(const ac_request *request) { return ac_begin_internal(request); }
bool ac_fixture_step(void) { return ac_step_internal(true); }
void ac_fixture_inspect(ac_private *out) { if (out) *out = ac_work; }
void ac_fixture_auxiliary(ac_auxiliary *out) { if (out) *out = ac_aux; }
bool ac_fixture_snapshot_refusal(bool enable) {
    if (atomic_load_explicit(&ac_state_word, memory_order_relaxed) != AC_VIRGIN) return false;
    ac_snapshot_refused = enable;
    return true;
}
#endif
#ifdef AO_WITNESS_SETUP
bool ac_model_stage_module(unsigned index, const char id[AC_ID_BYTES],
                           uint64_t process_id, uintptr_t lo, uintptr_t hi) {
    if (atomic_load_explicit(&ac_state_word, memory_order_relaxed) != AC_VIRGIN ||
        index >= MA_MODULES || ac_module_staged[index] || !id ||
        !process_id || hi <= lo || !ac_id_length(id)) return false;
    ma_module *m = &ma_modules[index];
    m->index = index; m->process_id = process_id; m->lo = lo; m->hi = hi;
    memcpy(m->id, id, AC_ID_BYTES);
    ac_module_staged[index] = true;
    return true;
}
bool ac_model_commit_module(unsigned index) {
#ifndef AO_CONTROL_OMIT_COMMIT_PHASE_GUARDS
    if (atomic_load_explicit(&ac_state_word, memory_order_relaxed) != AC_VIRGIN) return false;
#endif
    if (index >= MA_MODULES || !ac_module_staged[index] || ac_module_used[index] ||
        atomic_load_explicit(&ma_modules[index].committed, memory_order_acquire)) return false;
    ac_module_used[index] = true;
    atomic_store_explicit(&ma_modules[index].committed, 1u, memory_order_release);
    return true;
}
bool ac_model_stage_generated(unsigned binding, rc_owner_kind kind,
                              uint64_t process_id, const char id[AC_ID_BYTES],
                              rc_span island_instructions, rc_span trampoline_instructions) {
    if (atomic_load_explicit(&ac_state_word, memory_order_relaxed) != AC_VIRGIN ||
        binding >= RC_BINDINGS || ac_generated_staged[binding] || !process_id ||
        (kind != RC_OWNER_CORE && kind != RC_OWNER_COMMUNITY) ||
        island_instructions.end <= island_instructions.begin ||
        trampoline_instructions.end <= trampoline_instructions.begin ||
        (kind == RC_OWNER_COMMUNITY && (!id || !ac_id_length(id)))) return false;
    rc_metadata *m = &records[binding].immutable;
    /* The static record starts zero. Unused installer/receipt fields stay zero. */
    m->owner.kind = kind; m->owner.process_id = process_id;
    if (kind == RC_OWNER_COMMUNITY) memcpy(m->owner.id, id, AC_ID_BYTES);
    m->binding = binding;
    m->island_instructions = island_instructions;
    m->trampoline_instructions = trampoline_instructions;
    ac_generated_staged[binding] = true;
    return true;
}
bool ac_model_commit_generated(unsigned binding) {
#ifndef AO_CONTROL_OMIT_COMMIT_PHASE_GUARDS
    if (atomic_load_explicit(&ac_state_word, memory_order_relaxed) != AC_VIRGIN) return false;
#endif
    if (binding >= RC_BINDINGS || !ac_generated_staged[binding] || ac_generated_used[binding] ||
        atomic_load_explicit(&records[binding].published, memory_order_acquire)) return false;
    ac_generated_used[binding] = true;
    atomic_store_explicit(&records[binding].published, 1u, memory_order_release);
    return true;
}
bool ac_model_add_cell(uintptr_t address, uint64_t value, int64_t returned_bytes) {
    if (atomic_load_explicit(&ac_state_word, memory_order_relaxed) != AC_VIRGIN ||
        ac_cell_count >= AC_MODEL_CELLS || returned_bytes < -1 || returned_bytes > 8) return false;
    for (unsigned i = 0; i < ac_cell_count; ++i)
        if (ac_cells[i].address == address) return false;
    ac_cells[ac_cell_count++] = (ac_cell){address, value, returned_bytes};
    return true;
}
#endif
#ifdef AC_FIXTURE
bool ac_model_diagnostic(uintptr_t address, int *out_index) {
    if (!out_index || ac_aux.diagnostic_count >= AC_DIAGNOSTICS) return false;
    ac_diagnostic *event = &ac_aux.diagnostics[ac_aux.diagnostic_count++];
    event->address = address;
    event->index = ma_owner_index(address);
    *out_index = event->index;
    return true;
}
#endif
