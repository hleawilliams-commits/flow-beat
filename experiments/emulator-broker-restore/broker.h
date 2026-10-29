/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_RESTORE_BROKER_H
#define EMULATOR_RESTORE_BROKER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Semantic value mirrors, NOT ioctl wire structures. The backend explicitly
 * translates the reviewed native64 V2 ABI. No pointers appear in requests. */
#define MB_FRAME_BYTES 152u
#define MB_REQUIRED_FLAGS 31u
#define MB_OWNED_FLAGS 23u
#define MB_INJECTED_FLAG 8u
struct mb_state {
    uint64_t instance[2];
    uint64_t run_id, epoch, sequence;
    uint32_t flags;
    int32_t crc_len;
    uint8_t frame[MB_FRAME_BYTES];
};
struct mb_grant {
    uint64_t instance[2];
    uint64_t run_id, epoch, producer_id;
};
struct mb_request {
    struct mb_grant grant;
    uint64_t request_id;
    uint8_t frame[MB_FRAME_BYTES];
};
struct mb_restore_request {
    uint64_t restore_id;
    struct mb_state source; /* full expected source, bound before enqueue */
    uint32_t injected; /* strict 0/1, part of the request identity */
    uint8_t frame[MB_FRAME_BYTES];
};
enum mb_operation { MB_OP_NONE = 0, MB_OP_FRAME = 1, MB_OP_ROTATE = 2,
                    MB_OP_RESTORE = 3 };
struct mb_command {
    uint64_t instance[2];
    uint64_t run_id, epoch, sequence;
    uint32_t injected;
    uint8_t frame[MB_FRAME_BYTES];
};
enum mb_outcome {
    MB_OUTCOME_NONE = 0,
    MB_OUTCOME_COMMITTED = 1,
    MB_OUTCOME_REJECTED = 2,
    MB_OUTCOME_UNKNOWN = 3
};
struct mb_backend_reply { enum mb_outcome outcome; int error; };
struct mb_backend {
    void *context; /* retained immutable identity; future one live controller fd */
    /* Zero means complete snapshot. Nonzero means no usable snapshot. */
    int (*snapshot)(void *context, struct mb_state *out);
    /* COMMITTED/error0 = exact command committed; REJECTED/error>0 = command
     * definitely did not commit. UNKNOWN/error>=0 = delivery/outcome uncertain.
     * Callback must not retry, change fd, mutate command, or reenter broker. */
    struct mb_backend_reply (*submit)(void *context, enum mb_operation op,
                                      const struct mb_command *command);
};
struct mb_receipt {
    enum mb_outcome outcome;
    enum mb_operation operation;
    int backend_error;
    struct mb_request request; /* zero except for FRAME */
    struct mb_restore_request restore; /* zero except for RESTORE */
    struct mb_command command;
    struct mb_state expected_after;
};
struct mb_status {
    int admission_open, grant_issued, unresolved;
    uint64_t next_request_id;
    uint64_t next_restore_id; /* separate monotonic attempted-restore identity */
    struct mb_grant grant;
    struct mb_state expected; /* local known state, not a fresh kernel ACK */
    struct mb_receipt last;
};
struct mb_broker;
enum mb_result {
    MB_OK = 0,
    MB_ERR_INVALID = -1,
    MB_ERR_NOMEM = -2,
    MB_ERR_MUTEX = -3,
    MB_ERR_STATE = -4,
    MB_ERR_CLOSED = -5,
    MB_ERR_STALE = -6,
    MB_ERR_ID = -7,
    MB_ERR_CONFLICT = -8,
    MB_ERR_OVERFLOW = -9,
    MB_ERR_BACKEND = -10,
    MB_ERR_BUSY = -11
};
/* create verifies backend snapshot exactly equals expected_owned. It does not
 * acquire a fd or prove absence of another broker/pending syscall. */
int mb_create(const struct mb_backend *backend,
              const struct mb_state *expected_owned, struct mb_broker **out);
/* Caller must prove no concurrent method/callback remains before destroy.
 * No implicit backend close, release, rotation or write. */
void mb_destroy(struct mb_broker *broker);
/* One grant per injected epoch (flags31 only). Snapshot must equal local expected and all baseline
 * bytes. Re-adoption after quiesce requires successful generation rotation. */
int mb_adopt(struct mb_broker *broker, const uint8_t baseline[MB_FRAME_BYTES],
             uint64_t producer_id, struct mb_grant *out);
/* Pure copy helper. Caller/producer must stamp before enqueue; this helper
 * cannot detect stale payloads presented for reauthoring with a current grant. */
int mb_stamp(const struct mb_grant *grant, uint64_t request_id,
             const uint8_t frame[MB_FRAME_BYTES], struct mb_request *out);
/* MB_OK means inspect out.outcome: it is not necessarily COMMITTED.
 * Every return initializes out; API refusals leave retained last receipt intact.
 * Exact last FRAME duplicate returns cached outcome without backend access. */
int mb_submit(struct mb_broker *broker, const struct mb_request *request,
              struct mb_receipt *out);
int mb_quiesce(struct mb_broker *broker);
/* Closed, no unresolved submission, fresh exact current snapshot required.
 * A successful/reconciled rotation keeps broker/run, advances epoch, seq0.
 * New producer adoption remains a separate checked operation. */
int mb_rotate(struct mb_broker *broker, const uint8_t frame[MB_FRAME_BYTES],
              struct mb_receipt *out);
/* Closed admission, no unresolved command, exact source plus fresh snapshot.
 * Copies the request before clearing out. IDs start at1, advance only for a
 * backend attempt and never reset across FRAME/ROTATE/RESTORE. UINT64_MAX is
 * not an attemptable ID. Exact last RESTORE returns its cached receipt before
 * source/capacity checks; same ID with changed source/mode/bytes is a conflict.
 * A cached receipt is historical and does not assert current state or recovery.
 * Successful/reconciled RESTORE advances epoch, seq0 and clears the old grant.
 * Rejected attempts also consume their ID. Older replaced receipts cannot retry.
 * The caller must establish baseline authority and native-stop separately. */
int mb_restore(struct mb_broker *broker, const struct mb_restore_request *request,
               struct mb_receipt *out);
/* Positive exact expected state can reconcile one UNKNOWN. Lower or conflicting
 * state stays UNKNOWN. Never opens admission or retries a command. */
int mb_reconcile(struct mb_broker *broker, struct mb_receipt *out);
/* Copies local state only. Never queries kernel or grants a new producer. */
int mb_inspect(struct mb_broker *broker, struct mb_status *out);
#ifdef __cplusplus
}
#endif
#endif
