/* SPDX-License-Identifier: GPL-3.0-only
 * Experimental entry-local value copy, not a native pointer validator.
 * Caller must supply a valid native-owned task for the entire entry copy.
 * No hook, publication queue, identity allocator or navigation service here. */
#ifndef TC_CAPTURE_H
#define TC_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#ifdef __cplusplus
#define TC_STATIC_ASSERT static_assert
#else
#define TC_STATIC_ASSERT _Static_assert
#endif
TC_STATIC_ASSERT(CHAR_BIT==8 && sizeof(uintptr_t)==8, "64-bit byte-addressed host required");
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Exact firmware fields require little endian"
#endif

#define TC_SUBMIT_LR UINT64_C(0x1b01e04)
#define TC_EXECUTE_LR UINT64_C(0x237d800)
#define TC_TASK_VPTR UINT64_C(0x2d444d8)
/* Test seam for exact read-range accounting. It must behave as memcpy;
 * arbitrary/unmapped input remains invalid even with the production default. */
#ifndef TC_READ_BYTES
#define TC_READ_BYTES(dst, src, len) memcpy((dst),(src),(len))
#endif

enum tc_phase { TC_SUBMISSION=1, TC_EXECUTION=2 };
enum tc_copy_status { TC_COPIED=0, TC_BAD_PHASE, TC_BAD_CALLER, TC_NO_SCOPE,
    TC_NO_INVOCATION, TC_NULL_TASK, TC_BAD_ALIGNMENT, TC_ADDRESS_OVERFLOW, TC_WRONG_TYPE };
struct tc_values { uint64_t task_no, id; uint32_t delta_bits, cumulative_bits; };
struct tc_context {
    struct tc_values values;
    uint64_t run_id, invocation_id, thread_observation;
    uint32_t phase, status;
    uint64_t reserved;
};
TC_STATIC_ASSERT(sizeof(struct tc_context)==64, "wrapper context contract");
TC_STATIC_ASSERT(offsetof(struct tc_context,run_id)==24 &&
               offsetof(struct tc_context,phase)==48 &&
               offsetof(struct tc_context,reserved)==56, "context offsets");

static inline void tc_begin(struct tc_context *c, uint32_t phase, uint64_t caller_lr,
                            const void *task, uint64_t run_id,
                            uint64_t invocation_id, uint64_t thread_observation) {
    memset(c,0,sizeof *c);
    c->phase=phase; c->run_id=run_id; c->invocation_id=invocation_id;
    c->thread_observation=thread_observation;
    if (phase!=TC_SUBMISSION && phase!=TC_EXECUTION) { c->status=TC_BAD_PHASE; return; }
    if (caller_lr!=(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR)) { c->status=TC_BAD_CALLER; return; }
    if (!run_id) { c->status=TC_NO_SCOPE; return; }
    if (!invocation_id) { c->status=TC_NO_INVOCATION; return; }
    if (!task) { c->status=TC_NULL_TASK; return; }
    uintptr_t address=(uintptr_t)task;
    if (address & 7) { c->status=TC_BAD_ALIGNMENT; return; }
    if (address>UINTPTR_MAX-0x30) { c->status=TC_ADDRESS_OVERFLOW; return; }
    const unsigned char *p=(const unsigned char *)task;
    uint64_t vptr;
    TC_READ_BYTES(&vptr,p,8);
    if (vptr!=TC_TASK_VPTR) { c->status=TC_WRONG_TYPE; return; }
    TC_READ_BYTES(&c->values.task_no,p+0x10,8);
    TC_READ_BYTES(&c->values.id,p+0x20,8);
    TC_READ_BYTES(&c->values.delta_bits,p+0x28,4);
    TC_READ_BYTES(&c->values.cumulative_bits,p+0x2c,4);
    c->status=TC_COPIED;
}

enum tc_return_relation { TC_RETURN_UNQUALIFIED=0, TC_RETURN_ZERO,
    TC_RETURN_VALUE_MISMATCH, TC_RETURN_SAME_NONZERO_VALUE };
struct tc_return_record {
    struct tc_context entry;
    uint64_t native_result;
    uint32_t relation, reserved;
};
/* Uses only caller-owned pre-call values; no task address is stored or accepted. */
static inline struct tc_return_record tc_return(const struct tc_context *entry,
                                               uint64_t native_result) {
    struct tc_return_record out;
    memset(&out,0,sizeof out);
    out.entry=*entry;
    out.native_result=native_result;
    if (entry->phase!=TC_SUBMISSION || entry->status!=TC_COPIED ||
        !entry->run_id || !entry->invocation_id)
        out.relation=TC_RETURN_UNQUALIFIED;
    else if (!native_result) out.relation=TC_RETURN_ZERO;
    else if (native_result!=entry->values.task_no) out.relation=TC_RETURN_VALUE_MISMATCH;
    else out.relation=TC_RETURN_SAME_NONZERO_VALUE;
    return out;
}

/* A matching value is only a candidate: no native identity, admission,
 * cancellation, successful handling or completion state is represented. */
enum tc_candidate_relation { TC_MISSING=0, TC_INELIGIBLE, TC_OTHER_SCOPE,
    TC_ZERO_TASKNO, TC_DIFFERENT_VALUES, TC_SAME_VALUES_ONLY };
static inline enum tc_candidate_relation tc_candidate(const struct tc_context *post,
                                                       const struct tc_context *execute) {
    if (!post || !execute) return TC_MISSING;
    if (post->status!=TC_COPIED || execute->status!=TC_COPIED ||
        post->phase!=TC_SUBMISSION || execute->phase!=TC_EXECUTION ||
        !post->run_id || !execute->run_id || !post->invocation_id ||
        !execute->invocation_id) return TC_INELIGIBLE;
    if (post->run_id!=execute->run_id) return TC_OTHER_SCOPE;
    if (!post->values.task_no || !execute->values.task_no) return TC_ZERO_TASKNO;
    if (post->values.task_no!=execute->values.task_no || post->values.id!=execute->values.id ||
        post->values.delta_bits!=execute->values.delta_bits ||
        post->values.cumulative_bits!=execute->values.cumulative_bits) return TC_DIFFERENT_VALUES;
    return TC_SAME_VALUES_ONLY;
}
#undef TC_STATIC_ASSERT
#endif
