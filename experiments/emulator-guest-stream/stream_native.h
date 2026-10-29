/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_GUEST_STREAM_NATIVE_H
#define EMULATOR_GUEST_STREAM_NATIVE_H
#include <stddef.h>
#include <sys/types.h>
struct mbgs_io_result { ssize_t count; int error; };
struct mbgs_close_result { int result; int error; };
/* Thin POSIX boundary; fixtures replace these symbols. No retries or signal
 * policy changes. A positive/zero return records error0; only -1 saves errno. */
__attribute__((visibility("hidden"))) struct mbgs_io_result mbgs_sys_read(int, void *, size_t);
__attribute__((visibility("hidden"))) struct mbgs_io_result mbgs_sys_write(int, const void *, size_t);
__attribute__((visibility("hidden"))) struct mbgs_close_result mbgs_sys_close(int);
#endif
