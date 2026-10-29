/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_NATIVE64_ADAPTER_H
#define EMULATOR_NATIVE64_ADAPTER_H
#include "broker.h"
#include "syscalls.h"
struct mba_adapter;
struct mba_attach_report {
    int error; /* primary positive errno or zero; never replaced by cleanup */
    int cleanup_attempted;
    struct mba_sys_result cleanup;
};
struct mba_close_report { int attempted; struct mba_sys_result call; };
/* Duplicate the caller's already-claimed controller fd with CLOEXEC, then read
 * one complete owned STATE matching every expected field and byte. Original fd
 * must remain open/stable until attach returns, including failure cleanup.
 * No open, CLAIM, input, RELEASE, retry, reopen, or module identity discovery.
 * out and report must be distinct writable objects.
 * Returns zero or positive errno; clears outputs on every path. Success owns
 * only a new fd reference; other dup/fork/original writers remain possible. */
int mba_attach(int original_fd, const struct mb_state *expected,
               struct mba_adapter **out, struct mba_attach_report *report);
/* Immutable callback/context values; adapter must outlive every broker/user.
 * Calls must be serialized by one broker. This library supplies no mutex,
 * deadline, cancellation, cross-process exclusion or native-stop authority. */
int mba_get_backend(struct mba_adapter *adapter, struct mb_backend *out);
/* Explicit terminal lifecycle action, NOT recovery. Requires no remaining
 * brokers/callbacks referencing this adapter and caller-retained resolution of
 * uncertain operations. Consumes *adapter before one close attempt, then frees
 * storage; NEVER retries a failed close, releases by ioctl, queries or reopens.
 * Closing may be the final file release, leaving a sticky orphaned overlay.
 * adapter and report storage must be disjoint.
 * Zero means inspect report, not proven close/recovery success. Repeating with
 * a NULL *adapter reports no attempt. No callbacks auto-dispose on errors. */
int mba_dispose(struct mba_adapter **adapter, struct mba_close_report *report);
#endif
