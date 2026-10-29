/* SPDX-License-Identifier: GPL-2.0-only */
#include "stream.h"
#include <stdlib.h>
#include <string.h>

struct mbgs_owner {
    struct mbog_graph *graph;
    struct mbgs_evidence journal;
};

static int mbgs_terminal(struct mbgs_owner *owner, int reason)
{
    owner->journal.phase = MBGS_TERMINAL;
    if (!owner->journal.first_terminal) {
        owner->journal.first_terminal = reason;
        owner->journal.terminal_operation = owner->journal.current_operation;
    }
    return reason;
}

static int mbgs_capture_graph(struct mbgs_owner *owner)
{
    struct mbog_evidence copy = {0};
    if (!owner->graph) return MBGS_OK;
    if (mbog_inspect(owner->graph, &copy) != MBOG_OK) return MBGS_GRAPH;
    owner->journal.graph = copy;
    owner->journal.graph_evidence_valid = 1;
    return MBGS_OK;
}

int mbgs_create(struct mbgs_owner **out)
{
    struct mbgs_owner *owner;
    if (!out) return MBGS_INVALID;
    *out = NULL;
    owner = calloc(1, sizeof(*owner));
    if (!owner) return MBGS_NOMEM;
    owner->journal.phase = MBGS_ALLOCATED;
    owner->journal.stream_fd = -1;
    owner->journal.stream_close.descriptor = -1;
    *out = owner;
    return MBGS_OK;
}

int mbgs_take(struct mbgs_owner *owner, int *fd, struct mbog_graph **graph)
{
    struct mbog_evidence evidence = {0};
    if (!owner || !fd || !graph) return MBGS_INVALID;
    if (owner->journal.active) return MBGS_BUSY;
    if (owner->journal.disposal_started) return MBGS_CLOSED;
    if (owner->journal.taken || owner->journal.phase != MBGS_ALLOCATED)
        return MBGS_STATE;
    if (*fd < 0 || !*graph) return MBGS_INVALID;
    if (mbog_inspect(*graph, &evidence) != MBOG_OK) return MBGS_GRAPH;
    if (evidence.phase != MBOG_READY || evidence.disposal_started ||
        !evidence.dispatch_present || !evidence.dispatch_evidence_valid ||
        evidence.dispatch.phase != MBWD_RETAINED || evidence.dispatch.last_exchange ||
        evidence.last_exchange.attempted ||
        !evidence.claim_present || !evidence.claim_evidence_valid ||
        !evidence.claim.descriptor_held ||
        !evidence.adapter_present || !evidence.adapter_evidence_valid ||
        !evidence.adapter.descriptor_held)
        return MBGS_STATE;
    if (*fd == evidence.claim.retained_fd || *fd == evidence.adapter.retained_fd)
        return MBGS_INVALID;
    /* All fallible validation is complete. Retain both real resources before
     * consuming either caller slot; no allocation, I/O or callback intervenes. */
    owner->graph = *graph;
    owner->journal.stream_fd = *fd;
    owner->journal.descriptor_held = 1;
    owner->journal.graph = evidence;
    owner->journal.graph_evidence_valid = 1;
    owner->journal.taken = 1;
    owner->journal.current.number = 1;
    owner->journal.current.request_expected = MBW_HEADER_SIZE;
    owner->journal.phase = MBGS_HEADER;
    *graph = NULL;
    *fd = -1;
    return MBGS_OK;
}

