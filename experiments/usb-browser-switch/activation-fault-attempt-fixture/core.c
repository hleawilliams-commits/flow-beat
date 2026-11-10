/* SPDX-License-Identifier: GPL-3.0-only */
/* Separately authored value-only terminal attempt. This file neither contains
 * nor invokes cm_blame_fault or the prior attribution/installer fixture. */
#include "core.h"
#include <stdatomic.h>
#include <string.h>

#if (defined(FA_CONTROL_ADMIT_SECOND) || defined(FA_CONTROL_REOPEN_UNKNOWN) || \
     defined(FA_CONTROL_OMIT_ID_COPY) || defined(FA_CONTROL_OWNER_FROM_WRITE) || \
     defined(FA_CONTROL_INVENT_WRITE_RETURN) || defined(FA_CONTROL_RETRY_SHORT_WRITE) || \
     defined(FA_CONTROL_SKIP_AFTER_WRITE_ERROR) || defined(FA_CONTROL_STALE_ERRNO)) && !defined(FA_FIXTURE)
#error "value-only safe controls require FA_FIXTURE"
#endif
#if (defined(FA_CONTROL_ADMIT_SECOND) + defined(FA_CONTROL_REOPEN_UNKNOWN) + \
     defined(FA_CONTROL_OMIT_ID_COPY) + defined(FA_CONTROL_OWNER_FROM_WRITE) + \
     defined(FA_CONTROL_INVENT_WRITE_RETURN) + defined(FA_CONTROL_RETRY_SHORT_WRITE) + \
     defined(FA_CONTROL_SKIP_AFTER_WRITE_ERROR) + defined(FA_CONTROL_STALE_ERRNO)) > 1
#error "each exact safe control needs a separate executable"
#endif

static _Atomic unsigned fa_state_word = ATOMIC_VAR_INIT(FA_VIRGIN);
/* Only the admitted execution accesses these working values. The fixture may
 * inspect them serially while it is not advancing that execution. */
static fa_private fa_work;
static fa_result fa_final;
static const fa_script *fa_script_value;
#ifdef FA_CONTROL_OMIT_ID_COPY
static const char *fa_borrowed_id;
#endif
#ifdef FA_CONTROL_RETRY_SHORT_WRITE
static bool fa_retried;
#endif

static void fa_halt(void) {
    fa_work.working.violation = true;
    fa_work.phase = FA_HALTED;
}
/* Authored canonical storage means a nonempty bounded value with zero padding.
 * This is intentionally not SDK cm_id_ok, manifest or capability admission. */
static size_t fa_id_length(const char id[FA_ID_BYTES]) {
    size_t length = 0;
    while (length < FA_ID_BYTES && id[length] != 0) ++length;
    if (length == 0 || length == FA_ID_BYTES) return 0;
    for (size_t i = length; i < FA_ID_BYTES; ++i)
        if (id[i] != 0) return 0;
    return length;
}
static void fa_owner_value(fa_owner *out, const fa_owner *source) {
    memset(out, 0, sizeof *out);
    out->kind = source->kind;
    if (source->kind == FA_COMMUNITY) {
        out->index = source->index;
        out->process_id = source->process_id;
        memcpy(out->id, source->id, FA_ID_BYTES);
    }
}
static const char *fa_selected_id(void) {
#ifdef FA_CONTROL_OMIT_ID_COPY
    return fa_borrowed_id;
#else
    return fa_work.working.owner.id;
#endif
}

