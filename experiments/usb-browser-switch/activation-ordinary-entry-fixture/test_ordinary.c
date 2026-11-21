/* SPDX-License-Identifier: GPL-3.0-only */
/* New ordinary-entry witnesses. Only authored setup and public entry/observation
 * are used. File I/O below writes evidence; it is not a fault provider. */
#include "core.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AO_WITNESS_SETUP
#error "ordinary witness requires its setup-only configuration"
#endif
#ifdef AC_FIXTURE
#error "private fixture APIs are excluded"
#endif

#define MAX_CHECKS 16u
#define MAX_SETUP 24u
#define MAX_CALLS 2u
#define OWNER_PID UINT64_C(0x100000001)
#define DECOY_PID UINT64_C(0x200000002)
#define SPARE_PID UINT64_C(0x300000003)

static char owner_source[AC_ID_BYTES] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static char decoy_source[AC_ID_BYTES] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static char spare_source[AC_ID_BYTES] = "cccccccccccccccccccccccccccccccc";
static const char owner_initial[AC_ID_BYTES] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char mutated_id[AC_ID_BYTES] = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
static const char *selector_name;
static const char *check_names[MAX_CHECKS];
static const char *failed_names[MAX_CHECKS];
static unsigned check_count, failure_count;
static bool report_bounds = true, setup_ok = true;

typedef struct {
    const char *operation;
    bool after_entry, returned;
    unsigned index;
    rc_owner_kind kind;
    uint64_t process_id;
    uintptr_t lo, hi, island_lo, island_hi, trampoline_lo, trampoline_hi;
    uintptr_t cell_address;
    uint64_t cell_value;
    int64_t cell_bytes;
    char id[AC_ID_BYTES];
} setup_record;

typedef struct {
    bool null_request;
    ac_request before, after;
    ac_request_result returned;
    ac_snapshot snapshot;
} call_record;

static setup_record setup_records[MAX_SETUP];
static unsigned setup_count;
static call_record calls[MAX_CALLS];
static unsigned call_count;
static ac_snapshot initial_snapshot, post_setup_snapshot;
static bool have_post_setup_snapshot;
static bool post_module_commit, post_generated_commit;
static ac_request request;
static ac_owner expected_owner;

