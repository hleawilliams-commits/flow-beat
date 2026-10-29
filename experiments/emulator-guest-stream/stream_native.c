/* SPDX-License-Identifier: GPL-2.0-only */
#include "stream_native.h"
#include <errno.h>
#include <unistd.h>
struct mbgs_io_result mbgs_sys_read(int fd, void *buffer, size_t length)
{
    ssize_t count = read(fd, buffer, length);
    int saved = count == -1 ? errno : 0;
    return (struct mbgs_io_result){count, saved};
}
struct mbgs_io_result mbgs_sys_write(int fd, const void *buffer, size_t length)
{
    ssize_t count = write(fd, buffer, length);
    int saved = count == -1 ? errno : 0;
    return (struct mbgs_io_result){count, saved};
}
struct mbgs_close_result mbgs_sys_close(int fd)
{
    int result = close(fd);
    int saved = result == -1 ? errno : 0;
    return (struct mbgs_close_result){result, saved};
}
