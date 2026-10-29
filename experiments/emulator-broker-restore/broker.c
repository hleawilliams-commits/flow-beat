/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "broker.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct mb_broker {
    pthread_mutex_t mutex;
    struct mb_backend backend;
    struct mb_status state;
};

static int grant_valid(const struct mb_grant *g)
{
    return (g->instance[0] || g->instance[1]) && g->run_id && g->epoch && g->producer_id;
}
static int state_valid(const struct mb_state *s)
{
    return (s->instance[0] || s->instance[1]) && s->run_id && s->epoch &&
        (s->flags == MB_OWNED_FLAGS || s->flags == MB_REQUIRED_FLAGS) && s->crc_len == 96;
}
static int grant_equal(const struct mb_grant *a, const struct mb_grant *b)
{
    return a->instance[0] == b->instance[0] && a->instance[1] == b->instance[1] &&
        a->run_id == b->run_id && a->epoch == b->epoch && a->producer_id == b->producer_id;
}
static int state_equal(const struct mb_state *a, const struct mb_state *b)
{
    return a->instance[0] == b->instance[0] && a->instance[1] == b->instance[1] &&
        a->run_id == b->run_id && a->epoch == b->epoch && a->sequence == b->sequence &&
        a->flags == b->flags && a->crc_len == b->crc_len &&
        memcmp(a->frame, b->frame, MB_FRAME_BYTES) == 0;
}
static int request_equal(const struct mb_request *a, const struct mb_request *b)
{
    return grant_equal(&a->grant, &b->grant) && a->request_id == b->request_id &&
        memcmp(a->frame, b->frame, MB_FRAME_BYTES) == 0;
}
static int restore_equal(const struct mb_restore_request *a,
                          const struct mb_restore_request *b)
{
    return a->restore_id == b->restore_id && state_equal(&a->source, &b->source) &&
        a->injected == b->injected && memcmp(a->frame, b->frame, MB_FRAME_BYTES) == 0;
}
static int lock_broker(struct mb_broker *b)
{
    if (!b) return MB_ERR_INVALID;
    return pthread_mutex_lock(&b->mutex) == 0 ? MB_OK : MB_ERR_MUTEX;
}
static int finish(struct mb_broker *b, int result)
{
    return pthread_mutex_unlock(&b->mutex) == 0 ? result : MB_ERR_MUTEX;
}
static int check_snapshot(struct mb_broker *b)
{
    struct mb_state actual = {0};
    if (b->backend.snapshot(b->backend.context, &actual) != 0) {
        b->state.admission_open = 0;
        return MB_ERR_BACKEND;
    }
    if (!state_valid(&actual) || !state_equal(&actual, &b->state.expected)) {
        b->state.admission_open = 0;
        return MB_ERR_STATE;
    }
    return MB_OK;
}
static void apply_commit(struct mb_broker *b)
{
    b->state.expected = b->state.last.expected_after;
    b->state.unresolved = 0;
    if (b->state.last.operation == MB_OP_ROTATE ||
        b->state.last.operation == MB_OP_RESTORE) {
        b->state.admission_open = 0;
        b->state.grant_issued = 0;
        b->state.next_request_id = 1;
        memset(&b->state.grant, 0, sizeof(b->state.grant));
    }
}
static void build_command(struct mb_broker *b, enum mb_operation op,
                          const uint8_t frame[MB_FRAME_BYTES], uint32_t injected,
                          struct mb_receipt *receipt)
{
    memset(receipt, 0, sizeof(*receipt));
    receipt->outcome = MB_OUTCOME_UNKNOWN;
    receipt->operation = op;
    receipt->command.instance[0] = b->state.expected.instance[0];
    receipt->command.instance[1] = b->state.expected.instance[1];
    receipt->command.run_id = b->state.expected.run_id;
    receipt->command.epoch = b->state.expected.epoch;
    receipt->command.sequence = b->state.expected.sequence + 1;
    receipt->command.injected = injected;
    memcpy(receipt->command.frame, frame, MB_FRAME_BYTES);
    receipt->expected_after = b->state.expected;
    receipt->expected_after.flags = MB_OWNED_FLAGS | (injected ? MB_INJECTED_FLAG : 0u);
    memcpy(receipt->expected_after.frame, frame, MB_FRAME_BYTES);
    if (op == MB_OP_ROTATE || op == MB_OP_RESTORE) {
        receipt->expected_after.epoch++;
        receipt->expected_after.sequence = 0;
    } else {
        receipt->expected_after.sequence++;
    }
}
static void call_submit(struct mb_broker *b)
{
    struct mb_backend_reply reply;
    b->state.unresolved = 1;
    reply = b->backend.submit(b->backend.context, b->state.last.operation,
                              &b->state.last.command);
    /* A malformed callback result cannot prove that no side effect occurred. */
    if (reply.outcome == MB_OUTCOME_COMMITTED && reply.error == 0) {
        b->state.last.outcome = MB_OUTCOME_COMMITTED;
        b->state.last.backend_error = 0;
        apply_commit(b);
    } else if (reply.outcome == MB_OUTCOME_REJECTED && reply.error > 0) {
        b->state.last.outcome = MB_OUTCOME_REJECTED;
        b->state.last.backend_error = reply.error;
        b->state.unresolved = 0;
        b->state.admission_open = 0;
    } else {
        b->state.last.outcome = MB_OUTCOME_UNKNOWN;
        b->state.last.backend_error = reply.error >= 0 ? reply.error : 0;
        b->state.admission_open = 0;
    }
}
int mb_create(const struct mb_backend *backend,
              const struct mb_state *expected_owned, struct mb_broker **out)
{
    struct mb_broker *b;
    pthread_mutexattr_t attr;
    int rc;
    if (out) *out = NULL;
    if (!out || !backend || !backend->snapshot || !backend->submit ||
        !expected_owned || !state_valid(expected_owned)) return MB_ERR_INVALID;
    b = calloc(1, sizeof(*b));
    if (!b) return MB_ERR_NOMEM;
    if (pthread_mutexattr_init(&attr) != 0) { free(b); return MB_ERR_MUTEX; }
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    if (!rc) rc = pthread_mutex_init(&b->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc) { free(b); return MB_ERR_MUTEX; }
    b->backend = *backend;
    b->state.expected = *expected_owned;
    b->state.next_request_id = 1;
    b->state.next_restore_id = 1;
    rc = check_snapshot(b);
    if (rc != MB_OK) { pthread_mutex_destroy(&b->mutex); free(b); return rc; }
    *out = b;
    return MB_OK;
}
void mb_destroy(struct mb_broker *b)
{
    if (!b) return;
    /* Destruction requires external quiescence. If the mutex is busy, do not
     * free storage still potentially referenced by an operation/callback. */
    if (pthread_mutex_destroy(&b->mutex) == 0) free(b);
}
int mb_adopt(struct mb_broker *b, const uint8_t baseline[MB_FRAME_BYTES],
             uint64_t producer_id, struct mb_grant *out)
{
    uint8_t copy[MB_FRAME_BYTES];
    int rc;
    if (baseline) memcpy(copy, baseline, sizeof(copy));
    if (out) memset(out, 0, sizeof(*out));
    if (!baseline || !producer_id || !out) return MB_ERR_INVALID;
    rc = lock_broker(b); if (rc) return rc;
    if (b->state.unresolved) return finish(b, MB_ERR_BUSY);
    if (b->state.admission_open || b->state.grant_issued) return finish(b, MB_ERR_CLOSED);
    if (b->state.expected.flags != MB_REQUIRED_FLAGS) return finish(b, MB_ERR_STATE);
    rc = check_snapshot(b); if (rc) return finish(b, rc);
    if (memcmp(copy, b->state.expected.frame, sizeof(copy)) != 0)
        return finish(b, MB_ERR_STATE);
    b->state.grant.instance[0] = b->state.expected.instance[0];
    b->state.grant.instance[1] = b->state.expected.instance[1];
    b->state.grant.run_id = b->state.expected.run_id;
    b->state.grant.epoch = b->state.expected.epoch;
    b->state.grant.producer_id = producer_id;
    b->state.grant_issued = 1;
    b->state.admission_open = 1;
    *out = b->state.grant;
    return finish(b, MB_OK);
}
int mb_stamp(const struct mb_grant *grant, uint64_t request_id,
             const uint8_t frame[MB_FRAME_BYTES], struct mb_request *out)
{
    struct mb_request copy = {0};
    if (grant) copy.grant = *grant;
    if (frame) memcpy(copy.frame, frame, sizeof(copy.frame));
    copy.request_id = request_id;
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !grant || !frame || !grant_valid(&copy.grant) || !request_id)
        return MB_ERR_INVALID;
    if (request_id == UINT64_MAX) return MB_ERR_OVERFLOW;
    *out = copy;
    return MB_OK;
}
int mb_submit(struct mb_broker *b, const struct mb_request *request,
              struct mb_receipt *out)
{
    struct mb_request copy = {0};
    struct mb_receipt pending;
    int rc;
    if (request) copy = *request;
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !request || !grant_valid(&copy.grant) || !copy.request_id)
        return MB_ERR_INVALID;
    rc = lock_broker(b); if (rc) return rc;
    if (!b->state.grant_issued || !grant_equal(&copy.grant, &b->state.grant))
        return finish(b, MB_ERR_STALE);
    if (b->state.last.operation == MB_OP_FRAME &&
        copy.request_id == b->state.last.request.request_id) {
        if (request_equal(&copy, &b->state.last.request)) {
            *out = b->state.last;
            return finish(b, MB_OK);
        }
        b->state.admission_open = 0;
        return finish(b, MB_ERR_CONFLICT);
    }
    if (copy.request_id == UINT64_MAX || b->state.expected.sequence == UINT64_MAX) {
        b->state.admission_open = 0;
        return finish(b, MB_ERR_OVERFLOW);
    }
    if (copy.request_id != b->state.next_request_id)
        return finish(b, MB_ERR_ID);
    if (b->state.unresolved) return finish(b, MB_ERR_BUSY);
    if (!b->state.admission_open) return finish(b, MB_ERR_CLOSED);
    build_command(b, MB_OP_FRAME, copy.frame, 1, &pending);
    pending.request = copy;
    b->state.last = pending;
    b->state.next_request_id++;
    call_submit(b);
    *out = b->state.last;
    return finish(b, MB_OK);
}
int mb_quiesce(struct mb_broker *b)
{
    int rc = lock_broker(b); if (rc) return rc;
    b->state.admission_open = 0;
    return finish(b, MB_OK);
}
int mb_rotate(struct mb_broker *b, const uint8_t frame[MB_FRAME_BYTES],
              struct mb_receipt *out)
{
    uint8_t copy[MB_FRAME_BYTES];
    struct mb_receipt pending;
    int rc;
    if (frame) memcpy(copy, frame, sizeof(copy));
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !frame) return MB_ERR_INVALID;
    rc = lock_broker(b); if (rc) return rc;
    if (b->state.unresolved) return finish(b, MB_ERR_BUSY);
    if (b->state.admission_open) return finish(b, MB_ERR_CLOSED);
    if (b->state.expected.sequence == UINT64_MAX || b->state.expected.epoch == UINT64_MAX)
        return finish(b, MB_ERR_OVERFLOW);
    rc = check_snapshot(b); if (rc) return finish(b, rc);
    build_command(b, MB_OP_ROTATE, copy, 1, &pending);
    b->state.last = pending;
    call_submit(b);
    *out = b->state.last;
    return finish(b, MB_OK);
}
int mb_restore(struct mb_broker *b, const struct mb_restore_request *request,
               struct mb_receipt *out)
{
    struct mb_restore_request copy = {0};
    struct mb_receipt pending;
    int rc;
    if (request) copy = *request;
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !request || !copy.restore_id) return MB_ERR_INVALID;
    rc = lock_broker(b); if (rc) return rc;
    /* Resolve a retry by its retained identity before new-operation gates.
     * This is a historical answer: no fresh snapshot, resend or admission change. */
    if (b->state.last.operation == MB_OP_RESTORE &&
        copy.restore_id == b->state.last.restore.restore_id) {
        if (restore_equal(&copy, &b->state.last.restore)) {
            *out = b->state.last;
            return finish(b, MB_OK);
        }
        b->state.admission_open = 0;
        return finish(b, MB_ERR_CONFLICT);
    }
    if (!state_valid(&copy.source) || copy.injected > 1)
        return finish(b, MB_ERR_INVALID);
    if (b->state.unresolved) return finish(b, MB_ERR_BUSY);
    if (b->state.admission_open) return finish(b, MB_ERR_CLOSED);
    if (copy.restore_id == UINT64_MAX || b->state.next_restore_id == UINT64_MAX)
        return finish(b, MB_ERR_OVERFLOW);
    if (copy.restore_id != b->state.next_restore_id) return finish(b, MB_ERR_ID);
    if (!state_equal(&copy.source, &b->state.expected)) return finish(b, MB_ERR_STALE);
    if (b->state.expected.sequence == UINT64_MAX || b->state.expected.epoch == UINT64_MAX)
        return finish(b, MB_ERR_OVERFLOW);
    rc = check_snapshot(b); if (rc) return finish(b, rc);
    build_command(b, MB_OP_RESTORE, copy.frame, copy.injected, &pending);
    pending.restore = copy;
    b->state.last = pending;
    b->state.next_restore_id++;
    call_submit(b);
    *out = b->state.last;
    return finish(b, MB_OK);
}
int mb_reconcile(struct mb_broker *b, struct mb_receipt *out)
{
    struct mb_state actual = {0};
    int rc;
    if (out) memset(out, 0, sizeof(*out));
    if (!out) return MB_ERR_INVALID;
    rc = lock_broker(b); if (rc) return rc;
    if (!b->state.unresolved) {
        *out = b->state.last;
        return finish(b, MB_ERR_STATE);
    }
    b->state.admission_open = 0;
    if (b->backend.snapshot(b->backend.context, &actual) != 0) {
        *out = b->state.last;
        return finish(b, MB_ERR_BACKEND);
    }
    if (state_valid(&actual) && state_equal(&actual, &b->state.last.expected_after)) {
        b->state.last.outcome = MB_OUTCOME_COMMITTED;
        b->state.last.backend_error = 0;
        apply_commit(b);
    }
    *out = b->state.last;
    return finish(b, MB_OK);
}
int mb_inspect(struct mb_broker *b, struct mb_status *out)
{
    int rc;
    if (out) memset(out, 0, sizeof(*out));
    if (!out) return MB_ERR_INVALID;
    rc = lock_broker(b); if (rc) return rc;
    *out = b->state;
    return finish(b, MB_OK);
}