static void check(const char *name, bool passed) {
    if (check_count >= MAX_CHECKS) { report_bounds = false; return; }
    check_names[check_count++] = name;
    if (!passed) failed_names[failure_count++] = name;
}
static setup_record *setup_event(const char *operation, bool after_entry) {
    if (setup_count >= MAX_SETUP) { report_bounds = false; setup_ok = false; return NULL; }
    setup_record *e = &setup_records[setup_count++];
    e->operation = operation;
    e->after_entry = after_entry;
    return e;
}
static bool io_equal(ac_io_value a, ac_io_value b) {
    return a.raw == b.raw && a.failure_errno == b.failure_errno;
}
static bool request_equal(const ac_request *a, const ac_request *b) {
    return a->pc == b->pc && a->lr == b->lr && a->fp == b->fp &&
        io_equal(a->open, b->open) && io_equal(a->write, b->write) &&
        io_equal(a->sync, b->sync) && io_equal(a->close, b->close);
}
static bool owner_equal(const ac_owner *a, const ac_owner *b) {
    return a->kind == b->kind && a->index == b->index &&
        a->process_id == b->process_id && memcmp(a->id, b->id, AC_ID_BYTES) == 0;
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
static bool snapshot_equal(const ac_snapshot *a, const ac_snapshot *b) {
    return a->state == b->state && a->has_payload == b->has_payload &&
        result_equal(&a->result, &b->result);
}
static bool empty_snapshot(const ac_snapshot *s, ac_state state) {
    ac_snapshot expected = {0};
    expected.state = state;
    return snapshot_equal(s, &expected);
}
static void expected_identity(unsigned index) {
    memset(&expected_owner, 0, sizeof expected_owner);
    expected_owner.kind = AC_COMMUNITY;
    expected_owner.index = index;
    expected_owner.process_id = OWNER_PID;
    memcpy(expected_owner.id, owner_initial, AC_ID_BYTES);
}
static ac_request ordinary_request(uintptr_t pc, uintptr_t lr, uintptr_t fp) {
    ac_request v = {0};
    v.pc = pc; v.lr = lr; v.fp = fp;
    v.open = (ac_io_value){3, 77};
    v.write = (ac_io_value){32, 88};
    v.sync = (ac_io_value){0, 99};
    v.close = (ac_io_value){0, 111};
    return v;
}
static void stage_module(unsigned index, const char id[AC_ID_BYTES], uint64_t pid,
                         uintptr_t lo, uintptr_t hi, bool commit) {
    setup_record *s = setup_event("stage-module", false);
    if (!s) return;
    s->index = index; s->kind = RC_OWNER_COMMUNITY; s->process_id = pid;
    s->lo = lo; s->hi = hi;
    memcpy(s->id, id, AC_ID_BYTES);
    s->returned = ac_model_stage_module(index, id, pid, lo, hi);
    setup_ok = setup_ok && s->returned;
    if (commit) {
        s = setup_event("commit-module", false);
        if (!s) return;
        s->index = index;
        s->returned = ac_model_commit_module(index);
        setup_ok = setup_ok && s->returned;
    }
}
static void stage_generated(unsigned binding, const char id[AC_ID_BYTES], uint64_t pid,
                            uintptr_t island, uintptr_t trampoline, bool commit) {
    setup_record *s = setup_event("stage-generated", false);
    if (!s) return;
    s->index = binding; s->kind = RC_OWNER_COMMUNITY; s->process_id = pid;
    s->island_lo = island; s->island_hi = island + 8;
    s->trampoline_lo = trampoline; s->trampoline_hi = trampoline + 32;
    memcpy(s->id, id, AC_ID_BYTES);
    s->returned = ac_model_stage_generated(binding, RC_OWNER_COMMUNITY, pid, id,
        (rc_span){island, island + 8}, (rc_span){trampoline, trampoline + 32});
    setup_ok = setup_ok && s->returned;
    if (commit) {
        s = setup_event("commit-generated", false);
        if (!s) return;
        s->index = binding;
        s->returned = ac_model_commit_generated(binding);
        setup_ok = setup_ok && s->returned;
    }
}
static void add_cell(uintptr_t address, uint64_t value) {
    setup_record *s = setup_event("add-cell", false);
    if (!s) return;
    s->cell_address = address; s->cell_value = value; s->cell_bytes = 8;
    s->returned = ac_model_add_cell(address, value, 8);
    setup_ok = setup_ok && s->returned;
}
static void setup_direct(void) {
    stage_module(0, owner_source, OWNER_PID, 0x1000, 0x2000, true);
    expected_identity(0);
    request = ordinary_request(0x1100, 0, 0);
}
static void setup_generated(void) {
    stage_module(3, owner_source, OWNER_PID, 0x3000, 0x4000, true);
    stage_module(1, decoy_source, DECOY_PID, 0x5000, 0x6000, true);
    stage_generated(0, owner_source, OWNER_PID, 0x8000, 0x9000, true);
    expected_identity(3);
    request = ordinary_request(0x8004, 0x5100, 0);
}
static void setup_spares(void) {
    stage_module(2, spare_source, SPARE_PID, 0xc000, 0xd000, false);
    stage_generated(2, spare_source, SPARE_PID, 0xe000, 0xf000, false);
}
static call_record *ordinary_call(const ac_request *input) {
    if (call_count >= MAX_CALLS) { report_bounds = false; return NULL; }
    call_record *c = &calls[call_count++];
    c->null_request = input == NULL;
    if (input) c->before = *input;
    c->returned = ac_run(input);
    if (input) c->after = *input;
    ac_observe(&c->snapshot);
    return c;
}
static void attempt_post_entry_commits(void) {
    /* Two actual calls, each recorded before evaluating their conjunction. */
    setup_record *m = setup_event("commit-module", true);
    if (!m) return;
    m->index = 2;
    post_module_commit = ac_model_commit_module(2);
    m->returned = post_module_commit;
    setup_record *g = setup_event("commit-generated", true);
    if (!g) return;
    g->index = 2;
    post_generated_commit = ac_model_commit_generated(2);
    g->returned = post_generated_commit;
    ac_observe(&post_setup_snapshot);
    have_post_setup_snapshot = true;
}
static void expected_event(ac_event *event, ac_event_kind kind, int64_t raw, int error) {
    event->kind = kind;
    event->entered = true;
    event->returned_recorded = true;
    event->raw = raw;
    event->errno_valid = raw == -1;
    event->error_number = raw == -1 ? error : 0;
}
static ac_snapshot community_completion(const ac_request *input, bool open_refused,
                                         bool known_failure, bool short_write) {
    ac_snapshot s = {0};
    s.state = AC_ORDINARY_RETURNED; s.has_payload = true;
    s.result.selection_known = true; s.result.owner = expected_owner;
    s.result.known_failure = known_failure; s.result.short_write = short_write;
    s.result.event_count = open_refused ? 2 : 5;
    expected_event(&s.result.events[0], AC_SELECTION, 0, 0);
    expected_event(&s.result.events[1], AC_OPEN, input->open.raw, input->open.failure_errno);
    if (!open_refused) {
        expected_event(&s.result.events[2], AC_WRITE, input->write.raw, input->write.failure_errno);
        s.result.events[2].requested_length = 32;
        memcpy(s.result.events[2].payload, owner_initial, 32);
        expected_event(&s.result.events[3], AC_FSYNC, input->sync.raw, input->sync.failure_errno);
        expected_event(&s.result.events[4], AC_CLOSE, input->close.raw, input->close.failure_errno);
    }
    return s;
}
static ac_snapshot noowner_completion(void) {
    ac_snapshot s = {0};
    s.state = AC_ORDINARY_RETURNED; s.has_payload = true;
    s.result.selection_known = true; s.result.event_count = 2;
    expected_event(&s.result.events[0], AC_SELECTION, 0, 0);
    expected_event(&s.result.events[1], AC_UNBLAMED, 0, 0);
    return s;
}
static void initial_checks(void) {
    check("initial-public-state-is-virgin", empty_snapshot(&initial_snapshot, AC_VIRGIN));
    check("authored-setup-succeeds", setup_ok);
}
static void check_stable_inputs(void) {
    bool stable = true;
    for (unsigned i = 0; i < call_count; ++i)
        if (!calls[i].null_request && !request_equal(&calls[i].before, &calls[i].after)) stable = false;
    check("ordinary-call-input-remains-stable", stable);
}
static void case_positive(unsigned route) {
    if (route == 0) setup_direct();
    else if (route == 1) setup_generated();
    else {
        stage_module(3, owner_source, OWNER_PID, 0x3000, 0x4000, true);
        add_cell(0x10000, 0x10020);
        add_cell(0x10008, 0x3100);
        expected_identity(3);
        request = ordinary_request(0xa000, 0xb000, 0x10000);
    }
    initial_checks();
    call_record *c = ordinary_call(&request);
    ac_snapshot expected = community_completion(&request, false, false, false);
    check_stable_inputs();
    check("ordinary-success-returns-returned", c && c->returned == AC_RETURNED);
    check("ordinary-success-retains-full-owner", c && owner_equal(&c->snapshot.result.owner, &expected_owner));
    check("ordinary-success-publishes-complete-events", c && snapshot_equal(&c->snapshot, &expected));
}
static void case_noowner(void) {
    request = ordinary_request(0xa000, 0xb000, 1);
    request.open.raw = -2; request.write.raw = 99; request.sync.raw = 2; request.close.raw = 3;
    initial_checks();
    call_record *c = ordinary_call(&request);
    ac_snapshot expected = noowner_completion();
    check_stable_inputs();
    check("noowner-ordinary-entry-returns-returned", c && c->returned == AC_RETURNED);
    check("noowner-identity-is-canonical-zero", c && owner_equal(&c->snapshot.result.owner, &expected.result.owner));
    check("noowner-completes-with-unused-invalid-io", c && snapshot_equal(&c->snapshot, &expected));
}
static void case_incomplete(bool invalid_write) {
    if (invalid_write) { setup_direct(); request.open.raw = 0; request.write.raw = 33; }
    initial_checks();
    call_record *c = ordinary_call(invalid_write ? &request : NULL);
    check_stable_inputs();
    check("admitted-error-returns-incomplete", c && c->returned == AC_INCOMPLETE);
    check("incomplete-entry-exposes-no-payload", c && empty_snapshot(&c->snapshot, AC_ACTIVE_INCOMPLETE));
}
static void case_returned_error(bool open_refused) {
    setup_direct();
    if (open_refused) {
        request.open = (ac_io_value){-1, 91};
        request.write.raw = 99; request.sync.raw = 2; request.close.raw = 3;
    } else {
        request.open = (ac_io_value){0, 77};
        request.write = (ac_io_value){31, 88};
        request.sync = (ac_io_value){-1, 93};
        request.close = (ac_io_value){0, 111};
    }
    initial_checks();
    call_record *c = ordinary_call(&request);
    ac_snapshot expected = community_completion(&request, open_refused, true, !open_refused);
    check_stable_inputs();
    check("returned-failure-is-ordinary-completion", c && c->returned == AC_RETURNED);
    check("returned-failure-retains-selected-owner", c && owner_equal(&c->snapshot.result.owner, &expected_owner));
    check(open_refused ? "open-refusal-stops-later-events" : "short-write-sync-error-still-records-close",
          c && snapshot_equal(&c->snapshot, &expected));
}
static void case_terminal(bool completed) {
    setup_generated(); setup_spares();
    initial_checks();
    ac_request first_request = request;
    call_record *first = ordinary_call(completed ? &request : NULL);
    ac_snapshot expected = community_completion(&first_request, false, false, false);
    if (completed)
        check("terminal-first-entry-completes", first && first->returned == AC_RETURNED &&
              snapshot_equal(&first->snapshot, &expected));
    else
        check("terminal-first-entry-is-incomplete", first && first->returned == AC_INCOMPLETE &&
              empty_snapshot(&first->snapshot, AC_ACTIVE_INCOMPLETE));
    /* Caller storage changes only after ordinary return. Model copies stay fixed. */
    memcpy(owner_source, mutated_id, AC_ID_BYTES);
    request = ordinary_request(0x3300, 0, 0);
    request.open.raw = 7; request.write.raw = 1;
    check("post-return-caller-mutation-is-real", !request_equal(&first_request, &request) &&
          memcmp(owner_source, owner_initial, AC_ID_BYTES) != 0);
    call_record *second = ordinary_call(&request);
    check_stable_inputs();
    check("terminal-repeat-is-suppressed", second && second->returned == AC_SUPPRESSED);
    check("terminal-repeat-preserves-public-snapshot", first && second &&
          snapshot_equal(&first->snapshot, &second->snapshot));
    attempt_post_entry_commits();
    check("post-entry-setup-commits-are-refused", have_post_setup_snapshot &&
          !post_module_commit && !post_generated_commit);
    check("setup-refusal-preserves-public-snapshot", first && have_post_setup_snapshot &&
          snapshot_equal(&first->snapshot, &post_setup_snapshot));
}
static void case_control(unsigned which) {
    if (which == 1) setup_direct();
    else if (which != 3) setup_generated();
    if (which == 4) setup_spares();
    initial_checks();
    call_record *c = ordinary_call(which == 3 ? NULL : &request);
    check_stable_inputs();
    ac_snapshot expected = community_completion(&request, false, false, false);
    if (which == 0 || which == 1) {
        check(which == 0 ? "ordinary-entry-drives-to-publication" :
              "selection-return-does-not-end-ordinary-entry",
              c && c->returned == AC_RETURNED && snapshot_equal(&c->snapshot, &expected));
    } else if (which == 2) {
        check("published-entry-returns-returned", c && c->returned == AC_RETURNED);
        check("published-control-preserves-completed-payload", c && snapshot_equal(&c->snapshot, &expected));
    } else if (which == 3) {
        check("halted-entry-returns-incomplete", c && c->returned == AC_INCOMPLETE);
        check("halted-control-preserves-empty-active-state", c && empty_snapshot(&c->snapshot, AC_ACTIVE_INCOMPLETE));
    } else {
        check("setup-control-starts-from-completed-attempt", c && c->returned == AC_RETURNED &&
              snapshot_equal(&c->snapshot, &expected));
        attempt_post_entry_commits();
        check("post-entry-setup-commits-are-refused", have_post_setup_snapshot &&
              !post_module_commit && !post_generated_commit);
        check("setup-control-preserves-completed-payload", c && have_post_setup_snapshot &&
              snapshot_equal(&c->snapshot, &post_setup_snapshot));
    }
}

static void json_bool(FILE *f, bool value) { fputs(value ? "true" : "false", f); }
static void json_hex(FILE *f, const char bytes[AC_ID_BYTES]) {
    fputc('"', f);
    for (unsigned i = 0; i < AC_ID_BYTES; ++i) fprintf(f, "%02x", (unsigned)(unsigned char)bytes[i]);
    fputc('"', f);
}
static void json_io(FILE *f, ac_io_value value) {
    fprintf(f, "{\"raw\":%" PRId64 ",\"failure_errno\":%d}", value.raw, value.failure_errno);
}
static void json_request(FILE *f, const ac_request *v) {
    fprintf(f, "{\"pc\":%" PRIuPTR ",\"lr\":%" PRIuPTR ",\"fp\":%" PRIuPTR ",\"open\":", v->pc, v->lr, v->fp);
    json_io(f, v->open); fputs(",\"write\":", f); json_io(f, v->write);
    fputs(",\"sync\":", f); json_io(f, v->sync);
    fputs(",\"close\":", f); json_io(f, v->close); fputc('}', f);
}
static void json_owner(FILE *f, const ac_owner *v) {
    fprintf(f, "{\"kind\":%u,\"index\":%" PRIu32 ",\"process_id\":%" PRIu64 ",\"id_hex\":",
            (unsigned)v->kind, v->index, v->process_id);
    json_hex(f, v->id); fputc('}', f);
}
static void json_event(FILE *f, const ac_event *v) {
    fprintf(f, "{\"kind\":%u,\"entered\":", (unsigned)v->kind); json_bool(f, v->entered);
    fputs(",\"returned_recorded\":", f); json_bool(f, v->returned_recorded);
    fprintf(f, ",\"raw\":%" PRId64 ",\"errno_valid\":", v->raw); json_bool(f, v->errno_valid);
    fprintf(f, ",\"error_number\":%d,\"requested_length\":%zu,\"payload_hex\":", v->error_number, v->requested_length);
    json_hex(f, v->payload); fputc('}', f);
}
static void json_snapshot(FILE *f, const ac_snapshot *s) {
    fprintf(f, "{\"state\":%u,\"has_payload\":", (unsigned)s->state); json_bool(f, s->has_payload);
    fputs(",\"result\":{\"selection_known\":", f); json_bool(f, s->result.selection_known);
    fputs(",\"owner\":", f); json_owner(f, &s->result.owner);
    fputs(",\"known_failure\":", f); json_bool(f, s->result.known_failure);
    fputs(",\"short_write\":", f); json_bool(f, s->result.short_write);
    fputs(",\"violation\":", f); json_bool(f, s->result.violation);
    fprintf(f, ",\"event_count\":%u,\"events\":[", s->result.event_count);
    for (unsigned i = 0; i < AC_EVENT_CAPACITY; ++i) {
        if (i) fputc(',', f);
        json_event(f, &s->result.events[i]);
    }
    fputs("]}}", f);
}
static void common(FILE *f) {
    fprintf(f, "{\"schema\":1,\"selector\":\"%s\",\"checks\":%u,\"check_names\":[", selector_name, check_count);
    for (unsigned i = 0; i < check_count; ++i) {
        if (i) fputc(',', f);
        fprintf(f, "\"%s\"", check_names[i]);
    }
    fprintf(f, "],\"failures\":%u,\"failed_checks\":[", failure_count);
    for (unsigned i = 0; i < failure_count; ++i) {
        if (i) fputc(',', f);
        fprintf(f, "\"%s\"", failed_names[i]);
    }
    fputc(']', f);
}
static bool evidence(const char *path) {
    FILE *f = fopen(path, "wx");
    if (!f) return false;
    common(f);
    fputs(",\"source_scope\":{\"providers\":\"authored-values-only\",\"native_fault_provider_functions_present\":false,"
          "\"signals\":false,\"threads\":false,\"evidence_io\":\"stdio-report-only\","
          "\"attempt_observation\":\"public-before-entry-and-after-return-only\"},\"caller_id_before_hex\":", f);
    json_hex(f, owner_initial); fputs(",\"caller_id_after_hex\":", f); json_hex(f, owner_source);
    fputs(",\"expected_owner\":", f); json_owner(f, &expected_owner);
    fputs(",\"initial_snapshot\":", f); json_snapshot(f, &initial_snapshot);
    fputs(",\"setup\":[", f);
    for (unsigned i = 0; i < setup_count; ++i) {
        const setup_record *s = &setup_records[i];
        if (i) fputc(',', f);
        fprintf(f, "{\"operation\":\"%s\",\"after_entry\":", s->operation); json_bool(f, s->after_entry);
        fputs(",\"returned\":", f); json_bool(f, s->returned);
        fprintf(f, ",\"index\":%u,\"kind\":%u,\"process_id\":%" PRIu64 ",\"lo\":%" PRIuPTR ",\"hi\":%" PRIuPTR
                   ",\"island_lo\":%" PRIuPTR ",\"island_hi\":%" PRIuPTR ",\"trampoline_lo\":%" PRIuPTR
                   ",\"trampoline_hi\":%" PRIuPTR ",\"cell_address\":%" PRIuPTR ",\"cell_value\":%" PRIu64
                   ",\"cell_bytes\":%" PRId64 ",\"id_hex\":", s->index, (unsigned)s->kind, s->process_id,
                s->lo, s->hi, s->island_lo, s->island_hi, s->trampoline_lo, s->trampoline_hi,
                s->cell_address, s->cell_value, s->cell_bytes);
        json_hex(f, s->id); fputc('}', f);
    }
    fputs("],\"calls\":[", f);
    for (unsigned i = 0; i < call_count; ++i) {
        const call_record *c = &calls[i];
        if (i) fputc(',', f);
        fputs("{\"null_request\":", f); json_bool(f, c->null_request);
        fputs(",\"before\":", f); json_request(f, &c->before);
        fputs(",\"after\":", f); json_request(f, &c->after);
        fprintf(f, ",\"returned\":%u,\"snapshot\":", (unsigned)c->returned);
        json_snapshot(f, &c->snapshot); fputc('}', f);
    }
    fputs("],\"post_setup\":{\"observed\":", f); json_bool(f, have_post_setup_snapshot);
    fputs(",\"module_commit_returned\":", f); json_bool(f, post_module_commit);
    fputs(",\"generated_commit_returned\":", f); json_bool(f, post_generated_commit);
    fputs(",\"snapshot\":", f); json_snapshot(f, &post_setup_snapshot);
    fputs("}}\n", f);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok;
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    selector_name = argv[1];
    ac_observe(&initial_snapshot);
    if (!strcmp(selector_name, "ordinary-direct")) case_positive(0);
    else if (!strcmp(selector_name, "ordinary-generated")) case_positive(1);
    else if (!strcmp(selector_name, "ordinary-frame")) case_positive(2);
    else if (!strcmp(selector_name, "ordinary-noowner")) case_noowner();
    else if (!strcmp(selector_name, "ordinary-null")) case_incomplete(false);
    else if (!strcmp(selector_name, "ordinary-invalid-write")) case_incomplete(true);
    else if (!strcmp(selector_name, "ordinary-open-refusal")) case_returned_error(true);
    else if (!strcmp(selector_name, "ordinary-short-sync-error")) case_returned_error(false);
    else if (!strcmp(selector_name, "terminal-completed")) case_terminal(true);
    else if (!strcmp(selector_name, "terminal-incomplete")) case_terminal(false);
    else if (!strcmp(selector_name, "control-omit-driver")) case_control(0);
    else if (!strcmp(selector_name, "control-stop-after-selection")) case_control(1);
    else if (!strcmp(selector_name, "control-published-return")) case_control(2);
    else if (!strcmp(selector_name, "control-halted-return")) case_control(3);
    else if (!strcmp(selector_name, "control-post-entry-setup")) case_control(4);
    else return 2;
    check("report-bounds-respected", report_bounds && check_count < MAX_CHECKS &&
          call_count > 0 && call_count <= MAX_CALLS && setup_count <= MAX_SETUP);
    if (!evidence(argv[2])) return 4;
    common(stdout); fputs("}\n", stdout);
    if (ferror(stdout)) return 4;
    return failure_count ? 1 : 0;
}
