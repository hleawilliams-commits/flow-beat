/* SPDX-License-Identifier: GPL-2.0-only */
/* Generated from exact frozen broker.c bytes: prefix before int mb_create(,
 * then suffix beginning int mb_adopt(. Existing operation/helper bodies stay
 * unchanged; legacy mb_create/mb_destroy lifecycle definitions are excluded. */
#include "broker-core.inc"
#include "broker_owner.h"
#include "sync.h"

struct mbro_owner {
    struct mb_broker core;
    pthread_mutexattr_t attr;
    struct mbro_evidence evidence;
};

static int mbro_terminal(struct mbro_owner *owner, int result)
{
    owner->evidence.phase = MBRO_TERMINAL;
    if (!owner->evidence.terminal_error) owner->evidence.terminal_error = result;
    return result;
}

int mbro_create(const struct mb_backend *backend,
                const struct mb_state *expected, struct mbro_owner **out)
{
    struct mb_backend backend_copy = {0};
    struct mb_state expected_copy = {0};
    struct mbro_owner *owner;
    if (backend) backend_copy = *backend;
    if (expected) expected_copy = *expected;
    if (out) *out = NULL;
    if (!out || !backend || !backend_copy.snapshot || !backend_copy.submit ||
        !expected || !state_valid(&expected_copy))
        return MB_ERR_INVALID;
    owner = calloc(1, sizeof(*owner));
    if (!owner) return MB_ERR_NOMEM;
    owner->core.backend = backend_copy;
    owner->core.state.expected = expected_copy;
    owner->core.state.next_request_id = 1;
    owner->core.state.next_restore_id = 1;
    owner->evidence.phase = MBRO_ALLOCATED;
    owner->evidence.backend = backend_copy;
    owner->evidence.expected = expected_copy;
    *out = owner;
    return MB_OK;
}

int mbro_initialize(struct mbro_owner *owner)
{
    int result;
    if (!owner) return MB_ERR_INVALID;
    if (owner->evidence.disposal_started) return MB_ERR_CLOSED;
    if (owner->evidence.initialize_attempted ||
        owner->evidence.phase != MBRO_ALLOCATED)
        return MB_ERR_STATE;
    owner->evidence.initialize_attempted = 1;
    owner->evidence.attr_init_attempted = 1;
    owner->evidence.phase = MBRO_ATTR_INIT_ARMED;
    result = mbrs_attr_init(&owner->attr);
    owner->evidence.attr_init_result = result;
    owner->evidence.attr_init_returned = 1;
    if (result != 0) return mbro_terminal(owner, MB_ERR_MUTEX);
    owner->evidence.attr_held = 1;
    owner->evidence.phase = MBRO_ATTR_RETAINED;

    owner->evidence.attr_settype_attempted = 1;
    owner->evidence.phase = MBRO_ATTR_SETTYPE_ARMED;
    result = mbrs_attr_settype(&owner->attr, PTHREAD_MUTEX_ERRORCHECK);
    owner->evidence.attr_settype_result = result;
    owner->evidence.attr_settype_returned = 1;
    if (result != 0) return mbro_terminal(owner, MB_ERR_MUTEX);
    owner->evidence.phase = MBRO_ATTR_CONFIGURED;

    owner->evidence.mutex_init_attempted = 1;
    owner->evidence.phase = MBRO_MUTEX_INIT_ARMED;
    result = mbrs_mutex_init(&owner->core.mutex, &owner->attr);
    owner->evidence.mutex_init_result = result;
    owner->evidence.mutex_init_returned = 1;
    if (result != 0) return mbro_terminal(owner, MB_ERR_MUTEX);
    owner->evidence.mutex_held = 1;
    owner->evidence.phase = MBRO_INITIALIZED;
    return MB_OK;
}

