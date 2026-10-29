/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_OWNED_GRAPH_H
#define EMULATOR_OWNED_GRAPH_H
#include "claim.h"
#include "retained.h"
#include "broker_owner.h"
#include "guest_dispatch.h"

struct mbog_graph;
struct mbog_plan {
    struct mbco_plan claim;
    uint8_t session[16];
    uint64_t producer_id;
};
enum mbog_result {
    MBOG_OK = 0, MBOG_INVALID = -1, MBOG_NOMEM = -2, MBOG_STATE = -3,
    MBOG_CHILD = -4, MBOG_CLOSED = -5, MBOG_OVERFLOW = -6, MBOG_UNCERTAIN = -7
};
enum mbog_phase {
    MBOG_ALLOCATED = 0, MBOG_TAKING, MBOG_RETAINED, MBOG_STARTING,
    MBOG_READY, MBOG_TERMINAL, MBOG_DISPOSING
};
enum mbog_stage {
    MBOG_STAGE_NONE = 0,
    MBOG_STAGE_CLAIM_CREATE, MBOG_STAGE_TAKE,
    MBOG_STAGE_PREFLIGHT, MBOG_STAGE_CLAIM, MBOG_STAGE_POSTFLIGHT,
    MBOG_STAGE_GET_POST,
    MBOG_STAGE_ADAPTER_CREATE, MBOG_STAGE_DUPLICATE,
    MBOG_STAGE_ADAPTER_VALIDATE, MBOG_STAGE_GET_BACKEND,
    MBOG_STAGE_BROKER_CREATE, MBOG_STAGE_BROKER_INITIALIZE,
    MBOG_STAGE_BROKER_VALIDATE, MBOG_STAGE_GET_BROKER,
    MBOG_STAGE_DISPATCH_CREATE,
    MBOG_SETUP_STAGES,
    MBOG_STAGE_EXCHANGE = MBOG_SETUP_STAGES,
    MBOG_STAGE_DISPATCH_DESTROY, MBOG_STAGE_BROKER_DISPOSE,
    MBOG_STAGE_ADAPTER_DISPOSE, MBOG_STAGE_CLAIM_DISPOSE
};
struct mbog_call {
    int attempted, returned, result; /* raw child API result; stage names its API */
};
struct mbog_adapter_close {
    int attempted, returned, result; /* dispose API result, distinct from close */
    struct mba_close_report report;
};
struct mbog_claim_close {
    int attempted, returned, result;
    struct mbco_close_report report;
};
struct mbog_dispose_report {
    uint64_t attempt_number;
    int dispatch_attempted, dispatch_returned;
    int broker_attempted, broker_returned, broker_result;
    struct mbro_dispose_report broker_report;
    struct mbog_adapter_close adapter_close;
    struct mbog_claim_close claim_close;
    int completed, result, graph_released;
    int uncertain_close_seen;
};
struct mbog_evidence {
    enum mbog_phase phase;
    enum mbog_stage current_stage;
    struct mbog_plan plan;
    int take_attempted, start_attempted;
    struct mbog_call setup[MBOG_SETUP_STAGES];
    enum mbog_stage terminal_stage;
    int terminal_result; /* first failed child result, never later refusal */
    int validated_post_valid;
    struct mb_state validated_post;
    int claim_present, adapter_present, broker_present, dispatch_present;
    int claim_evidence_valid, adapter_evidence_valid;
    int broker_evidence_valid, dispatch_evidence_valid;
    /* If *_present is false these are archived, not current resource custody.
     * A consuming child dispose nulls its slot before the close returns. */
    struct mbco_evidence claim;
    struct mbra_evidence adapter;
    struct mbro_evidence broker; /* backend context and callbacks always NULL */
    struct mbw_diagnostic dispatch;
    struct mbog_call last_exchange;
    int disposal_started, uncertain_close_seen;
    uint64_t disposal_attempts;
    int first_disposal_valid, last_disposal_valid;
    struct mbog_dispose_report first_disposal, last_disposal;
    /* Once-only close records survive consumed children and later refusals. */
    struct mbog_adapter_close adapter_close;
    struct mbog_claim_close claim_close;
};

