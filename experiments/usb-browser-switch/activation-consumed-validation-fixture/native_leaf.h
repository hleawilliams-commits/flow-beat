/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OF_NATIVE_LEAF_H
#define OF_NATIVE_LEAF_H
#include "file_provider.h"
/* Each leaf performs exactly one selected libc call. It captures raw return and
 * failure errno immediately before writing its caller-owned result record.
 * Query identity bytes are written only after that pair has been captured. */
void of_leaf_open(int directory_fd, int flags, of_native_result *result);
void of_leaf_write(int descriptor, const char *payload, size_t length, of_native_result *result);
void of_leaf_sync(int descriptor, of_native_result *result);
void of_leaf_close(int descriptor, of_native_result *result);
void of_leaf_fstat(int descriptor, of_native_result *result, of_identity *identity);
void of_leaf_fstatat(int directory_fd, of_native_result *result, of_identity *identity);
#endif
