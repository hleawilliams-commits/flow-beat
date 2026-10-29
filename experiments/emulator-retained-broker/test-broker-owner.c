/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
/* Standalone retained lifecycle tests. Successful sync calls execute pthread;
 * injected errors occur BEFORE pthread and never simulate a busy live thread.
 * Backend values are owned fixture memory. No fd/native/device path exists. */
#include "broker_owner.h"
#include "sync.h"
#include "guest_dispatch.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned checks,failures;
static void ck(bool ok,const char *label)
{ checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);} }
static void pattern(uint8_t *p,unsigned seed)
{ for(unsigned i=0;i<MB_FRAME_BYTES;i++)p[i]=(uint8_t)(seed+23*i); }
static bool state_equal(const struct mb_state *a,const struct mb_state *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->sequence==b->sequence &&
        a->flags==b->flags && a->crc_len==b->crc_len &&
        !memcmp(a->frame,b->frame,MB_FRAME_BYTES);
}
static bool backend_equal(const struct mb_backend *a,const struct mb_backend *b)
{ return a->context==b->context && a->snapshot==b->snapshot && a->submit==b->submit; }
static bool report_equal(const struct mbro_dispose_report *a,const struct mbro_dispose_report *b)
{
    return a->attempt_number==b->attempt_number &&
        a->mutex_attempted==b->mutex_attempted && a->mutex_returned==b->mutex_returned &&
        a->mutex_result==b->mutex_result && a->attr_attempted==b->attr_attempted &&
        a->attr_returned==b->attr_returned && a->attr_result==b->attr_result &&
        a->completed==b->completed && a->result==b->result && a->owner_released==b->owner_released;
}
static bool evidence_equal(const struct mbro_evidence *a,const struct mbro_evidence *b)
{
#define SAME(n) (a->n==b->n)
    bool same=SAME(phase) && backend_equal(&a->backend,&b->backend) &&
        state_equal(&a->expected,&b->expected) && SAME(initialize_attempted) &&
        SAME(attr_init_attempted) && SAME(attr_init_returned) && SAME(attr_init_result) &&
        SAME(attr_settype_attempted) && SAME(attr_settype_returned) && SAME(attr_settype_result) &&
        SAME(mutex_init_attempted) && SAME(mutex_init_returned) && SAME(mutex_init_result) &&
        SAME(attr_held) && SAME(mutex_held) && SAME(validation_attempted) &&
        SAME(validation_returned) && SAME(validation_result) &&
        state_equal(&a->validation_state,&b->validation_state) && SAME(validation_matched) &&
        SAME(terminal_error) && SAME(disposal_started) && SAME(disposal_attempts) &&
        SAME(first_disposal_valid) && SAME(last_disposal_valid) &&
        report_equal(&a->first_disposal,&b->first_disposal) &&
        report_equal(&a->last_disposal,&b->last_disposal);
#undef SAME
    return same;
}
enum sync_kind { ATTR_INIT,ATTR_TYPE,MUTEX_INIT,MUTEX_DESTROY,ATTR_DESTROY,SYNC_COUNT };
static unsigned calls[SYNC_COUNT],real_calls[SYNC_COUNT];
static int injected[SYNC_COUNT],observed_type;
static struct mbro_owner *watch;
static struct mbro_owner **watch_slot;
static struct mbro_evidence seen[SYNC_COUNT];
static bool observed[SYNC_COUNT],published[SYNC_COUNT];
static void observe(enum sync_kind kind)
{
    calls[kind]++;
    if(watch){
        int rc=mbro_inspect(watch,&seen[kind]);
        observed[kind]=rc==MB_OK;
        published[kind]=watch_slot && *watch_slot==watch;
    }
}
int mbrs_attr_init(pthread_mutexattr_t *attr)
{
    observe(ATTR_INIT);if(injected[ATTR_INIT])return injected[ATTR_INIT];
    real_calls[ATTR_INIT]++;return pthread_mutexattr_init(attr);
}
int mbrs_attr_settype(pthread_mutexattr_t *attr,int type)
{
    observed_type=type;observe(ATTR_TYPE);if(injected[ATTR_TYPE])return injected[ATTR_TYPE];
    real_calls[ATTR_TYPE]++;return pthread_mutexattr_settype(attr,type);
}
int mbrs_mutex_init(pthread_mutex_t *mutex,const pthread_mutexattr_t *attr)
{
    observe(MUTEX_INIT);if(injected[MUTEX_INIT])return injected[MUTEX_INIT];
    real_calls[MUTEX_INIT]++;return pthread_mutex_init(mutex,attr);
}
int mbrs_mutex_destroy(pthread_mutex_t *mutex)
{
    observe(MUTEX_DESTROY);if(injected[MUTEX_DESTROY])return injected[MUTEX_DESTROY];
    real_calls[MUTEX_DESTROY]++;return pthread_mutex_destroy(mutex);
}
int mbrs_attr_destroy(pthread_mutexattr_t *attr)
{
    observe(ATTR_DESTROY);if(injected[ATTR_DESTROY])return injected[ATTR_DESTROY];
    real_calls[ATTR_DESTROY]++;return pthread_mutexattr_destroy(attr);
}
struct fake {
    struct mb_state state;
    unsigned snapshots,submits;
    int snapshot_result;
    size_t prefix;
    struct mbro_owner *owner;
    struct mbro_owner **published_slot;
    bool callback_observed,callback_published,input_zero;
    struct mbro_evidence callback_evidence;
    struct mb_command last_command;
};
static bool zero_bytes(const void *p,size_t n)
{ const uint8_t *b=p;for(size_t i=0;i<n;i++)if(b[i])return false;return true; }
static int fake_snapshot(void *context,struct mb_state *out)
{
    struct fake *f=context;f->snapshots++;
    f->input_zero=zero_bytes(out,sizeof(*out));
    if(f->owner){
        f->callback_observed=mbro_inspect(f->owner,&f->callback_evidence)==MB_OK;
        f->callback_published=f->published_slot && *f->published_slot==f->owner;
    }
    assert(f->prefix<=sizeof(*out));memcpy(out,&f->state,f->prefix);
    return f->snapshot_result;
}
static struct mb_backend_reply fake_submit(void *context,enum mb_operation op,const struct mb_command *command)
{
    struct fake *f=context;f->submits++;assert(op==MB_OP_FRAME && command);
    f->last_command=*command;
    f->state.instance[0]=command->instance[0];f->state.instance[1]=command->instance[1];
    f->state.run_id=command->run_id;f->state.epoch=command->epoch;
    f->state.sequence=command->sequence;f->state.flags=MB_REQUIRED_FLAGS;
    memcpy(f->state.frame,command->frame,MB_FRAME_BYTES);
    return(struct mb_backend_reply){MB_OUTCOME_COMMITTED,0};
}
static void reset(struct fake *f)
{
    memset(calls,0,sizeof calls);memset(real_calls,0,sizeof real_calls);
    memset(injected,0,sizeof injected);memset(seen,0,sizeof seen);
    memset(observed,0,sizeof observed);memset(published,0,sizeof published);
    watch=NULL;watch_slot=NULL;observed_type=-1;
    memset(f,0,sizeof(*f));
    f->state=(struct mb_state){.instance={UINT64_C(0x0102030405060708),UINT64_C(0x1112131415161718)},
        .run_id=7,.epoch=3,.sequence=5,.flags=MB_REQUIRED_FLAGS,.crc_len=96};
    pattern(f->state.frame,31);f->prefix=sizeof(f->state);
}
static struct mb_backend backend(struct fake *f)
{ return(struct mb_backend){f,fake_snapshot,fake_submit}; }
static struct mbro_owner *create(struct fake *f)
{
    struct mb_backend b=backend(f);struct mbro_owner *owner=NULL;
    assert(mbro_create(&b,&f->state,&owner)==MB_OK && owner);return owner;
}
static struct mbro_evidence evidence(struct mbro_owner *owner)
{
    struct mbro_evidence e={0};assert(mbro_inspect(owner,&e)==MB_OK);return e;
}
static bool unavailable(struct mbro_owner *owner,int result)
{
    struct mb_broker *b=(void*)(uintptr_t)1;
    return mbro_get_broker(owner,&b)==result && !b;
}
static unsigned sync_calls(void)
{ unsigned n=0;for(unsigned i=0;i<SYNC_COUNT;i++)n+=calls[i];return n; }
static void cleanup(struct mbro_owner **owner)
{
    /* All fixture calls have returned. Clearing an injected PRE-call refusal
     * establishes a new test-only explicit retry under known quiescence.
     * This is not recovery permission for an uncertain real pthread outcome. */
    memset(injected,0,sizeof injected);watch=NULL;watch_slot=NULL;
    if(owner && *owner){
        struct mbro_dispose_report r;
        assert(mbro_dispose(owner,&r)==MB_OK && !*owner && r.owner_released);
    }
}
static void ready(struct mbro_owner *owner)
{ assert(mbro_initialize(owner)==MB_OK);assert(mbro_validate(owner)==MB_OK); }
static bool setup_refusals(struct mbro_owner *owner,struct fake *f,int result)
{
    struct mbro_evidence before=evidence(owner);
    unsigned n=sync_calls(),snapshots=f->snapshots,submits=f->submits;
    int a=mbro_initialize(owner),b=mbro_validate(owner);
    struct mbro_evidence after=evidence(owner);
    return a==result && b==result && evidence_equal(&before,&after) &&
        sync_calls()==n && f->snapshots==snapshots && f->submits==submits;
}
/* Exactly one functional ck per selector, after safe fixture reclamation.
 * Setup assertions, compiler/sanitizer failures are not expected control results. */
