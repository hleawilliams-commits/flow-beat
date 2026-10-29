/* SPDX-License-Identifier: GPL-2.0-only */
#include "claim.h"
#include "subucom_ownership.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Actual pinned native64 Linux UAPI. Host fixtures provide explicit Linux ABI
 * shims; no encoding is inferred from a host macOS ioctl implementation. */
_Static_assert(sizeof(void *) == 8, "native64 pointers");
_Static_assert(AZ_OVERLAY_VERSION == 2, "V2");
_Static_assert(MB_FRAME_BYTES == AZ_OVERLAY_BYTES, "frame bytes");
_Static_assert(sizeof(struct az_overlay_command) == MBCO_COMMAND_BYTES, "command size");
_Static_assert(sizeof(struct az_overlay_state) == MBCO_STATE_BYTES, "state size");
_Static_assert(offsetof(struct az_overlay_command, version) == 0, "command version offset");
_Static_assert(offsetof(struct az_overlay_command, reserved) == 4, "command reserved offset");
_Static_assert(offsetof(struct az_overlay_command, instance) == 8, "command instance offset");
_Static_assert(offsetof(struct az_overlay_command, run_id) == 24, "command run offset");
_Static_assert(offsetof(struct az_overlay_command, epoch) == 32, "command epoch offset");
_Static_assert(offsetof(struct az_overlay_command, sequence) == 40, "command sequence offset");
_Static_assert(offsetof(struct az_overlay_state, version) == 0, "state version offset");
_Static_assert(offsetof(struct az_overlay_state, flags) == 4, "state flags offset");
_Static_assert(offsetof(struct az_overlay_state, instance) == 8, "state instance offset");
_Static_assert(offsetof(struct az_overlay_state, run_id) == 24, "state run offset");
_Static_assert(offsetof(struct az_overlay_state, epoch) == 32, "state epoch offset");
_Static_assert(offsetof(struct az_overlay_state, sequence) == 40, "state sequence offset");
_Static_assert(offsetof(struct az_overlay_state, crc_len) == 48, "state CRC offset");
_Static_assert(offsetof(struct az_overlay_state, reserved) == 52, "state reserved offset");
_Static_assert(offsetof(struct az_overlay_state, frame) == 56, "state frame offset");
_Static_assert(AZ_OVERLAY_CLAIM == 0x40305a50u, "CLAIM encoding");
_Static_assert(AZ_OVERLAY_STATE == 0x80d05a53u, "STATE encoding");
_Static_assert(AZ_OVERLAY_FENCED == 1u && AZ_OVERLAY_ACTIVE == 2u &&
               AZ_OVERLAY_CALLER_OWNER == 4u && AZ_OVERLAY_INJECTED == 8u &&
               AZ_OVERLAY_ENABLED == 16u, "V2 flags");
_Static_assert(MB_OWNED_FLAGS == 23u && MB_REQUIRED_FLAGS == 31u &&
               MB_INJECTED_FLAG == 8u, "semantic flags");

struct mbco_owner { struct mbco_evidence evidence; };

static int mbco_pre_valid(const struct mb_state *state)
{
    if (!(state->instance[0] || state->instance[1]) || state->crc_len != 96 ||
        state->epoch == UINT64_MAX)
        return 0;
    if (state->flags == 16u || state->flags == 24u)
        return !state->run_id && !state->epoch && !state->sequence;
    if (state->flags == 17u || state->flags == 25u)
        return state->run_id && state->epoch;
    return 0;
}

static int mbco_post_valid(const struct mb_state *state)
{
    return (state->instance[0] || state->instance[1]) &&
        state->run_id && state->epoch && state->crc_len == 96 &&
        (state->flags == MB_OWNED_FLAGS || state->flags == MB_REQUIRED_FLAGS);
}

static int mbco_equal(const struct mb_state *left, const struct mb_state *right)
{
    return left->instance[0] == right->instance[0] &&
        left->instance[1] == right->instance[1] &&
        left->run_id == right->run_id && left->epoch == right->epoch &&
        left->sequence == right->sequence && left->flags == right->flags &&
        left->crc_len == right->crc_len &&
        memcmp(left->frame, right->frame, MB_FRAME_BYTES) == 0;
}

