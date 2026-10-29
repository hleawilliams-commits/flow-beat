/* SPDX-License-Identifier: GPL-2.0-only */
#include "graph.h"
#include <stdlib.h>
#include <string.h>

struct mbog_graph {
    struct mbco_owner *claim;
    struct mbra_owner *adapter;
    struct mbro_owner *broker;
    struct mb_broker *broker_borrow;
    struct mbw_dispatch *dispatch;
    int original_fd; /* internal borrow from retained claim owner */
    struct mbog_evidence journal;
};

static int mbog_plan_valid(const struct mbog_plan *plan)
{
    const struct mb_state *pre = &plan->claim.expected_pre;
    unsigned session = 0;
    for (size_t i = 0; i < sizeof(plan->session); i++) session |= plan->session[i];
    if (!session || !plan->producer_id || !plan->claim.new_run ||
        !(pre->instance[0] || pre->instance[1]) ||
        pre->crc_len != 96 || pre->epoch == UINT64_MAX)
        return 0;
    if (pre->flags == 24u) return !pre->run_id && !pre->epoch && !pre->sequence;
    if (pre->flags == 25u) return pre->run_id && pre->epoch;
    return 0;
}

static void mbog_arm(struct mbog_graph *graph, enum mbog_stage stage)
{
    graph->journal.current_stage = stage;
    graph->journal.setup[stage].attempted = 1;
}

static int mbog_returned(struct mbog_graph *graph, enum mbog_stage stage, int result)
{
    graph->journal.setup[stage].result = result;
    graph->journal.setup[stage].returned = 1;
    if (result != 0) {
        graph->journal.phase = MBOG_TERMINAL;
        if (graph->journal.terminal_stage == MBOG_STAGE_NONE) {
            graph->journal.terminal_stage = stage;
            graph->journal.terminal_result = result;
        }
        return MBOG_CHILD;
    }
    return MBOG_OK;
}

int mbog_create(const struct mbog_plan *plan, struct mbog_graph **out)
{
    struct mbog_plan copy = {0};
    struct mbog_graph *graph;
    if (plan) copy = *plan;
    if (out) *out = NULL;
    if (!out || !plan || !mbog_plan_valid(&copy)) return MBOG_INVALID;
    graph = calloc(1, sizeof(*graph));
    if (!graph) return MBOG_NOMEM;
    graph->original_fd = -1;
    graph->journal.phase = MBOG_ALLOCATED;
    graph->journal.plan = copy;
    *out = graph;
    return MBOG_OK;
}

