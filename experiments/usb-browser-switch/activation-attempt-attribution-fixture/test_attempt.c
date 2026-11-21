/* SPDX-License-Identifier: GPL-3.0-only */
/* Fresh serial composition witnesses. Native fault providers are absent;
 * stdio below writes test evidence only. No predecessor executable is used. */
#include "core.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef AC_FIXTURE
#error Executable tests require the declared serial fixture configuration.
#endif

#define MAX_CHECKS 16u
#define MAX_OBSERVATIONS 128u
#define MAX_REQUESTS 16u
#define MAX_SETUP 48u
#define MAX_STEPS 96u
#define PID_A UINT64_C(0x100000001)
#define PID_B UINT64_C(0x200000002)
#define GEN_PC ((uintptr_t)0x100000)
#define FRAME ((uintptr_t)0x20000)

typedef struct {
    const char *label;
    unsigned step;
    ac_snapshot public_value;
    ac_private private_value;
    ac_auxiliary auxiliary;
} observation;
typedef struct {
    const char *actor;
    unsigned input, step;
    ac_state before, after;
    ac_request_result result;
} request_record;
typedef struct {
    const char *operation;
    unsigned index, step;
    uint64_t process_id, value;
    uintptr_t lo, hi, other_lo, other_hi, address;
    int64_t returned_bytes;
    unsigned owner_kind;
    char id[AC_ID_BYTES];
    bool result;
} setup_record;

static ac_request inputs[2];
static ac_owner expected_owner;
static char id_a[AC_ID_BYTES] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static char id_b[AC_ID_BYTES] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static observation observations[MAX_OBSERVATIONS];
static request_record requests[MAX_REQUESTS];
static setup_record setup_records[MAX_SETUP];
static unsigned observation_count, request_count, setup_count, steps;
static const char *check_names[MAX_CHECKS], *failed_checks[MAX_CHECKS];
static unsigned checks, failures;
static bool capacity_ok = true, schedule_ok = true, setup_ok = true;
static bool expect_adapter_error;
static const char *selector;

