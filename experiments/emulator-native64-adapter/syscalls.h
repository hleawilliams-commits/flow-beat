/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_NATIVE64_SYSCALLS_H
#define EMULATOR_NATIVE64_SYSCALLS_H
/* Internal native Linux libc boundary. Tests replace these three definitions;
 * production links native.c, never a transport or retrying shim. */
struct mba_sys_result { int result; int error; };
__attribute__((visibility("hidden"))) struct mba_sys_result mba_sys_duplicate(int fd);
__attribute__((visibility("hidden"))) struct mba_sys_result mba_sys_ioctl(int fd, unsigned long request, void *argument);
__attribute__((visibility("hidden"))) struct mba_sys_result mba_sys_close(int fd);
#endif
