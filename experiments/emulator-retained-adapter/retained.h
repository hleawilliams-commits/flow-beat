/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_RETAINED_ADAPTER_H
#define EMULATOR_RETAINED_ADAPTER_H
#include "adapter.h"

#define MBRA_STATE_BYTES 208u
struct mbra_owner;
enum mbra_phase {
    MBRA_ALLOCATED = 0,
    MBRA_DUPLICATE_ARMED,
    MBRA_DUPLICATE_RETAINED,
    MBRA_VALIDATE_ARMED,
    MBRA_READY,
    MBRA_TERMINAL
};
struct mbra_evidence {
    enum mbra_phase phase;
    int original_fd; /* historical borrowed integer, not original-fd custody */
    struct mb_state expected;
    int descriptor_held;
    int retained_fd; /* diagnostic only; -1 absent, zero is a valid descriptor */
    int duplicate_attempted, duplicate_returned;
    struct mba_sys_result duplicate_result;
    int validation_attempted, validation_returned;
    struct mba_sys_result validation_result;
    /* Initialized before STATE; exact returned bytes even on a failed/partial
     * copy. Diagnostic bytes are never a usable semantic snapshot by themselves. */
    uint8_t validation_wire[MBRA_STATE_BYTES];
    int validated_state_valid;
    struct mb_state validated_state;
    int terminal_error; /* first failed attempted stage, never a later refusal */
};

/* Allocate and copy the immutable plan; no syscall. Original remains borrowed
 * and must stay open/stable until duplicate returns. A caller must retain the
 * returned owner before calling either stage. Outputs must be disjoint from
 * inputs. Invalid plans return EINVAL with *out NULL and no allocation/I/O.
 * This does not acquire/CLAIM an endpoint or authenticate the supplied state. */
int mbra_create(int original_fd, const struct mb_state *expected,
                struct mbra_owner **out);
/* Exactly one duplication attempt. An actual distinct nonnegative return is
 * retained immediately, including fd0. Failure keeps the owner and raw result;
 * no close/free, retry or alternative descriptor. Repeats return EALREADY. */
int mbra_duplicate(struct mbra_owner *owner);
/* Exactly one STATE attempt after successful duplication. Publish armed intent
 * before the syscall and raw return/bytes before interpretation. Only full exact
 * correspondence admits callbacks. Failure retains descriptor, owner and
 * evidence. Before duplication succeeds: EPERM without an attempt; after a
 * validation attempt: EALREADY. Refusals never overwrite acquisition evidence. */
int mbra_validate(struct mbra_owner *owner);
/* Pure bounded copy: no syscall or callback. All API calls and copied callbacks
 * require external serialization, excluding state-changing reentry. Inspection
 * from a synchronous same-thread syscall fixture is permitted. No concurrent
 * snapshot, cancellation, process-death or durable-retention guarantee exists. */
int mbra_inspect(const struct mbra_owner *owner, struct mbra_evidence *out);
/* Export only after Ready. Both copied callbacks also check readiness before
 * invoking the unchanged frozen core. This bounded acquisition journal remains
 * immutable after Ready; broker receipts retain later operation uncertainty.
 * Owner must outlive every backend copy, broker and callback. Only the inner
 * core pointer stays private. Inspection exposes actual descriptor numbers as
 * diagnostics; they remain usable in-process. Callers must not mutate, close or
 * duplicate through them. This API enforces no alias exclusion and establishes
 * no original-fd custody or native-stop authority. */
int mbra_get_backend(struct mbra_owner *owner, struct mb_backend *out);
/* Explicit terminal disposal ONLY under the frozen adapter's external
 * quiescence/resolution preconditions: no remaining broker/backend/callback
 * references, and operation uncertainty resolved/accounted for independently.
 * Consume *owner before one close iff a duplicate is held, then free storage.
 * Preserve raw close result; zero API return is not proof of successful close
 * or recovery. Never retry, release by ioctl, restore or reopen. Capture needed
 * evidence before this call; it is not checked teardown or original-fd custody.
 * owner and report storage must be disjoint. NULL *owner makes no attempt. */
int mbra_dispose(struct mbra_owner **owner, struct mba_close_report *report);
#endif
