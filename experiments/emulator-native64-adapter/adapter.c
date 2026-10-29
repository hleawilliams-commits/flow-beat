/* SPDX-License-Identifier: GPL-2.0-only */
#include "adapter.h"
#include "subucom_ownership.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The production build uses the pinned Linux UAPI; host tests supply explicit
 * Linux type/ioctl shims. Do not derive these encodings from a host macOS ABI. */
_Static_assert(sizeof(void *) == 8, "native64 pointers");
_Static_assert(AZ_OVERLAY_VERSION == 2, "V2");
_Static_assert(MB_FRAME_BYTES == AZ_OVERLAY_BYTES, "frame bytes");
_Static_assert(sizeof(struct az_overlay_command) == 48, "command size");
_Static_assert(sizeof(struct az_overlay_frame) == 200, "frame size");
_Static_assert(sizeof(struct az_overlay_restore) == 208, "restore size");
_Static_assert(sizeof(struct az_overlay_state) == 208, "state size");
_Static_assert(offsetof(struct az_overlay_command, instance) == 8, "instance offset");
_Static_assert(offsetof(struct az_overlay_command, run_id) == 24, "run offset");
_Static_assert(offsetof(struct az_overlay_command, epoch) == 32, "epoch offset");
_Static_assert(offsetof(struct az_overlay_command, sequence) == 40, "sequence offset");
_Static_assert(offsetof(struct az_overlay_frame, frame) == 48, "frame offset");
_Static_assert(offsetof(struct az_overlay_restore, injected) == 48, "mode offset");
_Static_assert(offsetof(struct az_overlay_restore, reserved) == 52, "reserved offset");
_Static_assert(offsetof(struct az_overlay_restore, frame) == 56, "restore frame offset");
_Static_assert(offsetof(struct az_overlay_state, frame) == 56, "state frame offset");
_Static_assert(AZ_OVERLAY_FRAME == 0x40c85a51u, "FRAME encoding");
_Static_assert(AZ_OVERLAY_ROTATE == 0x40c85a54u, "ROTATE encoding");
_Static_assert(AZ_OVERLAY_RESTORE == 0x40d05a55u, "RESTORE encoding");
_Static_assert(AZ_OVERLAY_STATE == 0x80d05a53u, "STATE encoding");
_Static_assert(MB_OWNED_FLAGS == (AZ_OVERLAY_FENCED | AZ_OVERLAY_ACTIVE |
               AZ_OVERLAY_CALLER_OWNER | AZ_OVERLAY_ENABLED), "owned flags");
_Static_assert(MB_REQUIRED_FLAGS == (MB_OWNED_FLAGS | AZ_OVERLAY_INJECTED), "injected flags");