static void allocated_case(void)
{
    struct fake f;reset(&f);struct mb_backend b=backend(&f),saved=b;
    struct mb_state expected=f.state,saved_state=expected;struct mbro_owner *owner=NULL;
    int rc=mbro_create(&b,&expected,&owner);
    b.snapshot=NULL;b.context=NULL;expected.frame[151]^=1;
    struct mbro_evidence e=evidence(owner);
    unsigned n=sync_calls();int early=mbro_validate(owner);struct mbro_evidence after=evidence(owner);
    bool ok=rc==MB_OK && owner && e.phase==MBRO_ALLOCATED &&
        backend_equal(&e.backend,&saved) && state_equal(&e.expected,&saved_state) &&
        !e.attr_held && !e.mutex_held && !e.initialize_attempted && !e.validation_attempted &&
        !e.disposal_started && early==MB_ERR_STATE && evidence_equal(&e,&after) &&
        unavailable(owner,MB_ERR_STATE) && !n && !sync_calls() && !f.snapshots && !f.submits;
    cleanup(&owner);ck(ok,"broker_owner_copies_borrowed_plan_before_any_sync_or_backend_call");
}
static void init_arm_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);watch=owner;watch_slot=&owner;
    int rc=mbro_initialize(owner);struct mbro_evidence e=evidence(owner);
    bool ok=rc==MB_OK && observed[ATTR_INIT] && observed[ATTR_TYPE] && observed[MUTEX_INIT] &&
        published[ATTR_INIT] && published[ATTR_TYPE] && published[MUTEX_INIT] &&
        seen[ATTR_INIT].phase==MBRO_ATTR_INIT_ARMED && seen[ATTR_INIT].initialize_attempted &&
        seen[ATTR_INIT].attr_init_attempted && !seen[ATTR_INIT].attr_init_returned &&
        !seen[ATTR_INIT].attr_held && seen[ATTR_TYPE].phase==MBRO_ATTR_SETTYPE_ARMED &&
        seen[ATTR_TYPE].attr_held && seen[ATTR_TYPE].attr_init_returned &&
        seen[ATTR_TYPE].attr_settype_attempted && !seen[ATTR_TYPE].attr_settype_returned &&
        seen[MUTEX_INIT].phase==MBRO_MUTEX_INIT_ARMED && seen[MUTEX_INIT].attr_held &&
        !seen[MUTEX_INIT].mutex_held && seen[MUTEX_INIT].mutex_init_attempted &&
        !seen[MUTEX_INIT].mutex_init_returned && observed_type==PTHREAD_MUTEX_ERRORCHECK &&
        e.phase==MBRO_INITIALIZED && e.attr_held && e.mutex_held &&
        calls[ATTR_INIT]==1 && calls[ATTR_TYPE]==1 && calls[MUTEX_INIT]==1 &&
        real_calls[ATTR_INIT]==1 && real_calls[ATTR_TYPE]==1 && real_calls[MUTEX_INIT]==1 &&
        !calls[ATTR_DESTROY] && !calls[MUTEX_DESTROY] && !f.snapshots &&
        unavailable(owner,MB_ERR_STATE);
    cleanup(&owner);ck(ok,"broker_owner_is_published_and_each_initialization_call_is_armed");
}
static void init_failures_case(void)
{
    bool ok=true;
    for(unsigned stage=ATTR_INIT;stage<=MUTEX_INIT;stage++){
        struct fake f;reset(&f);struct mbro_owner *owner=create(&f);
        injected[stage]=stage==ATTR_INIT?EAGAIN:EINVAL;int rc=mbro_initialize(owner);
        struct mbro_evidence e=evidence(owner);
        int raw=stage==ATTR_INIT?e.attr_init_result:stage==ATTR_TYPE?e.attr_settype_result:e.mutex_init_result;
        bool one=rc==MB_ERR_MUTEX && e.phase==MBRO_TERMINAL &&
            e.terminal_error==MB_ERR_MUTEX && e.initialize_attempted && raw==injected[stage] &&
            e.attr_held==(stage!=ATTR_INIT) && !e.mutex_held &&
            calls[ATTR_INIT]==1 && calls[ATTR_TYPE]==(stage>=ATTR_TYPE?1u:0u) &&
            calls[MUTEX_INIT]==(stage==MUTEX_INIT?1u:0u) && !real_calls[stage] &&
            !calls[ATTR_DESTROY] && !calls[MUTEX_DESTROY] && !f.snapshots && !f.submits &&
            unavailable(owner,MB_ERR_STATE) && setup_refusals(owner,&f,MB_ERR_STATE);
        ok=one&&ok;cleanup(&owner);
    }
    ck(ok,"broker_owner_initialization_failure_retains_prior_resources_without_cleanup");
}
static void validate_arm_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);
    assert(mbro_initialize(owner)==MB_OK);f.owner=owner;f.published_slot=&owner;
    int rc=mbro_validate(owner);struct mbro_evidence e=evidence(owner);
    bool ok=rc==MB_OK && f.callback_observed && f.callback_published && f.input_zero &&
        f.callback_evidence.phase==MBRO_VALIDATE_ARMED &&
        f.callback_evidence.validation_attempted && !f.callback_evidence.validation_returned &&
        f.callback_evidence.attr_held && f.callback_evidence.mutex_held &&
        !f.callback_evidence.validation_matched && e.phase==MBRO_READY &&
        e.validation_returned && !e.validation_result && e.validation_matched &&
        state_equal(&e.validation_state,&f.state) && f.snapshots==1 && !f.submits &&
        !calls[ATTR_DESTROY] && !calls[MUTEX_DESTROY];
    cleanup(&owner);ck(ok,"broker_owner_retains_storage_and_armed_journal_before_snapshot_callback");
}
static void validation_failure_case(void)
{
    bool ok=true;
    for(unsigned whole=0;whole<2;whole++){
        struct fake f;reset(&f);struct mbro_owner *owner=create(&f);
        assert(mbro_initialize(owner)==MB_OK);f.prefix=whole?sizeof(f.state):83;f.snapshot_result=EIO;
        struct mb_state expected={0};memcpy(&expected,&f.state,f.prefix);
        int rc=mbro_validate(owner);struct mbro_evidence e=evidence(owner);
        bool one=rc==MB_ERR_BACKEND && e.phase==MBRO_TERMINAL &&
            e.terminal_error==MB_ERR_BACKEND && e.validation_attempted && e.validation_returned &&
            e.validation_result==EIO && !memcmp(&e.validation_state,&expected,sizeof expected) &&
            !e.validation_matched && e.attr_held && e.mutex_held && f.input_zero &&
            f.snapshots==1 && !f.submits && !calls[ATTR_DESTROY] && !calls[MUTEX_DESTROY] &&
            unavailable(owner,MB_ERR_STATE) && setup_refusals(owner,&f,MB_ERR_STATE);
        ok=one&&ok;cleanup(&owner);
    }
    ck(ok,"broker_owner_snapshot_error_keeps_partial_or_complete_returned_state");
}
static void mismatch_case(void)
{
    bool ok=true;
    for(unsigned which=0;which<4;which++){
        struct fake f;reset(&f);struct mbro_owner *owner=create(&f);
        assert(mbro_initialize(owner)==MB_OK);
        if(which==0)f.state.frame[151]^=1;
        if(which==1)f.state.instance[1]^=1;
        if(which==2)f.state.flags=MB_OWNED_FLAGS;
        if(which==3)f.state.crc_len=95;
        int rc=mbro_validate(owner);struct mbro_evidence e=evidence(owner);
        bool one=rc==MB_ERR_STATE && e.terminal_error==MB_ERR_STATE &&
            e.phase==MBRO_TERMINAL && e.validation_returned && !e.validation_result &&
            state_equal(&e.validation_state,&f.state) && !e.validation_matched &&
            e.attr_held && e.mutex_held && f.snapshots==1 && !f.submits &&
            !calls[ATTR_DESTROY] && !calls[MUTEX_DESTROY] &&
            unavailable(owner,MB_ERR_STATE) && setup_refusals(owner,&f,MB_ERR_STATE);
        ok=one&&ok;cleanup(&owner);
    }
    ck(ok,"broker_owner_requires_full_matching_state_before_export");
}
static void ready_repeat_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);ready(owner);
    struct mbro_evidence before=evidence(owner);struct mb_broker *a=NULL,*b=NULL;
    int first=mbro_get_broker(owner,&a);bool refused=setup_refusals(owner,&f,MB_ERR_STATE);
    int second=mbro_get_broker(owner,&b);struct mbro_evidence after=evidence(owner);
    struct mb_status status={0};int inspected=a?mb_inspect(a,&status):MB_ERR_INVALID;
    bool ok=first==MB_OK && second==MB_OK && a && a==b && inspected==MB_OK &&
        refused && evidence_equal(&before,&after) && before.phase==MBRO_READY &&
        before.validation_matched && state_equal(&status.expected,&f.state) &&
        !status.admission_open && !status.grant_issued && status.next_request_id==1 &&
        f.snapshots==1 && !f.submits && sync_calls()==3;
    a=b=NULL;cleanup(&owner);ck(ok,"broker_owner_ready_export_and_repeats_do_not_revalidate_or_rewrite_evidence");
}
static struct mbw_header header(uint32_t kind,uint32_t size,uint64_t exchange)
{
    struct mbw_header h={.kind=kind,.length=size,.exchange=exchange};
    for(unsigned i=0;i<16;i++)h.session[i]=(uint8_t)(0x51+i);
    return h;
}
static void dispatcher_case(void)
{
    struct fake f;reset(&f);struct mb_state seed=f.state;struct mbro_owner *owner=create(&f);ready(owner);
    struct mbro_evidence before=evidence(owner);struct mb_broker *b=NULL;
    assert(mbro_get_broker(owner,&b)==MB_OK && b);
    struct mbw_header h=header(MBW_ADOPT_REQUEST,MBW_ADOPT_REQUEST_SIZE,1);
    struct mbw_dispatch *d=NULL;int created=mbw_dispatch_create(b,h.session,&seed,99,&d);
    unsigned constructor_snapshots=f.snapshots;
    uint8_t request[256],response[668];size_t n=0,written=0;
    struct mbw_adopt_request adopt={.state=seed,.producer_id=99};
    assert(mbw_encode_adopt_request(&h,&adopt,request,sizeof request,&n)==MBW_OK);
    int ar=d?mbw_dispatch_exchange(d,request,n,response,sizeof response,&written):MBWD_INVALID;
    struct mbw_adopt_response adopted={0};struct mbw_header parsed={0};
    int ad=mbw_decode_adopt_response(response,written,&parsed,&adopted);
    struct mb_request frame={.grant=adopted.grant,.request_id=1};pattern(frame.frame,97);
    h=header(MBW_FRAME_REQUEST,MBW_FRAME_REQUEST_SIZE,2);
    assert(mbw_encode_frame_request(&h,&frame,request,sizeof request,&n)==MBW_OK);
    int sr=d?mbw_dispatch_exchange(d,request,n,response,sizeof response,&written):MBWD_INVALID;
    struct mbw_frame_response submitted={0};
    int sd=mbw_decode_frame_response(response,written,&parsed,&submitted);
    struct mbro_evidence after=evidence(owner);struct mb_status status={0};
    int inspected=mb_inspect(b,&status);
    bool ok=created==MBWD_OK && constructor_snapshots==1 && ar==MBWD_OK && ad==MBW_OK &&
        !adopted.result && !adopted.provenance && adopted.grant.producer_id==99 &&
        sr==MBWD_OK && sd==MBW_OK && !submitted.result && !submitted.provenance &&
        submitted.receipt.outcome==MB_OUTCOME_COMMITTED &&
        submitted.receipt.operation==MB_OP_FRAME && !submitted.receipt.backend_error &&
        submitted.receipt.command.sequence==seed.sequence+1 &&
        !memcmp(submitted.receipt.command.frame,frame.frame,MB_FRAME_BYTES) &&
        state_equal(&submitted.receipt.expected_after,&f.state) &&
        inspected==MB_OK && state_equal(&status.expected,&f.state) &&
        f.snapshots==2 && f.submits==1 && evidence_equal(&before,&after) &&
        !calls[MUTEX_DESTROY] && !calls[ATTR_DESTROY];
    if(d)mbw_dispatch_destroy(d);
    b=NULL;cleanup(&owner);ck(ok,"broker_owner_ready_core_uses_actual_dispatcher_without_lifecycle_journal_changes");
}
static void dispose_empty_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);
    struct mbro_evidence before=evidence(owner);int invalid=mbro_dispose(&owner,NULL);
    struct mbro_evidence after=evidence(owner);struct mbro_dispose_report r,again,missing;
    int rc=mbro_dispose(&owner,&r);bool consumed=!owner;
    int repeat=mbro_dispose(&owner,&again),absent=mbro_dispose(NULL,&missing);
    bool ok=invalid==MB_ERR_INVALID && evidence_equal(&before,&after) &&
        rc==MB_OK && consumed && r.completed && r.owner_released && r.attempt_number==1 &&
        !r.mutex_attempted && !r.attr_attempted && !r.result &&
        repeat==MB_OK && again.completed && !again.owner_released && !again.attempt_number &&
        absent==MB_ERR_INVALID && missing.completed && missing.result==MB_ERR_INVALID &&
        !missing.attempt_number && !sync_calls() && !f.snapshots && !f.submits;
    if(r.owner_released)owner=NULL;
    cleanup(&owner);ck(ok,"broker_owner_empty_disposal_and_invalid_report_have_no_sync_effect");
}
static void dispose_init_failure_case(void)
{
    bool ok=true;
    for(unsigned stage=ATTR_INIT;stage<=MUTEX_INIT;stage++){
        struct fake f;reset(&f);struct mbro_owner *owner=create(&f);injected[stage]=EINVAL;
        assert(mbro_initialize(owner)==MB_ERR_MUTEX);
        struct mbro_evidence before=evidence(owner);memset(injected,0,sizeof injected);
        struct mbro_dispose_report r;int rc=mbro_dispose(&owner,&r);bool consumed=!owner;
        bool one=rc==MB_OK && consumed && r.owner_released && r.completed &&
            r.attempt_number==1 && !r.mutex_attempted && r.attr_attempted==(stage!=ATTR_INIT) &&
            calls[MUTEX_DESTROY]==0 && calls[ATTR_DESTROY]==(stage!=ATTR_INIT?1u:0u) &&
            real_calls[ATTR_DESTROY]==(stage!=ATTR_INIT?1u:0u) &&
            before.terminal_error==MB_ERR_MUTEX && !f.snapshots && !f.submits;
        ok=one&&ok;if(r.owner_released)owner=NULL;cleanup(&owner);
    }
    ck(ok,"broker_owner_disposes_only_resources_acquired_before_initialization_failure");
}
static void dispose_ready_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);ready(owner);
    watch=owner;watch_slot=&owner;struct mbro_dispose_report r;
    int rc=mbro_dispose(&owner,&r);bool consumed=!owner;
    bool ok=rc==MB_OK && consumed && r.completed && r.owner_released && !r.result &&
        r.attempt_number==1 && r.mutex_attempted && r.mutex_returned && !r.mutex_result &&
        r.attr_attempted && r.attr_returned && !r.attr_result &&
        observed[MUTEX_DESTROY] && published[MUTEX_DESTROY] && observed[ATTR_DESTROY] &&
        published[ATTR_DESTROY] && seen[MUTEX_DESTROY].disposal_started &&
        seen[MUTEX_DESTROY].phase==MBRO_DISPOSING && seen[MUTEX_DESTROY].mutex_held &&
        seen[MUTEX_DESTROY].last_disposal.mutex_attempted &&
        !seen[MUTEX_DESTROY].last_disposal.mutex_returned &&
        !seen[MUTEX_DESTROY].last_disposal.completed &&
        !seen[ATTR_DESTROY].mutex_held && seen[ATTR_DESTROY].attr_held &&
        seen[ATTR_DESTROY].last_disposal.mutex_returned &&
        seen[ATTR_DESTROY].last_disposal.attr_attempted &&
        !seen[ATTR_DESTROY].last_disposal.attr_returned &&
        !seen[ATTR_DESTROY].last_disposal.completed &&
        calls[MUTEX_DESTROY]==1 && calls[ATTR_DESTROY]==1 &&
        real_calls[MUTEX_DESTROY]==1 && real_calls[ATTR_DESTROY]==1 &&
        f.snapshots==1 && !f.submits;
    if(r.owner_released)owner=NULL;
    cleanup(&owner);ck(ok,"broker_owner_disposal_arms_mutex_then_attribute_before_consuming_owner");
}
static void mutex_failure_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);ready(owner);
    struct mbro_owner *original=owner;injected[MUTEX_DESTROY]=EBUSY;
    struct mbro_dispose_report first;int rc=mbro_dispose(&owner,&first);
    struct mbro_evidence e=evidence(owner);
    bool kept=owner==original && e.attr_held && e.mutex_held &&
        first.completed && first.result==MB_ERR_MUTEX && !first.owner_released &&
        first.mutex_attempted && first.mutex_returned && first.mutex_result==EBUSY &&
        !first.attr_attempted && e.disposal_started && e.disposal_attempts==1 &&
        e.first_disposal_valid && e.last_disposal_valid &&
        report_equal(&e.first_disposal,&first) && report_equal(&e.last_disposal,&first) &&
        !calls[ATTR_DESTROY] && calls[MUTEX_DESTROY]==1 && !real_calls[MUTEX_DESTROY] &&
        unavailable(owner,MB_ERR_CLOSED) && setup_refusals(owner,&f,MB_ERR_CLOSED);
    injected[MUTEX_DESTROY]=0;struct mbro_dispose_report retried;
    int retry=mbro_dispose(&owner,&retried);bool consumed=!owner;
    bool ok=rc==MB_ERR_MUTEX && kept && retry==MB_OK && consumed && retried.owner_released &&
        retried.attempt_number==2 && retried.mutex_attempted && retried.attr_attempted &&
        calls[MUTEX_DESTROY]==2 && real_calls[MUTEX_DESTROY]==1 &&
        calls[ATTR_DESTROY]==1 && real_calls[ATTR_DESTROY]==1 && f.snapshots==1 && !f.submits;
    if(retried.owner_released)owner=NULL;
    cleanup(&owner);ck(ok,"broker_owner_failed_mutex_destroy_retains_every_resource_and_closes_exports");
}
static void attr_failure_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);ready(owner);
    struct mbro_owner *original=owner;injected[ATTR_DESTROY]=EINVAL;
    struct mbro_dispose_report first;int rc=mbro_dispose(&owner,&first);
    struct mbro_evidence e=evidence(owner);
    bool kept=owner==original && !e.mutex_held && e.attr_held &&
        first.completed && first.result==MB_ERR_MUTEX && !first.owner_released &&
        first.mutex_attempted && first.mutex_returned && !first.mutex_result &&
        first.attr_attempted && first.attr_returned && first.attr_result==EINVAL &&
        e.disposal_started && e.disposal_attempts==1 &&
        report_equal(&e.first_disposal,&first) && report_equal(&e.last_disposal,&first) &&
        calls[MUTEX_DESTROY]==1 && real_calls[MUTEX_DESTROY]==1 &&
        calls[ATTR_DESTROY]==1 && !real_calls[ATTR_DESTROY] &&
        unavailable(owner,MB_ERR_CLOSED) && setup_refusals(owner,&f,MB_ERR_CLOSED);
    injected[ATTR_DESTROY]=0;struct mbro_dispose_report retried;
    int retry=mbro_dispose(&owner,&retried);bool consumed=!owner;
    bool ok=rc==MB_ERR_MUTEX && kept && retry==MB_OK && consumed && retried.owner_released &&
        retried.attempt_number==2 && !retried.mutex_attempted && !retried.mutex_returned &&
        retried.attr_attempted && retried.attr_returned && !retried.attr_result &&
        calls[MUTEX_DESTROY]==1 && real_calls[MUTEX_DESTROY]==1 &&
        calls[ATTR_DESTROY]==2 && real_calls[ATTR_DESTROY]==1 && f.snapshots==1 && !f.submits;
    if(retried.owner_released)owner=NULL;
    cleanup(&owner);ck(ok,"broker_owner_attribute_failure_keeps_owner_and_retry_skips_released_mutex");
}
static void history_case(void)
{
    struct fake f;reset(&f);struct mbro_owner *owner=create(&f);
    assert(mbro_initialize(owner)==MB_OK);f.snapshot_result=EIO;
    assert(mbro_validate(owner)==MB_ERR_BACKEND);
    injected[MUTEX_DESTROY]=EBUSY;struct mbro_dispose_report first,second,third;
    int one=mbro_dispose(&owner,&first);struct mbro_evidence a=evidence(owner);
    injected[MUTEX_DESTROY]=EINVAL;int two=mbro_dispose(&owner,&second);
    struct mbro_evidence b=evidence(owner);
    bool retained=one==MB_ERR_MUTEX && two==MB_ERR_MUTEX &&
        a.terminal_error==MB_ERR_BACKEND && b.terminal_error==MB_ERR_BACKEND &&
        b.validation_result==EIO && b.validation_returned && !b.validation_matched &&
        b.disposal_attempts==2 && b.first_disposal_valid && b.last_disposal_valid &&
        report_equal(&b.first_disposal,&first) && report_equal(&b.last_disposal,&second) &&
        first.attempt_number==1 && first.mutex_result==EBUSY &&
        second.attempt_number==2 && second.mutex_result==EINVAL &&
        b.mutex_held && b.attr_held && calls[MUTEX_DESTROY]==2 &&
        !real_calls[MUTEX_DESTROY] && !calls[ATTR_DESTROY];
    injected[MUTEX_DESTROY]=0;int three=mbro_dispose(&owner,&third);bool consumed=!owner;
    bool ok=retained && three==MB_OK && consumed && third.owner_released &&
        third.attempt_number==3 && calls[MUTEX_DESTROY]==3 && real_calls[MUTEX_DESTROY]==1 &&
        calls[ATTR_DESTROY]==1 && real_calls[ATTR_DESTROY]==1 && f.snapshots==1 && !f.submits;
    if(third.owner_released)owner=NULL;
    cleanup(&owner);ck(ok,"broker_owner_explicit_disposal_retries_preserve_first_error_and_latest_report");
}
int main(int argc,char **argv)
{
    alarm(30);const char *which=argc>1?argv[1]:"all";bool all=!strcmp(which,"all"),selected=false;
#define RUN(name,call) do{if(all || !strcmp(which,name)){selected=true;call;}}while(0)
    RUN("allocated",allocated_case());RUN("init-arm",init_arm_case());RUN("init-failures",init_failures_case());
    RUN("validate-arm",validate_arm_case());RUN("validation-failure",validation_failure_case());
    RUN("mismatch",mismatch_case());RUN("ready-repeat",ready_repeat_case());RUN("dispatcher",dispatcher_case());
    RUN("dispose-empty",dispose_empty_case());RUN("dispose-init-failure",dispose_init_failure_case());
    RUN("dispose-ready",dispose_ready_case());RUN("mutex-failure",mutex_failure_case());
    RUN("attr-failure",attr_failure_case());RUN("disposal-history",history_case());
#undef RUN
    if(!selected)return 2;
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
