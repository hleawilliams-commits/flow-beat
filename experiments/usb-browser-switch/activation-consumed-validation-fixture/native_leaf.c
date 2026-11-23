/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE 1
#include "native_leaf.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

_Static_assert(CHAR_BIT == 8, "eight-bit bytes required");
_Static_assert(sizeof(int) == 4 && INT_MAX == 2147483647 && INT_MIN == (-2147483647-1),
               "selected Linux int ABI required");
_Static_assert(sizeof(size_t) == 8 && SIZE_MAX == UINT64_MAX, "64-bit size_t required");
_Static_assert(sizeof(ssize_t) == 8 && SSIZE_MAX == INT64_MAX, "64-bit ssize_t required");
_Static_assert(sizeof(int64_t) == 8 && INT64_MAX >= INT_MAX, "exact descriptor widening required");
#define OF_LEAF __attribute__((noinline,noclone))

static void of_finish_native(of_native_result *out, int64_t raw, int error_number)
{
    out->raw = raw;
    out->error_valid = raw == -1;
    out->error_number = raw == -1 ? error_number : 0;
    out->returned = true;
}
static void of_copy_identity(of_identity *out, const struct stat *value)
{
    out->device = value->st_dev;
    out->inode = value->st_ino;
    out->uid = value->st_uid;
    out->links = value->st_nlink;
    out->mode = value->st_mode;
    out->size = value->st_size;
}
OF_LEAF void of_leaf_open(int directory_fd, int flags, of_native_result *out)
{
    out->entered = true;
    int raw = openat(directory_fd, OF_FILE_NAME, flags, (mode_t)0600);
    int saved_errno = raw == -1 ? errno : 0;
    of_finish_native(out, (int64_t)raw, saved_errno);
}
OF_LEAF void of_leaf_write(int descriptor, const char *payload, size_t length,
                           of_native_result *out)
{
    out->entered = true;
    ssize_t raw = write(descriptor, payload, length);
    int saved_errno = raw == -1 ? errno : 0;
    of_finish_native(out, (int64_t)raw, saved_errno);
}
OF_LEAF void of_leaf_sync(int descriptor, of_native_result *out)
{
    out->entered = true;
    int raw = fsync(descriptor);
    int saved_errno = raw == -1 ? errno : 0;
    of_finish_native(out, (int64_t)raw, saved_errno);
}
OF_LEAF void of_leaf_close(int descriptor, of_native_result *out)
{
    out->entered = true;
    int raw = close(descriptor);
    int saved_errno = raw == -1 ? errno : 0;
    of_finish_native(out, (int64_t)raw, saved_errno);
}
OF_LEAF void of_leaf_fstat(int descriptor, of_native_result *out, of_identity *identity)
{
    struct stat value;
    out->entered = true;
    int raw = fstat(descriptor, &value);
    int saved_errno = raw == -1 ? errno : 0;
    of_finish_native(out, (int64_t)raw, saved_errno);
    if (raw == 0) of_copy_identity(identity, &value);
}
OF_LEAF void of_leaf_fstatat(int directory_fd, of_native_result *out, of_identity *identity)
{
    struct stat value;
    out->entered = true;
    int raw = fstatat(directory_fd, OF_FILE_NAME, &value, AT_SYMLINK_NOFOLLOW);
    int saved_errno = raw == -1 ? errno : 0;
    of_finish_native(out, (int64_t)raw, saved_errno);
    if (raw == 0) of_copy_identity(identity, &value);
}
