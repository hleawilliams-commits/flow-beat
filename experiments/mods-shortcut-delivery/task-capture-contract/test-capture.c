/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned checks, failures, scenarios, read_count, forbidden_reads;
static const char *scenario;
static const unsigned char *live_task;
static size_t offsets[64], widths[64];
static void tracked_read(void *to,const void *from,size_t n) {
    uintptr_t p=(uintptr_t)from, base=(uintptr_t)live_task;
    size_t offset=p>=base ? p-base : SIZE_MAX;
    int allowed=(offset==0 && n==8) || (offset==0x10 && n==8) ||
                (offset==0x20 && n==8) || (offset==0x28 && n==4) ||
                (offset==0x2c && n==4);
    forbidden_reads+=!allowed;
    if (read_count<64) { offsets[read_count]=offset; widths[read_count]=n; }
    read_count++;
    memcpy(to,from,n);
}
#define TC_READ_BYTES tracked_read
#include "capture.h"
#define CHECK(x,name) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr,"FAIL %s/%s\n",scenario,name); } } while (0)
static void start(const char *name) { scenario=name; scenarios++; }
static void task_init(unsigned char *p,uint64_t number,uint64_t id,uint32_t delta,uint32_t count) {
    memset(p,0xa5,56);
    uint64_t vptr=TC_TASK_VPTR;
    memcpy(p,&vptr,8); memcpy(p+0x10,&number,8); memcpy(p+0x20,&id,8);
    memcpy(p+0x28,&delta,4); memcpy(p+0x2c,&count,4);
    live_task=p; read_count=0;
}
static struct tc_context capture(unsigned char *p,uint32_t phase,uint64_t number,
                                  uint64_t id,uint32_t delta,uint32_t count,
                                  uint64_t invocation) {
    struct tc_context c;
    task_init(p,number,id,delta,count);
    tc_begin(&c,phase,phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR,p,17,invocation,23);
    return c;
}
static void gates(void) {
    start("gates");
    _Alignas(8) unsigned char p[56]; struct tc_context c;
    task_init(p,42,99,1,2);
    struct { uint32_t phase,status; uint64_t lr,scope,invocation; const void *p; } cases[]={
        {99,TC_BAD_PHASE,TC_SUBMIT_LR,17,1,p},
        {TC_SUBMISSION,TC_BAD_CALLER,TC_SUBMIT_LR+4,17,1,(void *)(uintptr_t)1},
        {TC_EXECUTION,TC_BAD_CALLER,TC_SUBMIT_LR,17,1,p},
        {TC_SUBMISSION,TC_NO_SCOPE,TC_SUBMIT_LR,0,1,p},
        {TC_SUBMISSION,TC_NO_INVOCATION,TC_SUBMIT_LR,17,0,p},
        {TC_SUBMISSION,TC_NULL_TASK,TC_SUBMIT_LR,17,1,NULL},
        {TC_SUBMISSION,TC_BAD_ALIGNMENT,TC_SUBMIT_LR,17,1,p+1},
        {TC_SUBMISSION,TC_ADDRESS_OVERFLOW,TC_SUBMIT_LR,17,1,(void *)(uintptr_t)(UINTPTR_MAX-7)},
    };
    for (unsigned i=0;i<sizeof cases/sizeof *cases;i++) {
        memset(&c,0x5a,sizeof c); read_count=0;
        tc_begin(&c,cases[i].phase,cases[i].lr,cases[i].p,cases[i].scope,cases[i].invocation,23);
        CHECK(c.status==cases[i].status,"rejection-reason");
        CHECK(read_count==0,"reject-before-native-read");
        struct tc_values zero={0};
        CHECK(!memcmp(&c.values,&zero,sizeof zero) && c.reserved==0,"rejection-no-stale-payload");
        struct tc_return_record r=tc_return(&c,42);
        CHECK(r.relation==TC_RETURN_UNQUALIFIED,"rejected-copy-has-no-qualified-return");
    }
    uint64_t wrong=TC_TASK_VPTR+8; memcpy(p,&wrong,8); read_count=0;
    tc_begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,p,17,1,23);
    CHECK(c.status==TC_WRONG_TYPE && read_count==1,"wrong-type-only-vptr-read");
}
static void exact_values(void) {
    start("exact-values"); _Alignas(8) unsigned char p[56];
    task_init(p,UINT64_C(0xfedcba9876543210),UINT64_C(0xabcdef0187654321),
              UINT32_C(0x80000000),UINT32_C(0xffffffff));
    unsigned char before[56]; memcpy(before,p,sizeof before);
    struct tc_context c;
    tc_begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,p,17,4,23);
    CHECK(!memcmp(p,before,sizeof p),"native-task-unchanged");
    CHECK(c.status==TC_COPIED,"copied");
    CHECK(c.values.task_no==UINT64_C(0xfedcba9876543210) &&
          c.values.id==UINT64_C(0xabcdef0187654321) &&
          c.values.delta_bits==UINT32_C(0x80000000) &&
          c.values.cumulative_bits==UINT32_C(0xffffffff),"full-width-values");
    const size_t expected_offsets[]={0,0x10,0x20,0x28,0x2c}, expected_widths[]={8,8,8,4,4};
    CHECK(read_count==5 && !memcmp(offsets,expected_offsets,sizeof expected_offsets) &&
          !memcmp(widths,expected_widths,sizeof expected_widths),"exact-five-read-ranges");
    CHECK(c.run_id==17 && c.invocation_id==4 && c.thread_observation==23 && !c.reserved,"observer-metadata");
    struct tc_return_record r=tc_return(&c,UINT64_C(0xfedcba9876543210));
    CHECK(r.native_result==UINT64_C(0xfedcba9876543210),"full-width-return");
    CHECK(r.relation==TC_RETURN_SAME_NONZERO_VALUE && !r.reserved,"matching-return-is-only-value-match");
    r=tc_return(&c,UINT64_C(0x1234567876543210));
    CHECK(r.relation==TC_RETURN_VALUE_MISMATCH,"high-bit-return-mismatch");
}
static void freed_before_return(void) {
    start("freed-before-return");
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *mapping=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if (mapping==MAP_FAILED) abort();
    unsigned char *p=mapping+page-56;
    struct tc_context c=capture(p,TC_SUBMISSION,75,99,1,2,5);
    unsigned reads_before=read_count;
    /* Synthetic native callee consumed/freed its task before returning. No
     * saved task pointer is available to tc_return's interface or context. */
    if (munmap(mapping,page)) abort();
    live_task=NULL;
    struct tc_return_record r=tc_return(&c,75);
    CHECK(r.entry.values.task_no==75 && r.entry.values.id==99,"pre-call-copy-survives-unmap");
    CHECK(r.relation==TC_RETURN_SAME_NONZERO_VALUE && r.entry.invocation_id==5,"return-retains-own-invocation");
    CHECK(read_count==reads_before,"no-post-return-native-read");
}
static void nested(void) {
    start("nested"); _Alignas(8) unsigned char a[56],b[56];
    struct tc_context outer=capture(a,TC_SUBMISSION,81,51,1,4,11);
    struct tc_context inner=capture(b,TC_SUBMISSION,81,52,2,5,12);
    struct tc_return_record ir=tc_return(&inner,81), or=tc_return(&outer,81);
    CHECK(ir.entry.invocation_id==12 && ir.entry.values.id==52,"inner-local-context");
    CHECK(or.entry.invocation_id==11 && or.entry.values.id==51,"outer-not-latest-context");
    CHECK(ir.relation==TC_RETURN_SAME_NONZERO_VALUE && or.relation==TC_RETURN_SAME_NONZERO_VALUE,
          "duplicate-taskno-does-not-pair-returns");
    memset(a,0,56); memset(b,0xff,56);
    CHECK(outer.values.id==51 && inner.values.id==52,"copies-independent-of-native-mutation");
}
static void candidate_limits(void) {
    start("candidate-limits"); _Alignas(8) unsigned char p[56];
    struct tc_context post=capture(p,TC_SUBMISSION,91,88,1,4,21);
    struct tc_context exec=capture(p,TC_EXECUTION,91,88,1,4,22);
    CHECK(tc_candidate(&post,&exec)==TC_SAME_VALUES_ONLY,"execution-before-return-allowed");
    struct tc_return_record r=tc_return(&post,91);
    CHECK(r.relation==TC_RETURN_SAME_NONZERO_VALUE,"later-return-separate-observation");
    CHECK(tc_return(&exec,91).relation==TC_RETURN_UNQUALIFIED,"executor-return-not-a-submission");
    CHECK(tc_candidate(&post,NULL)==TC_MISSING && tc_candidate(NULL,&exec)==TC_MISSING,
          "missing-is-not-cancellation");
    struct tc_context duplicate=exec; duplicate.invocation_id=23;
    CHECK(tc_candidate(&post,&exec)==TC_SAME_VALUES_ONLY &&
          tc_candidate(&post,&duplicate)==TC_SAME_VALUES_ONLY,"identical-duplicates-remain-multiple-candidates");
    duplicate.values.id++;
    CHECK(tc_candidate(&post,&duplicate)==TC_DIFFERENT_VALUES,"same-taskno-different-payload");
    duplicate=exec; duplicate.run_id++;
    CHECK(tc_candidate(&post,&duplicate)==TC_OTHER_SCOPE,"scope-reuse-not-joined");
    duplicate=exec; duplicate.invocation_id=0;
    CHECK(tc_candidate(&post,&duplicate)==TC_INELIGIBLE,"missing-observer-invocation");
    duplicate=exec; duplicate.status=TC_WRONG_TYPE;
    CHECK(tc_candidate(&post,&duplicate)==TC_INELIGIBLE,"capture-rejection-not-native-failure");
    CHECK(tc_return(&post,0).relation==TC_RETURN_ZERO,"zero-return-not-proof-of-cancellation");
    CHECK(tc_return(&post,92).relation==TC_RETURN_VALUE_MISMATCH,"return-mismatch-retained");
}
static void wrap_values(void) {
    start("wrap-values"); _Alignas(8) unsigned char p[56];
    const uint64_t values[]={UINT64_MAX,0,1};
    for (unsigned i=0;i<3;i++) {
        struct tc_context post=capture(p,TC_SUBMISSION,values[i],33,UINT32_C(0x7fffffff),8,31+i);
        struct tc_context exec=capture(p,TC_EXECUTION,values[i],33,UINT32_C(0x7fffffff),8,41+i);
        CHECK(post.status==TC_COPIED && post.values.task_no==values[i],"zero-and-wide-taskno-visible");
        CHECK(tc_candidate(&post,&exec)==(values[i]?TC_SAME_VALUES_ONLY:TC_ZERO_TASKNO),
              "wrap-like-values-do-not-prove-identity");
    }
    struct tc_context earlier=capture(p,TC_SUBMISSION,5,33,1,9,51);
    struct tc_context later=capture(p,TC_EXECUTION,4,33,1,9,52);
    CHECK(tc_candidate(&earlier,&later)==TC_DIFFERENT_VALUES,"decrease-not-interpreted-as-wrap");
}
int main(void) {
    gates(); exact_values(); freed_before_return(); nested(); candidate_limits(); wrap_values();
    scenario="all-reads";
    CHECK(forbidden_reads==0,"no-dispatcher-tag-or-padding-read");
    printf("checks=%u failures=%u scenarios=%u\n",checks,failures,scenarios);
    return failures?1:0;
}