static int mbgs_read_step(struct mbgs_owner *owner)
{
    struct mbgs_record *record = &owner->journal.current;
    struct mbgs_io_result result;
    size_t remaining;
    owner->journal.current_operation = MBGS_OP_READ;
    if (record->read_calls == UINT64_MAX)
        return mbgs_terminal(owner, MBGS_OVERFLOW);
    if (record->request_expected > sizeof(record->request) ||
        record->request_received >= record->request_expected)
        return mbgs_terminal(owner, MBGS_STATE);
    remaining = record->request_expected - record->request_received;
    record->read_calls++;
    record->last_read = (struct mbgs_io_call){0};
    record->last_read.attempted = 1;
    record->last_read.offset = record->request_received;
    record->last_read.requested = remaining;
    owner->journal.active = 1;
    result = mbgs_sys_read(owner->journal.stream_fd,
                           record->request + record->request_received, remaining);
    record->last_read.raw = result;
    record->last_read.returned = 1;
    owner->journal.active = 0;
    if (result.count == -1) return mbgs_terminal(owner, MBGS_IO);
    if (result.count < 0 || (size_t)result.count > remaining)
        return mbgs_terminal(owner, MBGS_COUNT);
    if (result.count == 0) return mbgs_terminal(owner, MBGS_EOF);
    record->request_received += (size_t)result.count;
    if (record->request_received != record->request_expected) return MBGS_PROGRESS;
    if (owner->journal.phase == MBGS_HEADER) {
        struct mbw_header header = {0};
        if (mbw_decode_header(record->request, MBW_HEADER_SIZE, &header) != MBW_OK ||
            !((header.kind == MBW_ADOPT_REQUEST && header.length == MBW_ADOPT_REQUEST_SIZE) ||
              (header.kind == MBW_FRAME_REQUEST && header.length == MBW_FRAME_REQUEST_SIZE)))
            return mbgs_terminal(owner, MBGS_PROTOCOL);
        record->header = header;
        record->header_valid = 1;
        record->request_expected = header.length;
        owner->journal.phase = MBGS_BODY;
    } else {
        owner->journal.phase = MBGS_DISPATCH;
    }
    return MBGS_PROGRESS;
}

static int mbgs_dispatch_step(struct mbgs_owner *owner)
{
    struct mbgs_record *record = &owner->journal.current;
    struct mbw_header response = {0};
    size_t written = 0, expected;
    uint32_t kind;
    int result;
    owner->journal.current_operation = MBGS_OP_DISPATCH;
    if (record->dispatch.attempted || !record->header_valid ||
        record->request_received != record->request_expected)
        return mbgs_terminal(owner, MBGS_STATE);
    record->dispatch.attempted = 1;
    owner->journal.active = 1;
    result = mbog_exchange(owner->graph, record->request, record->request_received,
                           record->response, sizeof(record->response), &written);
    record->dispatch.result = result;
    record->dispatch.written = written;
    record->dispatch.returned = 1;
    owner->journal.active = 0;
    if (result != MBWD_OK) return mbgs_terminal(owner, MBGS_GRAPH);
    expected = record->header.kind == MBW_ADOPT_REQUEST ?
        MBW_ADOPT_RESPONSE_SIZE : MBW_FRAME_RESPONSE_SIZE;
    kind = record->header.kind == MBW_ADOPT_REQUEST ?
        MBW_ADOPT_RESPONSE : MBW_FRAME_RESPONSE;
    if (written != expected || written > sizeof(record->response) ||
        mbw_decode_header(record->response, MBW_HEADER_SIZE, &response) != MBW_OK ||
        response.length != written || response.kind != kind ||
        response.exchange != record->header.exchange ||
        memcmp(response.session, record->header.session, sizeof(response.session)))
        return mbgs_terminal(owner, MBGS_PROTOCOL);
    record->response_length = written;
    owner->journal.phase = MBGS_WRITE;
    return MBGS_PROGRESS;
}

static int mbgs_write_step(struct mbgs_owner *owner)
{
    struct mbgs_record *record = &owner->journal.current;
    struct mbgs_io_result result;
    size_t remaining;
    owner->journal.current_operation = MBGS_OP_WRITE;
    if (record->write_calls == UINT64_MAX)
        return mbgs_terminal(owner, MBGS_OVERFLOW);
    if (!record->response_length || record->response_length > sizeof(record->response) ||
        record->response_sent >= record->response_length)
        return mbgs_terminal(owner, MBGS_STATE);
    remaining = record->response_length - record->response_sent;
    record->write_calls++;
    record->last_write = (struct mbgs_io_call){0};
    record->last_write.attempted = 1;
    record->last_write.offset = record->response_sent;
    record->last_write.requested = remaining;
    owner->journal.active = 1;
    result = mbgs_sys_write(owner->journal.stream_fd,
                            record->response + record->response_sent, remaining);
    record->last_write.raw = result;
    record->last_write.returned = 1;
    owner->journal.active = 0;
    if (result.count == -1) return mbgs_terminal(owner, MBGS_IO);
    if (result.count < 0 || (size_t)result.count > remaining)
        return mbgs_terminal(owner, MBGS_COUNT);
    if (result.count == 0) return mbgs_terminal(owner, MBGS_NO_PROGRESS);
    record->response_sent += (size_t)result.count;
    if (record->response_sent != record->response_length) return MBGS_PROGRESS;
    owner->journal.phase = MBGS_WRITTEN;
    return MBGS_RECORD_WRITTEN;
}