static int mbco_decode(const struct az_overlay_state *wire, struct mb_state *state)
{
    if (wire->version != AZ_OVERLAY_VERSION || wire->reserved) return EPROTO;
    state->instance[0] = wire->instance[0];
    state->instance[1] = wire->instance[1];
    state->run_id = wire->run_id;
    state->epoch = wire->epoch;
    state->sequence = wire->sequence;
    state->flags = wire->flags;
    state->crc_len = wire->crc_len;
    memcpy(state->frame, wire->frame, MB_FRAME_BYTES);
    return 0;
}

static int mbco_error(struct mba_sys_result call)
{
    return call.result == -1 && call.error > 0 ? call.error : EPROTO;
}

static int mbco_terminal(struct mbco_owner *owner, int error)
{
    owner->evidence.phase = MBCO_TERMINAL;
    if (!owner->evidence.terminal_error) owner->evidence.terminal_error = error;
    return error;
}

static int mbco_claim_rejected(int error)
{
    /* Exact pinned CLAIM/input-only precommit errors. EACCES belongs to other
     * mutators, not this CLAIM whitelist; EBUSY is definite rejection here. */
    switch (error) {
    case EBADF: case EPERM: case EOPNOTSUPP: case EFAULT: case EINVAL:
    case ESTALE: case EBUSY: case EOVERFLOW: case ENOTTY: return 1;
    default: return 0;
    }
}

int mbco_create(const struct mbco_plan *plan, struct mbco_owner **out)
{
    struct mbco_plan copy = {0};
    struct az_overlay_command command = {0};
    struct mbco_owner *owner;
    if (plan) copy = *plan;
    if (out) *out = NULL;
    if (!plan || !out || !copy.new_run || !mbco_pre_valid(&copy.expected_pre))
        return EINVAL;
    command.version = AZ_OVERLAY_VERSION;
    command.instance[0] = copy.expected_pre.instance[0];
    command.instance[1] = copy.expected_pre.instance[1];
    command.run_id = copy.new_run;
    command.epoch = copy.expected_pre.epoch;
    /* CLAIM requires command.sequence0 even when a fenced prestate ended at
     * UINT64_MAX. Do not increment or reuse the prior sequence. */
    owner = calloc(1, sizeof(*owner));
    if (!owner) return ENOMEM;
    owner->evidence.phase = MBCO_ALLOCATED;
    owner->evidence.plan = copy;
    owner->evidence.expected_post = copy.expected_pre;
    owner->evidence.expected_post.run_id = copy.new_run;
    owner->evidence.expected_post.epoch = copy.expected_pre.epoch + 1;
    owner->evidence.expected_post.sequence = 0;
    owner->evidence.expected_post.flags = MB_OWNED_FLAGS |
        (copy.expected_pre.flags & MB_INJECTED_FLAG);
    owner->evidence.retained_fd = -1;
    memcpy(owner->evidence.claim_command, &command, sizeof(command));
    *out = owner;
    return 0;
}

int mbco_take(struct mbco_owner *owner, int *fd)
{
    if (!owner || !fd) return EINVAL;
    if (owner->evidence.descriptor_held) return EALREADY;
    if (owner->evidence.phase != MBCO_ALLOCATED) return EPERM;
    if (*fd < 0) return EINVAL;
    owner->evidence.retained_fd = *fd;
    owner->evidence.descriptor_held = 1;
    owner->evidence.phase = MBCO_FD_RETAINED;
    *fd = -1;
    return 0;
}

int mbco_preflight(struct mbco_owner *owner)
{
    struct az_overlay_state wire = {0};
    struct mb_state state = {0};
    struct mba_sys_result call;
    if (!owner) return EINVAL;
    if (owner->evidence.preflight_attempted) return EALREADY;
    if (owner->evidence.phase != MBCO_FD_RETAINED ||
        !owner->evidence.descriptor_held)
        return EPERM;
    owner->evidence.preflight_attempted = 1;
    owner->evidence.phase = MBCO_PREFLIGHT_ARMED;
    call = mba_sys_ioctl(owner->evidence.retained_fd, AZ_OVERLAY_STATE, &wire);
    owner->evidence.preflight_result = call;
    memcpy(owner->evidence.preflight_wire, &wire, sizeof(wire));
    owner->evidence.preflight_returned = 1;
    if (call.result != 0) return mbco_terminal(owner, mbco_error(call));
    if (mbco_decode(&wire, &state) || !mbco_pre_valid(&state))
        return mbco_terminal(owner, EPROTO);
    if (!mbco_equal(&owner->evidence.plan.expected_pre, &state))
        return mbco_terminal(owner, ESTALE);
    owner->evidence.phase = MBCO_PRE_MATCHED;
    return 0;
}

