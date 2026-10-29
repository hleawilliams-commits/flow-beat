/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_GUEST_STREAM_H
#define EMULATOR_GUEST_STREAM_H
#include "graph.h"
#include "stream_native.h"

struct mbgs_owner;
enum mbgs_result {
    MBGS_OK = 0, MBGS_PROGRESS = 1, MBGS_RECORD_WRITTEN = 2,
    MBGS_INVALID = -1, MBGS_NOMEM = -2, MBGS_STATE = -3, MBGS_BUSY = -4,
    MBGS_CLOSED = -5, MBGS_EOF = -6, MBGS_IO = -7, MBGS_PROTOCOL = -8,
    MBGS_COUNT = -9, MBGS_GRAPH = -10, MBGS_OVERFLOW = -11,
    MBGS_UNCERTAIN = -12, MBGS_NO_PROGRESS = -13
};
enum mbgs_phase {
    MBGS_ALLOCATED = 0, MBGS_HEADER, MBGS_BODY, MBGS_DISPATCH,
    MBGS_WRITE, MBGS_WRITTEN, MBGS_TERMINAL, MBGS_DISPOSING
};
enum mbgs_operation {
    MBGS_OP_NONE = 0, MBGS_OP_READ, MBGS_OP_DISPATCH, MBGS_OP_WRITE,
    MBGS_OP_GRAPH_DISPOSE, MBGS_OP_GRAPH_FINALIZE, MBGS_OP_STREAM_CLOSE
};
struct mbgs_io_call {
    int attempted, returned;
    size_t offset, requested;
    struct mbgs_io_result raw;
};
struct mbgs_dispatch_call {
    int attempted, returned, result;
    size_t written; /* exact returned output, before size validation */
};
struct mbgs_record {
    uint64_t number, read_calls, write_calls;
    size_t request_received, request_expected;
    size_t response_length, response_sent;
    int header_valid;
    struct mbw_header header;
    struct mbgs_io_call last_read, last_write;
    struct mbgs_dispatch_call dispatch;
    /* Exact retained bounded bytes, including unvalidated read effects after
     * failed calls. Only request_received credits valid positive read counts. */
    uint8_t request[MBW_MAX_REQUEST_SIZE];
    uint8_t response[MBW_MAX_RECORD_SIZE];
};
struct mbgs_stream_close {
    int attempted, returned, descriptor;
    struct mbgs_close_result raw;
};
struct mbgs_dispose_report {
    uint64_t attempt_number;
    int graph_attempted, graph_returned, graph_result;
    struct mbog_dispose_report graph_report;
    struct mbgs_stream_close stream_close;
    int completed, result, owner_released, uncertainty_seen;
};
struct mbgs_finalize_call {
    int attempted, returned, result;
};
struct mbgs_evidence {
    enum mbgs_phase phase;
    enum mbgs_operation current_operation;
    int active; /* same-thread reentry guard, not synchronization */
    int taken, descriptor_held, stream_fd;
    int graph_present, graph_evidence_valid;
    /* If graph_present is false this is archived evidence, not graph custody.
     * Actual fd diagnostics remain usable numbers; no alias exclusion. */
    struct mbog_evidence graph;
    struct mbgs_record current;
    int last_completed_valid;
    struct mbgs_record last_completed;
    int first_terminal;
    enum mbgs_operation terminal_operation;
    int disposal_started, uncertainty_seen;
    uint64_t disposal_attempts, graph_finalize_attempts;
    int first_disposal_valid, last_disposal_valid;
    struct mbgs_dispose_report first_disposal, last_disposal;
    struct mbgs_stream_close stream_close;
    struct mbgs_finalize_call last_graph_finalize;
};

/* Inactive POSIX stream framing owner. No open/socket/connect/acquisition,
 * thread, background loop or endpoint startup. All inputs/output objects must
 * be disjoint. All APIs require external serialization and stable resources.
 * Same-thread syscall/backend fixture inspection is allowed; state-changing
 * reentry while active returns BUSY without altering the in-flight journal.
 * This is not concurrency, durable custody or process-death protection.
 *
 * The caller must independently arrange suitable SIGPIPE policy if expecting
 * write to return EPIPE. No global signal policy is changed here. Blocking calls
 * may block indefinitely; one-step bounds the number of calls, not latency.
 * Nonlocal exit/process death may prevent returned evidence from being recorded.
 */
int mbgs_create(struct mbgs_owner **out);
/* Allocation happened in create. Pure validation then retain BOTH already-open
 * transport fd and fresh Ready graph before consuming caller slots to -1/NULL.
 * Require graph's dispatcher never exchanged. Reject numeric fd equality with
 * its live original/duplicate, without claiming same-file alias exclusion.
 * Failure leaves both caller slots unchanged. No raw graph getter is provided.
 * Caller transfers exclusive use and must not retain operational aliases. */
int mbgs_take(struct mbgs_owner *, int *stream_fd, struct mbog_graph **graph);
/* At most one read, write, or mbog_exchange per call. Read only the remaining
 * header48, then only the declared canonical request remainder (max256).
 * Full request dispatch is armed exactly once; its returned bytes/result stay
 * retained before any interpretation. Response writes use only remaining bytes.
 * Every error, EINTR/EAGAIN, read EOF, zero write or impossible count is terminal;
 * no retry/reopen/replay/flush/disposal follows. Progress credits only a valid
 * positive count, ignoring stale errno on that positive return.
 * RECORD_WRITTEN means locally returned write counts cover the response; it is
 * not peer receipt, broker/native success or authority. No read of a subsequent
 * record occurs until explicit next. Terminal/disposal repeats return CLOSED. */
int mbgs_step(struct mbgs_owner *);
/* Memory-only after RECORD_WRITTEN: archive exact current record into one fixed
 * last_completed slot, clear current and advance checked record number. No I/O.
 * Older completed records are not an unbounded log. Overflow refuses untouched. */
int mbgs_next(struct mbgs_owner *);
/* Pure bounded copies, including pure graph inspect; no I/O, locks or callback.
 * Child callable pointers stay scrubbed by graph inspect. Fd numbers are actual
 * diagnostics and must not be used to mutate/close/duplicate the resources.
 * A consumed close slot with returned=false is not evidence of close success. */
int mbgs_inspect(const struct mbgs_owner *, struct mbgs_evidence *);
/* Explicit only after independent external quiescence and resolution/accounting
 * for all transport/graph uncertainty, aliases, native ownership and callbacks.
 * Permanently close step/next; archive graph, then invoke actual graph disposal.
 * If graph remains or reports error, stop before transport close. Only after
 * graph is absent, consume fd slot before exactly one close and retain raw result.
 * Never retry a consumed fd number. No EOF/error implies permission to dispose.
 * Any graph/transport close uncertainty latches shell retention, even after later
 * known cleanup. Known full success consumes/frees owner. Refusal preserves prior
 * evidence. Each explicit retry needs renewed independent justification.
 * No native RELEASE/FRAME/restore or stream flush is added. */
int mbgs_dispose(struct mbgs_owner **, struct mbgs_dispose_report *);
/* Explicit externally justified memory-only graph shell finalization; delegates
 * to mbog_finalize, which refuses while graph children remain. Archives evidence
 * first and preserves outer uncertainty. Requires disposal already started. */
int mbgs_finalize_graph(struct mbgs_owner *);
/* Explicit memory-only owner discard after external accounting: requires
 * disposal_started, no graph, no transport fd, and no active operation. */
int mbgs_finalize(struct mbgs_owner **);
#endif