int mbro_validate(struct mbro_owner *owner)
{
    struct mb_state actual = {0};
    int result;
    if (!owner) return MB_ERR_INVALID;
    if (owner->evidence.disposal_started) return MB_ERR_CLOSED;
    if (owner->evidence.validation_attempted ||
        owner->evidence.phase != MBRO_INITIALIZED || !owner->evidence.mutex_held)
        return MB_ERR_STATE;
    owner->evidence.validation_attempted = 1;
    owner->evidence.phase = MBRO_VALIDATE_ARMED;
    result = owner->core.backend.snapshot(owner->core.backend.context, &actual);
    owner->evidence.validation_result = result;
    owner->evidence.validation_state = actual;
    owner->evidence.validation_returned = 1;
    if (result != 0) return mbro_terminal(owner, MB_ERR_BACKEND);
    if (!state_valid(&actual) ||
        !state_equal(&actual, &owner->evidence.expected))
        return mbro_terminal(owner, MB_ERR_STATE);
    owner->evidence.validation_matched = 1;
    owner->evidence.phase = MBRO_READY;
    return MB_OK;
}

int mbro_get_broker(struct mbro_owner *owner, struct mb_broker **out)
{
    if (out) *out = NULL;
    if (!owner || !out) return MB_ERR_INVALID;
    if (owner->evidence.disposal_started) return MB_ERR_CLOSED;
    if (owner->evidence.phase != MBRO_READY || !owner->evidence.mutex_held)
        return MB_ERR_STATE;
    *out = &owner->core;
    return MB_OK;
}

int mbro_inspect(const struct mbro_owner *owner, struct mbro_evidence *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!owner || !out) return MB_ERR_INVALID;
    *out = owner->evidence;
    return MB_OK;
}

static void mbro_record_disposal(struct mbro_owner *owner,
                                 const struct mbro_dispose_report *report)
{
    owner->evidence.last_disposal = *report;
    owner->evidence.last_disposal_valid = 1;
    if (report->attempt_number == 1) {
        owner->evidence.first_disposal = *report;
        owner->evidence.first_disposal_valid = 1;
    }
}

static int mbro_disposal_failed(struct mbro_owner *owner,
                                struct mbro_dispose_report *report)
{
    report->completed = 1;
    report->result = MB_ERR_MUTEX;
    mbro_record_disposal(owner, report);
    return report->result;
}

int mbro_dispose(struct mbro_owner **slot, struct mbro_dispose_report *report)
{
    struct mbro_owner *owner;
    if (report) memset(report, 0, sizeof(*report));
    if (!slot || !report) {
        if (report) {
            report->completed = 1;
            report->result = MB_ERR_INVALID;
        }
        return MB_ERR_INVALID;
    }
    owner = *slot;
    if (!owner) {
        report->completed = 1;
        report->result = MB_OK;
        return MB_OK;
    }
    /* This relies on external quiescence, not a core lock. No copied broker
     * alias may be used again after disposal begins, even if a destroy fails. */
    owner->evidence.disposal_started = 1;
    owner->evidence.phase = MBRO_DISPOSING;
    owner->core.state.admission_open = 0;
    if (owner->evidence.disposal_attempts == UINT64_MAX) {
        report->completed = 1;
        report->result = MB_ERR_OVERFLOW;
        return MB_ERR_OVERFLOW;
    }
    owner->evidence.disposal_attempts++;
    report->attempt_number = owner->evidence.disposal_attempts;
    mbro_record_disposal(owner, report);
    if (owner->evidence.mutex_held) {
        report->mutex_attempted = 1;
        mbro_record_disposal(owner, report);
        report->mutex_result = mbrs_mutex_destroy(&owner->core.mutex);
        report->mutex_returned = 1;
        mbro_record_disposal(owner, report);
        if (report->mutex_result != 0)
            return mbro_disposal_failed(owner, report);
        owner->evidence.mutex_held = 0;
    }
    if (owner->evidence.attr_held) {
        report->attr_attempted = 1;
        mbro_record_disposal(owner, report);
        report->attr_result = mbrs_attr_destroy(&owner->attr);
        report->attr_returned = 1;
        mbro_record_disposal(owner, report);
        if (report->attr_result != 0)
            return mbro_disposal_failed(owner, report);
        owner->evidence.attr_held = 0;
    }
    report->completed = 1;
    report->result = MB_OK;
    report->owner_released = 1;
    mbro_record_disposal(owner, report);
    *slot = NULL;
    free(owner);
    return MB_OK;
}
