/* SPDX-License-Identifier: GPL-3.0-only */
/* New serial authored-value harness. It never calls the frozen consumer,
 * native fault providers, signals, threads, loader or installed hooks.
 * File output below retains evidence only, not modeled blame persistence. */
#include "core.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FA_FIXTURE
#error The executable harness requires the declared serial scheduling seam.
#endif

#define MAX_CHECKS 16u
#define MAX_OBSERVATIONS 160u
#define MAX_REQUESTS 96u
#define MAX_STEPS 40u

typedef struct {
    const char *label;
    fa_snapshot public_value;
    fa_private private_value;
} observation;
typedef struct {
    const char *actor;
    fa_state captured, before, after;
    fa_request_result result;
} request_record;

static fa_script script, decoy;
static fa_owner expected_owner;
static observation observations[MAX_OBSERVATIONS];
static request_record requests[MAX_REQUESTS];
static unsigned observation_count, request_count, steps;
static const char *check_names[MAX_CHECKS], *failed_checks[MAX_CHECKS];
static unsigned checks, failures;
static bool capacity_ok = true, schedule_ok = true;
static const char *selector;

static void check(const char *name, bool passed) {
    if (checks >= MAX_CHECKS || failures >= MAX_CHECKS) {
        capacity_ok = false;
        return;
    }
    check_names[checks++] = name;
    if (!passed) failed_checks[failures++] = name;
}

static void set_id(char out[FA_ID_BYTES], const char *value) {
    size_t n = strlen(value);
    if (n == 0 || n >= FA_ID_BYTES) {
        schedule_ok = false;
        return;
    }
    memset(out, 0, FA_ID_BYTES);
    memcpy(out, value, n);
}

static bool owner_equal(const fa_owner *a, const fa_owner *b) {
    return a->kind == b->kind && a->index == b->index &&
        a->process_id == b->process_id &&
        memcmp(a->id, b->id, FA_ID_BYTES) == 0;
}

static bool event_equal(const fa_event *a, const fa_event *b) {
    return a->kind == b->kind && a->entered == b->entered &&
        a->returned_recorded == b->returned_recorded && a->raw == b->raw &&
        a->errno_valid == b->errno_valid && a->error_number == b->error_number &&
        a->requested_length == b->requested_length &&
        memcmp(a->payload, b->payload, FA_ID_BYTES) == 0;
}

static bool result_equal(const fa_result *a, const fa_result *b) {
    if (a->selection_known != b->selection_known ||
        !owner_equal(&a->owner, &b->owner) ||
        a->known_failure != b->known_failure || a->short_write != b->short_write ||
        a->violation != b->violation || a->event_count != b->event_count) return false;
    for (unsigned i = 0; i < FA_EVENT_CAPACITY; ++i)
        if (!event_equal(&a->events[i], &b->events[i])) return false;
    return true;
}

static bool private_equal(const fa_private *a, const fa_private *b) {
    return a->phase == b->phase && a->next_kind == b->next_kind &&
        a->current_event == b->current_event && result_equal(&a->working, &b->working) &&
        a->raw_oracle.available == b->raw_oracle.available &&
        a->raw_oracle.sequence == b->raw_oracle.sequence &&
        a->raw_oracle.kind == b->raw_oracle.kind &&
        owner_equal(&a->raw_oracle.selection, &b->raw_oracle.selection) &&
        a->raw_oracle.io.raw == b->raw_oracle.io.raw &&
        a->raw_oracle.io.failure_errno == b->raw_oracle.io.failure_errno;
}

static bool no_payload(const fa_snapshot *s) {
    const fa_result empty = {0};
    return !s->has_payload && result_equal(&s->result, &empty);
}

static void observe(const char *label) {
    if (observation_count >= MAX_OBSERVATIONS) {
        capacity_ok = false;
        return;
    }
    observation *o = &observations[observation_count++];
    o->label = label;
    fa_observe(&o->public_value);
    fa_fixture_inspect(&o->private_value);
}

