/* SPDX-License-Identifier: GPL-3.0-only */
/* New ordinary-provider witnesses. Only authored setup, ordinary ac_run and
 * public ac_observe are used. The transcript records prepared values BEFORE
 * each C return; it is not private engine raw/record telemetry.
 * Evidence stdio is separate from the authored fault-provider callbacks. */
#include "core.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef AO_WITNESS_SETUP
#error "provider witness requires setup-only configuration"
#endif
#ifdef AC_FIXTURE
#error "private scheduler and inspector APIs are excluded"
#endif

#define MAX_CHECKS 20u
#define MAX_SETUP 16u
#define MAX_CALLS 3u
#define MAX_CALLBACKS 12u
#define CONTEXTS 4u
#define OWNER_PID UINT64_C(0x100000001)
#define DECOY_PID UINT64_C(0x200000002)
#define WIDE_TOKEN INT64_C(0x100000001)

static char owner_source[AC_ID_BYTES] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char owner_initial[AC_ID_BYTES] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char decoy_id[AC_ID_BYTES] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char mutated_id[AC_ID_BYTES] = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
static const char *selector_name;
static const char *check_names[MAX_CHECKS], *failed_names[MAX_CHECKS];
static unsigned check_count, failure_count;
static bool report_bounds = true, setup_ok = true;

typedef struct {
    const char *operation;
    bool returned;
    unsigned index;
    rc_owner_kind kind;
    uint64_t process_id;
    uintptr_t lo, hi, island_lo, island_hi, trampoline_lo, trampoline_hi;
    char id[AC_ID_BYTES];
} setup_record;
typedef struct {
    bool null_request;
    ac_request before, after;
    ac_request_result returned;
    ac_snapshot snapshot;
} call_record;
typedef struct {
    unsigned context_id;
    ac_event_kind kind;
    ac_owner owner_arg;
    ap_write_args write_arg;
    int64_t token_arg;
    bool entry_observed, exit_observed;
    ac_snapshot entry_snapshot, exit_snapshot;
    bool has_prepared_return, prepared_void_return;
    ac_io_value prepared_return;
    bool nested_attempted, nested_null;
    unsigned nested_call_index;
    ac_request_result nested_return;
    ac_snapshot nested_before, nested_after;
} callback_record;
typedef struct {
    unsigned id;
    ac_io_value open, write, sync, close;
    bool observe;
    int nested_kind;
    bool nested_null, nested_started;
    const ac_request *nested_request;
    unsigned invocations[AC_EVENT_CAPACITY];
} provider_context;
typedef struct {
    bool performed;
    char id_before[AC_ID_BYTES], id_after[AC_ID_BYTES];
    ac_request request_before, request_after;
} mutation_record;

static provider_context contexts[CONTEXTS];
/* The only alternate binding used by the dedicated core control. Its storage,
 * pointer value and authored return configuration all remain live and stable. */
void *const ap_control_alternate_context = &contexts[2];
/* Tables are configured during setup, then unchanged through every ordinary call;
 * the engine receives const table pointers, not physically immutable mappings. */
static ap_provider_table tables[CONTEXTS], tables_before[CONTEXTS];
static provider_context configurations_before[CONTEXTS];
static ac_request request, nested_request, repeat_request;
static ac_owner expected_owner;
static ac_snapshot initial_snapshot;
static setup_record setup_records[MAX_SETUP];
static call_record calls[MAX_CALLS];
static callback_record callbacks[MAX_CALLBACKS];
static unsigned setup_count, call_count, callback_count;
static mutation_record mutation;
static unsigned normalized_callbacks[MAX_CALLBACKS], normalized_count;
static bool normalization_valid;