/* Inactive, externally serialized composition; no endpoint open or startup.
 * create allocates/copies only and validates the injected-only plan before fd
 * transfer. Preflags24/25 are accepted with the exact CLAIM preconditions;
 * idle16/17 are refused, with no injected FRAME added. Session must contain a
 * nonzero byte and producer_id must be nonzero. No native authority is proved.
 * On create error *out is NULL. Inputs and outputs must be disjoint. */
int mbog_create(const struct mbog_plan *plan, struct mbog_graph **out);
/* Caller retains graph before take. Validate args, then create/store a CLAIM
 * child before transferring the already-open original fd (including fd0).
 * A child error leaves graph retained; caller slot changes only if taken.
 * One take attempt; no retry/replacement of a failed child. */
int mbog_take(struct mbog_graph *graph, int *fd);
/* One construction attempt after take. Store every child before effectful
 * calls and retain each armed/returned result. Failure retains all prior
 * children, original/duplicate, synchronization objects and evidence.
 * Ready means dispatcher construction complete, not producer admission.
 * There are no raw broker, backend, dispatcher or child getters. */
int mbog_start(struct mbog_graph *graph);
/* Returns original MBWD_* codes, not MBOG_*: invalid graph is MBWD_INVALID,
 * non-Ready/disposal is MBWD_CLOSED. Forward exactly once. Construction Ready
 * persists across dispatch errors so the frozen dispatcher's exact historical
 * cache retrieval remains available until disposal. Decode response semantics;
 * MBWD_OK alone is not commit/native-delivery evidence. Buffers disjoint. */
int mbog_exchange(struct mbog_graph *graph, const uint8_t *request, size_t length,
                   uint8_t *response, size_t capacity, size_t *written);
/* Pure bounded copied diagnostics: only child pure inspectors, never mb_inspect,
 * mutex locks, backend calls or I/O. Same-thread synchronous callback inspection
 * is permitted; state-changing reentry/concurrency is not. No child mutation is
 * exposed. Copied broker backend pointers are deliberately scrubbed to NULL.
 * Fd diagnostics remain usable numbers; callers must not close, mutate or
 * duplicate through them. Alias exclusion is not enforced. Archived evidence
 * plus attempted/returned close records distinguish consumption from success. */
int mbog_inspect(const struct mbog_graph *graph, struct mbog_evidence *out);
/* Requires independent external quiescence and resolution/accounting for all
 * operation uncertainty, aliases and native ownership. No in-flight exchange,
 * callback, copied pointer use or future exchange/borrowed use may remain.
 * Inspection, renewed explicit disposal and finalization remain permitted.
 * Permanently close graph exchange, archive/destroy dispatcher, then dispose
 * broker. Any retained broker or broker error stops before either fd child.
 * After broker release, archive/dispose duplicate, then original. Each child
 * close is armed first and its raw result retained after child consumption.
 * Any uncertain duplicate close stops with original retained; a later explicit
 * call under renewed external accounting skips the consumed duplicate.
 * Close numbers are never retried. Any uncertain close latches retention of the
 * graph shell, even when later calls retire its remaining children.
 * Fully known-success disposal consumes graph and returns its final report.
 * Otherwise retain graph/evidence. No implicit RELEASE, FRAME, restore, retry
 * or authority inference. A repeated call is not permission for safe teardown.
 * uint64 attempt overflow refuses before any child call or journal replacement.
 * Capture needed diagnostics before successful free. NULL *graph is no attempt. */
int mbog_dispose(struct mbog_graph **graph, struct mbog_dispose_report *report);
/* Memory-only discard of a disposal-started shell once every child is absent,
 * after independent external accounting of retained uncertainty. No I/O and no
 * fd retry. Refuse while any child remains. NULL *graph is already finalized. */
int mbog_finalize(struct mbog_graph **graph);
#endif