static fa_request_result request(const char *actor, const fa_script *s, fa_state captured) {
    fa_snapshot before, after;
    fa_observe(&before);
    fa_request_result answer = fa_fixture_begin(s, captured);
    fa_observe(&after);
    if (request_count < MAX_REQUESTS) {
        requests[request_count++] = (request_record){actor, captured,
            before.state, after.state, answer};
    } else capacity_ok = false;
    observe("after-request");
    return answer;
}

static bool step(void) {
    if (steps >= MAX_STEPS) {
        schedule_ok = false;
        return false;
    }
    ++steps;
    if (!fa_fixture_step()) {
        schedule_ok = false;
        observe("step-refused");
        return false;
    }
    observe("after-step");
    return true;
}

static bool begin(void) {
    return request("winner", &script, FA_VIRGIN) == FA_ADMITTED;
}

static bool finish(void) {
    for (unsigned i = 0; i <= MAX_STEPS; ++i) {
        fa_snapshot s;
        fa_observe(&s);
        if (s.state == FA_ORDINARY_RETURNED) return s.has_payload;
        if (!step()) return false;
    }
    schedule_ok = false;
    return false;
}

static bool until(fa_event_kind kind, fa_phase phase) {
    for (unsigned i = 0; i <= MAX_STEPS; ++i) {
        fa_private p;
        fa_fixture_inspect(&p);
        if (p.next_kind == kind && p.phase == phase) return true;
        if (p.phase == FA_PUBLISHED || p.phase == FA_HALTED || !step()) return false;
    }
    schedule_ok = false;
    return false;
}

static bool before_publication(void) {
    for (unsigned i = 0; i <= MAX_STEPS; ++i) {
        fa_private p;
        fa_fixture_inspect(&p);
        if (p.phase == FA_READY_COMPLETE) return true;
        if (p.phase == FA_PUBLISHED || p.phase == FA_HALTED || !step()) return false;
    }
    schedule_ok = false;
    return false;
}

static bool suppress_preserves(void) {
    fa_private before, after;
    fa_snapshot s;
    fa_fixture_inspect(&before);
    fa_observe(&s);
    fa_request_result result = request("later", &decoy, s.state);
    fa_fixture_inspect(&after);
    return result == FA_SUPPRESSED && private_equal(&before, &after);
}

static fa_snapshot snapshot(void) {
    fa_snapshot s;
    fa_observe(&s);
    return s;
}

static const fa_event *event_of(const fa_result *r, fa_event_kind kind) {
    for (unsigned i = 0; i < r->event_count && i < FA_EVENT_CAPACITY; ++i)
        if (r->events[i].kind == kind) return &r->events[i];
    return NULL;
}

static unsigned count_kind(const fa_result *r, fa_event_kind kind) {
    unsigned n = 0;
    for (unsigned i = 0; i < r->event_count && i < FA_EVENT_CAPACITY; ++i)
        if (r->events[i].kind == kind) ++n;
    return n;
}

static bool ordinary(const fa_snapshot *s) {
    return s->state == FA_ORDINARY_RETURNED && s->has_payload &&
        s->result.selection_known && !s->result.violation;
}

static bool sequence(const fa_result *r, bool unblamed, bool open_failed) {
    const fa_event_kind community[] = {FA_SELECTION, FA_OPEN, FA_WRITE, FA_FSYNC, FA_CLOSE};
    const fa_event_kind none[] = {FA_SELECTION, FA_UNBLAMED};
    const fa_event_kind *want = unblamed ? none : community;
    unsigned n = unblamed || open_failed ? 2u : 5u;
    if (r->event_count != n) return false;
    for (unsigned i = 0; i < n; ++i)
        if (r->events[i].kind != want[i] || !r->events[i].entered ||
            !r->events[i].returned_recorded) return false;
    return true;
}

static bool write_bytes(const fa_result *r) {
    const fa_event *e = event_of(r, FA_WRITE);
    return e && e->requested_length == strlen(expected_owner.id) &&
        memcmp(e->payload, expected_owner.id, FA_ID_BYTES) == 0;
}