int mbgs_step(struct mbgs_owner *owner)
{
    if (!owner) return MBGS_INVALID;
    if (owner->journal.active) return MBGS_BUSY;
    if (owner->journal.disposal_started || owner->journal.phase == MBGS_TERMINAL)
        return MBGS_CLOSED;
    switch (owner->journal.phase) {
    case MBGS_HEADER:
    case MBGS_BODY:
        return mbgs_read_step(owner);
    case MBGS_DISPATCH:
        return mbgs_dispatch_step(owner);
    case MBGS_WRITE:
        return mbgs_write_step(owner);
    case MBGS_WRITTEN:
        return MBGS_RECORD_WRITTEN;
    default:
        return MBGS_STATE;
    }
}

int mbgs_next(struct mbgs_owner *owner)
{
    uint64_t next;
    if (!owner) return MBGS_INVALID;
    if (owner->journal.active) return MBGS_BUSY;
    if (owner->journal.disposal_started || owner->journal.phase == MBGS_TERMINAL)
        return MBGS_CLOSED;
    if (owner->journal.phase != MBGS_WRITTEN) return MBGS_STATE;
    if (owner->journal.current.number == UINT64_MAX) return MBGS_OVERFLOW;
    next = owner->journal.current.number + 1;
    owner->journal.last_completed = owner->journal.current;
    owner->journal.last_completed_valid = 1;
    memset(&owner->journal.current, 0, sizeof(owner->journal.current));
    owner->journal.current.number = next;
    owner->journal.current.request_expected = MBW_HEADER_SIZE;
    owner->journal.current_operation = MBGS_OP_NONE;
    owner->journal.phase = MBGS_HEADER;
    return MBGS_OK;
}

int mbgs_inspect(const struct mbgs_owner *owner, struct mbgs_evidence *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!owner || !out) return MBGS_INVALID;
    *out = owner->journal;
    out->graph_present = owner->graph != NULL;
    if (owner->graph) {
        if (mbog_inspect(owner->graph, &out->graph) != MBOG_OK) return MBGS_GRAPH;
        out->graph_evidence_valid = 1;
    }
    return MBGS_OK;
}

static void mbgs_record_disposal(struct mbgs_owner *owner,
                                 const struct mbgs_dispose_report *report)
{
    owner->journal.last_disposal = *report;
    owner->journal.last_disposal_valid = 1;
    if (report->attempt_number == 1) {
        owner->journal.first_disposal = *report;
        owner->journal.first_disposal_valid = 1;
    }
}

static int mbgs_disposal_done(struct mbgs_owner *owner,
                              struct mbgs_dispose_report *report, int result)
{
    report->completed = 1;
    report->result = result;
    report->uncertainty_seen = owner->journal.uncertainty_seen;
    mbgs_record_disposal(owner, report);
    return result;
}

