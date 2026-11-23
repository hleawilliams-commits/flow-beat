/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE 1
#include "file_provider.h"
#include "native_leaf.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>

#define OF_METHOD __attribute__((noinline,noclone))

static ac_io_value of_infra_value(void)
{
    return (ac_io_value){ -2, OF_INFRA_REFUSAL };
}
static void of_fail(of_context *context, of_infrastructure why)
{
    if (context && context->report.infrastructure == OF_INFRA_NONE)
        context->report.infrastructure = why;
}
static bool of_canonical(const char id[AC_ID_BYTES], size_t length)
{
    if (length == 0 || length >= AC_ID_BYTES) return false;
    for (size_t i=0; i<length; ++i) {
        unsigned char c=(unsigned char)id[i];
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
              (c>='0' && c<='9') || c=='.' || c=='_' || c=='-')) return false;
    }
    for (size_t i=length; i<AC_ID_BYTES; ++i) if (id[i] != 0) return false;
    return true;
}
static bool of_owner_valid(ac_owner owner)
{
    size_t n=0;
    while (n<AC_ID_BYTES && owner.id[n]) ++n;
    return owner.kind == AC_COMMUNITY && owner.index < MA_MODULES &&
           owner.process_id != 0 && of_canonical(owner.id,n);
}
static bool of_same_identity(const of_identity *a, const of_identity *b)
{
    return a->device == b->device && a->inode == b->inode;
}
static bool of_file_identity(const of_identity *id, uid_t uid)
{
    return S_ISREG(id->mode) && id->uid == uid && id->links == (nlink_t)1;
}
static of_callback_record *of_begin_callback(of_context *context, of_operation operation)
{
    if (!context) return NULL;
    of_report *r=&context->report;
    if (!r->setup_complete || r->frozen || context->cleanup_requested) {
        of_fail(context,OF_INFRA_PHASE);
        return NULL;
    }
    if (r->callback_count >= OF_CALLBACK_CAPACITY) {
        of_fail(context,OF_INFRA_CAPACITY);
        return NULL;
    }
    of_callback_record *call=&r->callbacks[r->callback_count++];
    call->operation=operation;
    call->entered=true;
    call->native_first=r->native_count;
    call->custody_before=r->custody;
    if (operation != OF_UNBLAMED) ++r->file_callback_count;
    return call;
}
static ac_io_value of_prepare_return(of_context *context, of_callback_record *call,
                                     of_origin origin, ac_io_value value)
{
    call->origin=origin;
    call->supplied=value;
    call->native_count=context->report.native_count-call->native_first;
    call->custody_after=context->report.custody;
    call->prepared=true;
    /* This log is before the ordinary callback return. It cannot establish that
     * the caller returned or the engine recorded/published the value. */
    return value;
}
static of_native_record *of_reserve_native(of_context *context, of_operation operation)
{
    of_report *r=&context->report;
    if (r->native_count >= OF_NATIVE_CAPACITY) {
        of_fail(context,OF_INFRA_CAPACITY);
        return NULL;
    }
    of_native_record *record=&r->native[r->native_count++];
    record->operation=operation;
    record->descriptor=-1;
    return record;
}
static of_query_record *of_reserve_query(of_context *context, of_query_kind kind)
{
    of_report *r=&context->report;
    if (r->query_count >= OF_QUERY_CAPACITY) {
        of_fail(context,OF_INFRA_CAPACITY);
        return NULL;
    }
    of_query_record *record=&r->queries[r->query_count++];
    record->kind=kind;
    record->descriptor=-1;
    return record;
}
static ac_io_value of_pair(const of_native_result *result)
{
    return (ac_io_value){result->raw, result->error_valid ? result->error_number : 0};
}
OF_METHOD bool of_token_eligible(const of_context *context, int64_t token)
{
    /* One combined veto precedes every narrowing and actual follow-up call. */
    return token >= 0 && token <= INT_MAX && context->report.has_saved_descriptor &&
           token == (int64_t)context->report.saved_descriptor &&
           context->report.custody == OF_VERIFIED_HELD;
}
/* The explicit method boundary is evidence-only: it adds no authority or call. */
static OF_METHOD bool of_validate_created(of_context *context, const of_native_record *native)
{
    of_query_record *by_fd=of_reserve_query(context,OF_QUERY_FILE_FD);
    if (!by_fd) return false;
    by_fd->descriptor=context->report.saved_descriptor;
    of_leaf_fstat(by_fd->descriptor,&by_fd->result,&by_fd->identity);
    by_fd->identity_available=by_fd->result.returned && by_fd->result.raw==0;
    of_query_record *by_name=of_reserve_query(context,OF_QUERY_FILE_NAME);
    if (!by_name) return false;
    by_name->descriptor=context->directory_fd;
    by_name->flags=AT_SYMLINK_NOFOLLOW;
    of_leaf_fstatat(context->directory_fd,&by_name->result,&by_name->identity);
    by_name->identity_available=by_name->result.returned && by_name->result.raw==0;
    if (!by_fd->identity_available || !by_name->identity_available ||
        !of_same_identity(&by_fd->identity,&by_name->identity) ||
        !of_file_identity(&by_fd->identity,context->report.expected_uid) ||
        !of_file_identity(&by_name->identity,context->report.expected_uid)) return false;
    /* Complete raw evidence must precede the authored consumed decision. None of
     * these additional predicates changes a failed native query into success. */
    if (!context->raw_identity_reserved || context->raw_identity.complete ||
        context->consumed_validation.evaluated ||
        !context->report.setup_complete || !context->report.directory_validated ||
        !context->report.has_saved_descriptor ||
        context->report.custody != OF_OWNED_HELD_UNVALIDATED ||
        native->operation != OF_OPEN || native->descriptor != context->directory_fd ||
        native->flags != (O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC) ||
        native->creation_mode != (mode_t)0600 ||
        !native->result.entered || !native->result.returned ||
        native->result.raw < 0 || native->result.raw > INT_MAX ||
        native->result.raw != (int64_t)context->report.saved_descriptor ||
        native->result.error_valid || native->result.error_number != 0 ||
        by_fd->kind != OF_QUERY_FILE_FD ||
        by_fd->descriptor != context->report.saved_descriptor || by_fd->flags != 0 ||
        !by_fd->result.entered || !by_fd->result.returned ||
        by_fd->result.raw != 0 || by_fd->result.error_valid || by_fd->result.error_number != 0 ||
        by_name->kind != OF_QUERY_FILE_NAME ||
        by_name->descriptor != context->directory_fd || by_name->flags != AT_SYMLINK_NOFOLLOW ||
        !by_name->result.entered || !by_name->result.returned ||
        by_name->result.raw != 0 || by_name->result.error_valid || by_name->result.error_number != 0 ||
        by_fd->identity.size != 0 || by_name->identity.size != 0) return false;
    of_native_identity_receipt *receipt=&context->raw_identity;
    receipt->directory_fd=context->directory_fd;
    receipt->opened_fd=context->report.saved_descriptor;
    receipt->expected_uid=context->report.expected_uid;
    receipt->expected_directory=context->report.expected_directory;
    memcpy(receipt->fixed_name,OF_FILE_NAME,sizeof receipt->fixed_name);
    receipt->open_record=*native;
    receipt->by_fd=*by_fd;
    receipt->by_name=*by_name;
    receipt->complete=true; /* Ordinary serial complete-last validity, not atomic publication. */
    context->consumed_validation.evaluated=true;
    context->consumed_validation.raw_valid=true;
    context->consumed_validation.authored_veto=true;
    context->consumed_validation.consumed_valid=false;
    /* Deliberately refuse before accepted creation or VerifiedHeld promotion. */
    return false;
}
OF_METHOD ac_io_value of_open_callback(void *opaque, ac_owner owner)
{
    of_context *context=opaque;
    of_callback_record *call=of_begin_callback(context,OF_OPEN);
    if (!call) return of_infra_value();
    call->owner_argument=owner;
    if (!of_owner_valid(owner) || context->report.has_saved_descriptor ||
        context->report.custody != OF_NO_DESCRIPTOR) {
        of_fail(context,OF_INFRA_ARGUMENT);
        return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,of_infra_value());
    }
    /* Reserve the sole receipt as well as both prospective query records before
     * opening. It is context storage, never another attempt admission authority. */
    if (context->report.query_count > OF_QUERY_CAPACITY-2u ||
        context->raw_identity_reserved || context->raw_identity.complete ||
        context->consumed_validation.evaluated) {
        of_fail(context,OF_INFRA_CAPACITY);
        return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,of_infra_value());
    }
    of_native_record *native=of_reserve_native(context,OF_OPEN);
    if (!native) return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,of_infra_value());
    context->raw_identity_reserved=true;
    native->descriptor=context->directory_fd;
    native->flags=(context->report.mode==OF_READONLY ? O_RDONLY : O_WRONLY) |
                  O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC;
    native->creation_mode=(mode_t)0600;
    of_leaf_open(context->directory_fd,native->flags,&native->result);
    /* First custody stores after the native pair: no query/helper/instrumentation
     * intervenes. The leaf returned a real int, so this narrowing is exact. */
    if (native->result.raw >= 0) {
        context->report.saved_descriptor=(int)native->result.raw;
        context->report.has_saved_descriptor=true;
        context->report.custody=OF_OWNED_HELD_UNVALIDATED;
    }
    errno=OF_ERRNO_CLOBBER;
    if (native->result.raw >= 0 && !of_validate_created(context,native)) {
        of_fail(context,OF_INFRA_FILE_VALIDATION);
        ac_io_value supplied=of_infra_value();
#ifdef CV_CONTROL_NORMALIZE_REFUSAL
        if (context->raw_identity.complete && context->consumed_validation.evaluated &&
            context->consumed_validation.raw_valid && context->consumed_validation.authored_veto &&
            !context->consumed_validation.consumed_valid)
            supplied.raw=-1; /* Only the supplied domain changes; raw custody does not. */
#endif
        return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,supplied);
    }
    ac_io_value supplied=of_pair(&native->result);
    of_origin origin=OF_NATIVE;
    return of_prepare_return(context,call,origin,supplied);
}
OF_METHOD ac_io_value of_write_callback(void *opaque, ap_write_args args)
{
    of_context *context=opaque;
    of_callback_record *call=of_begin_callback(context,OF_WRITE);
    if (!call) return of_infra_value();
    call->token_argument=args.accepted_open_token;
    call->requested_length=args.requested_length;
    memcpy(call->payload_argument,args.payload,AC_ID_BYTES);
    if (!of_token_eligible(context,args.accepted_open_token))
        return of_prepare_return(context,call,OF_PROVIDER_REFUSAL,(ac_io_value){-1,OF_TOKEN_REFUSAL});
    if (!of_canonical(args.payload,args.requested_length)) {
        of_fail(context,OF_INFRA_ARGUMENT);
        return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,of_infra_value());
    }
    of_native_record *native=of_reserve_native(context,OF_WRITE);
    if (!native) return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,of_infra_value());
    native->descriptor=(int)args.accepted_open_token;
    native->requested_length=args.requested_length;
    memcpy(native->payload,args.payload,AC_ID_BYTES);
    of_leaf_write(native->descriptor,native->payload,native->requested_length,&native->result);
    errno=OF_ERRNO_CLOBBER;
    ac_io_value supplied=of_pair(&native->result);
    of_origin origin=OF_NATIVE;
    if (context->report.mode==OF_INVALID_WRITE_MODE &&
        native->result.raw == (int64_t)args.requested_length) {
        supplied=(ac_io_value){(int64_t)args.requested_length+1,0};
        origin=OF_SUPPLIED_INVALID;
    }
    return of_prepare_return(context,call,origin,supplied);
}
OF_METHOD ac_io_value of_sync_callback(void *opaque, int64_t token)
{
    of_context *context=opaque;
    of_callback_record *call=of_begin_callback(context,OF_SYNC);
    if (!call) return of_infra_value();
    call->token_argument=token;
    if (!of_token_eligible(context,token))
        return of_prepare_return(context,call,OF_PROVIDER_REFUSAL,(ac_io_value){-1,OF_TOKEN_REFUSAL});
    of_native_record *native=of_reserve_native(context,OF_SYNC);
    if (!native) return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,of_infra_value());
    native->descriptor=(int)token;
    of_leaf_sync(native->descriptor,&native->result);
    errno=OF_ERRNO_CLOBBER;
    return of_prepare_return(context,call,OF_NATIVE,of_pair(&native->result));
}
OF_METHOD ac_io_value of_close_callback(void *opaque, int64_t token)
{
    of_context *context=opaque;
    of_callback_record *call=of_begin_callback(context,OF_CLOSE);
    if (!call) return of_infra_value();
    call->token_argument=token;
    if (!of_token_eligible(context,token))
        return of_prepare_return(context,call,OF_PROVIDER_REFUSAL,(ac_io_value){-1,OF_TOKEN_REFUSAL});
    if (context->report.mode==OF_CLOSE_REFUSAL_MODE)
        return of_prepare_return(context,call,OF_SUPPLIED_REFUSAL,(ac_io_value){-1,OF_CLOSE_REFUSAL});
    of_native_record *native=of_reserve_native(context,OF_CLOSE);
    if (!native) return of_prepare_return(context,call,OF_INFRASTRUCTURE_REFUSAL,of_infra_value());
    native->descriptor=(int)token;
    context->report.custody=OF_UNKNOWN_ENTERED;
    of_leaf_close(native->descriptor,&native->result);
    context->report.custody=native->result.raw==0 ? OF_CLOSED_RETURNED : OF_UNKNOWN_AFTER_CLOSE_RETURN;
    errno=OF_ERRNO_CLOBBER;
    return of_prepare_return(context,call,OF_NATIVE,of_pair(&native->result));
}
OF_METHOD void of_unblamed_callback(void *opaque)
{
    of_context *context=opaque;
    of_callback_record *call=of_begin_callback(context,OF_UNBLAMED);
    if (!call) return;
    ++context->report.unblamed_count;
    (void)of_prepare_return(context,call,OF_OPAQUE_AUTHORED,(ac_io_value){0,0});
}
const ap_provider_table of_provider_table={
    of_open_callback,of_write_callback,of_sync_callback,of_close_callback,of_unblamed_callback
};
OF_METHOD bool of_prepare(of_context *context, int directory_fd,
                          const of_identity *expected, uid_t expected_uid, of_mode mode)
{
    if (!context || !expected || directory_fd<0 || (unsigned)mode>(unsigned)OF_INVALID_WRITE_MODE) return false;
    if (context->report.setup_complete || context->report.query_count ||
        context->report.callback_count || context->report.native_count || context->report.frozen ||
        context->report.has_saved_descriptor || context->cleanup_requested ||
        context->raw_identity_reserved || context->raw_identity.complete ||
        context->consumed_validation.evaluated || context->consumed_validation.raw_valid ||
        context->consumed_validation.authored_veto || context->consumed_validation.consumed_valid) return false;
    /* Preparation establishes complete stable reachable functions without entering
     * the attempt, reading its state or creating a second admission authority. */
    if (!of_provider_table.open || !of_provider_table.write || !of_provider_table.sync ||
        !of_provider_table.close || !of_provider_table.unblamed) return false;
    context->directory_fd=directory_fd;
    context->report.mode=mode;
    context->report.expected_directory=*expected;
    context->report.expected_uid=expected_uid;
    context->report.saved_descriptor=-1;
    of_query_record *query=of_reserve_query(context,OF_QUERY_DIRECTORY);
    if (!query) return false;
    query->descriptor=directory_fd;
    of_leaf_fstat(directory_fd,&query->result,&query->identity);
    query->identity_available=query->result.returned && query->result.raw==0;
    if (!query->identity_available || !S_ISDIR(query->identity.mode) ||
        !S_ISDIR(expected->mode) || !of_same_identity(&query->identity,expected) ||
        query->identity.uid!=expected_uid || expected->uid!=expected_uid ||
        (query->identity.mode & (mode_t)077)!=0) {
        of_fail(context,OF_INFRA_SETUP);
        return false;
    }
    context->report.directory_validated=true;
    context->report.setup_complete=true;
    return true;
}
OF_METHOD bool of_freeze(of_context *context, of_report *precleanup,
                         of_native_identity_receipt *raw_identity,
                         of_consumed_validation_record *consumed_validation)
{
    /* The caller supplies distinct, nonaliasing sibling outputs with stable storage. */
    if (!context || !precleanup || !raw_identity || !consumed_validation ||
        context->report.frozen || context->cleanup_requested) return false;
    context->report.frozen=true;
    *precleanup=context->report;
    *raw_identity=context->raw_identity;
    *consumed_validation=context->consumed_validation;
#ifdef CV_CONTROL_CORRUPT_QUERY_EXPORT
    if (raw_identity->complete && consumed_validation->evaluated &&
        consumed_validation->raw_valid && consumed_validation->authored_veto &&
        !consumed_validation->consumed_valid)
        precleanup->queries[1].result.raw=-1; /* Only this export field changes. */
#endif
    return true;
}
OF_METHOD bool of_cleanup_held(of_context *context, of_cleanup_record *out)
{
    if (!context || !out || !context->report.frozen || context->cleanup_requested) return false;
    context->cleanup_requested=true;
    of_cleanup_record *cleanup=&context->cleanup;
    cleanup->requested=true;
    cleanup->descriptor=context->report.has_saved_descriptor ? context->report.saved_descriptor : -1;
    cleanup->before=context->report.custody;
    cleanup->after=cleanup->before;
    cleanup->eligible=context->report.has_saved_descriptor && cleanup->descriptor>=0 &&
        (cleanup->before==OF_OWNED_HELD_UNVALIDATED || cleanup->before==OF_VERIFIED_HELD);
    if (cleanup->eligible) {
#ifndef CV_CONTROL_OMIT_HELD_CLEANUP
        cleanup->attempted=true;
        context->report.custody=OF_UNKNOWN_ENTERED;
        of_leaf_close(cleanup->descriptor,&cleanup->result);
        context->report.custody=cleanup->result.raw==0 ? OF_CLOSED_RETURNED : OF_UNKNOWN_AFTER_CLOSE_RETURN;
        cleanup->after=context->report.custody;
#endif
    }
    *out=*cleanup;
    return true;
}