static bool errno_matches(const fa_event *e, fa_io_value value) {
    return e && e->raw == value.raw && e->errno_valid == (value.raw == -1) &&
        e->error_number == (value.raw == -1 ? value.failure_errno : 0);
}

static void setup(void) {
    memset(&script, 0, sizeof(script));
    script.selection.kind = FA_COMMUNITY;
    script.selection.index = 2;
    script.selection.process_id = 41;
    set_id(script.selection.id, "owner");
    script.open = (fa_io_value){7, 91};
    script.write = (fa_io_value){5, 92};
    script.sync = (fa_io_value){0, 93};
    script.close = (fa_io_value){0, 94};
    expected_owner = script.selection;
    decoy = script;
    decoy.selection.index = 9;
    decoy.selection.process_id = 99;
    set_id(decoy.selection.id, "other");
    observe("initial");
}

static void no_community_script(void) {
    memset(&script.selection, 0, sizeof(script.selection));
    expected_owner = script.selection;
}

static void admission_case(bool reverse, bool controlled) {
    fa_snapshot first = snapshot(), second = snapshot();
    const char *a = reverse ? "B" : "A", *b = reverse ? "A" : "B";
    const fa_script *winner_input = reverse ? &decoy : &script;
    const fa_script *loser_input = reverse ? &script : &decoy;
    expected_owner = winner_input->selection;
    fa_request_result winner = request(a, winner_input, first.state);
    fa_private before, after;
    fa_fixture_inspect(&before);
    fa_request_result loser = request(b, loser_input, second.state);
    fa_fixture_inspect(&after);
    bool unique = winner == FA_ADMITTED && loser == FA_SUPPRESSED;
    if (controlled) {
        check("terminal-admission-single-winner", unique);
        return;
    }
    bool unchanged = private_equal(&before, &after);
    bool no_events = before.working.event_count == 0 && after.working.event_count == 0;
    bool completed = finish();
    fa_snapshot s = snapshot();
    check("single-admission-authority", unique);
    check("loser-preserves-private-state", unchanged && completed &&
          owner_equal(&s.result.owner, &expected_owner));
    check("admission-precedes-providers", no_events);
}

static void precas_case(bool complete_first) {
    fa_state captured = snapshot().state;
    fa_request_result nested = request("nested-before-CAS", &script, captured);
    bool progressed = !complete_first || finish();
    fa_private before, after;
    fa_fixture_inspect(&before);
    fa_request_result outer = request("resumed-outer", &decoy, captured);
    fa_fixture_inspect(&after);
    check("nested-precas-request-wins", nested == FA_ADMITTED && progressed);
    check("outer-precas-request-suppressed", outer == FA_SUPPRESSED);
    check("precas-winner-state-preserved", private_equal(&before, &after) &&
          snapshot().state == (complete_first ? FA_ORDINARY_RETURNED : FA_ACTIVE_INCOMPLETE));
}

static void nested_case(bool unblamed) {
    if (unblamed) no_community_script();
    bool admitted = begin(), all_suppressed = admitted, all_preserved = true;
    unsigned visited[FA_EVENT_CAPACITY][4] = {{0}};
    bool saw_ready_complete = false, saw_published = false;
    for (unsigned i = 0; i <= MAX_STEPS; ++i) {
        fa_private before, after;
        fa_fixture_inspect(&before);
        if ((unsigned)before.next_kind < FA_EVENT_CAPACITY && before.phase <= FA_RECORDED)
            ++visited[(unsigned)before.next_kind][(unsigned)before.phase];
        saw_ready_complete |= before.phase == FA_READY_COMPLETE;
        saw_published |= before.phase == FA_PUBLISHED;
        fa_state state = snapshot().state;
        fa_request_result answer = request("nested-boundary", &decoy, state);
        fa_fixture_inspect(&after);
        all_suppressed &= answer == FA_SUPPRESSED;
        all_preserved &= private_equal(&before, &after);
        if (state == FA_ORDINARY_RETURNED) break;
        if (!step()) break;
    }
    const fa_event_kind kinds[] = {FA_SELECTION, FA_OPEN, FA_WRITE, FA_FSYNC, FA_CLOSE};
    bool covered = true;
    for (unsigned p = 0; p < 4; ++p) {
        if (unblamed) covered &= visited[FA_SELECTION][p] == 1 && visited[FA_UNBLAMED][p] == 1;
        else for (unsigned k = 0; k < 5; ++k) covered &= visited[kinds[k]][p] == 1;
    }
    fa_snapshot s = snapshot();
    check("all-post-admission-boundaries-suppress", all_suppressed && covered &&
          saw_ready_complete && saw_published);
    check("nested-requests-preserve-private-state", all_preserved);
    check("nested-branch-completes-once", ordinary(&s) &&
          sequence(&s.result, unblamed, false));
}

