/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_RETAINED_BROKER_OWNER_H
#define EMULATOR_RETAINED_BROKER_OWNER_H
#include "broker.h"

struct mbro_owner;
enum mbro_phase {
    MBRO_ALLOCATED = 0,
    MBRO_ATTR_INIT_ARMED,
    MBRO_ATTR_RETAINED,
    MBRO_ATTR_SETTYPE_ARMED,
    MBRO_ATTR_CONFIGURED,
    MBRO_MUTEX_INIT_ARMED,
    MBRO_INITIALIZED,
    MBRO_VALIDATE_ARMED,
    MBRO_READY,
    MBRO_TERMINAL,
    MBRO_DISPOSING
};
struct mbro_dispose_report {
    uint64_t attempt_number; /* zero for no accepted owner disposal attempt */
    int mutex_attempted, mutex_returned, mutex_result;
    int attr_attempted, attr_returned, attr_result;
    int completed; /* result is final only when this is set */
    int result; /* MB_* result, raw pthread results retained separately */
    int owner_released;
};
struct mbro_evidence {
    enum mbro_phase phase;
    struct mb_backend backend; /* copied borrowed pointers, not owned context */
    struct mb_state expected;
    int initialize_attempted;
    int attr_init_attempted, attr_init_returned, attr_init_result;
    int attr_settype_attempted, attr_settype_returned, attr_settype_result;
    int mutex_init_attempted, mutex_init_returned, mutex_init_result;
    int attr_held, mutex_held; /* known successful init, not yet destroyed */
    int validation_attempted, validation_returned, validation_result;
    struct mb_state validation_state; /* initialized/returned callback value */
    int validation_matched;
    int terminal_error; /* first initialization/validation failure */
    int disposal_started;
    uint64_t disposal_attempts; /* bounded; overflow refuses without a call */
    int first_disposal_valid, last_disposal_valid;
    struct mbro_dispose_report first_disposal, last_disposal;
};

/* All APIs return existing MB_* results. Allocate and copy backend/expected
 * only: no pthread or backend call. Reject an invalid expected state or missing
 * callback with MB_ERR_INVALID and *out NULL. The caller must retain the owner
 * before initialization. Output storage must be disjoint from inputs.
 * Backend context/callbacks remain borrowed: caller retains their actual owner
 * through all broker uses and disposal. No fd, CLAIM or native authority is
 * acquired by copying these pointers. */
int mbro_create(const struct mb_backend *backend,
                const struct mb_state *expected, struct mbro_owner **out);
/* One attempt: attr_init, settype(ERRORCHECK), mutex_init. Arm each call first;
 * retain every raw result and each successfully initialized object. The attr
 * remains held even after successful mutex initialization. A failure retains
 * everything already acquired, performs no cleanup and returns MB_ERR_MUTEX.
 * Repeated or wrong-phase calls return MB_ERR_STATE without calls or journal
 * changes. Once disposal starts, setup and export return MB_ERR_CLOSED. */
int mbro_initialize(struct mbro_owner *owner);
/* One snapshot attempt after initialization, armed before the callback. Retain
 * its zero-initialized/returned complete semantic value and raw callback result
 * before interpretation. Nonzero result is MB_ERR_BACKEND; malformed/different
 * state is MB_ERR_STATE. Only exact full correspondence reaches Ready.
 * Failure retains owner, mutex, attr and borrowed backend; no implicit destroy. */
int mbro_validate(struct mbro_owner *owner);
/* Ready only. The returned pointer borrows the embedded frozen broker for its
 * existing mb_* methods/dispatcher. Do not call legacy mb_destroy on it: that
 * lifecycle implementation is not linked. The owner must outlive every use.
 * The first disposal attempt permanently invalidates ALL previously exported
 * broker borrows, even when disposal fails and the owner remains retained. */
int mbro_get_broker(struct mbro_owner *owner, struct mb_broker **out);
/* Pure bounded owner-journal copy: no mutex lock, mb_inspect or backend call.
 * All lifecycle calls require external serialization and no state-changing
 * reentry. Same-thread synchronous wrapper/callback inspection is permitted.
 * This is not a concurrent operational snapshot or borrowed-context custody. */
int mbro_inspect(const struct mbro_owner *owner, struct mbro_evidence *out);
/* Explicit disposal requires independently established external quiescence:
 * no broker aliases, outstanding methods/callbacks or future borrowed uses may
 * remain, and operation uncertainty must be independently resolved/accounted
 * for. The first accepted call permanently closes owner export/admission.
 * Never use a prior broker pointer after this call, including on failure.
 *
 * Destroy each remaining initialized resource at most once per call: mutex
 * first, then attr only after mutex success. A failure retains *owner, storage,
 * all remaining resources, and the raw first/last reports. After mutex success
 * plus attr failure the core mutex is gone; pure mbro_inspect remains permitted.
 * A later explicit call may retry ONLY under renewed independent quiescence
 * and resolution; retained state is not permission or proof of a safe retry.
 * On full success consume *owner and free storage. No backend close, CLAIM,
 * RELEASE, restore, synchronization retry or implicit cleanup is performed.
 * At UINT64_MAX accepted attempts refuse with MB_ERR_OVERFLOW and no wrapper
 * call or first/last report replacement. NULL *owner returns MB_OK/no attempt.
 * owner/report storage must be disjoint. Capture needed evidence before free. */
int mbro_dispose(struct mbro_owner **owner, struct mbro_dispose_report *report);
#endif