int mbgs_dispose(struct mbgs_owner **slot, struct mbgs_dispose_report *report)
{
    struct mbgs_owner *owner;
    int result;
    if (report) memset(report, 0, sizeof(*report));
    if (!slot || !report) {
        if (report) { report->completed = 1; report->result = MBGS_INVALID; }
        return MBGS_INVALID;
    }
    owner = *slot;
    if (!owner) { report->completed = 1; return MBGS_OK; }
    if (owner->journal.active) {
        report->completed = 1;
        report->result = MBGS_BUSY;
        return MBGS_BUSY;
    }
    if (owner->journal.disposal_started && owner->journal.uncertainty_seen &&
        !owner->graph && !owner->journal.descriptor_held) {
        report->completed = 1;
        report->result = MBGS_UNCERTAIN;
        report->uncertainty_seen = 1;
        return MBGS_UNCERTAIN;
    }
    owner->journal.disposal_started = 1;
    owner->journal.phase = MBGS_DISPOSING;
    if (owner->journal.disposal_attempts == UINT64_MAX) {
        report->completed = 1;
        report->result = MBGS_OVERFLOW;
        return MBGS_OVERFLOW;
    }
    report->attempt_number = ++owner->journal.disposal_attempts;
    mbgs_record_disposal(owner, report);
    if (owner->graph) {
        if (mbgs_capture_graph(owner) != MBGS_OK)
            return mbgs_disposal_done(owner, report, MBGS_GRAPH);
        report->graph_attempted = 1;
        owner->journal.current_operation = MBGS_OP_GRAPH_DISPOSE;
        mbgs_record_disposal(owner, report);
        owner->journal.active = 1;
        result = mbog_dispose(&owner->graph, &report->graph_report);
        report->graph_result = result;
        report->graph_returned = 1;
        owner->journal.active = 0;
        if (report->graph_report.uncertain_close_seen)
            owner->journal.uncertainty_seen = 1;
        if (mbgs_capture_graph(owner) != MBGS_OK)
            return mbgs_disposal_done(owner, report, MBGS_GRAPH);
        if (owner->journal.graph_evidence_valid &&
            owner->journal.graph.uncertain_close_seen)
            owner->journal.uncertainty_seen = 1;
        mbgs_record_disposal(owner, report);
        if (result != MBOG_OK || owner->graph)
            return mbgs_disposal_done(owner, report,
                result == MBOG_UNCERTAIN ? MBGS_UNCERTAIN : MBGS_GRAPH);
    }
    if (owner->journal.descriptor_held) {
        int fd = owner->journal.stream_fd;
        owner->journal.stream_close = (struct mbgs_stream_close){0};
        owner->journal.stream_close.attempted = 1;
        owner->journal.stream_close.descriptor = fd;
        owner->journal.descriptor_held = 0;
        owner->journal.stream_fd = -1;
        owner->journal.current_operation = MBGS_OP_STREAM_CLOSE;
        report->stream_close = owner->journal.stream_close;
        mbgs_record_disposal(owner, report);
        owner->journal.active = 1;
        owner->journal.stream_close.raw = mbgs_sys_close(fd);
        owner->journal.stream_close.returned = 1;
        owner->journal.active = 0;
        report->stream_close = owner->journal.stream_close;
        if (owner->journal.stream_close.raw.result != 0)
            owner->journal.uncertainty_seen = 1;
        mbgs_record_disposal(owner, report);
    }
    if (owner->journal.uncertainty_seen)
        return mbgs_disposal_done(owner, report, MBGS_UNCERTAIN);
    report->owner_released = 1;
    mbgs_disposal_done(owner, report, MBGS_OK);
    *slot = NULL;
    free(owner);
    return MBGS_OK;
}

int mbgs_finalize_graph(struct mbgs_owner *owner)
{
    int result;
    if (!owner) return MBGS_INVALID;
    if (owner->journal.active) return MBGS_BUSY;
    if (!owner->journal.disposal_started) return MBGS_STATE;
    if (!owner->graph) return MBGS_OK;
    if (owner->journal.graph_finalize_attempts == UINT64_MAX) return MBGS_OVERFLOW;
    if (mbgs_capture_graph(owner) != MBGS_OK) return MBGS_GRAPH;
    if (owner->journal.graph.uncertain_close_seen) owner->journal.uncertainty_seen = 1;
    owner->journal.graph_finalize_attempts++;
    owner->journal.last_graph_finalize = (struct mbgs_finalize_call){1, 0, 0};
    owner->journal.current_operation = MBGS_OP_GRAPH_FINALIZE;
    owner->journal.active = 1;
    result = mbog_finalize(&owner->graph);
    owner->journal.last_graph_finalize.result = result;
    owner->journal.last_graph_finalize.returned = 1;
    owner->journal.active = 0;
    if (mbgs_capture_graph(owner) != MBGS_OK) return MBGS_GRAPH;
    return result == MBOG_OK ? MBGS_OK : MBGS_GRAPH;
}

int mbgs_finalize(struct mbgs_owner **slot)
{
    struct mbgs_owner *owner;
    if (!slot) return MBGS_INVALID;
    owner = *slot;
    if (!owner) return MBGS_OK;
    if (owner->journal.active) return MBGS_BUSY;
    if (!owner->journal.disposal_started || owner->graph || owner->journal.descriptor_held)
        return MBGS_STATE;
    *slot = NULL;
    free(owner);
    return MBGS_OK;
}