static void copy_case(bool long_id, bool controlled) {
    set_id(script.selection.id, long_id ? "abcdefghijklmnopqrstuvwx12345678" : "a");
    script.write.raw = (int64_t)strlen(script.selection.id);
    expected_owner = script.selection;
    bool reached = begin() && until(FA_SELECTION, FA_RECORDED);
    set_id(script.selection.id, long_id ? "ABCDEFGHIJKLMNOPQRSTUVWX87654321" : "b");
    if (!controlled) {
        script.selection.index = 12;
        script.selection.process_id = 700;
    }
    bool done = finish();
    fa_snapshot s = snapshot();
    bool copied = reached && done && owner_equal(&s.result.owner, &expected_owner) &&
        write_bytes(&s.result);
    if (controlled) {
        check("selected-owner-copy-survives-input-change", copied);
        return;
    }
    bool canonical = s.result.owner.id[strlen(expected_owner.id)] == 0;
    for (size_t i = strlen(expected_owner.id); i < FA_ID_BYTES; ++i)
        canonical &= s.result.owner.id[i] == 0;
    check("owner-copy-survives-input-change", copied);
    check("owner-id-is-canonical", canonical);
    check("copy-case-completes-once", ordinary(&s) && sequence(&s.result, false, false) &&
          suppress_preserves());
}

static void no_community_case(void) {
    no_community_script();
    bool done = begin() && finish();
    fa_snapshot s = snapshot();
    check("no-community-is-not-fabricated-owner", done && ordinary(&s) &&
          owner_equal(&s.result.owner, &expected_owner));
    check("one-opaque-unblamed-return", sequence(&s.result, true, false) &&
          count_kind(&s.result, FA_UNBLAMED) == 1);
    check("unblamed-completion-remains-terminal", suppress_preserves());
}

static void open_case(int mode) {
    script.open.raw = mode == 0 ? 0 : mode == 1 ? 7 : -1;
    bool done = begin() && finish();
    fa_snapshot s = snapshot();
    const fa_event *op = event_of(&s.result, FA_OPEN);
    if (mode == 2) {
        check("failed-open-is-known-ordinary-return", done && ordinary(&s) &&
              s.result.known_failure && !s.result.short_write && errno_matches(op, script.open));
        check("failed-open-skips-later-operations", sequence(&s.result, false, true));
        check("failed-open-retains-owner", owner_equal(&s.result.owner, &expected_owner) &&
              suppress_preserves());
    } else {
        check(mode == 0 ? "open-zero-is-valid" : "positive-open-fd-retained",
              done && ordinary(&s) && op && op->raw == script.open.raw);
        check("successful-errno-is-invalid", errno_matches(op, script.open));
        check("ordinary-owner-and-sequence-retained", sequence(&s.result, false, false) &&
              owner_equal(&s.result.owner, &expected_owner) && write_bytes(&s.result) &&
              !s.result.known_failure && !s.result.short_write && suppress_preserves());
    }
}