__attribute__((noinline, noclone))
static bool fa_admit_internal(const fa_script *script, fa_state captured_state) {
    unsigned expected = FA_VIRGIN;
    bool admitted = atomic_compare_exchange_strong_explicit(
        &fa_state_word, &expected, FA_ACTIVE_INCOMPLETE,
        memory_order_acq_rel, memory_order_acquire);
#ifdef FA_CONTROL_ADMIT_SECOND
    /* Deliberately wrong permission from a stale pre-observation. Dedicated
     * control stops both admitted actors before a second selection event. */
    if (!admitted && captured_state == FA_VIRGIN) {
        admitted = true;
        atomic_store_explicit(&fa_state_word, FA_ACTIVE_INCOMPLETE, memory_order_relaxed);
    }
#elif defined(FA_CONTROL_REOPEN_UNKNOWN)
    /* Deliberately wrong reuse of an admitted/unknown attempt. No native
     * operation or second provider sequence is performed by this control. */
    (void)captured_state;
    if (!admitted && expected == FA_ACTIVE_INCOMPLETE) {
        admitted = true;
        atomic_store_explicit(&fa_state_word, FA_ACTIVE_INCOMPLETE, memory_order_relaxed);
    }
#else
    (void)captured_state; /* An observation is never baseline permission. */
#endif
    if (!admitted) return false;
    memset(&fa_work, 0, sizeof fa_work);
    fa_work.phase = FA_READY_ENTER;
    fa_work.next_kind = FA_SELECTION;
    fa_script_value = script; /* No input field was accessed before admission. */
#ifdef FA_CONTROL_OMIT_ID_COPY
    fa_borrowed_id = NULL;
#endif
#ifdef FA_CONTROL_RETRY_SHORT_WRITE
    fa_retried = false;
#endif
    return true;
}
static bool fa_enter(void) {
    if (fa_work.working.event_count >= FA_EVENT_CAPACITY) {
        fa_halt(); return false;
    }
    unsigned index = fa_work.working.event_count++;
    fa_work.current_event = index;
    fa_event *event = &fa_work.working.events[index];
    memset(event, 0, sizeof *event);
    event->kind = fa_work.next_kind;
    event->entered = true; /* May have been invoked, not a returned side effect. */
    fa_work.raw_oracle.available = false;
    if (event->kind == FA_WRITE) {
        const char *id = fa_selected_id();
        size_t length = id ? fa_id_length(id) : 0;
        if (!length) { fa_halt(); return false; }
        event->requested_length = length;
        memcpy(event->payload, id, length);
#ifdef FA_CONTROL_INVENT_WRITE_RETURN
        /* Wrong authoritative receipt, not an extra diagnostic counter.
         * Raw oracle stays unavailable; control inspects before any later step. */
        event->returned_recorded = true;
        event->raw = (int64_t)length;
#endif
    }
    fa_work.phase = FA_ENTERED;
    return true;
}
static bool fa_receive(void) {
    if (!fa_script_value) { fa_halt(); return false; }
    fa_raw_oracle *raw = &fa_work.raw_oracle;
    unsigned sequence = raw->sequence + 1u;
    memset(raw, 0, sizeof *raw);
    raw->sequence = sequence;
    raw->kind = fa_work.next_kind;
    switch (raw->kind) {
    case FA_SELECTION:
        if (fa_script_value->selection.kind != FA_COMMUNITY &&
            fa_script_value->selection.kind != FA_NO_COMMUNITY) {
            fa_halt(); return false;
        }
        if (fa_script_value->selection.kind == FA_COMMUNITY &&
            !fa_id_length(fa_script_value->selection.id)) {
            fa_halt(); return false;
        }
        /* The raw latch is always an honest value copy, including under the
         * ID-custody control. The control separately borrows the live script. */
        fa_owner_value(&raw->selection, &fa_script_value->selection);
        break;
    case FA_UNBLAMED:
        /* Only authored ordinary return is represented. No native unblamed
         * status, live-mod flag, clock, append, or quarantine provider exists. */
        break;
    case FA_OPEN: raw->io = fa_script_value->open; break;
    case FA_WRITE: raw->io = fa_script_value->write; break;
    case FA_FSYNC: raw->io = fa_script_value->sync; break;
    case FA_CLOSE: raw->io = fa_script_value->close; break;
    default: fa_halt(); return false;
    }
    raw->available = true;
    fa_work.phase = FA_RAW_AVAILABLE;
    return true;
}
static bool fa_record(void) {
    fa_event *event = &fa_work.working.events[fa_work.current_event];
    const fa_raw_oracle *raw = &fa_work.raw_oracle;
    if (!raw->available || raw->kind != event->kind) {
        fa_halt(); return false;
    }
    if (event->kind == FA_SELECTION) {
#ifdef FA_CONTROL_OMIT_ID_COPY
        memset(&fa_work.working.owner, 0, sizeof fa_work.working.owner);
        fa_work.working.owner.kind = raw->selection.kind;
        if (raw->selection.kind == FA_COMMUNITY) {
            /* Omit only the ID custody copy; preserve the resolved identity.
             * The raw oracle remains copied, and this script stays alive. */
            fa_work.working.owner.index = raw->selection.index;
            fa_work.working.owner.process_id = raw->selection.process_id;
            fa_borrowed_id = fa_script_value->selection.id;
        }
#else
        fa_owner_value(&fa_work.working.owner, &raw->selection);
#endif
        fa_work.working.selection_known = true;
    } else if (event->kind != FA_UNBLAMED) {
        int64_t value = raw->io.raw;
        bool valid = event->kind == FA_OPEN ? value >= -1 :
            event->kind == FA_WRITE ? value >= -1 &&
                (value < 0 || (uint64_t)value <= event->requested_length) :
                (value == 0 || value == -1);
        if (!valid) { fa_halt(); return false; }
        event->raw = value;
        event->errno_valid = value == -1;
        event->error_number = event->errno_valid ? raw->io.failure_errno : 0;
#ifdef FA_CONTROL_STALE_ERRNO
        if (event->kind == FA_OPEN && value == 0) {
            event->errno_valid = true;
            event->error_number = raw->io.failure_errno;
        }
#endif
        if (value == -1) fa_work.working.known_failure = true;
        if (event->kind == FA_WRITE) {
            if (value >= 0 && (uint64_t)value < event->requested_length)
                fa_work.working.short_write = true;
#ifdef FA_CONTROL_OWNER_FROM_WRITE
            if (value == -1)
                memset(&fa_work.working.owner, 0, sizeof fa_work.working.owner);
#endif
        }
    }
    event->returned_recorded = true;
    fa_work.phase = FA_RECORDED;
    return true;
}
static void fa_next(void) {
    const fa_event *event = &fa_work.working.events[fa_work.current_event];
    switch (event->kind) {
    case FA_SELECTION:
        fa_work.next_kind = fa_work.working.owner.kind == FA_COMMUNITY
            ? FA_OPEN : FA_UNBLAMED;
        break;
    case FA_UNBLAMED:
    case FA_CLOSE:
        fa_work.phase = FA_READY_COMPLETE; return;
    case FA_OPEN:
        if (event->raw == -1) { fa_work.phase = FA_READY_COMPLETE; return; }
        fa_work.next_kind = FA_WRITE; break;
    case FA_WRITE:
#ifdef FA_CONTROL_SKIP_AFTER_WRITE_ERROR
        if (event->raw == -1) { fa_work.phase = FA_READY_COMPLETE; return; }
#endif
#ifdef FA_CONTROL_RETRY_SHORT_WRITE
        if (!fa_retried && event->raw > 0 &&
            (uint64_t)event->raw < event->requested_length) {
            fa_retried = true;
            fa_work.next_kind = FA_WRITE;
            break;
        }
#endif
        fa_work.next_kind = FA_FSYNC; break;
    case FA_FSYNC:
        fa_work.next_kind = FA_CLOSE; break;
    default:
        fa_halt(); return;
    }
    fa_work.phase = FA_READY_ENTER;
}
__attribute__((noinline, noclone))
static void fa_publish_internal(void) {
#ifdef FA_CONTROL_OMIT_ID_COPY
    if (fa_work.working.owner.kind == FA_COMMUNITY && fa_borrowed_id)
        memcpy(fa_work.working.owner.id, fa_borrowed_id, FA_ID_BYTES);
#endif
    fa_final = fa_work.working;
    fa_work.phase = FA_PUBLISHED;
    /* No working or final field is modified by the baseline after this store. */
    atomic_store_explicit(&fa_state_word, FA_ORDINARY_RETURNED, memory_order_release);
}
static bool fa_step_internal(void) {
    /* Only the admitted synchronous execution or its serial fixture driver
     * reaches this helper; this load is not a second admission authority. */
    if (atomic_load_explicit(&fa_state_word, memory_order_relaxed) != FA_ACTIVE_INCOMPLETE)
        return false;
    switch (fa_work.phase) {
    case FA_READY_ENTER: return fa_enter();
    case FA_ENTERED: return fa_receive();
    case FA_RAW_AVAILABLE: return fa_record();
    case FA_RECORDED:
        fa_next(); return fa_work.phase != FA_HALTED;
    case FA_READY_COMPLETE:
        fa_publish_internal(); return true;
    default: return false;
    }
}
fa_request_result fa_run(const fa_script *script) {
    if (!fa_admit_internal(script, FA_VIRGIN)) return FA_SUPPRESSED;
    while (fa_step_internal()) { /* Bounded value progression, never CAS retry. */ }
    return fa_work.phase == FA_PUBLISHED ? FA_RETURNED : FA_INCOMPLETE;
}
__attribute__((noinline))
void fa_observe(fa_snapshot *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    unsigned state = atomic_load_explicit(&fa_state_word, memory_order_acquire);
    out->state = (fa_state)state;
    if (state == FA_ORDINARY_RETURNED) {
        out->has_payload = true;
        out->result = fa_final;
    }
}
#ifdef FA_FIXTURE
fa_request_result fa_fixture_begin(const fa_script *script, fa_state captured_state) {
    return fa_admit_internal(script, captured_state) ? FA_ADMITTED : FA_SUPPRESSED;
}
bool fa_fixture_step(void) { return fa_step_internal(); }
void fa_fixture_inspect(fa_private *out) {
    if (out) *out = fa_work;
}
#endif
