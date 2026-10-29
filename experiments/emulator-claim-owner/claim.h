/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_CLAIM_OWNER_H
#define EMULATOR_CLAIM_OWNER_H
#include "broker.h"
#include "syscalls.h"

#define MBCO_STATE_BYTES 208u
#define MBCO_COMMAND_BYTES 48u

struct mbco_owner;
struct mbco_plan {
    struct mb_state expected_pre;
    uint64_t new_run;
};
enum mbco_phase {
    MBCO_ALLOCATED = 0,
    MBCO_FD_RETAINED,
    MBCO_PREFLIGHT_ARMED,
    MBCO_PRE_MATCHED,
    MBCO_CLAIM_ARMED,
    MBCO_CLAIM_COMMITTED,
    MBCO_POSTFLIGHT_ARMED,
    MBCO_READY,
    MBCO_TERMINAL
};
struct mbco_evidence {
    enum mbco_phase phase;
    struct mbco_plan plan;
    struct mb_state expected_post;
    int descriptor_held;
    int retained_fd; /* -1 absent, zero is valid; an actual usable fd number */
    int preflight_attempted, preflight_returned;
    struct mba_sys_result preflight_result;
    uint8_t preflight_wire[MBCO_STATE_BYTES];
    /* Constructed at create, never passed directly to a mutable ioctl argument. */
    uint8_t claim_command[MBCO_COMMAND_BYTES];
    int claim_attempted, claim_returned;
    struct mba_sys_result claim_result;
    enum mb_outcome claim_outcome;
    int claim_error;
    int postflight_attempted, postflight_returned;
    struct mba_sys_result postflight_result;
    uint8_t postflight_wire[MBCO_STATE_BYTES];
    int validated_post_valid;
    struct mb_state validated_post;
    int terminal_error; /* first failed attempted stage, never later refusal */
};
struct mbco_close_report {
    int attempted;
    struct mba_sys_result call;
};

/* All APIs return zero or a positive errno. Allocate and copy the immutable
 * plan and construct its zeroed native64 CLAIM command before taking any fd;
 * no I/O. Fresh prestate flags16/24 require run/epoch/sequence0. Fenced17/25
 * require nonzero run/epoch and allow any sequence. Both require nonzero
 * 128-bit instance, CRC96, epoch<UINT64_MAX; new_run must be nonzero.
 * Invalid plans leave *out NULL without allocation or I/O. Output storage
 * must be disjoint from inputs. This does not open/authenticate an endpoint. */
int mbco_create(const struct mbco_plan *plan, struct mbco_owner **out);
/* Transfer an already-open original descriptor, including fd0. No allocation
 * or I/O: retain the actual fd before setting *fd=-1. Caller must retain owner
 * first and have exclusive authority to transfer this fd slot. A successful
 * earlier take returns EALREADY; other refusals leave the caller slot intact.
 * This does not prove no other descriptor aliases the same file description. */
int mbco_take(struct mbco_owner *owner, int *fd);
/* Each stage has one attempt, armed before I/O and captured before interpreting
 * a return. STATE retains all208 initialized/returned bytes even on failure;
 * those diagnostic bytes alone are not a usable semantic snapshot.
 * Attempted-stage repeats return EALREADY, other wrong phases EPERM, without
 * I/O or rewriting evidence. Any attempted failure is terminal and keeps owner
 * and original fd. There is no implicit close, RELEASE, FRAME, retry or restore. */
int mbco_preflight(struct mbco_owner *owner);
/* Only after exact preflight. Return0 means CLAIM Committed, not Ready.
 * rc0 ignores stale errno. Exactly rc-1 with EBADF/EPERM/EOPNOTSUPP/EFAULT/
 * EINVAL/ESTALE/EBUSY/EOVERFLOW/ENOTTY is Rejected; every other failure is
 * Unknown. EBUSY can mean a same-file alias already owns the overlay; Rejected
 * says this command did not commit, not that this fd is unowned or safe to close.
 * The raw result and classified outcome remain independent of postflight. */
int mbco_claim(struct mbco_owner *owner);
/* Only after Committed. Require the full expected poststate, including all152
 * frame bytes. A failure leaves claim_outcome Committed and retains custody;
 * Unknown CLAIM cannot reach this stage or be promoted by a later snapshot. */
int mbco_postflight(struct mbco_owner *owner);
/* Pure bounded copies, no syscalls/callbacks. Calls require external
 * serialization and prohibit state-changing reentry. Synchronous same-thread
 * syscall-fixture inspection is permitted. No concurrency/durable-retention
 * guarantee. Descriptor diagnostics are actual usable numbers: callers must
 * not mutate, close or duplicate through them. Alias exclusion is not enforced. */
int mbco_inspect(const struct mbco_owner *owner, struct mbco_evidence *out);
/* Ready only; copied historical validated state, not fresh state, native-stop
 * authority, an adapter/backend, or proof that an alias has not changed it. */
int mbco_get_validated_post(const struct mbco_owner *owner, struct mb_state *out);
/* Explicit terminal disposal ONLY after external quiescence and independent
 * resolution/accounting for operation uncertainty, ownership and all aliases.
 * No outstanding operation or reference may remain. This precondition is not
 * checked here. Consume *owner before exactly one close iff a descriptor is
 * held, then free; never retry uncertain close. Zero API return means inspect
 * the raw report, not successful close, recovery or native-stop proof. Capture
 * needed evidence first. NULL *owner makes no attempt. Owner/report storage
 * must be disjoint. No implicit disposal is performed by any other API. */
int mbco_dispose(struct mbco_owner **owner, struct mbco_close_report *report);
#endif