static void write_case(int mode) {
    script.write.raw = mode == 0 ? 0 : mode == 1 ? 2 : -1;
    bool done = begin() && finish();
    fa_snapshot s = snapshot();
    const fa_event *wr = event_of(&s.result, FA_WRITE);
    const char *name = mode == 0 ? "zero-write-is-short-known-return" :
        mode == 1 ? "positive-short-write-is-known-return" : "failed-write-is-known-return";
    check(name, done && ordinary(&s) && errno_matches(wr, script.write) &&
          s.result.short_write == (mode != 2) && s.result.known_failure == (mode == 2));
    check("returned-write-still-syncs-and-closes", sequence(&s.result, false, false) &&
          write_bytes(&s.result));
    check("write-outcome-retains-owner", owner_equal(&s.result.owner, &expected_owner) &&
          suppress_preserves());
}

static void sync_close_case(int mode) {
    if (mode != 1) script.sync = (fa_io_value){-1, 81};
    if (mode != 0) script.close = (fa_io_value){-1, 82};
    if (mode == 2) script.write = (fa_io_value){2, 83};
    bool done = begin() && finish();
    fa_snapshot s = snapshot();
    const fa_event *sy = event_of(&s.result, FA_FSYNC), *cl = event_of(&s.result, FA_CLOSE);
    if (mode == 0) {
        check("sync-failure-is-independent", done && ordinary(&s) &&
              s.result.known_failure && !s.result.short_write && errno_matches(sy, script.sync));
        check("close-after-sync-failure-recorded", errno_matches(cl, script.close));
    } else if (mode == 1) {
        check("close-failure-is-independent", done && ordinary(&s) &&
              s.result.known_failure && !s.result.short_write && errno_matches(cl, script.close));
        check("close-error-grants-no-retry", suppress_preserves());
    } else {
        check("short-write-and-failure-flags-coexist", done && ordinary(&s) &&
              s.result.known_failure && s.result.short_write);
        check("each-return-retains-own-errno", errno_matches(event_of(&s.result, FA_WRITE), script.write) &&
              errno_matches(sy, script.sync) && errno_matches(cl, script.close));
    }
    check("sync-close-outcome-retains-owner", owner_equal(&s.result.owner, &expected_owner) &&
          sequence(&s.result, false, false) && suppress_preserves());
}

static void completion_case(void) {
    fa_snapshot initial = snapshot();
    bool admitted = begin(), hidden = true;
    while (snapshot().state != FA_ORDINARY_RETURNED && steps < MAX_STEPS) {
        fa_snapshot active = snapshot();
        hidden &= active.state == FA_ACTIVE_INCOMPLETE && no_payload(&active);
        if (!step()) break;
    }
    fa_snapshot a = snapshot(), b = snapshot();
    check("virgin-observation-hides-payload", initial.state == FA_VIRGIN && no_payload(&initial));
    check("incomplete-observation-hides-payload", admitted && hidden);
    check("completion-publishes-immutable-payload", ordinary(&a) && ordinary(&b) &&
          result_equal(&a.result, &b.result) && sequence(&a.result, false, false));
    check("returned-attempt-remains-terminal", suppress_preserves());
}

static bool stop_selector(const char *name, fa_event_kind *kind, fa_phase *phase) {
    const char *stage_names[] = {"selection", "unblamed", "open", "write", "sync", "close"};
    const char *cut_names[] = {"before", "entered", "raw", "recorded"};
    char expected[64];
    for (unsigned k = 0; k < 6; ++k) for (unsigned p = 0; p < 4; ++p) {
        int n = snprintf(expected, sizeof(expected), "stop-%s-%s", stage_names[k], cut_names[p]);
        if (n > 0 && (size_t)n < sizeof(expected) && strcmp(name, expected) == 0) {
            *kind = (fa_event_kind)k;
            *phase = (fa_phase)p;
            return true;
        }
    }
    return false;
}