int mbog_take(struct mbog_graph *graph, int *fd)
{
    int result, original;
    if (!graph || !fd) return MBOG_INVALID;
    if (graph->journal.disposal_started) return MBOG_CLOSED;
    if (graph->journal.take_attempted || graph->journal.phase != MBOG_ALLOCATED)
        return MBOG_STATE;
    if (*fd < 0) return MBOG_INVALID;
    original = *fd;
    graph->journal.take_attempted = 1;
    graph->journal.phase = MBOG_TAKING;
    mbog_arm(graph, MBOG_STAGE_CLAIM_CREATE);
    result = mbco_create(&graph->journal.plan.claim, &graph->claim);
    if (mbog_returned(graph, MBOG_STAGE_CLAIM_CREATE, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_TAKE);
    result = mbco_take(graph->claim, fd);
    if (mbog_returned(graph, MBOG_STAGE_TAKE, result)) return MBOG_CHILD;
    graph->original_fd = original;
    graph->journal.phase = MBOG_RETAINED;
    return MBOG_OK;
}

int mbog_start(struct mbog_graph *graph)
{
    struct mb_backend backend = {0};
    int result;
    if (!graph) return MBOG_INVALID;
    if (graph->journal.disposal_started) return MBOG_CLOSED;
    if (graph->journal.start_attempted || graph->journal.phase != MBOG_RETAINED)
        return MBOG_STATE;
    graph->journal.start_attempted = 1;
    graph->journal.phase = MBOG_STARTING;

    mbog_arm(graph, MBOG_STAGE_PREFLIGHT);
    result = mbco_preflight(graph->claim);
    if (mbog_returned(graph, MBOG_STAGE_PREFLIGHT, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_CLAIM);
    result = mbco_claim(graph->claim);
    if (mbog_returned(graph, MBOG_STAGE_CLAIM, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_POSTFLIGHT);
    result = mbco_postflight(graph->claim);
    if (mbog_returned(graph, MBOG_STAGE_POSTFLIGHT, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_GET_POST);
    result = mbco_get_validated_post(graph->claim, &graph->journal.validated_post);
    if (mbog_returned(graph, MBOG_STAGE_GET_POST, result)) return MBOG_CHILD;
    graph->journal.validated_post_valid = 1;

    mbog_arm(graph, MBOG_STAGE_ADAPTER_CREATE);
    result = mbra_create(graph->original_fd, &graph->journal.validated_post,
                         &graph->adapter);
    if (mbog_returned(graph, MBOG_STAGE_ADAPTER_CREATE, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_DUPLICATE);
    result = mbra_duplicate(graph->adapter);
    if (mbog_returned(graph, MBOG_STAGE_DUPLICATE, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_ADAPTER_VALIDATE);
    result = mbra_validate(graph->adapter);
    if (mbog_returned(graph, MBOG_STAGE_ADAPTER_VALIDATE, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_GET_BACKEND);
    result = mbra_get_backend(graph->adapter, &backend);
    if (mbog_returned(graph, MBOG_STAGE_GET_BACKEND, result)) return MBOG_CHILD;

    mbog_arm(graph, MBOG_STAGE_BROKER_CREATE);
    result = mbro_create(&backend, &graph->journal.validated_post, &graph->broker);
    if (mbog_returned(graph, MBOG_STAGE_BROKER_CREATE, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_BROKER_INITIALIZE);
    result = mbro_initialize(graph->broker);
    if (mbog_returned(graph, MBOG_STAGE_BROKER_INITIALIZE, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_BROKER_VALIDATE);
    result = mbro_validate(graph->broker);
    if (mbog_returned(graph, MBOG_STAGE_BROKER_VALIDATE, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_GET_BROKER);
    result = mbro_get_broker(graph->broker, &graph->broker_borrow);
    if (mbog_returned(graph, MBOG_STAGE_GET_BROKER, result)) return MBOG_CHILD;
    mbog_arm(graph, MBOG_STAGE_DISPATCH_CREATE);
    result = mbw_dispatch_create(graph->broker_borrow, graph->journal.plan.session,
                                 &graph->journal.validated_post,
                                 graph->journal.plan.producer_id, &graph->dispatch);
    if (mbog_returned(graph, MBOG_STAGE_DISPATCH_CREATE, result)) return MBOG_CHILD;
    graph->journal.phase = MBOG_READY;
    return MBOG_OK;
}

int mbog_exchange(struct mbog_graph *graph, const uint8_t *request, size_t length,
                   uint8_t *response, size_t capacity, size_t *written)
{
    int result;
    if (written) *written = 0;
    if (!graph) return MBWD_INVALID;
    if (graph->journal.disposal_started || graph->journal.phase != MBOG_READY ||
        !graph->dispatch)
        return MBWD_CLOSED;
    graph->journal.current_stage = MBOG_STAGE_EXCHANGE;
    graph->journal.last_exchange = (struct mbog_call){1, 0, 0};
    result = mbw_dispatch_exchange(graph->dispatch, request, length,
                                   response, capacity, written);
    graph->journal.last_exchange.result = result;
    graph->journal.last_exchange.returned = 1;
    /* Dispatcher admission/cache semantics are independent of construction
     * readiness. In particular, a later exact cache retrieval is still allowed. */
    return result;
}

static int mbog_copy_children(const struct mbog_graph *graph, struct mbog_evidence *out)
{
    out->claim_present = graph->claim != NULL;
    out->adapter_present = graph->adapter != NULL;
    out->broker_present = graph->broker != NULL;
    out->dispatch_present = graph->dispatch != NULL;
    if (graph->claim) {
        if (mbco_inspect(graph->claim, &out->claim) != 0) return MBOG_CHILD;
        out->claim_evidence_valid = 1;
    }
    if (graph->adapter) {
        if (mbra_inspect(graph->adapter, &out->adapter) != 0) return MBOG_CHILD;
        out->adapter_evidence_valid = 1;
    }
    if (graph->broker) {
        int result = mbro_inspect(graph->broker, &out->broker);
        memset(&out->broker.backend, 0, sizeof(out->broker.backend));
        if (result != MB_OK) return MBOG_CHILD;
        out->broker_evidence_valid = 1;
    }
    /* Also scrub archived copies, so no retained adapter callback escapes. */
    memset(&out->broker.backend, 0, sizeof(out->broker.backend));
    if (graph->dispatch) {
        if (mbw_dispatch_inspect(graph->dispatch, &out->dispatch) != MBWD_OK)
            return MBOG_CHILD;
        out->dispatch_evidence_valid = 1;
    }
    return MBOG_OK;
}

int mbog_inspect(const struct mbog_graph *graph, struct mbog_evidence *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!graph || !out) return MBOG_INVALID;
    *out = graph->journal;
    return mbog_copy_children(graph, out);
}

static void mbog_record_disposal(struct mbog_graph *graph,
                                 const struct mbog_dispose_report *report)
{
    graph->journal.last_disposal = *report;
    graph->journal.last_disposal_valid = 1;
    if (report->attempt_number == 1) {
        graph->journal.first_disposal = *report;
        graph->journal.first_disposal_valid = 1;
    }
}

static int mbog_disposal_done(struct mbog_graph *graph,
                              struct mbog_dispose_report *report, int result)
{
    report->completed = 1;
    report->result = result;
    report->uncertain_close_seen = graph->journal.uncertain_close_seen;
    mbog_record_disposal(graph, report);
    return result;
}

static int mbog_empty(const struct mbog_graph *graph)
{
    return !graph->dispatch && !graph->broker && !graph->broker_borrow &&
        !graph->adapter && !graph->claim;
}

int mbog_dispose(struct mbog_graph **slot, struct mbog_dispose_report *report)
{
    struct mbog_graph *graph;
    int result;
    if (report) memset(report, 0, sizeof(*report));
    if (!slot || !report) {
        if (report) { report->completed = 1; report->result = MBOG_INVALID; }
        return MBOG_INVALID;
    }
    graph = *slot;
    if (!graph) { report->completed = 1; return MBOG_OK; }
    /* An empty uncertain shell needs explicit memory-only finalization.
     * Refusal does not replace any prior disposal/close evidence. */
    if (graph->journal.disposal_started && graph->journal.uncertain_close_seen &&
        mbog_empty(graph)) {
        report->completed = 1;
        report->result = MBOG_UNCERTAIN;
        report->uncertain_close_seen = 1;
        return MBOG_UNCERTAIN;
    }
    graph->journal.disposal_started = 1;
    graph->journal.phase = MBOG_DISPOSING;
    if (graph->journal.disposal_attempts == UINT64_MAX) {
        report->completed = 1;
        report->result = MBOG_OVERFLOW;
        report->uncertain_close_seen = graph->journal.uncertain_close_seen;
        return MBOG_OVERFLOW;
    }
    graph->journal.disposal_attempts++;
    report->attempt_number = graph->journal.disposal_attempts;
    mbog_record_disposal(graph, report);
    /* Capture all pure diagnostics before any child can be consumed. Current
     * presence is recomputed by inspect; archived held flags are historical. */
    if (mbog_copy_children(graph, &graph->journal) != MBOG_OK)
        return mbog_disposal_done(graph, report, MBOG_CHILD);
    if (graph->dispatch) {
        struct mbw_dispatch *dispatch = graph->dispatch;
        graph->journal.current_stage = MBOG_STAGE_DISPATCH_DESTROY;
        report->dispatch_attempted = 1;
        mbog_record_disposal(graph, report);
        graph->dispatch = NULL;
        mbw_dispatch_destroy(dispatch);
        report->dispatch_returned = 1;
        mbog_record_disposal(graph, report);
    }
    /* This private borrow is now permanently invalid, including when broker
     * destruction fails and leaves only some synchronization resources held. */
    graph->broker_borrow = NULL;
    if (graph->broker) {
        graph->journal.current_stage = MBOG_STAGE_BROKER_DISPOSE;
        report->broker_attempted = 1;
        mbog_record_disposal(graph, report);
        result = mbro_dispose(&graph->broker, &report->broker_report);
        report->broker_result = result;
        report->broker_returned = 1;
        mbog_record_disposal(graph, report);
        if (result != MB_OK || graph->broker)
            return mbog_disposal_done(graph, report, MBOG_CHILD);
    }
    if (graph->adapter) {
        if (graph->journal.adapter_close.attempted)
            return mbog_disposal_done(graph, report, MBOG_CHILD);
        /* The child sets its slot NULL before invoking close. Keeping this
         * record in the graph makes that unreturned consumed state inspectable. */
        graph->journal.current_stage = MBOG_STAGE_ADAPTER_DISPOSE;
        graph->journal.adapter_close.attempted = 1;
        report->adapter_close = graph->journal.adapter_close;
        mbog_record_disposal(graph, report);
        result = mbra_dispose(&graph->adapter, &graph->journal.adapter_close.report);
        graph->journal.adapter_close.result = result;
        graph->journal.adapter_close.returned = 1;
        report->adapter_close = graph->journal.adapter_close;
        mbog_record_disposal(graph, report);
        if (report->adapter_close.report.attempted &&
            report->adapter_close.report.call.result != 0) {
            graph->journal.uncertain_close_seen = 1;
            return mbog_disposal_done(graph, report, MBOG_UNCERTAIN);
        }
        if (result != 0 || graph->adapter)
            return mbog_disposal_done(graph, report, MBOG_CHILD);
    }
    if (graph->claim) {
        if (graph->journal.claim_close.attempted)
            return mbog_disposal_done(graph, report, MBOG_CHILD);
        graph->journal.current_stage = MBOG_STAGE_CLAIM_DISPOSE;
        graph->journal.claim_close.attempted = 1;
        report->claim_close = graph->journal.claim_close;
        mbog_record_disposal(graph, report);
        result = mbco_dispose(&graph->claim, &graph->journal.claim_close.report);
        graph->journal.claim_close.result = result;
        graph->journal.claim_close.returned = 1;
        report->claim_close = graph->journal.claim_close;
        mbog_record_disposal(graph, report);
        if (report->claim_close.report.attempted &&
            report->claim_close.report.call.result != 0) {
            graph->journal.uncertain_close_seen = 1;
            return mbog_disposal_done(graph, report, MBOG_UNCERTAIN);
        }
        if (result != 0 || graph->claim)
            return mbog_disposal_done(graph, report, MBOG_CHILD);
    }
    if (graph->journal.uncertain_close_seen)
        return mbog_disposal_done(graph, report, MBOG_UNCERTAIN);
    report->graph_released = 1;
    mbog_disposal_done(graph, report, MBOG_OK);
    *slot = NULL;
    free(graph);
    return MBOG_OK;
}

int mbog_finalize(struct mbog_graph **slot)
{
    struct mbog_graph *graph;
    if (!slot) return MBOG_INVALID;
    graph = *slot;
    if (!graph) return MBOG_OK;
    if (!graph->journal.disposal_started || !mbog_empty(graph)) return MBOG_STATE;
    *slot = NULL;
    free(graph);
    return MBOG_OK;
}