struct mba_adapter { int fd; uint64_t instance[2], run_id; };
static int mba_state_valid(const struct mb_state *s)
{
    return (s->instance[0] || s->instance[1]) && s->run_id && s->epoch &&
        (s->flags == MB_OWNED_FLAGS || s->flags == MB_REQUIRED_FLAGS) && s->crc_len == 96;
}
static int mba_state_equal(const struct mb_state *a, const struct mb_state *b)
{
    return a->instance[0] == b->instance[0] && a->instance[1] == b->instance[1] &&
        a->run_id == b->run_id && a->epoch == b->epoch && a->sequence == b->sequence &&
        a->flags == b->flags && a->crc_len == b->crc_len &&
        memcmp(a->frame, b->frame, MB_FRAME_BYTES) == 0;
}
static int mba_error(struct mba_sys_result call)
{
    return call.result == -1 && call.error > 0 ? call.error : EPROTO;
}
static int mba_snapshot(void *context, struct mb_state *out)
{
    struct mba_adapter *a = context;
    struct az_overlay_state wire = {0};
    struct mb_state state = {0};
    struct mba_sys_result call;
    if (out) memset(out, 0, sizeof(*out));
    if (!a || !out) return EINVAL;
    call = mba_sys_ioctl(a->fd, AZ_OVERLAY_STATE, &wire);
    if (call.result != 0) return mba_error(call);
    if (wire.version != AZ_OVERLAY_VERSION || wire.reserved) return EPROTO;
    state.instance[0] = wire.instance[0];
    state.instance[1] = wire.instance[1];
    state.run_id = wire.run_id;
    state.epoch = wire.epoch;
    state.sequence = wire.sequence;
    state.flags = wire.flags;
    state.crc_len = wire.crc_len;
    memcpy(state.frame, wire.frame, MB_FRAME_BYTES);
    if (!mba_state_valid(&state)) return EPROTO;
    if (state.instance[0] != a->instance[0] || state.instance[1] != a->instance[1] ||
        state.run_id != a->run_id) return ESTALE;
    *out = state;
    return 0;
}
static int mba_precommit_errno(int error)
{
    /* Exact pinned input-only handler errors before any mutation. This is not
     * a transport-error classification or a claim about a different driver. */
    switch (error) {
    case EBADF: case EPERM: case EOPNOTSUPP: case EFAULT: case EINVAL:
    case ESTALE: case EACCES: case EOVERFLOW: case ENOTTY: return 1;
    default: return 0;
    }
}
static struct mb_backend_reply mba_submit(void *context, enum mb_operation op,
                                         const struct mb_command *command)
{
    struct mba_adapter *a = context;
    struct mb_command copy = {0};
    struct az_overlay_command header = {0};
    struct az_overlay_frame frame = {0};
    struct az_overlay_restore restore = {0};
    struct mba_sys_result call;
    unsigned long request;
    void *argument;
    if (command) copy = *command;
    if (!a || !command || !copy.epoch || !copy.sequence ||
        (op != MB_OP_FRAME && op != MB_OP_ROTATE && op != MB_OP_RESTORE) ||
        (op == MB_OP_RESTORE ? copy.injected > 1 : copy.injected != 1))
        return (struct mb_backend_reply){MB_OUTCOME_REJECTED, EINVAL};
    if (copy.instance[0] != a->instance[0] || copy.instance[1] != a->instance[1] ||
        copy.run_id != a->run_id)
        return (struct mb_backend_reply){MB_OUTCOME_REJECTED, ESTALE};
    header.version = AZ_OVERLAY_VERSION;
    header.instance[0] = copy.instance[0];
    header.instance[1] = copy.instance[1];
    header.run_id = copy.run_id;
    header.epoch = copy.epoch;
    header.sequence = copy.sequence;
    switch (op) {
    case MB_OP_FRAME:
    case MB_OP_ROTATE:
        frame.header = header;
        memcpy(frame.frame, copy.frame, MB_FRAME_BYTES);
        request = op == MB_OP_FRAME ? AZ_OVERLAY_FRAME : AZ_OVERLAY_ROTATE;
        argument = &frame;
        break;
    case MB_OP_RESTORE:
        restore.header = header;
        restore.injected = copy.injected;
        memcpy(restore.frame, copy.frame, MB_FRAME_BYTES);
        request = AZ_OVERLAY_RESTORE;
        argument = &restore;
        break;
    default:
        return (struct mb_backend_reply){MB_OUTCOME_REJECTED, EINVAL};
    }
    call = mba_sys_ioctl(a->fd, request, argument);
    if (call.result == 0) return (struct mb_backend_reply){MB_OUTCOME_COMMITTED, 0};
    if (call.result == -1 && mba_precommit_errno(call.error))
        return (struct mb_backend_reply){MB_OUTCOME_REJECTED, call.error};
    return (struct mb_backend_reply){MB_OUTCOME_UNKNOWN, mba_error(call)};
}
int mba_attach(int original_fd, const struct mb_state *expected,
               struct mba_adapter **out, struct mba_attach_report *report)
{
    struct mb_state copy = {0}, actual = {0};
    struct mba_adapter *a;
    struct mba_sys_result retained;
    int error;
    if (expected) copy = *expected;
    if (out) *out = NULL;
    if (report) memset(report, 0, sizeof(*report));
    if (!report) return EINVAL;
    if (!out || !expected || original_fd < 0 || !mba_state_valid(&copy)) {
        report->error = EINVAL;
        return EINVAL;
    }
    a = calloc(1, sizeof(*a));
    if (!a) { report->error = ENOMEM; return ENOMEM; }
    retained = mba_sys_duplicate(original_fd);
    if (retained.result < 0 || retained.result == original_fd) {
        /* A real F_DUPFD_CLOEXEC cannot return the still-open original fd.
         * Do not close that original if a broken syscall boundary claims it. */
        report->error = retained.result < 0 ? mba_error(retained) : EPROTO;
        free(a);
        return report->error;
    }
    a->fd = retained.result;
    a->instance[0] = copy.instance[0];
    a->instance[1] = copy.instance[1];
    a->run_id = copy.run_id;
    error = mba_snapshot(a, &actual);
    if (!error && !mba_state_equal(&copy, &actual)) error = ESTALE;
    if (error) {
        report->error = error;
        report->cleanup_attempted = 1;
        report->cleanup = mba_sys_close(a->fd);
        free(a);
        return error;
    }
    *out = a;
    return 0;
}
int mba_get_backend(struct mba_adapter *a, struct mb_backend *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!a || !out) return EINVAL;
    *out = (struct mb_backend){a, mba_snapshot, mba_submit};
    return 0;
}
int mba_dispose(struct mba_adapter **adapter, struct mba_close_report *report)
{
    struct mba_adapter *a;
    if (report) memset(report, 0, sizeof(*report));
    if (!adapter || !report) return EINVAL;
    a = *adapter;
    if (!a) return 0;
    *adapter = NULL;
    report->attempted = 1;
    report->call = mba_sys_close(a->fd);
    free(a);
    return 0;
}
