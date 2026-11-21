/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef OF_FILE_PROVIDER_H
#define OF_FILE_PROVIDER_H
#include "core.h"
#include <sys/types.h>
#include <sys/stat.h>

#if defined(AC_FIXTURE) || defined(AP_CONTROL_EARLY_PROVIDER) || \
    defined(AP_CONTROL_WRONG_SUPPRESSED_RETURN) || defined(AP_CONTROL_OMIT_UNBLAMED) || \
    defined(AP_CONTROL_ALTERNATE_CONTEXT) || defined(AP_CONTROL_DECOY_WRITE) || \
    defined(AP_CONTROL_REPLACE_CLOSE_RETURN) || defined(AP_CONTROL_REPEAT_SHORT_WRITE) || \
    defined(AP_CONTROL_NARROW_OPEN_TOKEN)
#error "only the new ordinary-file controls are permitted"
#endif
#if (defined(OF_CONTROL_CONSUME_AMBIENT_ERROR) + defined(OF_CONTROL_CORRUPT_REPORTED_TOKEN) + \
     defined(OF_CONTROL_DECOY_OWNED_WRITE) + defined(OF_CONTROL_OMIT_NATIVE_SYNC) + \
     defined(OF_CONTROL_OMIT_NATIVE_CLOSE) + defined(OF_CONTROL_EXTRA_OWNED_WRITE)) > 1
#error "one ordinary-file control per executable"
#endif

#define OF_FILE_NAME "witness.dat"
#define OF_NATIVE_CAPACITY 8u
#define OF_CALLBACK_CAPACITY 8u
#define OF_QUERY_CAPACITY 4u
#define OF_TOKEN_REFUSAL 7101
#define OF_CLOSE_REFUSAL 7102
#define OF_ERRNO_CLOBBER 7103
#define OF_INFRA_REFUSAL 7104

typedef enum {
    OF_NORMAL=0, OF_READONLY=1, OF_CLOSE_REFUSAL_MODE=2, OF_INVALID_WRITE_MODE=3
} of_mode;
typedef enum {
    OF_NO_DESCRIPTOR=0, OF_OWNED_HELD_UNVALIDATED=1, OF_VERIFIED_HELD=2,
    OF_CLOSE_ENTERED=3, OF_CLOSED_RETURNED=4,
    OF_UNKNOWN_AFTER_CLOSE_RETURN=5, OF_UNKNOWN_ENTERED=6
} of_custody;
typedef enum {
    OF_NATIVE=0, OF_PROVIDER_REFUSAL=1, OF_SUPPLIED_REFUSAL=2,
    OF_SUPPLIED_INVALID=3, OF_SUPPLIED_SUCCESS=4, OF_INFRASTRUCTURE_REFUSAL=5,
    OF_OPAQUE_AUTHORED=6, OF_CONTROL_REPORTED=7
} of_origin;
typedef enum { OF_OPEN=0, OF_WRITE=1, OF_SYNC=2, OF_CLOSE=3, OF_UNBLAMED=4 } of_operation;
typedef enum { OF_QUERY_DIRECTORY=0, OF_QUERY_FILE_FD=1, OF_QUERY_FILE_NAME=2 } of_query_kind;
typedef enum {
    OF_INFRA_NONE=0, OF_INFRA_SETUP=1, OF_INFRA_CAPACITY=2,
    OF_INFRA_FILE_VALIDATION=3, OF_INFRA_ARGUMENT=4, OF_INFRA_PHASE=5
} of_infrastructure;
/* Native comparison types are preserved. Reporting conversions are a separate,
 * checked harness responsibility; these are not arbitrary wire integers. */
typedef struct {
    dev_t device;
    ino_t inode;
    uid_t uid;
    nlink_t links;
    mode_t mode;
    off_t size;
} of_identity;
typedef struct {
    bool entered, returned;
    int64_t raw;
    bool error_valid;
    int error_number;
} of_native_result;
typedef struct {
    of_operation operation;
    int descriptor;
    int flags;
    mode_t creation_mode;
    size_t requested_length;
    char payload[AC_ID_BYTES];
    of_native_result result;
} of_native_record;
typedef struct {
    of_query_kind kind;
    int descriptor;
    int flags;
    of_native_result result;
    bool identity_available;
    of_identity identity;
} of_query_record;
typedef struct {
    of_operation operation;
    bool entered, prepared;
    of_origin origin;
    ac_io_value supplied;
    ac_owner owner_argument;
    int64_t token_argument;
    size_t requested_length;
    char payload_argument[AC_ID_BYTES];
    unsigned native_first, native_count;
    of_custody custody_before, custody_after;
} of_callback_record;
typedef struct {
    bool setup_complete, directory_validated, frozen;
    of_mode mode;
    of_identity expected_directory;
    uid_t expected_uid;
    of_custody custody;
    bool has_saved_descriptor;
    int saved_descriptor;
    bool creation_identity_available;
    of_identity creation_identity;
    of_infrastructure infrastructure;
    unsigned callback_count, file_callback_count, unblamed_count;
    unsigned native_count, query_count;
    of_callback_record callbacks[OF_CALLBACK_CAPACITY];
    of_native_record native[OF_NATIVE_CAPACITY];
    of_query_record queries[OF_QUERY_CAPACITY];
} of_report;
typedef struct {
    bool requested, eligible, attempted;
    int descriptor;
    of_custody before, after;
    of_native_result result;
} of_cleanup_record;
typedef struct {
    int directory_fd;
    of_report report;
    bool cleanup_requested;
    of_cleanup_record cleanup;
} of_context;

extern const ap_provider_table of_provider_table;
/* Fresh zero/unused context only, before the attempt. Directory fd is inherited
 * from the same-Linux supervisor; setup records one actual fstat and opens no fd. */
bool of_prepare(of_context *context, int directory_fd,
                const of_identity *expected_directory, uid_t expected_uid, of_mode mode);
/* Pure bounded value copy: preserve this output before separate cleanup.
 * Call only after the synchronous engine call(s) have returned. */
bool of_freeze(of_context *context, of_report *precleanup);
/* No callback, engine entry or unknown/closed-token use. One actual close only
 * for still-held custody; its own raw outcome never changes precleanup evidence. */
bool of_cleanup_held(of_context *context, of_cleanup_record *out);
#endif
