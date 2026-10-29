/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "syscalls.h"
#if !defined(__linux__) || __SIZEOF_POINTER__ != 8
#error "The production syscall boundary requires native64 Linux"
#endif
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
/* Every wrapper calls libc once and captures errno immediately only on error.
 * No EINTR retry: mutator uncertainty belongs to the retained broker receipt. */
struct mba_sys_result mba_sys_duplicate(int fd)
{
    int rc = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    int saved = rc == -1 ? errno : 0;
    return (struct mba_sys_result){rc, saved};
}
struct mba_sys_result mba_sys_ioctl(int fd, unsigned long request, void *argument)
{
    int rc = ioctl(fd, request, argument);
    int saved = rc == -1 ? errno : 0;
    return (struct mba_sys_result){rc, saved};
}
struct mba_sys_result mba_sys_close(int fd)
{
    int rc = close(fd);
    int saved = rc == -1 ? errno : 0;
    return (struct mba_sys_result){rc, saved};
}