static void stopped_case(fa_event_kind kind, fa_phase phase) {
    if (kind == FA_UNBLAMED) no_community_script();
    bool reached = begin() && until(kind, phase);
    fa_private p;
    fa_fixture_inspect(&p);
    fa_snapshot s = snapshot();
    bool distinct = false;
    if (phase == FA_READY_ENTER) {
        distinct = count_kind(&p.working, kind) == 0;
    } else if (p.current_event < p.working.event_count && p.current_event < FA_EVENT_CAPACITY) {
        const fa_event *e = &p.working.events[p.current_event];
        distinct = e->kind == kind && e->entered;
        if (phase == FA_ENTERED) distinct &= !e->returned_recorded && !p.raw_oracle.available;
        if (phase == FA_RAW_AVAILABLE) distinct &= !e->returned_recorded && p.raw_oracle.available &&
            p.raw_oracle.kind == kind;
        if (phase == FA_RECORDED) distinct &= e->returned_recorded;
    }
    check("stop-position-reached", reached && p.phase == phase && p.next_kind == kind);
    check("stopped-observation-hides-payload", s.state == FA_ACTIVE_INCOMPLETE && no_payload(&s));
    check("raw-return-and-receipt-are-distinct", distinct);
    check("stopped-attempt-remains-terminal", suppress_preserves());
}

static void prepublication_case(void) {
    bool reached = begin() && before_publication();
    fa_private p;
    fa_fixture_inspect(&p);
    fa_snapshot s = snapshot();
    check("all-returns-recorded-before-publication", reached && sequence(&p.working, false, false));
    check("unpublished-completion-hides-payload", s.state == FA_ACTIVE_INCOMPLETE && no_payload(&s));
    check("unpublished-completion-remains-terminal", suppress_preserves());
}

static bool control_case(const char *name) {
    if (strcmp(name, "control-admit-second-contender") == 0) {
        admission_case(false, true);
    } else if (strcmp(name, "control-reopen-after-unknown") == 0) {
        bool reached = begin() && until(FA_WRITE, FA_ENTERED);
        fa_request_result result = request("unknown-contender", &decoy, FA_ACTIVE_INCOMPLETE);
        check("unknown-attempt-stays-terminal", reached && result == FA_SUPPRESSED);
    } else if (strcmp(name, "control-omit-owner-id-copy") == 0) {
        copy_case(true, true);
    } else if (strcmp(name, "control-derive-owner-from-write-success") == 0) {
        script.write.raw = -1;
        bool done = begin() && finish();
        fa_snapshot s = snapshot();
        check("selected-owner-independent-of-io-outcome", done && ordinary(&s) &&
              owner_equal(&s.result.owner, &expected_owner));
    } else if (strcmp(name, "control-invent-unreturned-write-result") == 0) {
        bool reached = begin() && until(FA_WRITE, FA_ENTERED);
        fa_private p;
        fa_fixture_inspect(&p);
        const fa_event *e = event_of(&p.working, FA_WRITE);
        check("armed-write-without-return-remains-unknown", reached && e && e->entered &&
              !e->returned_recorded && !p.raw_oracle.available);
    } else if (strcmp(name, "control-retry-short-write") == 0) {
        script.write.raw = 2;
        bool done = begin() && finish();
        fa_snapshot s = snapshot();
        check("short-write-retains-single-write-sequence", done && ordinary(&s) &&
              sequence(&s.result, false, false) && count_kind(&s.result, FA_WRITE) == 1);
    } else if (strcmp(name, "control-skip-close-after-write-error") == 0) {
        script.write.raw = -1;
        bool done = begin() && finish();
        fa_snapshot s = snapshot();
        check("returned-write-error-still-fsyncs-and-closes", done && ordinary(&s) &&
              sequence(&s.result, false, false));
    } else if (strcmp(name, "control-retain-stale-errno-after-success") == 0) {
        script.open.raw = 0;
        bool done = begin() && finish();
        fa_snapshot s = snapshot();
        check("successful-return-has-no-valid-failure-errno", done && ordinary(&s) &&
              errno_matches(event_of(&s.result, FA_OPEN), script.open));
    } else return false;
    return true;
}

