/* SPDX-License-Identifier: GPL-2.0-only */
/* The generated include is the exact frozen adapter.c byte prefix preceding
 * int mba_attach(. It supplies unchanged ABI assertions, the private core and
 * snapshot/submit classification. No legacy public lifecycle is linked here. */
#include "native64-core.inc"
#include "retained.h"

_Static_assert(sizeof(struct az_overlay_state) == MBRA_STATE_BYTES,
               "retained STATE diagnostic size");

struct mbra_owner {
    struct mba_adapter core;
    struct mbra_evidence evidence;
};

static int mbra_terminal(struct mbra_owner *owner, int error)
{
    owner->evidence.phase = MBRA_TERMINAL;
    if (!owner->evidence.terminal_error)
        owner->evidence.terminal_error = error;
    return error;
}

int mbra_create(int original_fd, const struct mb_state *expected,
                struct mbra_owner **out)
{
    struct mb_state copy = {0};
    struct mbra_owner *owner;
    if (expected) copy = *expected;
    if (out) *out = NULL;
    if (!out || !expected || original_fd < 0 || !mba_state_valid(&copy))
        return EINVAL;
    owner = calloc(1, sizeof(*owner));
    if (!owner) return ENOMEM;
    owner->core.fd = -1;
    owner->core.instance[0] = copy.instance[0];
    owner->core.instance[1] = copy.instance[1];
    owner->core.run_id = copy.run_id;
    owner->evidence.phase = MBRA_ALLOCATED;
    owner->evidence.original_fd = original_fd;
    owner->evidence.expected = copy;
    owner->evidence.retained_fd = -1;
    *out = owner;
    return 0;
}

int mbra_duplicate(struct mbra_owner *owner)
{
    struct mba_sys_result call;
    if (!owner) return EINVAL;
    if (owner->evidence.duplicate_attempted) return EALREADY;
    owner->evidence.duplicate_attempted = 1;
    owner->evidence.phase = MBRA_DUPLICATE_ARMED;
    call = mba_sys_duplicate(owner->evidence.original_fd);
    owner->evidence.duplicate_result = call;
    owner->evidence.duplicate_returned = 1;
    if (call.result < 0)
        return mbra_terminal(owner, mba_error(call));
    if (call.result == owner->evidence.original_fd)
        return mbra_terminal(owner, EPROTO);
    /* A successful return is the retained descriptor, even with stale errno.
     * No allocation or fallible operation separates this publication from it. */
    owner->core.fd = call.result;
    owner->evidence.retained_fd = call.result;
    owner->evidence.descriptor_held = 1;
    owner->evidence.phase = MBRA_DUPLICATE_RETAINED;
    return 0;
}

int mbra_validate(struct mbra_owner *owner)
{
    struct az_overlay_state wire;
    struct mb_state state = {0};
    struct mba_sys_result call;
    if (!owner) return EINVAL;
    if (owner->evidence.validation_attempted) return EALREADY;
    if (owner->evidence.phase != MBRA_DUPLICATE_RETAINED ||
        !owner->evidence.descriptor_held)
        return EPERM;
    memset(&wire, 0, sizeof(wire));
    owner->evidence.validation_attempted = 1;
    owner->evidence.phase = MBRA_VALIDATE_ARMED;
    call = mba_sys_ioctl(owner->core.fd, AZ_OVERLAY_STATE, &wire);
    owner->evidence.validation_result = call;
    memcpy(owner->evidence.validation_wire, &wire, sizeof(wire));
    owner->evidence.validation_returned = 1;
    if (call.result != 0)
        return mbra_terminal(owner, mba_error(call));
    if (wire.version != AZ_OVERLAY_VERSION || wire.reserved)
        return mbra_terminal(owner, EPROTO);
    state.instance[0] = wire.instance[0];
    state.instance[1] = wire.instance[1];
    state.run_id = wire.run_id;
    state.epoch = wire.epoch;
    state.sequence = wire.sequence;
    state.flags = wire.flags;
    state.crc_len = wire.crc_len;
    memcpy(state.frame, wire.frame, MB_FRAME_BYTES);
    if (!mba_state_valid(&state))
        return mbra_terminal(owner, EPROTO);
    if (!mba_state_equal(&owner->evidence.expected, &state))
        return mbra_terminal(owner, ESTALE);
    owner->evidence.validated_state = state;
    owner->evidence.validated_state_valid = 1;
    owner->evidence.phase = MBRA_READY;
    return 0;
}

int mbra_inspect(const struct mbra_owner *owner, struct mbra_evidence *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!owner || !out) return EINVAL;
    *out = owner->evidence;
    return 0;
}

static int mbra_snapshot(void *context, struct mb_state *out)
{
    struct mbra_owner *owner = context;
    if (out) memset(out, 0, sizeof(*out));
    if (!owner || !out) return EINVAL;
    if (owner->evidence.phase != MBRA_READY) return EPERM;
    return mba_snapshot(&owner->core, out);
}

static struct mb_backend_reply mbra_submit(void *context, enum mb_operation op,
                                           const struct mb_command *command)
{
    struct mbra_owner *owner = context;
    if (!owner) return (struct mb_backend_reply){MB_OUTCOME_REJECTED, EINVAL};
    if (owner->evidence.phase != MBRA_READY)
        return (struct mb_backend_reply){MB_OUTCOME_REJECTED, EPERM};
    return mba_submit(&owner->core, op, command);
}

int mbra_get_backend(struct mbra_owner *owner, struct mb_backend *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!owner || !out) return EINVAL;
    if (owner->evidence.phase != MBRA_READY) return EPERM;
    *out = (struct mb_backend){owner, mbra_snapshot, mbra_submit};
    return 0;
}

int mbra_dispose(struct mbra_owner **slot, struct mba_close_report *report)
{
    struct mbra_owner *owner;
    if (report) memset(report, 0, sizeof(*report));
    if (!slot || !report) return EINVAL;
    owner = *slot;
    if (!owner) return 0;
    *slot = NULL;
    if (owner->evidence.descriptor_held) {
        report->attempted = 1;
        report->call = mba_sys_close(owner->core.fd);
    }
    free(owner);
    return 0;
}