static void check(const char *name, bool passed) {
    if (check_count >= MAX_CHECKS) { report_bounds = false; return; }
    check_names[check_count++] = name;
    if (!passed) failed_names[failure_count++] = name;
}
static bool io_equal(ac_io_value a, ac_io_value b) {
    return a.raw == b.raw && a.failure_errno == b.failure_errno;
}
static bool owner_equal(const ac_owner *a, const ac_owner *b) {
    return a->kind == b->kind && a->index == b->index &&
        a->process_id == b->process_id && memcmp(a->id, b->id, AC_ID_BYTES) == 0;
}
static bool request_equal(const ac_request *a, const ac_request *b) {
    return a->pc == b->pc && a->lr == b->lr && a->fp == b->fp &&
        a->providers == b->providers && a->context == b->context;
}
static bool event_equal(const ac_event *a, const ac_event *b) {
    return a->kind == b->kind && a->entered == b->entered &&
        a->returned_recorded == b->returned_recorded && a->raw == b->raw &&
        a->errno_valid == b->errno_valid && a->error_number == b->error_number &&
        a->requested_length == b->requested_length &&
        memcmp(a->payload, b->payload, AC_ID_BYTES) == 0;
}
static bool snapshot_equal(const ac_snapshot *a, const ac_snapshot *b) {
    if (a->state != b->state || a->has_payload != b->has_payload ||
        a->result.selection_known != b->result.selection_known ||
        !owner_equal(&a->result.owner, &b->result.owner) ||
        a->result.known_failure != b->result.known_failure ||
        a->result.short_write != b->result.short_write ||
        a->result.violation != b->result.violation ||
        a->result.event_count != b->result.event_count) return false;
    for (unsigned i = 0; i < AC_EVENT_CAPACITY; ++i)
        if (!event_equal(&a->result.events[i], &b->result.events[i])) return false;
    return true;
}
static bool empty_snapshot(const ac_snapshot *s, ac_state state) {
    ac_snapshot expected = {0};
    expected.state = state;
    return snapshot_equal(s, &expected);
}
static unsigned context_id(const void *p) {
    if (!p) return 0;
    for (unsigned i = 0; i < CONTEXTS; ++i)
        if (p == &contexts[i]) return i + 1u;
    return UINT32_MAX;
}
static unsigned table_id(const ap_provider_table *p) {
    if (!p) return 0;
    for (unsigned i = 0; i < CONTEXTS; ++i)
        if (p == &tables[i]) return i + 1u;
    return UINT32_MAX;
}
static bool table_equal(const ap_provider_table *a, const ap_provider_table *b) {
    return a->open == b->open && a->write == b->write && a->sync == b->sync &&
        a->close == b->close && a->unblamed == b->unblamed;
}
static bool configuration_equal(const provider_context *a, const provider_context *b) {
    return a->id == b->id && io_equal(a->open, b->open) &&
        io_equal(a->write, b->write) && io_equal(a->sync, b->sync) &&
        io_equal(a->close, b->close) && a->observe == b->observe &&
        a->nested_kind == b->nested_kind && a->nested_null == b->nested_null &&
        a->nested_request == b->nested_request;
}
static bool write_equal(const ap_write_args *a, const ap_write_args *b) {
    return a->accepted_open_token == b->accepted_open_token &&
        a->requested_length == b->requested_length &&
        memcmp(a->payload, b->payload, AC_ID_BYTES) == 0;
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
static provider_context *live_context(void *p) {
    unsigned id = context_id(p);
    if (id == 0 || id > CONTEXTS) { report_bounds = false; return &contexts[0]; }
    return &contexts[id - 1u];
}
static callback_record *callback_begin(provider_context *c, ac_event_kind kind) {
    if (callback_count >= MAX_CALLBACKS || (unsigned)kind >= AC_EVENT_CAPACITY) {
        report_bounds = false;
        return NULL;
    }
    callback_record *r = &callbacks[callback_count++];
    r->context_id = c->id;
    r->kind = kind;
    ++c->invocations[kind];
    if (c->observe) {
        r->entry_observed = true;
        ac_observe(&r->entry_snapshot);
    }
    return r;
}
static void callback_nest(provider_context *c, callback_record *r) {
    if (!r || c->nested_kind != (int)r->kind || c->nested_started) return;
    /* This flag is callback-context accounting, not another admission gate. */
    c->nested_started = true;
    r->nested_attempted = true;
    r->nested_null = c->nested_null;
    ac_observe(&r->nested_before);
    r->nested_call_index = call_count;
    call_record *nested = ordinary_call(c->nested_null ? NULL : c->nested_request);
    if (nested) r->nested_return = nested->returned;
    ac_observe(&r->nested_after);
}
static void callback_exit(provider_context *c, callback_record *r) {
    if (r && c->observe) {
        r->exit_observed = true;
        ac_observe(&r->exit_snapshot);
    }
}
static ac_io_value provider_open(void *opaque, ac_owner owner_value) {
    provider_context *c = live_context(opaque);
    callback_record *r = callback_begin(c, AC_OPEN);
    if (r) r->owner_arg = owner_value;
    callback_nest(c, r);
    callback_exit(c, r);
    ac_io_value value = c->open;
    if (r) { r->has_prepared_return = true; r->prepared_return = value; }
    return value;
}
static ac_io_value provider_write(void *opaque, ap_write_args args_value) {
    provider_context *c = live_context(opaque);
    callback_record *r = callback_begin(c, AC_WRITE);
    if (r) { r->write_arg = args_value; r->token_arg = args_value.accepted_open_token; }
    callback_nest(c, r);
    callback_exit(c, r);
    ac_io_value value = c->write;
    if (r) { r->has_prepared_return = true; r->prepared_return = value; }
    return value;
}
static ac_io_value provider_sync(void *opaque, int64_t accepted_open_token) {
    provider_context *c = live_context(opaque);
    callback_record *r = callback_begin(c, AC_FSYNC);
    if (r) r->token_arg = accepted_open_token;
    callback_nest(c, r);
    callback_exit(c, r);
    ac_io_value value = c->sync;
    if (r) { r->has_prepared_return = true; r->prepared_return = value; }
    return value;
}
static ac_io_value provider_close(void *opaque, int64_t accepted_open_token) {
    provider_context *c = live_context(opaque);
    callback_record *r = callback_begin(c, AC_CLOSE);
    if (r) r->token_arg = accepted_open_token;
    callback_nest(c, r);
    callback_exit(c, r);
    ac_io_value value = c->close;
    if (r) { r->has_prepared_return = true; r->prepared_return = value; }
    return value;
}
static void provider_unblamed(void *opaque) {
    provider_context *c = live_context(opaque);
    callback_record *r = callback_begin(c, AC_UNBLAMED);
    callback_nest(c, r);
    callback_exit(c, r);
    if (r) r->prepared_void_return = true;
}
static ac_request authored_request(uintptr_t pc, uintptr_t lr, uintptr_t fp, unsigned context) {
    ac_request r = {0};
    r.pc = pc; r.lr = lr; r.fp = fp;
    r.providers = &tables[context]; r.context = &contexts[context];
    return r;
}
static void initialize_providers(void) {
    for (unsigned i = 0; i < CONTEXTS; ++i) {
        contexts[i].id = i + 1u;
        contexts[i].open = (ac_io_value){0, 77};
        contexts[i].write = (ac_io_value){32, 88};
        contexts[i].sync = (ac_io_value){0, 99};
        contexts[i].close = (ac_io_value){0, 0};
        contexts[i].observe = true;
        contexts[i].nested_kind = -1;
        tables[i] = (ap_provider_table){provider_open, provider_write, provider_sync,
                                       provider_close, provider_unblamed};
    }
    /* Unused unblamed callback is deliberately absent on the winning Community table. */
    tables[0].unblamed = NULL;
    nested_request = authored_request(0xd100, 0xe100, 1, 1);
    repeat_request = authored_request(0xd200, 0xe200, 1, 3);
    contexts[0].nested_request = &nested_request;
}
static setup_record *new_setup(const char *operation) {
    if (setup_count >= MAX_SETUP) { report_bounds = false; setup_ok = false; return NULL; }
    setup_record *s = &setup_records[setup_count++];
    s->operation = operation;
    return s;
}
static void stage_module(unsigned index, const char id[AC_ID_BYTES], uint64_t pid,
                         uintptr_t lo, uintptr_t hi) {
    setup_record *s = new_setup("stage-module");
    if (!s) return;
    s->index = index; s->kind = RC_OWNER_COMMUNITY; s->process_id = pid;
    s->lo = lo; s->hi = hi; memcpy(s->id, id, AC_ID_BYTES);
    s->returned = ac_model_stage_module(index, id, pid, lo, hi);
    setup_ok = setup_ok && s->returned;
    s = new_setup("commit-module");
    if (!s) return;
    s->index = index; s->returned = ac_model_commit_module(index);
    setup_ok = setup_ok && s->returned;
}
static void stage_generated(void) {
    setup_record *s = new_setup("stage-generated");
    if (!s) return;
    s->index = 0; s->kind = RC_OWNER_COMMUNITY; s->process_id = OWNER_PID;
    s->island_lo = 0x8000; s->island_hi = 0x8008;
    s->trampoline_lo = 0x9000; s->trampoline_hi = 0x9020;
    memcpy(s->id, owner_source, AC_ID_BYTES);
    s->returned = ac_model_stage_generated(0, RC_OWNER_COMMUNITY, OWNER_PID,
        owner_source, (rc_span){0x8000, 0x8008}, (rc_span){0x9000, 0x9020});
    setup_ok = setup_ok && s->returned;
    s = new_setup("commit-generated");
    if (!s) return;
    s->index = 0; s->returned = ac_model_commit_generated(0);
    setup_ok = setup_ok && s->returned;
}
static void identity(unsigned index) {
    expected_owner.kind = AC_COMMUNITY;
    expected_owner.index = index;
    expected_owner.process_id = OWNER_PID;
    memcpy(expected_owner.id, owner_initial, AC_ID_BYTES);
}
static void setup_community(bool generated) {
    if (generated) {
        stage_module(3, owner_source, OWNER_PID, 0x3000, 0x4000);
        stage_module(1, decoy_id, DECOY_PID, 0x5000, 0x6000);
        stage_generated();
        identity(3);
        request = authored_request(0x8004, 0x5100, 0, 0);
    } else {
        stage_module(0, owner_source, OWNER_PID, 0x1000, 0x2000);
        identity(0);
        request = authored_request(0x1100, 0, 0, 0);
    }
}
static void setup_noowner(void) {
    request = authored_request(0xa000, 0xb000, 1, 0);
    tables[0] = (ap_provider_table){NULL, NULL, NULL, NULL, provider_unblamed};
    contexts[0].open = (ac_io_value){-2, 77};
    contexts[0].write = (ac_io_value){99, 88};
    contexts[0].sync = (ac_io_value){2, 99};
    contexts[0].close = (ac_io_value){3, 111};
}
static void freeze_bindings(void) {
    for (unsigned i = 0; i < CONTEXTS; ++i) {
        tables_before[i] = tables[i];
        configurations_before[i] = contexts[i];
    }
}
static void start_checks(void) {
    check("initial-public-state-is-virgin", empty_snapshot(&initial_snapshot, AC_VIRGIN));
    check("authored-setup-succeeds", setup_ok);
}
static void stable_checks(void) {
    bool stable = true;
    for (unsigned i = 0; i < call_count; ++i)
        if (!calls[i].null_request && !request_equal(&calls[i].before, &calls[i].after)) stable = false;
    for (unsigned i = 0; i < CONTEXTS; ++i)
        if (!table_equal(&tables_before[i], &tables[i]) ||
            !configuration_equal(&configurations_before[i], &contexts[i])) stable = false;
    check("ordinary-call-inputs-and-bindings-stay-stable", stable);
    bool observed_active = true;
    for (unsigned i = 0; i < callback_count; ++i) {
        const callback_record *r = &callbacks[i];
        if ((r->entry_observed && !empty_snapshot(&r->entry_snapshot, AC_ACTIVE_INCOMPLETE)) ||
            (r->exit_observed && !empty_snapshot(&r->exit_snapshot, AC_ACTIVE_INCOMPLETE)))
            observed_active = false;
    }
    check("observed-callback-boundaries-have-empty-active-state", observed_active);
}
static void expected_event(ac_event *e, ac_event_kind kind, int64_t raw, int error) {
    e->kind = kind; e->entered = true; e->returned_recorded = true;
    e->raw = raw; e->errno_valid = raw == -1; e->error_number = raw == -1 ? error : 0;
}
static ac_snapshot expected_completion(bool noowner, bool open_refused) {
    ac_snapshot s = {0};
    s.state = AC_ORDINARY_RETURNED; s.has_payload = true;
    s.result.selection_known = true; s.result.owner = expected_owner;
    expected_event(&s.result.events[0], AC_SELECTION, 0, 0);
    if (noowner) {
        s.result.event_count = 2;
        expected_event(&s.result.events[1], AC_UNBLAMED, 0, 0);
        return s;
    }
    const provider_context *c = &configurations_before[0];
    s.result.event_count = open_refused ? 2 : 5;
    s.result.known_failure = c->open.raw == -1;
    expected_event(&s.result.events[1], AC_OPEN, c->open.raw, c->open.failure_errno);
    if (!open_refused) {
        expected_event(&s.result.events[2], AC_WRITE, c->write.raw, c->write.failure_errno);
        s.result.events[2].requested_length = 32;
        memcpy(s.result.events[2].payload, owner_initial, AC_ID_BYTES);
        expected_event(&s.result.events[3], AC_FSYNC, c->sync.raw, c->sync.failure_errno);
        expected_event(&s.result.events[4], AC_CLOSE, c->close.raw, c->close.failure_errno);
        s.result.known_failure = s.result.known_failure || c->write.raw == -1 ||
            c->sync.raw == -1 || c->close.raw == -1;
        s.result.short_write = c->write.raw >= 0 && c->write.raw < 32;
    }
    return s;
}
static bool duplicate_equal(const callback_record *a, const callback_record *b) {
    return a->kind == AC_WRITE && b->kind == AC_WRITE &&
        a->context_id == b->context_id && owner_equal(&a->owner_arg, &b->owner_arg) &&
        write_equal(&a->write_arg, &b->write_arg) && a->token_arg == b->token_arg &&
        a->entry_observed == b->entry_observed && a->exit_observed == b->exit_observed &&
        snapshot_equal(&a->entry_snapshot, &b->entry_snapshot) &&
        snapshot_equal(&a->exit_snapshot, &b->exit_snapshot) &&
        a->has_prepared_return && b->has_prepared_return &&
        io_equal(a->prepared_return, b->prepared_return) &&
        !a->prepared_void_return && !b->prepared_void_return &&
        !a->nested_attempted && !b->nested_attempted &&
        a->nested_null == b->nested_null && a->nested_call_index == b->nested_call_index &&
        a->nested_return == b->nested_return &&
        snapshot_equal(&a->nested_before, &b->nested_before) &&
        snapshot_equal(&a->nested_after, &b->nested_after);
}
static void normalize_callbacks(bool allow_one_identical_write_duplicate) {
    normalized_count = 0;
    normalization_valid = true;
    unsigned skipped = 0;
    for (unsigned i = 0; i < callback_count; ++i) {
        if (allow_one_identical_write_duplicate && i > 0 &&
            callbacks[i].kind == AC_WRITE && callbacks[i - 1u].kind == AC_WRITE) {
            if (skipped != 0 || !duplicate_equal(&callbacks[i - 1u], &callbacks[i])) {
                normalization_valid = false;
            } else {
                ++skipped;
                continue;
            }
        }
        normalized_callbacks[normalized_count++] = i;
    }
}
static bool sequence_matches(const ac_event_kind *kinds, unsigned count) {
    if (!normalization_valid || normalized_count != count) return false;
    for (unsigned i = 0; i < count; ++i) {
        const callback_record *r = &callbacks[normalized_callbacks[i]];
        if (r->kind != kinds[i]) return false;
        if (r->context_id == 0 || r->context_id > CONTEXTS) return false;
        const provider_context *c = &configurations_before[r->context_id - 1u];
        if (r->kind == AC_UNBLAMED) {
            if (r->has_prepared_return || !r->prepared_void_return) return false;
        } else {
            ac_io_value expected;
            switch (r->kind) {
            case AC_OPEN: expected = c->open; break;
            case AC_WRITE: expected = c->write; break;
            case AC_FSYNC: expected = c->sync; break;
            case AC_CLOSE: expected = c->close; break;
            default: return false;
            }
            if (!r->has_prepared_return || r->prepared_void_return ||
                !io_equal(r->prepared_return, expected)) return false;
        }
    }
    return true;
}
static bool all_contexts_bound(void) {
    for (unsigned i = 0; i < callback_count; ++i)
        if (callbacks[i].context_id != 1) return false;
    return true;
}
static bool callback_owner_and_lengths(void) {
    for (unsigned i = 0; i < normalized_count; ++i) {
        const callback_record *r = &callbacks[normalized_callbacks[i]];
        if (r->kind == AC_OPEN && !owner_equal(&r->owner_arg, &expected_owner)) return false;
        if (r->kind == AC_WRITE && r->write_arg.requested_length != 32) return false;
    }
    return true;
}
static bool callback_payloads_match(void) {
    for (unsigned i = 0; i < callback_count; ++i)
        if (callbacks[i].kind == AC_WRITE &&
            memcmp(callbacks[i].write_arg.payload, owner_initial, AC_ID_BYTES) != 0) return false;
    return true;
}
static bool callback_tokens_match(void) {
    int64_t token = configurations_before[0].open.raw;
    for (unsigned i = 0; i < callback_count; ++i) {
        const callback_record *r = &callbacks[i];
        if (r->kind == AC_WRITE && (r->write_arg.accepted_open_token != token ||
                                    r->token_arg != token)) return false;
        if ((r->kind == AC_FSYNC || r->kind == AC_CLOSE) && r->token_arg != token) return false;
    }
    return true;
}
static unsigned callback_kind_count(ac_event_kind kind) {
    unsigned count = 0;
    for (unsigned i = 0; i < callback_count; ++i)
        if (callbacks[i].kind == kind) ++count;
    return count;
}
static unsigned context_invocations(unsigned index) {
    unsigned count = 0;
    for (unsigned i = 0; i < AC_EVENT_CAPACITY; ++i) count += contexts[index].invocations[i];
    return count;
}
static const ac_event_kind full_sequence[] = {AC_OPEN, AC_WRITE, AC_FSYNC, AC_CLOSE};
static const ac_event_kind noowner_sequence[] = {AC_UNBLAMED};
static const ac_event_kind open_sequence[] = {AC_OPEN};
static const ac_event_kind invalid_sequence[] = {AC_OPEN, AC_WRITE};

static void complete_case(bool noowner, bool open_refused) {
    freeze_bindings(); start_checks();
    call_record *c = ordinary_call(&request);
    normalize_callbacks(false);
    ac_snapshot expected = expected_completion(noowner, open_refused);
    stable_checks();
    check("ordinary-provider-entry-returns-returned", c && c->returned == AC_RETURNED);
    check("public-completion-matches-authored-return-values", c && snapshot_equal(&c->snapshot, &expected));
    check("callback-sequence-and-prepared-values-match", sequence_matches(
        noowner ? noowner_sequence : open_refused ? open_sequence : full_sequence,
        noowner || open_refused ? 1u : 4u));
    check("callback-receives-bound-context", all_contexts_bound());
    check("callback-receives-copied-owner-and-length", callback_owner_and_lengths());
    check("write-callback-receives-recorded-payload", callback_payloads_match());
    check("accepted-open-token-reaches-followup-callbacks", callback_tokens_match());
}
static void incomplete_case(bool missing) {
    setup_community(false);
    if (missing) tables[0].write = NULL;
    else contexts[0].write = (ac_io_value){33, 88};
    freeze_bindings(); start_checks();
    call_record *c = ordinary_call(&request);
    normalize_callbacks(false);
    stable_checks();
    check("invalid-or-missing-provider-returns-incomplete", c && c->returned == AC_INCOMPLETE);
    check("incomplete-provider-exposes-no-public-payload", c && empty_snapshot(&c->snapshot, AC_ACTIVE_INCOMPLETE));
    check(missing ? "missing-write-has-no-callback-invocation" : "invalid-write-retains-prepared-return-transcript",
          sequence_matches(missing ? open_sequence : invalid_sequence, missing ? 1u : 2u));
    check("callback-receives-bound-context", all_contexts_bound());
    check("callback-receives-copied-owner-and-length", callback_owner_and_lengths());
    check("write-callback-receives-recorded-payload", callback_payloads_match());
    check("accepted-open-token-reaches-followup-callbacks", callback_tokens_match());
}
static void nested_checks(void) {
    unsigned attempts = 0;
    bool returned = true, empty = true;
    for (unsigned i = 0; i < callback_count; ++i) {
        const callback_record *r = &callbacks[i];
        if (!r->nested_attempted) continue;
        ++attempts;
        if (r->nested_return != AC_SUPPRESSED) returned = false;
        if (!empty_snapshot(&r->nested_before, AC_ACTIVE_INCOMPLETE) ||
            !empty_snapshot(&r->nested_after, AC_ACTIVE_INCOMPLETE)) empty = false;
    }
    check("exactly-one-guarded-nested-call-occurs", attempts == 1 && call_count == 2);
    check("nested-entry-returns-suppressed", returned && attempts == 1);
    check("nested-loser-invokes-no-provider", context_invocations(1) == 0);
    check("nested-boundaries-preserve-empty-active-state", empty && attempts == 1);
}
static void nested_case(ac_event_kind kind, bool null_nested) {
    bool noowner = kind == AC_UNBLAMED;
    if (noowner) setup_noowner(); else setup_community(false);
    contexts[0].nested_kind = (int)kind;
    contexts[0].nested_null = null_nested;
    freeze_bindings(); start_checks();
    call_record *c = ordinary_call(&request);
    normalize_callbacks(false);
    ac_snapshot expected = expected_completion(noowner, false);
    stable_checks(); nested_checks();
    check("outer-provider-entry-completes-after-nested-loser", c && c->returned == AC_RETURNED &&
          snapshot_equal(&c->snapshot, &expected));
    check("callback-sequence-and-prepared-values-match", sequence_matches(
        noowner ? noowner_sequence : full_sequence, noowner ? 1u : 4u));
    check("callback-receives-bound-context", all_contexts_bound());
    check("callback-receives-copied-owner-and-length", callback_owner_and_lengths());
    check("write-callback-receives-recorded-payload", callback_payloads_match());
    check("accepted-open-token-reaches-followup-callbacks", callback_tokens_match());
}
static void mutate_after_return(void) {
    mutation.performed = true;
    memcpy(mutation.id_before, owner_source, AC_ID_BYTES);
    mutation.request_before = request;
    memcpy(owner_source, mutated_id, AC_ID_BYTES);
    request.pc = 0xd000; request.lr = 0xe000; request.fp = 1;
    memcpy(mutation.id_after, owner_source, AC_ID_BYTES);
    mutation.request_after = request;
}
static void repeat_case(bool incomplete, bool spy_control) {
    setup_community(!incomplete);
    if (incomplete) tables[0].write = NULL;
    if (spy_control) {
        tables[3] = (ap_provider_table){NULL, NULL, NULL, NULL, provider_unblamed};
        contexts[3].observe = false;
        contexts[3].nested_kind = -1;
    }
    freeze_bindings(); start_checks();
    call_record *first = ordinary_call(&request);
    ac_snapshot expected = expected_completion(false, false);
    unsigned first_callbacks = callback_count;
    if (!spy_control) mutate_after_return();
    call_record *second = ordinary_call(&repeat_request);
    normalize_callbacks(false);
    stable_checks();
    check("first-provider-attempt-has-expected-terminal-state", first &&
        first->returned == (incomplete ? AC_INCOMPLETE : AC_RETURNED) &&
        (incomplete ? empty_snapshot(&first->snapshot, AC_ACTIVE_INCOMPLETE) :
                      snapshot_equal(&first->snapshot, &expected)));
    if (!spy_control)
        check("post-return-caller-source-mutation-is-real", mutation.performed &&
              memcmp(mutation.id_before, owner_initial, AC_ID_BYTES) == 0 &&
              memcmp(mutation.id_after, mutated_id, AC_ID_BYTES) == 0 &&
              mutation.request_before.pc != mutation.request_after.pc &&
              mutation.request_before.lr != mutation.request_after.lr &&
              mutation.request_before.fp != mutation.request_after.fp &&
              mutation.request_before.providers == mutation.request_after.providers &&
              mutation.request_before.context == mutation.request_after.context);
    check("terminal-repeat-returns-suppressed", second && second->returned == AC_SUPPRESSED);
    check("terminal-repeat-preserves-public-snapshot", first && second &&
          snapshot_equal(&first->snapshot, &second->snapshot));
    check("suppressed-entry-does-not-call-provider", callback_count == first_callbacks &&
          context_invocations(3) == 0);
}
static void control_unblamed(void) {
    setup_noowner();
    freeze_bindings(); start_checks();
    call_record *c = ordinary_call(&request);
    normalize_callbacks(false);
    stable_checks();
    ac_snapshot expected = expected_completion(true, false);
    check("ordinary-provider-entry-returns-returned", c && c->returned == AC_RETURNED);
    check("public-completion-matches-authored-return-values", c && snapshot_equal(&c->snapshot, &expected));
    /* This assertion alone owns the absent callback/observation case. */
    check("unblamed-callback-is-invoked-once", callback_count == 1 &&
          sequence_matches(noowner_sequence, 1) && all_contexts_bound());
}
static void control_values(unsigned which) {
    setup_community(true);
    if (which == 6) contexts[0].close = (ac_io_value){-1, 93};
    if (which == 7) contexts[0].write = (ac_io_value){31, 88};
    if (which == 8) contexts[0].open = (ac_io_value){WIDE_TOKEN, 77};
    freeze_bindings(); start_checks();
    call_record *c = ordinary_call(&request);
    normalize_callbacks(which == 7);
    stable_checks();
    ac_snapshot expected = expected_completion(false, false);
    check("ordinary-provider-entry-returns-returned", c && c->returned == AC_RETURNED);
    if (which == 6) {
        ac_snapshot unaffected = {0};
        if (c) unaffected = c->snapshot;
        /* These fields have one owner: the close receipt assertion below. */
        unaffected.result.events[4] = expected.result.events[4];
        unaffected.result.known_failure = expected.result.known_failure;
        check("close-control-preserves-other-completion-fields", c && snapshot_equal(&unaffected, &expected));
        check("close-receipt-matches-callback-return", c &&
              event_equal(&c->snapshot.result.events[4], &expected.result.events[4]) &&
              c->snapshot.result.known_failure == expected.result.known_failure);
    } else {
        check("public-completion-matches-authored-return-values", c && snapshot_equal(&c->snapshot, &expected));
    }
    check("callback-sequence-and-prepared-values-match", sequence_matches(full_sequence, 4));
    check("callback-receives-bound-context", all_contexts_bound());
    check("callback-receives-copied-owner-and-length", callback_owner_and_lengths());
    check("write-callback-receives-recorded-payload", callback_payloads_match());
    check("accepted-open-token-reaches-followup-callbacks", callback_tokens_match());
    if (which == 7)
        check("short-write-dispatch-is-single-attempt", callback_kind_count(AC_WRITE) == 1);
}

static void json_bool(FILE *f, bool b) { fputs(b ? "true" : "false", f); }
static void json_hex(FILE *f, const char bytes[AC_ID_BYTES]) {
    fputc('"', f);
    for (unsigned i = 0; i < AC_ID_BYTES; ++i) fprintf(f, "%02x", (unsigned)(unsigned char)bytes[i]);
    fputc('"', f);
}
static void json_io(FILE *f, ac_io_value v) {
    fprintf(f, "{\"raw\":%" PRId64 ",\"failure_errno\":%d}", v.raw, v.failure_errno);
}
static void json_request(FILE *f, const ac_request *r) {
    fprintf(f, "{\"pc\":%" PRIuPTR ",\"lr\":%" PRIuPTR ",\"fp\":%" PRIuPTR
               ",\"table_id\":%u,\"context_id\":%u}", r->pc, r->lr, r->fp,
            table_id(r->providers), context_id(r->context));
}
static void json_owner(FILE *f, const ac_owner *o) {
    fprintf(f, "{\"kind\":%u,\"index\":%" PRIu32 ",\"process_id\":%" PRIu64 ",\"id_hex\":",
            (unsigned)o->kind, o->index, o->process_id);
    json_hex(f, o->id); fputc('}', f);
}
static void json_event(FILE *f, const ac_event *e) {
    fprintf(f, "{\"kind\":%u,\"entered\":", (unsigned)e->kind); json_bool(f, e->entered);
    fputs(",\"returned_recorded\":", f); json_bool(f, e->returned_recorded);
    fprintf(f, ",\"raw\":%" PRId64 ",\"errno_valid\":", e->raw); json_bool(f, e->errno_valid);
    fprintf(f, ",\"error_number\":%d,\"requested_length\":%zu,\"payload_hex\":",
            e->error_number, e->requested_length);
    json_hex(f, e->payload); fputc('}', f);
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
static void json_table(FILE *f, const ap_provider_table *t) {
    fputs("{\"open\":", f); json_bool(f, t->open != NULL);
    fputs(",\"write\":", f); json_bool(f, t->write != NULL);
    fputs(",\"sync\":", f); json_bool(f, t->sync != NULL);
    fputs(",\"close\":", f); json_bool(f, t->close != NULL);
    fputs(",\"unblamed\":", f); json_bool(f, t->unblamed != NULL);
    fputc('}', f);
}
static void json_configuration(FILE *f, const provider_context *c) {
    fprintf(f, "{\"id\":%u,\"open\":", c->id); json_io(f, c->open);
    fputs(",\"write\":", f); json_io(f, c->write);
    fputs(",\"sync\":", f); json_io(f, c->sync);
    fputs(",\"close\":", f); json_io(f, c->close);
    fputs(",\"observe\":", f); json_bool(f, c->observe);
    fprintf(f, ",\"nested_kind\":%d,\"nested_null\":", c->nested_kind); json_bool(f, c->nested_null);
    fputs(",\"nested_request_present\":", f); json_bool(f, c->nested_request != NULL);
    fputc('}', f);
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
    fputs(",\"source_scope\":{\"providers\":\"authored-c-callbacks-only\","
          "\"native_fault_provider_functions_present\":false,\"signals\":false,\"threads\":false,"
          "\"evidence_io\":\"stdio-report-only\","
          "\"attempt_observation\":\"public-entry-return-and-callback-boundaries\","
          "\"callback_return_evidence\":\"prepared-before-C-return-not-private-raw-telemetry\"},"
          "\"caller_id_before_hex\":", f);
    json_hex(f, owner_initial); fputs(",\"caller_id_after_hex\":", f); json_hex(f, owner_source);
    fputs(",\"expected_owner\":", f); json_owner(f, &expected_owner);
    fputs(",\"initial_snapshot\":", f); json_snapshot(f, &initial_snapshot);
    fputs(",\"setup\":[", f);
    for (unsigned i = 0; i < setup_count; ++i) {
        const setup_record *s = &setup_records[i];
        if (i) fputc(',', f);
        fprintf(f, "{\"operation\":\"%s\",\"returned\":", s->operation); json_bool(f, s->returned);
        fprintf(f, ",\"index\":%u,\"kind\":%u,\"process_id\":%" PRIu64 ",\"lo\":%" PRIuPTR
                   ",\"hi\":%" PRIuPTR ",\"island_lo\":%" PRIuPTR ",\"island_hi\":%" PRIuPTR
                   ",\"trampoline_lo\":%" PRIuPTR ",\"trampoline_hi\":%" PRIuPTR ",\"id_hex\":",
                s->index, (unsigned)s->kind, s->process_id, s->lo, s->hi,
                s->island_lo, s->island_hi, s->trampoline_lo, s->trampoline_hi);
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
    fputs("],\"callbacks\":[", f);
    for (unsigned i = 0; i < callback_count; ++i) {
        const callback_record *r = &callbacks[i];
        if (i) fputc(',', f);
        fprintf(f, "{\"context_id\":%u,\"kind\":%u,\"owner_arg\":", r->context_id, (unsigned)r->kind);
        json_owner(f, &r->owner_arg);
        fprintf(f, ",\"write_arg\":{\"accepted_open_token\":%" PRId64 ",\"requested_length\":%zu,\"payload_hex\":",
                r->write_arg.accepted_open_token, r->write_arg.requested_length);
        json_hex(f, r->write_arg.payload);
        fprintf(f, "},\"token_arg\":%" PRId64 ",\"entry_observed\":", r->token_arg);
        json_bool(f, r->entry_observed);
        fputs(",\"exit_observed\":", f); json_bool(f, r->exit_observed);
        fputs(",\"entry_snapshot\":", f); json_snapshot(f, &r->entry_snapshot);
        fputs(",\"exit_snapshot\":", f); json_snapshot(f, &r->exit_snapshot);
        fputs(",\"has_prepared_return\":", f); json_bool(f, r->has_prepared_return);
        fputs(",\"prepared_void_return\":", f); json_bool(f, r->prepared_void_return);
        fputs(",\"prepared_return\":", f); json_io(f, r->prepared_return);
        fputs(",\"nested_attempted\":", f); json_bool(f, r->nested_attempted);
        fputs(",\"nested_null\":", f); json_bool(f, r->nested_null);
        fprintf(f, ",\"nested_call_index\":%u,\"nested_return\":%u,\"nested_before\":",
                r->nested_call_index, (unsigned)r->nested_return);
        json_snapshot(f, &r->nested_before);
        fputs(",\"nested_after\":", f); json_snapshot(f, &r->nested_after); fputc('}', f);
    }
    fputs("],\"contexts\":[", f);
    for (unsigned i = 0; i < CONTEXTS; ++i) {
        if (i) fputc(',', f);
        fputs("{\"before\":", f); json_configuration(f, &configurations_before[i]);
        fputs(",\"after\":", f); json_configuration(f, &contexts[i]);
        fputs(",\"table_before\":", f); json_table(f, &tables_before[i]);
        fputs(",\"table_after\":", f); json_table(f, &tables[i]);
        fputs(",\"nested_started\":", f); json_bool(f, contexts[i].nested_started);
        fputs(",\"invocations\":[", f);
        for (unsigned j = 0; j < AC_EVENT_CAPACITY; ++j) {
            if (j) fputc(',', f);
            fprintf(f, "%u", contexts[i].invocations[j]);
        }
        fputs("]}", f);
    }
    fputs("],\"mutation\":{\"performed\":", f); json_bool(f, mutation.performed);
    fputs(",\"id_before_hex\":", f); json_hex(f, mutation.id_before);
    fputs(",\"id_after_hex\":", f); json_hex(f, mutation.id_after);
    fputs(",\"request_before\":", f); json_request(f, &mutation.request_before);
    fputs(",\"request_after\":", f); json_request(f, &mutation.request_after);
    fputs("},\"normalization\":{\"valid\":", f); json_bool(f, normalization_valid);
    fputs(",\"indices\":[", f);
    for (unsigned i = 0; i < normalized_count; ++i) {
        if (i) fputc(',', f);
        fprintf(f, "%u", normalized_callbacks[i]);
    }
    fputs("]}}\n", f);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok;
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    selector_name = argv[1];
    ac_observe(&initial_snapshot);
    initialize_providers();
    if (!strcmp(selector_name, "provider-generated-fd-zero")) {
        setup_community(true); complete_case(false, false);
    } else if (!strcmp(selector_name, "provider-wide-open-token")) {
        setup_community(true); contexts[0].open = (ac_io_value){WIDE_TOKEN, 77};
        complete_case(false, false);
    } else if (!strcmp(selector_name, "provider-no-owner")) {
        setup_noowner(); complete_case(true, false);
    } else if (!strcmp(selector_name, "provider-open-refusal")) {
        setup_community(false); contexts[0].open = (ac_io_value){-1, 71};
        tables[0].write = NULL; tables[0].sync = NULL; tables[0].close = NULL;
        complete_case(false, true);
    } else if (!strcmp(selector_name, "provider-short-sync-error")) {
        setup_community(false); contexts[0].write = (ac_io_value){31, 88};
        contexts[0].sync = (ac_io_value){-1, 72}; complete_case(false, false);
    } else if (!strcmp(selector_name, "provider-write-error")) {
        setup_community(false); contexts[0].write = (ac_io_value){-1, 73};
        complete_case(false, false);
    } else if (!strcmp(selector_name, "provider-invalid-write")) incomplete_case(false);
    else if (!strcmp(selector_name, "provider-missing-write")) incomplete_case(true);
    else if (!strcmp(selector_name, "nested-unblamed")) nested_case(AC_UNBLAMED, false);
    else if (!strcmp(selector_name, "nested-open")) nested_case(AC_OPEN, false);
    else if (!strcmp(selector_name, "nested-write")) nested_case(AC_WRITE, false);
    else if (!strcmp(selector_name, "nested-null")) nested_case(AC_OPEN, true);
    else if (!strcmp(selector_name, "repeat-after-returned")) repeat_case(false, false);
    else if (!strcmp(selector_name, "repeat-after-incomplete")) repeat_case(true, false);
    else if (!strcmp(selector_name, "control-suppressed-provider")) repeat_case(false, true);
    else if (!strcmp(selector_name, "control-nested-return")) nested_case(AC_OPEN, false);
    else if (!strcmp(selector_name, "control-unblamed-dispatch")) control_unblamed();
    else if (!strcmp(selector_name, "control-bound-context")) control_values(4);
    else if (!strcmp(selector_name, "control-write-payload")) control_values(5);
    else if (!strcmp(selector_name, "control-close-return")) control_values(6);
    else if (!strcmp(selector_name, "control-short-write-once")) control_values(7);
    else if (!strcmp(selector_name, "control-wide-token")) control_values(8);
    else return 2;
    check("report-bounds-respected", report_bounds && check_count < MAX_CHECKS &&
          setup_count <= MAX_SETUP && call_count > 0 && call_count <= MAX_CALLS &&
          callback_count <= MAX_CALLBACKS && normalized_count <= MAX_CALLBACKS);
    if (!evidence(argv[2])) return 4;
    common(stdout); fputs("}\n", stdout);
    if (ferror(stdout)) return 4;
    return failure_count ? 1 : 0;
}