int mbco_claim(struct mbco_owner *owner)
{
    struct az_overlay_command command;
    struct mba_sys_result call;
    if (!owner) return EINVAL;
    if (owner->evidence.claim_attempted) return EALREADY;
    if (owner->evidence.phase != MBCO_PRE_MATCHED ||
        !owner->evidence.descriptor_held)
        return EPERM;
    /* A mutable syscall wrapper receives its own copy; immutable authored
     * command bytes remain available even if that argument is overwritten. */
    memcpy(&command, owner->evidence.claim_command, sizeof(command));
    owner->evidence.claim_attempted = 1;
    owner->evidence.phase = MBCO_CLAIM_ARMED;
    call = mba_sys_ioctl(owner->evidence.retained_fd, AZ_OVERLAY_CLAIM, &command);
    owner->evidence.claim_result = call;
    owner->evidence.claim_returned = 1;
    if (call.result == 0) {
        owner->evidence.claim_outcome = MB_OUTCOME_COMMITTED;
        owner->evidence.claim_error = 0;
        owner->evidence.phase = MBCO_CLAIM_COMMITTED;
        return 0;
    }
    owner->evidence.claim_error = mbco_error(call);
    owner->evidence.claim_outcome =
        call.result == -1 && mbco_claim_rejected(call.error)
        ? MB_OUTCOME_REJECTED : MB_OUTCOME_UNKNOWN;
    return mbco_terminal(owner, owner->evidence.claim_error);
}

int mbco_postflight(struct mbco_owner *owner)
{
    struct az_overlay_state wire = {0};
    struct mb_state state = {0};
    struct mba_sys_result call;
    if (!owner) return EINVAL;
    if (owner->evidence.postflight_attempted) return EALREADY;
    if (owner->evidence.phase != MBCO_CLAIM_COMMITTED ||
        !owner->evidence.descriptor_held ||
        owner->evidence.claim_outcome != MB_OUTCOME_COMMITTED)
        return EPERM;
    owner->evidence.postflight_attempted = 1;
    owner->evidence.phase = MBCO_POSTFLIGHT_ARMED;
    call = mba_sys_ioctl(owner->evidence.retained_fd, AZ_OVERLAY_STATE, &wire);
    owner->evidence.postflight_result = call;
    memcpy(owner->evidence.postflight_wire, &wire, sizeof(wire));
    owner->evidence.postflight_returned = 1;
    if (call.result != 0) return mbco_terminal(owner, mbco_error(call));
    if (mbco_decode(&wire, &state) || !mbco_post_valid(&state))
        return mbco_terminal(owner, EPROTO);
    if (!mbco_equal(&owner->evidence.expected_post, &state))
        return mbco_terminal(owner, ESTALE);
    owner->evidence.validated_post = state;
    owner->evidence.validated_post_valid = 1;
    owner->evidence.phase = MBCO_READY;
    return 0;
}

int mbco_inspect(const struct mbco_owner *owner, struct mbco_evidence *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!owner || !out) return EINVAL;
    *out = owner->evidence;
    return 0;
}

int mbco_get_validated_post(const struct mbco_owner *owner, struct mb_state *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!owner || !out) return EINVAL;
    if (owner->evidence.phase != MBCO_READY) return EPERM;
    *out = owner->evidence.validated_post;
    return 0;
}

int mbco_dispose(struct mbco_owner **slot, struct mbco_close_report *report)
{
    struct mbco_owner *owner;
    if (report) memset(report, 0, sizeof(*report));
    if (!slot || !report) return EINVAL;
    owner = *slot;
    if (!owner) return 0;
    *slot = NULL;
    if (owner->evidence.descriptor_held) {
        report->attempted = 1;
        report->call = mba_sys_close(owner->evidence.retained_fd);
    }
    free(owner);
    return 0;
}