static bool run_selector(void) {
    fa_event_kind kind;
    fa_phase phase;
    if (strcmp(selector, "admission-ab") == 0) admission_case(false, false);
    else if (strcmp(selector, "admission-ba") == 0) admission_case(true, false);
    else if (strcmp(selector, "precas-active") == 0) precas_case(false);
    else if (strcmp(selector, "precas-returned") == 0) precas_case(true);
    else if (strcmp(selector, "nested-community") == 0) nested_case(false);
    else if (strcmp(selector, "nested-unblamed") == 0) nested_case(true);
    else if (strcmp(selector, "copy-one") == 0) copy_case(false, false);
    else if (strcmp(selector, "copy-thirtytwo") == 0) copy_case(true, false);
    else if (strcmp(selector, "no-community") == 0) no_community_case();
    else if (strcmp(selector, "open-zero") == 0) open_case(0);
    else if (strcmp(selector, "open-seven") == 0) open_case(1);
    else if (strcmp(selector, "open-failure") == 0) open_case(2);
    else if (strcmp(selector, "write-zero") == 0) write_case(0);
    else if (strcmp(selector, "write-short") == 0) write_case(1);
    else if (strcmp(selector, "write-failure") == 0) write_case(2);
    else if (strcmp(selector, "sync-failure") == 0) sync_close_case(0);
    else if (strcmp(selector, "close-failure") == 0) sync_close_case(1);
    else if (strcmp(selector, "combined-failures") == 0) sync_close_case(2);
    else if (strcmp(selector, "completion-observation") == 0) completion_case();
    else if (strcmp(selector, "stop-before-publication") == 0) prepublication_case();
    else if (stop_selector(selector, &kind, &phase)) stopped_case(kind, phase);
    else if (!control_case(selector)) return false;
    return true;
}