static void check(const char *name, bool passed) {
    if (checks >= MAX_CHECKS || failures >= MAX_CHECKS) { capacity_ok = false; return; }
    check_names[checks++] = name;
    if (!passed) failed_checks[failures++] = name;
}
static ac_private priv(void) { ac_private p; ac_fixture_inspect(&p); return p; }
static ac_snapshot pub(void) { ac_snapshot s; ac_observe(&s); return s; }
static ac_auxiliary aux(void) { ac_auxiliary a; ac_fixture_auxiliary(&a); return a; }
static bool owner_equal(const ac_owner *a, const ac_owner *b) {
    return a->kind == b->kind && a->index == b->index && a->process_id == b->process_id &&
        memcmp(a->id, b->id, AC_ID_BYTES) == 0;
}
static bool event_equal(const ac_event *a, const ac_event *b) {
    return a->kind == b->kind && a->entered == b->entered &&
        a->returned_recorded == b->returned_recorded && a->raw == b->raw &&
        a->errno_valid == b->errno_valid && a->error_number == b->error_number &&
        a->requested_length == b->requested_length &&
        memcmp(a->payload, b->payload, AC_ID_BYTES) == 0;
}
static bool result_equal(const ac_result *a, const ac_result *b) {
    if (a->selection_known != b->selection_known || !owner_equal(&a->owner, &b->owner) ||
        a->known_failure != b->known_failure || a->short_write != b->short_write ||
        a->violation != b->violation || a->event_count != b->event_count) return false;
    for (unsigned i = 0; i < AC_EVENT_CAPACITY; ++i)
        if (!event_equal(&a->events[i], &b->events[i])) return false;
    return true;
}
static bool module_equal(const ma_module_value *a, const ma_module_value *b) {
    return a->committed == b->committed && a->index == b->index &&
        a->process_id == b->process_id && a->lo == b->lo && a->hi == b->hi &&
        memcmp(a->id, b->id, AC_ID_BYTES) == 0;
}
static bool selection_equal(const ac_selector *a, const ac_selector *b) {
    if (a->initialized != b->initialized || a->phase != b->phase || a->status != b->status ||
        a->error != b->error || a->pc != b->pc || a->lr != b->lr || a->fp != b->fp ||
        a->nfp != b->nfp || a->ra != b->ra || a->chosen_index != b->chosen_index ||
        a->frame_count != b->frame_count || a->trace_count != b->trace_count ||
        a->probe_count != b->probe_count || a->cell_count != b->cell_count ||
        a->snapshot_count != b->snapshot_count || a->address_bound != b->address_bound ||
        !owner_equal(&a->value, &b->value)) return false;
    for (unsigned i = 0; i < AC_SELECT_CALLS; ++i) {
        const ac_select_call *x = &a->trace[i], *y = &b->trace[i];
        if (x->operation != y->operation || x->address != y->address ||
            x->owner_index != y->owner_index || x->bytes_returned != y->bytes_returned ||
            x->cell_value != y->cell_value || x->actual_success != y->actual_success ||
            x->consumed_success != y->consumed_success || x->substituted != y->substituted ||
            !module_equal(&x->snapshot, &y->snapshot)) return false;
    }
    return true;
}
static bool private_equal(const ac_private *a, const ac_private *b) {
    return a->phase == b->phase && a->next_kind == b->next_kind &&
        a->current_event == b->current_event && result_equal(&a->working, &b->working) &&
        a->raw_oracle.available == b->raw_oracle.available &&
        a->raw_oracle.sequence == b->raw_oracle.sequence &&
        a->raw_oracle.kind == b->raw_oracle.kind &&
        owner_equal(&a->raw_oracle.selection, &b->raw_oracle.selection) &&
        a->raw_oracle.io.raw == b->raw_oracle.io.raw &&
        a->raw_oracle.io.failure_errno == b->raw_oracle.io.failure_errno &&
        selection_equal(&a->selector, &b->selector) &&
        a->adapter_error_returned == b->adapter_error_returned &&
        a->adapter_error == b->adapter_error;
}
static bool no_payload(const ac_snapshot *s) {
    const ac_result empty = {0};
    return !s->has_payload && result_equal(&s->result, &empty);
}
static bool ordinary(const ac_snapshot *s) {
    return s->state == AC_ORDINARY_RETURNED && s->has_payload && !s->result.violation;
}
static unsigned work_count(void) {
    ac_private p = priv(); ac_auxiliary a = aux();
    unsigned count = p.selector.trace_count + a.core_query_count;
    for (unsigned i = 0; i < a.scratch_count && i < AC_SCRATCH_RUNS; ++i)
        count += a.scratch[i].trace_count;
    return count;
}
static void observe(const char *label) {
    if (observation_count >= MAX_OBSERVATIONS) { capacity_ok = false; return; }
    observation *o = &observations[observation_count++];
    o->label = label; o->step = steps;
    ac_observe(&o->public_value); ac_fixture_inspect(&o->private_value);
    ac_fixture_auxiliary(&o->auxiliary);
}
static setup_record *setup_entry(const char *operation, unsigned index) {
    if (setup_count >= MAX_SETUP) { capacity_ok = false; return NULL; }
    setup_record *r = &setup_records[setup_count++];
    r->operation = operation; r->index = index; r->step = steps;
    return r;
}
static bool stage_module(unsigned index, const char id[AC_ID_BYTES], uint64_t pid,
                         uintptr_t lo, uintptr_t hi) {
    setup_record *r = setup_entry("stage-module", index);
    if (!r) return false;
    r->process_id = pid; r->lo = lo; r->hi = hi; memcpy(r->id, id, AC_ID_BYTES);
    r->result = ac_model_stage_module(index, id, pid, lo, hi);
    setup_ok &= r->result; return r->result;
}
static bool commit_module(unsigned index) {
    setup_record *r = setup_entry("commit-module", index);
    if (!r) return false;
    r->result = ac_model_commit_module(index); setup_ok &= r->result;
    observe("after-module-publication"); return r->result;
}
static bool generated(rc_owner_kind kind, uint64_t pid, const char id[AC_ID_BYTES]) {
    setup_record *r = setup_entry("stage-generated", 0);
    if (!r) return false;
    r->owner_kind = (unsigned)kind; r->process_id = pid; memcpy(r->id, id, AC_ID_BYTES);
    r->lo = GEN_PC; r->hi = GEN_PC + 8; r->other_lo = 0x110000; r->other_hi = 0x11000c;
    r->result = ac_model_stage_generated(0, kind, pid, id,
        (rc_span){r->lo, r->hi}, (rc_span){r->other_lo, r->other_hi});
    setup_ok &= r->result;
    r = setup_entry("commit-generated", 0);
    if (!r) return false;
    r->result = ac_model_commit_generated(0); setup_ok &= r->result;
    return r->result;
}
static bool cell(uintptr_t address, uint64_t value, int64_t bytes) {
    setup_record *r = setup_entry("add-cell", 0);
    if (!r) return false;
    r->address = address; r->value = value; r->returned_bytes = bytes;
    r->result = ac_model_add_cell(address, value, bytes); setup_ok &= r->result;
    return r->result;
}
static void choose(unsigned index, uint64_t pid, const char id[AC_ID_BYTES]) {
    memset(&expected_owner, 0, sizeof expected_owner);
    expected_owner.kind = AC_COMMUNITY; expected_owner.index = index;
    expected_owner.process_id = pid; memcpy(expected_owner.id, id, AC_ID_BYTES);
}
static void module_a(bool commit) {
    (void)stage_module(3, id_a, PID_A, 0x70000, 0x71000);
    if (commit) (void)commit_module(3);
    choose(3, PID_A, id_a);
}
static void module_b(void) {
    (void)stage_module(2, id_b, PID_B, 0x80000, 0x81000); (void)commit_module(2);
}
static void generated_a(void) {
    module_a(true); (void)generated(RC_OWNER_COMMUNITY, PID_A, id_a);
}
static void stage_mask(void) { (void)stage_module(0, id_b, PID_B, GEN_PC, GEN_PC + 0x100); }
static ac_request_result request(const char *actor, unsigned input) {
    ac_snapshot before = pub();
    ac_request_result result = ac_fixture_begin(&inputs[input]);
    ac_snapshot after = pub();
    if (request_count < MAX_REQUESTS)
        requests[request_count++] = (request_record){actor, input, steps,
            before.state, after.state, result};
    else capacity_ok = false;
    observe("after-request"); return result;
}
static bool begin(void) { return request("winner", 0) == AC_ADMITTED; }
static bool step(void) {
    if (steps >= MAX_STEPS) { schedule_ok = false; return false; }
    ++steps; bool result = ac_fixture_step();
    ac_private p = priv();
    if (!result && !(expect_adapter_error && p.phase == AC_HALTED &&
                     p.adapter_error_returned && p.adapter_error == AC_ERROR_SNAPSHOT))
        schedule_ok = false;
    observe(result ? "after-step" : "step-returned-false"); return result;
}
static bool finish(void) {
    for (unsigned i = 0; i <= MAX_STEPS; ++i) {
        ac_snapshot s = pub();
        if (s.state == AC_ORDINARY_RETURNED) return s.has_payload;
        if (!step()) return false;
    }
    schedule_ok = false; return false;
}
static bool until_phase(ac_phase phase) {
    for (unsigned i = 0; i <= MAX_STEPS; ++i) {
        ac_private p = priv();
        if (p.phase == phase) return true;
        if (p.phase == AC_HALTED || p.phase == AC_PUBLISHED || !step()) return false;
    }
    schedule_ok = false; return false;
}
static bool until_selector(ac_select_phase phase) {
    for (unsigned i = 0; i <= MAX_STEPS; ++i) {
        ac_private p = priv();
        if (p.phase == AC_ENTERED && p.selector.phase == phase) return true;
        if (p.phase == AC_HALTED || p.phase >= AC_RAW_AVAILABLE || !step()) return false;
    }
    schedule_ok = false; return false;
}
static bool suppress_preserves(unsigned input) {
    ac_private before = priv(); unsigned count = work_count();
    ac_request_result answer = request("loser", input);
    ac_private after = priv();
    return answer == AC_SUPPRESSED && work_count() == count && private_equal(&before, &after);
}
static bool sequence(const ac_result *r, bool unblamed, bool open_failed) {
    static const ac_event_kind normal[] = {AC_SELECTION, AC_OPEN, AC_WRITE, AC_FSYNC, AC_CLOSE};
    unsigned count = unblamed || open_failed ? 2u : 5u;
    if (!r->selection_known || r->event_count != count) return false;
    for (unsigned i = 0; i < count; ++i) {
        ac_event_kind kind = unblamed && i == 1 ? AC_UNBLAMED : normal[i];
        if (r->events[i].kind != kind || !r->events[i].entered ||
            !r->events[i].returned_recorded) return false;
    }
    return true;
}
static bool write_matches(const ac_result *r) {
    if (!sequence(r, false, false)) return false;
    return r->events[2].requested_length == 32 &&
        memcmp(r->events[2].payload, expected_owner.id, 32) == 0 && r->events[2].payload[32] == 0;
}
static bool pending_honest(void) {
    for (unsigned i = 0; i < observation_count; ++i) {
        const ac_private *p = &observations[i].private_value;
        if (p->phase == AC_ENTERED && p->next_kind == AC_SELECTION &&
            (p->raw_oracle.available || p->raw_oracle.sequence != 0)) return false;
    }
    return true;
}
static bool probe_at(const ac_selector *s, unsigned ordinal, ac_select_phase op,
                     uintptr_t address, int index) {
    unsigned found = 0;
    for (unsigned i = 0; i < s->trace_count && i < AC_SELECT_CALLS; ++i) {
        const ac_select_call *c = &s->trace[i];
        if (c->operation != AC_SELECT_PC && c->operation != AC_SELECT_LR &&
            c->operation != AC_SELECT_FRAME_OWNER) continue;
        if (found++ == ordinal) return c->operation == op && c->address == address && c->owner_index == index;
    }
    return false;
}
static const ac_select_call *snapshot_call(const ac_selector *s) {
    for (unsigned i = 0; i < s->trace_count && i < AC_SELECT_CALLS; ++i)
        if (s->trace[i].operation == AC_SELECT_SNAPSHOT) return &s->trace[i];
    return NULL;
}
static void admission_case(bool reverse) {
    module_a(true); module_b(); inputs[0].pc = 0x70010; inputs[1].pc = 0x80010;
    unsigned winner = reverse ? 1u : 0u, loser = 1u - winner;
    if (reverse) choose(2, PID_B, id_b);
    bool admitted = request(reverse ? "B" : "A", winner) == AC_ADMITTED;
    bool quiet = suppress_preserves(loser);
    bool raw = admitted && until_phase(AC_RAW_AVAILABLE);
    ac_private at_raw = priv(); quiet &= suppress_preserves(loser);
    bool recorded = raw && until_phase(AC_RECORDED); quiet &= suppress_preserves(loser);
    bool done = recorded && finish(); quiet &= suppress_preserves(loser);
    ac_snapshot s = pub(); ac_private p = priv();
    check("sole-admission-owns-selection", admitted && done && p.selector.probe_count == 1 && p.selector.snapshot_count == 1);
    check("suppressed-requests-do-no-work", quiet);
    check("winner-address-selects-exact-owner", ordinary(&s) && owner_equal(&s.result.owner, &expected_owner) &&
          probe_at(&p.selector, 0, AC_SELECT_PC, inputs[winner].pc, (int)expected_owner.index));
    check("selection-return-occurs-once", raw && at_raw.raw_oracle.sequence == 1 && pending_honest());
    check("completion-copies-working-owner", ordinary(&s) && result_equal(&s.result, &p.working) && write_matches(&s.result));
}
static void identity_case(bool direct, bool control_binding, bool control_process) {
    if (direct) {
        (void)stage_module(0, id_a, PID_A, 0x90000, 0x91000); (void)commit_module(0);
        choose(0, PID_A, id_a); inputs[0].pc = 0x90010;
    } else generated_a();
    if (control_binding) {
        (void)stage_module(0, id_b, PID_B, 0x90000, 0x91000); (void)commit_module(0);
    }
    if (control_binding || control_process) inputs[0].open.raw = -1;
    bool reached = begin() && until_phase(AC_RAW_AVAILABLE);
    ac_private raw = priv();
    bool done = reached && finish(); ac_snapshot s = pub(); ac_private p = priv();
    if (control_binding) {
        check("generated-selection-uses-resolved-module-slot", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
    } else if (control_process) {
        check("selection-handoff-preserves-full-process-identity", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
    } else if (direct) {
        check("module-zero-is-valid-owner", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
        check("direct-selection-reaches-write", write_matches(&s.result) && p.selector.probe_count == 1);
    } else {
        check("generated-binding-resolves-module-three", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
        check("exact-process-and-id-reach-raw-working-final", reached &&
              owner_equal(&raw.raw_oracle.selection, &expected_owner) &&
              owner_equal(&p.selector.value, &expected_owner) && owner_equal(&p.working.owner, &expected_owner) &&
              owner_equal(&s.result.owner, &expected_owner));
        check("generated-selection-write-uses-copied-id", write_matches(&s.result));
        check("holes-do-not-hide-generated-owner", p.selector.probe_count == 1 && p.selector.snapshot_count == 1 &&
              probe_at(&p.selector, 0, AC_SELECT_PC, GEN_PC, 3));
    }
}
static void precedence_case(unsigned kind, bool control) {
    module_a(true); module_b(); inputs[0].lr = 0x80010;
    if (kind == 0) (void)generated(RC_OWNER_COMMUNITY, PID_A, id_a);
    else if (kind == 1) {
        const char empty[AC_ID_BYTES] = {0}; (void)generated(RC_OWNER_CORE, 44, empty);
        choose(2, PID_B, id_b);
    } else {
        (void)generated(RC_OWNER_COMMUNITY, UINT64_C(0x300000003), id_a);
        inputs[0].lr = 0x50010; inputs[0].fp = FRAME;
        (void)cell(FRAME, FRAME, 8); (void)cell(FRAME + 8, 0x70010, 8);
    }
    if (control) inputs[0].open.raw = -1;
    bool done = begin() && finish(); ac_snapshot s = pub(); ac_private p = priv();
    if (control) {
        check("core-pc-continues-community-fallback", done && ordinary(&s) &&
              owner_equal(&s.result.owner, &expected_owner) && sequence(&s.result, false, true));
        return;
    }
    if (kind == 0) {
        check("generated-pc-precedes-module-lr", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
        check("selected-pc-stops-later-addresses", p.selector.probe_count == 1 && p.selector.cell_count == 0);
        check("selected-community-branch-is-exact", write_matches(&s.result));
    } else if (kind == 1) {
        check("core-negative-continues-to-lr", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
        check("fallback-probe-order-is-pc-then-lr", p.selector.probe_count == 2 &&
              probe_at(&p.selector, 0, AC_SELECT_PC, GEN_PC, -1) && probe_at(&p.selector, 1, AC_SELECT_LR, 0x80010, 2));
        check("fallback-community-branch-is-exact", write_matches(&s.result));
    } else {
        check("unmatched-generated-identity-reaches-frame", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
        check("frame-pair-precedes-owner-query", p.selector.trace_count >= 5 &&
              p.selector.trace[2].operation == AC_SELECT_NFP && p.selector.trace[2].address == FRAME &&
              p.selector.trace[2].bytes_returned == 8 && p.selector.trace[3].operation == AC_SELECT_RA &&
              p.selector.trace[3].address == FRAME + 8 && p.selector.trace[3].bytes_returned == 8 &&
              probe_at(&p.selector, 2, AC_SELECT_FRAME_OWNER, 0x70010, 3));
        check("frame-owner-precedes-nonincreasing-stop", p.selector.cell_count == 2 && p.selector.probe_count == 3 && p.selector.nfp == FRAME);
        check("frame-selected-branch-is-exact", write_matches(&s.result));
    }
}
static void publication_case(bool before_snapshot) {
    if (before_snapshot) { generated_a(); stage_mask(); }
    else {
        (void)stage_module(3, id_a, PID_A, 0x60000, 0x62000); choose(3, PID_A, id_a);
        inputs[0].pc = 0x60010; inputs[0].lr = 0x61010;
    }
    bool reached = begin() && until_selector(before_snapshot ? AC_SELECT_SNAPSHOT : AC_SELECT_LR);
    ac_private cut = priv(); bool committed = commit_module(before_snapshot ? 0 : 3);
    bool done = reached && committed && finish(); ac_snapshot s = pub(); ac_private p = priv();
    if (before_snapshot) {
        check("chosen-index-survives-pre-snapshot-masking", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
        const ac_select_call *c = snapshot_call(&p.selector);
        check("exact-selected-slot-is-snapshotted", cut.selector.chosen_index == 3 && c && c->actual_success && c->snapshot.index == 3);
        check("snapshot-masking-does-not-requery", p.selector.probe_count == 1 && write_matches(&s.result));
    } else {
        check("later-query-sees-newly-published-slot", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
        check("earlier-pc-miss-is-not-restarted", cut.selector.probe_count == 1 && p.selector.probe_count == 2 &&
              probe_at(&p.selector, 0, AC_SELECT_PC, 0x60010, -1) && probe_at(&p.selector, 1, AC_SELECT_LR, 0x61010, 3));
        check("pending-selection-has-no-raw-return", !cut.raw_oracle.available && cut.raw_oracle.sequence == 0 && pending_honest());
        check("newly-visible-owner-reaches-completion", write_matches(&s.result));
    }
}
static void frozen_positive(bool control) {
    generated_a(); stage_mask();
    if (control) inputs[0].open.raw = -1;
    bool reached = begin() && until_phase(AC_RAW_AVAILABLE);
    ac_private raw = priv(); (void)commit_module(0);
    ac_snapshot hidden = pub(); bool recorded = reached && until_phase(AC_RECORDED);
    ac_private at_record = priv(); bool done = recorded && finish(); ac_snapshot s = pub();
    if (control) {
        check("recorded-selection-matches-original-return", reached && done && ordinary(&s) &&
              owner_equal(&raw.raw_oracle.selection, &at_record.working.owner) && owner_equal(&s.result.owner, &expected_owner));
        return;
    }
    check("raw-selection-survives-later-masking", reached && done && ordinary(&s) &&
          owner_equal(&raw.raw_oracle.selection, &expected_owner) && owner_equal(&at_record.working.owner, &expected_owner) &&
          owner_equal(&s.result.owner, &expected_owner));
    check("frozen-owner-is-used-by-write", write_matches(&s.result) && priv().selector.probe_count == 1);
    check("raw-and-working-selection-stay-private", no_payload(&hidden) && pending_honest());
}
static void frozen_negative(bool control, bool diagnostic) {
    (void)stage_module(3, id_a, PID_A, GEN_PC, GEN_PC + 0x100);
    memset(&expected_owner, 0, sizeof expected_owner);
    if (control) inputs[0].open.raw = -1;
    bool reached = begin() && until_phase(AC_RAW_AVAILABLE);
    ac_private raw = priv();
    if (!diagnostic) (void)commit_module(3);
    bool done = reached && finish(); ac_snapshot before = pub(); ac_private p_before = priv();
    if (control) {
        check("noowner-return-is-not-refreshed", reached && done && ordinary(&before) &&
              owner_equal(&raw.raw_oracle.selection, &expected_owner) && owner_equal(&before.result.owner, &expected_owner) &&
              sequence(&before.result, true, false));
        return;
    }
    if (diagnostic) {
        (void)commit_module(3); unsigned count = work_count(); int index = -9;
        bool diagnostic_ok = ac_model_diagnostic(GEN_PC, &index); observe("after-diagnostic");
        ac_snapshot after = pub(); ac_private p_after = priv(); ac_auxiliary a = aux();
        check("diagnostic-reader-sees-later-owner", done && diagnostic_ok && index == 3 && a.diagnostic_count == 1 &&
              a.diagnostics[0].address == GEN_PC && a.diagnostics[0].index == 3);
        check("diagnostic-does-not-mutate-completed-attempt", ordinary(&before) && ordinary(&after) &&
              result_equal(&before.result, &after.result) && private_equal(&p_before, &p_after) && work_count() == count);
        check("diagnostic-cannot-reopen-admission", suppress_preserves(1) && sequence(&after.result, true, false));
        return;
    }
    check("negative-return-survives-late-ownership", reached && done && ordinary(&before) &&
          owner_equal(&raw.raw_oracle.selection, &expected_owner) && owner_equal(&before.result.owner, &expected_owner));
    check("negative-selection-has-one-opaque-branch", sequence(&before.result, true, false) && p_before.selector.probe_count == 2);
    check("late-ownership-does-not-reopen-attempt", suppress_preserves(1));
}
static void io_case(void) {
    generated_a(); inputs[0].open.raw = 0; inputs[0].write.raw = -1;
    bool done = begin() && finish(); ac_snapshot s = pub();
    check("selected-owner-survives-write-error", done && ordinary(&s) && owner_equal(&s.result.owner, &expected_owner));
    check("fd-zero-error-still-syncs-and-closes", sequence(&s.result, false, false) && s.result.events[1].raw == 0 &&
          s.result.events[2].raw == -1 && s.result.events[3].raw == 0 && s.result.events[4].raw == 0);
    check("io-failure-does-not-erase-selection", s.result.known_failure && !s.result.short_write &&
          s.result.events[2].errno_valid && s.result.events[2].error_number == inputs[0].write.failure_errno &&
          write_matches(&s.result));
}
static void stopped_case(unsigned cut, bool control) {
    generated_a(); bool reached = begin();
    if (cut == 0) reached = reached && until_phase(AC_ENTERED);
    else if (cut == 1) reached = reached && until_selector(AC_SELECT_SNAPSHOT);
    else if (cut == 2) reached = reached && until_phase(AC_RAW_AVAILABLE);
    else if (cut == 3) reached = reached && until_phase(AC_RECORDED);
    else reached = reached && until_phase(AC_READY_COMPLETE);
    ac_private p = priv(); ac_snapshot s = pub();
    if (control) {
        check("selection-return-remains-private-until-completion", reached &&
              s.state == AC_ACTIVE_INCOMPLETE && no_payload(&s));
        return;
    }
    bool distinct = false;
    if (cut == 0) distinct = p.phase == AC_ENTERED && p.selector.trace_count == 0 && !p.raw_oracle.available;
    else if (cut == 1) distinct = p.phase == AC_ENTERED && p.selector.chosen_index == 3 &&
        p.selector.snapshot_count == 0 && !p.raw_oracle.available && p.raw_oracle.sequence == 0;
    else if (cut == 2) distinct = p.phase == AC_RAW_AVAILABLE && p.raw_oracle.available &&
        p.raw_oracle.sequence == 1 && !p.working.events[0].returned_recorded;
    else if (cut == 3) distinct = p.phase == AC_RECORDED && p.working.events[0].returned_recorded &&
        owner_equal(&p.working.owner, &expected_owner);
    else distinct = p.phase == AC_READY_COMPLETE && sequence(&p.working, false, false);
    check("selection-stop-retains-exact-stage", reached && distinct && pending_honest());
    check("stopped-selection-hides-public-payload", s.state == AC_ACTIVE_INCOMPLETE && no_payload(&s));
    check("stopped-selection-rejects-later-request", suppress_preserves(1));
}
static void snapshot_error_case(void) {
    generated_a(); expect_adapter_error = true;
    setup_record *r = setup_entry("consume-snapshot-refusal", 0);
    if (r) { r->result = ac_fixture_snapshot_refusal(true); setup_ok &= r->result; }
    bool reached = begin() && until_selector(AC_SELECT_SNAPSHOT);
    bool returned = !step(); ac_private p = priv(); ac_snapshot s = pub();
    const ac_select_call *c = snapshot_call(&p.selector);
    check("snapshot-actual-result-remains-successful", reached && c && c->actual_success && c->snapshot.committed &&
          c->snapshot.index == 3 && c->snapshot.process_id == PID_A && memcmp(c->snapshot.id, id_a, AC_ID_BYTES) == 0);
    check("consumed-snapshot-error-is-explicit", returned && c && !c->consumed_success && c->substituted &&
          p.adapter_error_returned && p.adapter_error == AC_ERROR_SNAPSHOT && p.working.violation);
    check("returned-error-is-not-successful-selection", p.phase == AC_HALTED && !p.raw_oracle.available &&
          p.raw_oracle.sequence == 0 && !p.working.selection_known && !p.working.events[0].returned_recorded &&
          s.state == AC_ACTIVE_INCOMPLETE && no_payload(&s));
    check("returned-invariant-error-stays-terminal", suppress_preserves(1));
}
static void early_selection_control(void) {
    module_a(true); module_b(); inputs[0].pc = 0x70010; inputs[1].pc = 0x80010;
    bool admitted = begin(); unsigned before = work_count();
    ac_request_result loser = request("loser", 1);
    check("suppressed-request-performs-no-selection", admitted && loser == AC_SUPPRESSED && work_count() == before);
}
static void second_admission_control(void) {
    generated_a(); bool admitted = begin(); bool done = admitted && finish(); ac_snapshot s = pub();
    check("one-admission-reaches-selection-and-completion", admitted && done && ordinary(&s) &&
          owner_equal(&s.result.owner, &expected_owner));
}
static bool run_selector(void) {
    if (!strcmp(selector, "admission-ab")) admission_case(false);
    else if (!strcmp(selector, "admission-ba")) admission_case(true);
    else if (!strcmp(selector, "generated-slot3")) identity_case(false, false, false);
    else if (!strcmp(selector, "direct-slot0")) identity_case(true, false, false);
    else if (!strcmp(selector, "generated-pc")) precedence_case(0, false);
    else if (!strcmp(selector, "core-lr")) precedence_case(1, false);
    else if (!strcmp(selector, "unmatched-frame")) precedence_case(2, false);
    else if (!strcmp(selector, "publish-between-queries")) publication_case(false);
    else if (!strcmp(selector, "mask-before-snapshot")) publication_case(true);
    else if (!strcmp(selector, "mask-after-raw")) frozen_positive(false);
    else if (!strcmp(selector, "noowner-late")) frozen_negative(false, false);
    else if (!strcmp(selector, "fd-zero-write-error")) io_case();
    else if (!strcmp(selector, "stop-entered")) stopped_case(0, false);
    else if (!strcmp(selector, "stop-index")) stopped_case(1, false);
    else if (!strcmp(selector, "stop-raw")) stopped_case(2, false);
    else if (!strcmp(selector, "stop-recorded")) stopped_case(3, false);
    else if (!strcmp(selector, "stop-prepublication")) stopped_case(4, false);
    else if (!strcmp(selector, "snapshot-error")) snapshot_error_case();
    else if (!strcmp(selector, "diagnostic-after-completion")) frozen_negative(false, true);
    else if (!strcmp(selector, "control-selection-order")) early_selection_control();
    else if (!strcmp(selector, "control-single-authority")) second_admission_control();
    else if (!strcmp(selector, "control-resolved-index")) identity_case(false, true, false);
    else if (!strcmp(selector, "control-process-handoff")) identity_case(false, false, true);
    else if (!strcmp(selector, "control-frozen-selection")) frozen_positive(true);
    else if (!strcmp(selector, "control-core-fallback")) precedence_case(1, true);
    else if (!strcmp(selector, "control-noowner-freeze")) frozen_negative(true, false);
    else if (!strcmp(selector, "control-private-selection")) stopped_case(2, true);
    else return false;
    return true;
}

/* Serialization is deliberately explicit: no struct bytes or padding are read. */
static void string(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
        else if (*p < 32) fprintf(out, "\\u%04x", (unsigned)*p);
        else fputc(*p, out);
    }
    fputc('"', out);
}
static const char *boolean(bool b) { return b ? "true" : "false"; }
static void hex(FILE *out, const char value[AC_ID_BYTES]) {
    fputc('"', out);
    for (unsigned i = 0; i < AC_ID_BYTES; ++i) fprintf(out, "%02x", (unsigned)(unsigned char)value[i]);
    fputc('"', out);
}
static void json_owner(FILE *out, const ac_owner *v) {
    fprintf(out, "{\"kind\":%u,\"index\":%" PRIu32 ",\"process_id\":%" PRIu64 ",\"id_hex\":",
        (unsigned)v->kind, v->index, v->process_id); hex(out, v->id); fputc('}', out);
}
static void json_io(FILE *out, ac_io_value v) {
    fprintf(out, "{\"raw\":%" PRId64 ",\"failure_errno\":%d}", v.raw, v.failure_errno);
}
static void json_input(FILE *out, const ac_request *v) {
    fprintf(out, "{\"pc\":%" PRIuPTR ",\"lr\":%" PRIuPTR ",\"fp\":%" PRIuPTR ",\"open\":", v->pc, v->lr, v->fp);
    json_io(out, v->open); fputs(",\"write\":", out); json_io(out, v->write);
    fputs(",\"sync\":", out); json_io(out, v->sync); fputs(",\"close\":", out); json_io(out, v->close); fputc('}', out);
}
static void json_result(FILE *out, const ac_result *v) {
    fprintf(out, "{\"selection_known\":%s,\"owner\":", boolean(v->selection_known)); json_owner(out, &v->owner);
    fprintf(out, ",\"known_failure\":%s,\"short_write\":%s,\"violation\":%s,\"event_count\":%u,\"events\":[",
        boolean(v->known_failure), boolean(v->short_write), boolean(v->violation), v->event_count);
    for (unsigned i = 0; i < AC_EVENT_CAPACITY; ++i) {
        const ac_event *e = &v->events[i];
        fprintf(out, "%s{\"kind\":%u,\"entered\":%s,\"returned_recorded\":%s,\"raw\":%" PRId64
            ",\"errno_valid\":%s,\"error_number\":%d,\"requested_length\":%zu,\"payload_hex\":",
            i ? "," : "", (unsigned)e->kind, boolean(e->entered), boolean(e->returned_recorded), e->raw,
            boolean(e->errno_valid), e->error_number, e->requested_length);
        hex(out, e->payload); fputc('}', out);
    }
    fputs("]}", out);
}
static void json_snapshot(FILE *out, const ac_snapshot *v) {
    fprintf(out, "{\"state\":%u,\"has_payload\":%s,\"result\":", (unsigned)v->state, boolean(v->has_payload));
    json_result(out, &v->result); fputc('}', out);
}
static void json_module(FILE *out, const ma_module_value *v) {
    fprintf(out, "{\"committed\":%s,\"index\":%u,\"process_id\":%" PRIu64
        ",\"lo\":%" PRIuPTR ",\"hi\":%" PRIuPTR ",\"id_hex\":",
        boolean(v->committed), v->index, v->process_id, v->lo, v->hi); hex(out, v->id); fputc('}', out);
}
static void json_selector(FILE *out, const ac_selector *v) {
    fprintf(out, "{\"initialized\":%s,\"phase\":%u,\"status\":%u,\"error\":%u,\"pc\":%" PRIuPTR
        ",\"lr\":%" PRIuPTR ",\"fp\":%" PRIuPTR ",\"nfp\":%" PRIuPTR ",\"ra\":%" PRIuPTR
        ",\"chosen_index\":%d,\"frame_count\":%u,\"trace_count\":%u,\"probe_count\":%u,\"cell_count\":%u,\"snapshot_count\":%u,\"address_bound\":%s,\"value\":",
        boolean(v->initialized), (unsigned)v->phase, (unsigned)v->status, (unsigned)v->error,
        v->pc, v->lr, v->fp, v->nfp, v->ra, v->chosen_index, v->frame_count, v->trace_count,
        v->probe_count, v->cell_count, v->snapshot_count, boolean(v->address_bound));
    json_owner(out, &v->value); fputs(",\"trace\":[", out);
    for (unsigned i = 0; i < v->trace_count && i < AC_SELECT_CALLS; ++i) {
        const ac_select_call *c = &v->trace[i];
        fprintf(out, "%s{\"operation\":%u,\"address\":%" PRIuPTR ",\"owner_index\":%d,\"bytes_returned\":%" PRId64
            ",\"cell_value\":%" PRIu64 ",\"actual_success\":%s,\"consumed_success\":%s,\"substituted\":%s,\"snapshot\":",
            i ? "," : "", (unsigned)c->operation, c->address, c->owner_index, c->bytes_returned,
            c->cell_value, boolean(c->actual_success), boolean(c->consumed_success), boolean(c->substituted));
        json_module(out, &c->snapshot); fputc('}', out);
    }
    fputs("]}", out);
}
static void json_private(FILE *out, const ac_private *v) {
    fprintf(out, "{\"phase\":%u,\"next_kind\":%u,\"current_event\":%u,\"working\":",
        (unsigned)v->phase, (unsigned)v->next_kind, v->current_event); json_result(out, &v->working);
    fprintf(out, ",\"raw_oracle\":{\"available\":%s,\"sequence\":%u,\"kind\":%u,\"selection\":",
        boolean(v->raw_oracle.available), v->raw_oracle.sequence, (unsigned)v->raw_oracle.kind);
    json_owner(out, &v->raw_oracle.selection); fputs(",\"io\":", out); json_io(out, v->raw_oracle.io);
    fputs("},\"selector\":", out); json_selector(out, &v->selector);
    fprintf(out, ",\"adapter_error_returned\":%s,\"adapter_error\":%u}",
        boolean(v->adapter_error_returned), (unsigned)v->adapter_error);
}
static void json_auxiliary(FILE *out, const ac_auxiliary *v) {
    fprintf(out, "{\"scratch_count\":%u,\"diagnostic_count\":%u,\"core_query_count\":%u,\"violation\":%s,\"scratch\":[",
        v->scratch_count, v->diagnostic_count, v->core_query_count, boolean(v->violation));
    for (unsigned i = 0; i < v->scratch_count && i < AC_SCRATCH_RUNS; ++i) {
        if (i) fputc(',', out);
        json_selector(out, &v->scratch[i]);
    }
    fputs("],\"diagnostics\":[", out);
    for (unsigned i = 0; i < v->diagnostic_count && i < AC_DIAGNOSTICS; ++i)
        fprintf(out, "%s{\"address\":%" PRIuPTR ",\"index\":%d}", i ? "," : "", v->diagnostics[i].address, v->diagnostics[i].index);
    fputs("],\"core_queries\":[", out);
    for (unsigned i = 0; i < v->core_query_count && i < AC_SCRATCH_RUNS; ++i)
        fprintf(out, "%s{\"address\":%" PRIuPTR ",\"found\":%s,\"kind\":%u}", i ? "," : "", v->core_queries[i].address,
            boolean(v->core_queries[i].found), (unsigned)v->core_queries[i].kind);
    fputs("]}", out);
}
static void common(FILE *out) {
    fputs("\"schema\":1,\"selector\":", out); string(out, selector);
    fprintf(out, ",\"checks\":%u,\"check_names\":[", checks);
    for (unsigned i = 0; i < checks; ++i) { if (i) fputc(',', out); string(out, check_names[i]); }
    fprintf(out, "],\"failures\":%u,\"failed_checks\":[", failures);
    for (unsigned i = 0; i < failures; ++i) { if (i) fputc(',', out); string(out, failed_checks[i]); }
    fputc(']', out);
}
static void evidence(FILE *out) {
    fputc('{', out); common(out);
    fputs(",\"source_scope\":{\"providers\":\"authored-values-only\",\"native_fault_provider_functions_present\":false,\"signals\":false,\"threads\":false,\"evidence_io\":\"stdio-report-only\"},\"inputs\":[", out);
    json_input(out, &inputs[0]); fputc(',', out); json_input(out, &inputs[1]);
    fputs("],\"expected_owner\":", out); json_owner(out, &expected_owner);
    fprintf(out, ",\"steps\":%u,\"setup\":[", steps);
    for (unsigned i = 0; i < setup_count; ++i) {
        const setup_record *r = &setup_records[i];
        if (i) fputc(',', out);
        fputs("{\"operation\":", out); string(out, r->operation);
        fprintf(out, ",\"index\":%u,\"step\":%u,\"process_id\":%" PRIu64 ",\"value\":%" PRIu64
            ",\"lo\":%" PRIuPTR ",\"hi\":%" PRIuPTR ",\"other_lo\":%" PRIuPTR ",\"other_hi\":%" PRIuPTR
            ",\"address\":%" PRIuPTR ",\"returned_bytes\":%" PRId64 ",\"owner_kind\":%u,\"id_hex\":",
            r->index, r->step, r->process_id, r->value, r->lo, r->hi, r->other_lo, r->other_hi,
            r->address, r->returned_bytes, r->owner_kind);
        hex(out, r->id); fprintf(out, ",\"result\":%s}", boolean(r->result));
    }
    fputs("],\"requests\":[", out);
    for (unsigned i = 0; i < request_count; ++i) {
        const request_record *r = &requests[i];
        if (i) fputc(',', out);
        fputs("{\"actor\":", out); string(out, r->actor);
        fprintf(out, ",\"input\":%u,\"step\":%u,\"before\":%u,\"after\":%u,\"result\":%u}",
            r->input, r->step, (unsigned)r->before, (unsigned)r->after, (unsigned)r->result);
    }
    fputs("],\"observations\":[", out);
    for (unsigned i = 0; i < observation_count; ++i) {
        const observation *o = &observations[i];
        if (i) fputc(',', out);
        fputs("{\"label\":", out); string(out, o->label); fprintf(out, ",\"step\":%u,\"public\":", o->step);
        json_snapshot(out, &o->public_value); fputs(",\"private\":", out); json_private(out, &o->private_value);
        fputs(",\"auxiliary\":", out); json_auxiliary(out, &o->auxiliary); fputc('}', out);
    }
    fputs("]}\n", out);
}
int main(int argc, char **argv) {
    if (argc != 3) return 64;
    selector = argv[1];
    for (unsigned i = 0; i < 2; ++i) {
        inputs[i].pc = GEN_PC; inputs[i].lr = 0x50010;
        inputs[i].open = (ac_io_value){7, 91}; inputs[i].write = (ac_io_value){32, 92};
        inputs[i].sync = (ac_io_value){0, 93}; inputs[i].close = (ac_io_value){0, 94};
    }
    observe("initial");
    if (!run_selector()) return 64;
    observe("final"); ac_private p = priv(); ac_auxiliary a = aux();
    bool expected_error = expect_adapter_error && p.phase == AC_HALTED &&
        p.adapter_error_returned && p.adapter_error == AC_ERROR_SNAPSHOT && p.working.violation;
    check("schedule-bounded", schedule_ok && setup_ok && steps <= MAX_STEPS && !a.violation &&
          (!p.working.violation || expected_error));
    check("report-capacity", capacity_ok && observation_count <= MAX_OBSERVATIONS &&
          request_count <= MAX_REQUESTS && setup_count <= MAX_SETUP && checks < MAX_CHECKS);
    FILE *out = fopen(argv[2], "wx");
    if (!out) return 74;
    evidence(out); int bad = ferror(out); if (fclose(out) != 0) bad = 1;
    if (bad) return 74;
    fputc('{', stdout); common(stdout); fputs("}\n", stdout);
    if (fflush(stdout) != 0 || ferror(stdout)) return 74;
    return failures ? 1 : 0;
}