static void json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
        else if (*p < 32) fprintf(out, "\\u%04x", (unsigned)*p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

static void hex_id(FILE *out, const char value[FA_ID_BYTES]) {
    fputc('"', out);
    for (unsigned i = 0; i < FA_ID_BYTES; ++i) fprintf(out, "%02x", (unsigned)(unsigned char)value[i]);
    fputc('"', out);
}

static void json_owner(FILE *out, const fa_owner *o) {
    fprintf(out, "{\"kind\":%u,\"index\":%" PRIu32 ",\"process_id\":%" PRIu64 ",\"id_hex\":",
            (unsigned)o->kind, o->index, o->process_id);
    hex_id(out, o->id);
    fputc('}', out);
}

static void json_io(FILE *out, fa_io_value v) {
    fprintf(out, "{\"raw\":%" PRId64 ",\"failure_errno\":%d}", v.raw, v.failure_errno);
}

static void json_script(FILE *out, const fa_script *s) {
    fputs("{\"selection\":", out); json_owner(out, &s->selection);
    fputs(",\"open\":", out); json_io(out, s->open);
    fputs(",\"write\":", out); json_io(out, s->write);
    fputs(",\"sync\":", out); json_io(out, s->sync);
    fputs(",\"close\":", out); json_io(out, s->close);
    fputc('}', out);
}

static void json_result(FILE *out, const fa_result *r) {
    fprintf(out, "{\"selection_known\":%s,\"owner\":", r->selection_known ? "true" : "false");
    json_owner(out, &r->owner);
    fprintf(out, ",\"known_failure\":%s,\"short_write\":%s,\"violation\":%s,\"event_count\":%u,\"events\":[",
            r->known_failure ? "true" : "false", r->short_write ? "true" : "false",
            r->violation ? "true" : "false", r->event_count);
    for (unsigned i = 0; i < FA_EVENT_CAPACITY; ++i) {
        const fa_event *e = &r->events[i];
        fprintf(out, "%s{\"kind\":%u,\"entered\":%s,\"returned_recorded\":%s,\"raw\":%" PRId64
                ",\"errno_valid\":%s,\"error_number\":%d,\"requested_length\":%zu,\"payload_hex\":",
                i ? "," : "", (unsigned)e->kind, e->entered ? "true" : "false",
                e->returned_recorded ? "true" : "false", e->raw, e->errno_valid ? "true" : "false",
                e->error_number, e->requested_length);
        hex_id(out, e->payload);
        fputc('}', out);
    }
    fputs("]}", out);
}

static void json_snapshot(FILE *out, const fa_snapshot *s) {
    fprintf(out, "{\"state\":%u,\"has_payload\":%s,\"result\":", (unsigned)s->state,
            s->has_payload ? "true" : "false");
    json_result(out, &s->result);
    fputc('}', out);
}

static void json_private(FILE *out, const fa_private *p) {
    fprintf(out, "{\"phase\":%u,\"next_kind\":%u,\"current_event\":%u,\"working\":",
            (unsigned)p->phase, (unsigned)p->next_kind, p->current_event);
    json_result(out, &p->working);
    fprintf(out, ",\"raw_oracle\":{\"available\":%s,\"sequence\":%u,\"kind\":%u,\"selection\":",
            p->raw_oracle.available ? "true" : "false", p->raw_oracle.sequence,
            (unsigned)p->raw_oracle.kind);
    json_owner(out, &p->raw_oracle.selection);
    fputs(",\"io\":", out);
    json_io(out, p->raw_oracle.io);
    fputs("}}", out);
}

static void common(FILE *out) {
    fputs("\"schema\":1,\"selector\":", out);
    json_string(out, selector);
    fprintf(out, ",\"checks\":%u,\"check_names\":[", checks);
    for (unsigned i = 0; i < checks; ++i) {
        if (i) fputc(',', out);
        json_string(out, check_names[i]);
    }
    fprintf(out, "],\"failures\":%u,\"failed_checks\":[", failures);
    for (unsigned i = 0; i < failures; ++i) {
        if (i) fputc(',', out);
        json_string(out, failed_checks[i]);
    }
    fputc(']', out);
}

static void evidence(FILE *out) {
    fputc('{', out);
    common(out);
    fputs(",\"source_scope\":{\"providers\":\"authored-values-only\",\"native_fault_provider_functions_present\":false,"
          "\"signals\":false,\"threads\":false,\"evidence_io\":\"stdio-report-only\"},\"script\":", out);
    json_script(out, &script);
    fputs(",\"decoy_script\":", out); json_script(out, &decoy);
    fputs(",\"expected_owner\":", out); json_owner(out, &expected_owner);
    fprintf(out, ",\"steps\":%u,\"requests\":[", steps);
    for (unsigned i = 0; i < request_count; ++i) {
        const request_record *r = &requests[i];
        if (i) fputc(',', out);
        fputs("{\"actor\":", out); json_string(out, r->actor);
        fprintf(out, ",\"captured\":%u,\"before\":%u,\"after\":%u,\"result\":%u}",
                (unsigned)r->captured, (unsigned)r->before, (unsigned)r->after, (unsigned)r->result);
    }
    fputs("],\"observations\":[", out);
    for (unsigned i = 0; i < observation_count; ++i) {
        const observation *o = &observations[i];
        if (i) fputc(',', out);
        fputs("{\"label\":", out); json_string(out, o->label);
        fputs(",\"public\":", out); json_snapshot(out, &o->public_value);
        fputs(",\"private\":", out); json_private(out, &o->private_value);
        fputc('}', out);
    }
    fputs("]}\n", out);
}

int main(int argc, char **argv) {
    if (argc != 3) return 64;
    selector = argv[1];
    setup();
    if (!run_selector()) return 64;
    observe("final");
    fa_private p;
    fa_fixture_inspect(&p);
    check("schedule-bounded", schedule_ok && steps <= MAX_STEPS && !p.working.violation);
    check("report-capacity", capacity_ok && observation_count <= MAX_OBSERVATIONS &&
          request_count <= MAX_REQUESTS && checks < MAX_CHECKS);
    FILE *out = fopen(argv[2], "wx");
    if (!out) return 74;
    evidence(out);
    int bad = ferror(out);
    if (fclose(out) != 0) bad = 1;
    if (bad) return 74;
    fputc('{', stdout); common(stdout); fputs("}\n", stdout);
    if (fflush(stdout) != 0 || ferror(stdout)) return 74;
    return failures ? 1 : 0;
}
