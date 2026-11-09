/* Offline claim/receipt composition, authored RA/loader/capabilities only.
 * No call enters patched targets. All spawned threads join before final checks.
 * Scheduler deadlines are fixture failure bounds, not latency guarantees.
 * rc_* JSON serializers below are copied exactly from the pinned predecessor;
 * no predecessor selector, invocation helper, or main is reused. */
#define _GNU_SOURCE 1
#include "composition.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#if !defined(CC_FIXTURE) || !defined(RC_FIXTURE)
#error "explicit authored ingress and receipt fixture configurations required"
#endif

unsigned long rc_target_hits[4], rc_replacement_hits[4];
static rc_binding bindings[RC_BINDINGS];
static unsigned char before[RC_BINDINGS][16];
static cc_loader_input inputs[CC_MODULES];
static unsigned input_count, entries[CC_MODULES], inits[CC_MODULES];
static const char *check_names[128], *failed_checks[128];
static size_t checks, failures;
static long page_size;
static void infra(const char *s) { fprintf(stderr,"FIXTURE_ABORT %s\n",s);exit(2); }
static void check(bool yes,const char *name) {
    if(checks==128)infra("check capacity");
    check_names[checks++]=name;
    if(!yes)failed_checks[failures++]=name;
}
typedef struct {
    const char *label;
    char id[CC_ID_BYTES];
    unsigned binding,captured;
    uintptr_t target,replacement,original,caller_ra;
    uint64_t faults;
    bool recursive;
    cc_claim_result result;
} claim_record;
typedef struct { const char *label; cc_module_snapshot module; } module_record;
typedef struct { const char *label; cc_status result; unsigned index; cc_module_snapshot module; } run_record;
static claim_record claims[64];static size_t claim_count;
static module_record modules[64];static size_t module_count;
static run_record runs[32];static size_t run_count;
static rc_attempt core_attempts[4];static size_t core_count;
static void save_claim(const char *label,const char *id,unsigned binding,
    uintptr_t target,void *replacement,void **original,uintptr_t ra,
    unsigned captured,uint64_t faults,bool recursive,cc_claim_result r) {
    if(claim_count==64 || !id || strlen(id)>=CC_ID_BYTES)infra("claim evidence capacity");
    claim_record *c=&claims[claim_count++];
    memset(c,0,sizeof *c);
    c->label=label;memcpy(c->id,id,strlen(id)+1);
    c->binding=binding;c->target=target;c->replacement=(uintptr_t)replacement;
    c->original=(uintptr_t)original;c->caller_ra=ra;c->captured=captured;
    c->faults=faults;c->recursive=recursive;c->result=r;
}
static cc_module_snapshot observe(unsigned index,const char *label) {
    cc_module_snapshot m;memset(&m,0,sizeof m);
    if(!cc_module_get(index,&m))infra("module snapshot");
    if(module_count==64)infra("module history capacity");
    modules[module_count++]=(module_record){label,m};return m;
}
static uintptr_t caller(unsigned index) { return inputs[index].base+0x100; }
static cc_claim_result call_values(const char *label,const char *id,unsigned binding,
    uintptr_t target,void *replacement,void **original,uintptr_t ra,unsigned captured,uint64_t faults,bool recursive) {
    cc_claim_result r;memset(&r,0,sizeof r);
    int result=recursive?cc_claim_authored_recursive(id,target,replacement,original,ra,captured,faults,&r):
        cc_claim_authored(id,target,replacement,original,ra,captured,faults,&r);
    if(result!=r.sdk_result)infra("SDK return/output disagreement");
    save_claim(label,id,binding,target,replacement,original,ra,captured,faults,recursive,r);return r;
}
static cc_claim_result claim(const char *label,unsigned index,unsigned binding,uint64_t faults) {
    return call_values(label,inputs[index].id,binding,bindings[binding].target,
        bindings[binding].replacement,bindings[binding].original,caller(index),cc_capture_session(),faults,false);
}
static bool declined(cc_claim_result r) {
    return r.sdk_result==-1 && !r.installer_called && !r.accounted &&
        !r.receipt.installer_entered && !r.receipt.admitted &&
        !r.receipt.metadata_published && !r.receipt.original_published && !r.receipt.redirect_published;
}
static bool published(const cc_claim_result *r) {
    const rc_attempt *a=&r->receipt;rc_metadata m;
    return r->installer_called && a->installer_entered && a->admitted && a->prepared &&
        a->trampoline_prewrite_validated && a->permit_validated && a->installer_result==1 &&
        a->metadata_published && a->original_published && a->redirect_published &&
        a->metadata_order==1 && a->original_order==2 && a->redirect_order==3 &&
        a->original_saw_metadata && a->redirect_saw_original &&
        (uintptr_t)__atomic_load_n(bindings[a->emission.binding].original,__ATOMIC_ACQUIRE)==a->emission.trampoline &&
        rc_lookup(a->emission.trampoline,&m) && m.target==a->emission.target;
}
static bool no_original(unsigned binding) {
    return __atomic_load_n(bindings[binding].original,__ATOMIC_ACQUIRE)==NULL;
}
static bool untouched(unsigned binding) {
    int p=rc_query_protection(bindings[binding].target,16);
    return no_original(binding) && p>=0 && (p&PROT_READ) &&
        memcmp(before[binding],(void *)bindings[binding].target,16)==0;
}
static cc_loader_input loader(const char *id,unsigned index) {
    cc_loader_input x;memset(&x,0,sizeof x);
    if(strlen(id)>=CC_ID_BYTES)infra("authored ID length");
    strcpy(x.id,id);x.process_id=UINT64_C(0x1122334455667700)+index;
    x.handle_present=x.link_query_succeeded=x.link_map_present=x.object_matched=x.base_valid=true;
    x.base=UINT64_C(0x10000000)+(uintptr_t)index*0x10000;
    x.load_count=1;x.loads[0]=(cc_load){true,0,0x1000};x.hooks=true;return x;
}
static void begin(unsigned count) {
    input_count=count;
    if(cc_loader_begin(inputs,count)!=CC_OK)infra("loader record capture");
}
static int empty_init(unsigned index,void *context) { (void)index;(void)context;return 0; }
typedef struct {
    cc_init_fn init;
    void *user;
    bool reject_descriptor;
} entry_context;
static cc_descriptor entry(unsigned index,void *context) {
    entry_context *c=context;entries[index]++;
    cc_descriptor d={inputs[index].id,!c->reject_descriptor,true,c->init?c->init:empty_init};
    return d;
}
static cc_status run(unsigned index,entry_context *context,const char *label) {
    cc_module_snapshot m;memset(&m,0,sizeof m);
    cc_status s=cc_run_module(index,entry,context,&m);
    if(run_count==32)infra("run capacity");
    runs[run_count++]=(run_record){label,s,index,m};return s;
}

/* Two independently bounded condition-variable gates, never an elapsed-time
 * assertion. The callbacks only copy borrowed observations and signal/wait.
 * They do not call the composition, receipt or logging APIs. */
typedef struct {
    unsigned domain,point,index;
    bool has_result,has_attempt;
    cc_claim_result result;rc_attempt attempt;
} checkpoint;
static struct {
    pthread_mutex_t mutex;pthread_cond_t cond;
    bool enabled,held,released,failed,cm_busy,writer_busy;
    cc_test_point point;
    unsigned holds,releases;
    checkpoint events[128];size_t count;
} gate={.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};
static struct timespec deadline(unsigned seconds) {
    struct timespec t;if(clock_gettime(CLOCK_REALTIME,&t)!=0)infra("clock");
    t.tv_sec+=(time_t)seconds;return t;
}
static void note(unsigned domain,unsigned point,unsigned index,const cc_claim_result *r,const rc_attempt *a) {
    if(pthread_mutex_lock(&gate.mutex)!=0)abort();
    if(gate.count<128) {
        checkpoint *e=&gate.events[gate.count++];memset(e,0,sizeof *e);
        e->domain=domain;e->point=point;e->index=index;e->has_result=r!=NULL;e->has_attempt=a!=NULL;
        if(r)e->result=*r;
        if(a)e->attempt=*a;
    } else gate.failed=true;
    if(domain==0 && point==CC_TEST_CM_BUSY)gate.cm_busy=true;
    if(domain==0 && point==CC_TEST_WRITER_BUSY)gate.writer_busy=true;
    if(domain==0 && gate.enabled && point==(unsigned)gate.point && !gate.held) {
        gate.held=true;gate.holds++;pthread_cond_broadcast(&gate.cond);
        struct timespec end=deadline(15);
        while(!gate.released) {
            int s=pthread_cond_timedwait(&gate.cond,&gate.mutex,&end);
            if(s!=0){gate.failed=true;gate.released=true;break;}
        }
    }
    pthread_cond_broadcast(&gate.cond);
    if(pthread_mutex_unlock(&gate.mutex)!=0)abort();
}
void cc_fixture_checkpoint(cc_test_point p,unsigned i,const cc_claim_result *r,const rc_attempt *a){note(0,p,i,r,a);}
void rc_fixture_checkpoint(rc_test_point p,unsigned i,const rc_attempt *a){note(1,p,i,NULL,a);}
static void arm(cc_test_point point) {
    pthread_mutex_lock(&gate.mutex);
    gate.enabled=true;gate.held=false;gate.released=false;gate.cm_busy=false;gate.writer_busy=false;gate.point=point;
    pthread_mutex_unlock(&gate.mutex);
}
static bool wait_event(bool busy) {
    pthread_mutex_lock(&gate.mutex);struct timespec end=deadline(5);bool yes;
    for(;;) {
        yes=busy?gate.cm_busy:gate.held;
        if(yes)break;
        int s=pthread_cond_timedwait(&gate.cond,&gate.mutex,&end);
        if(s!=0)break;
    }
    pthread_mutex_unlock(&gate.mutex);return yes;
}
static void release_gate(void) {
    pthread_mutex_lock(&gate.mutex);gate.released=true;gate.enabled=false;gate.releases++;
    pthread_cond_broadcast(&gate.cond);pthread_mutex_unlock(&gate.mutex);
}
typedef struct {
    unsigned index,binding,captured;
    int sdk;cc_claim_result result;
} claim_job;
static void *claim_worker(void *p) {
    claim_job *j=p;
    j->sdk=cc_claim_authored(inputs[j->index].id,bindings[j->binding].target,
        bindings[j->binding].replacement,bindings[j->binding].original,caller(j->index),j->captured,0,&j->result);
    return NULL;
}
static bool start_claim(claim_job *j,pthread_t *thread) { return pthread_create(thread,NULL,claim_worker,j)==0; }
static void join_claim(pthread_t thread,claim_job *j,const char *label) {
    if(pthread_join(thread,NULL)!=0)infra("claim join");
    if(j->sdk!=j->result.sdk_result)infra("thread return/output disagreement");
    save_claim(label,inputs[j->index].id,j->binding,bindings[j->binding].target,
        bindings[j->binding].replacement,bindings[j->binding].original,
        caller(j->index),j->captured,0,false,j->result);
}
typedef struct { unsigned index;entry_context context;cc_status status;cc_module_snapshot module; } run_job;
static void *run_worker(void *p) {
    run_job *j=p;j->status=cc_run_module(j->index,entry,&j->context,&j->module);return NULL;
}

/* Policy: successes consume different fixed targets; all rejected paths use
 * actual admission before resource work, not a mock installer counter. */
static cc_claim_result hooks_result,endpoint_lo,endpoint_hi;
static bool policy_invalid,policy_unknown,policy_closed,policy_wrong_loading;
static int policy_init(unsigned index,void *context) {
    (void)context;inits[index]++;
    if(index==0){hooks_result=claim("false-hooks",0,2,0);return 0;}
    unsigned token=cc_capture_session();uintptr_t lo=inputs[index].base,hi=lo+0x1000;
    cc_claim_result a=call_values("caller-before-lo",inputs[index].id,0,bindings[0].target,bindings[0].replacement,bindings[0].original,lo-1,token,0,false);
    cc_claim_result b=call_values("caller-at-hi",inputs[index].id,0,bindings[0].target,bindings[0].replacement,bindings[0].original,hi,token,0,false);
    cc_claim_result c=call_values("target-outside-text",inputs[index].id,0,1,bindings[0].replacement,bindings[0].original,lo,token,0,false);
    cc_claim_result d=call_values("closed-other-module",inputs[0].id,0,bindings[0].target,bindings[0].replacement,bindings[0].original,caller(0),token,0,false);
    cc_claim_result e=call_values("wrong-captured-session",inputs[index].id,0,bindings[0].target,bindings[0].replacement,bindings[0].original,lo,4,0,false);
    policy_invalid=declined(a)&&a.status==CC_CALLER_REFUSED&&declined(b)&&b.status==CC_CALLER_REFUSED&&declined(c)&&c.status==CC_TARGET_REFUSED&&declined(e)&&e.status==CC_SESSION_REFUSED;
    policy_wrong_loading=declined(d)&&d.status==CC_INIT_REFUSED;
    endpoint_lo=call_values("caller-at-lo",inputs[index].id,0,bindings[0].target,bindings[0].replacement,bindings[0].original,lo,token,0,false);
    endpoint_hi=call_values("caller-before-hi",inputs[index].id,1,bindings[1].target,bindings[1].replacement,bindings[1].original,hi-1,token,0,false);
    return 0;
}
static void policy_case(void) {
    inputs[0]=loader("no-hooks",0);inputs[0].hooks=false;
    inputs[1]=loader("policy-good",1);inputs[2]=loader("any-hooks",2);inputs[2].firmware_any=true;
    begin(3);
    cc_claim_result u=call_values("unknown-id","absent-module",0,bindings[0].target,bindings[0].replacement,bindings[0].original,caller(0),0,0,false);
    cc_claim_result c=claim("before-init",1,0,0);
    policy_unknown=declined(u)&&u.status==CC_UNKNOWN_ID;policy_closed=declined(c)&&c.status==CC_INIT_REFUSED;
    entry_context ctx={policy_init,NULL,false};
    cc_status r0=run(0,&ctx,"false-hooks-run");cc_module_snapshot m0=observe(0,"after-false-hooks");
    cc_status r1=run(1,&ctx,"valid-policy-run");cc_module_snapshot m1=observe(1,"after-valid-policy");
    cc_status r2=run(2,&ctx,"any-hooks-run");cc_module_snapshot m2=observe(2,"after-any-hooks");
    check(policy_unknown,"unknown-id-refused");
    check(policy_invalid,"caller-target-and-session-boundaries");
    check(policy_closed&&policy_wrong_loading,"closed-and-wrong-loading-refused");
    check(hooks_result.sdk_result==-1&&hooks_result.status==CC_HOOKS_REFUSED,"hooks-refused");
    check(!hooks_result.installer_called&&!hooks_result.receipt.admitted&&!hooks_result.receipt.installer_entered,"hooks-no-installer");
    check(m0.nhooks==0,"hooks-zero-count");
    check(r0==CC_OK&&r1==CC_OK&&endpoint_lo.sdk_result==0&&endpoint_hi.sdk_result==0&&published(&endpoint_lo)&&published(&endpoint_hi)&&m1.nhooks==2,"caller-half-open-endpoints-admitted");
    uintptr_t rp0=(uintptr_t)bindings[0].replacement,rp1=(uintptr_t)bindings[1].replacement;
    check((rp0<m1.lo||rp0>=m1.hi)&&(rp1<m1.lo||rp1>=m1.hi)&&
        endpoint_lo.sdk_result==0&&endpoint_hi.sdk_result==0,"fixed-replacement-outside-caller-envelope");
    check(r2!=CC_OK&&entries[2]==0&&inits[2]==0&&!m2.session_used&&m2.nhooks==0,"any-hooks-loader-refused");
}
static cc_claim_result id_claims[2];
static int id_init(unsigned index,void *context) {(void)context;inits[index]++;id_claims[index]=claim("id32-claim",index,index,0);return index==1?-19:0;}
static void id_case(void) {
    inputs[0]=loader("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab",0);
    inputs[1]=loader("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac",1);begin(2);
    entry_context c={id_init,NULL,false};cc_status a=run(0,&c,"id32-first"),b=run(1,&c,"id32-second");
    cc_module_snapshot x=observe(0,"id32-first-closed"),y=observe(1,"id32-second-failed");
    rc_metadata mx,my;bool lx=rc_lookup(id_claims[0].receipt.emission.trampoline,&mx),ly=rc_lookup(id_claims[1].receipt.emission.trampoline,&my);
    check(strlen(inputs[0].id)==32&&strlen(inputs[1].id)==32&&strcmp(x.id,inputs[0].id)==0&&strcmp(y.id,inputs[1].id)==0&&strcmp(x.id,y.id)!=0,"source-id32-distinct");
    check(lx&&ly&&strcmp(mx.owner.id,inputs[0].id)==0&&strcmp(my.owner.id,inputs[1].id)==0&&strcmp(mx.owner.id,my.owner.id)!=0,"owner-id32-exact");
    check(a==CC_OK&&b==CC_INIT_FAILED&&id_claims[0].sdk_result==0&&id_claims[1].sdk_result==0&&x.nhooks==1&&y.nhooks==1,"id32-separate-accounting");
    check(published(&id_claims[0])&&published(&id_claims[1])&&y.phase==CC_FAILED&&!y.callbacks_retained&&y.init_result==-19,"id32-failed-init-retains-publication");
}
static claim_job queued;static pthread_t queued_thread;static bool queued_started,queued_observed,queued_joined;
static int preopen_init(unsigned index,void *context) {(void)context;inits[index]++;release_gate();if(queued_started){join_claim(queued_thread,&queued,"preopen-claim");queued_joined=true;}return 0;}
static int stale_start_init(unsigned index,void *context) {
    (void)context;inits[index]++;queued=(claim_job){.index=index,.binding=1,.captured=cc_capture_session()};
    arm(CC_TEST_BEFORE_ADMISSION);queued_started=start_claim(&queued,&queued_thread);
    queued_observed=queued_started&&wait_event(false);if(!queued_observed)release_gate();return 0;
}
static int stale_finish_init(unsigned index,void *context) {(void)context;inits[index]++;release_gate();if(queued_started){join_claim(queued_thread,&queued,"closed-session-claim");queued_joined=true;}return 0;}
static void queued_case(void) {
    inputs[0]=loader("preopen",0);inputs[1]=loader("old-session",1);inputs[2]=loader("new-session",2);begin(3);
    queued=(claim_job){.index=0,.binding=0,.captured=cc_capture_session()};
    arm(CC_TEST_BEFORE_ADMISSION);queued_started=start_claim(&queued,&queued_thread);
    bool first_held=queued_started&&wait_event(false);if(!first_held)release_gate();
    entry_context a={preopen_init,NULL,false};cc_status ar=run(0,&a,"preopen-run");
    if(queued_started&&!queued_joined){release_gate();join_claim(queued_thread,&queued,"preopen-fallback-join");queued_joined=true;}
    cc_claim_result first=queued.result;bool first_joined=queued_joined;cc_module_snapshot m0=observe(0,"preopen-closed");
    queued_joined=false;entry_context b={stale_start_init,NULL,false},c={stale_finish_init,NULL,false};
    cc_status br=run(1,&b,"old-session-run"),cr=run(2,&c,"new-session-run");
    if(queued_started&&!queued_joined){release_gate();join_claim(queued_thread,&queued,"stale-fallback-join");queued_joined=true;}
    cc_module_snapshot m1=observe(1,"old-session-closed"),m2=observe(2,"new-session-closed");
    check(first.sdk_result==-1&&first.status==CC_SESSION_REFUSED,"preopen-session-refused");
    check(!first.installer_called&&!first.receipt.installer_entered&&!first.receipt.admitted,"preopen-session-no-installer");
    check(m0.nhooks==0,"preopen-session-zero-count");
    check(declined(queued.result)&&queued.result.status==CC_INIT_REFUSED&&m1.nhooks==0&&m2.nhooks==0,"closed-session-not-adopted");
    check(first_held&&first_joined&&queued_observed&&queued_joined&&!gate.failed&&ar==CC_OK&&br==CC_OK&&cr==CC_OK,"queued-workers-released-and-joined");
}
static claim_job admitted;static pthread_t admitted_thread;static bool admitted_started,admitted_held;
static int admitted_init(unsigned index,void *context) {
    (void)context;inits[index]++;admitted=(claim_job){.index=index,.binding=0,.captured=cc_capture_session()};
    admitted_started=start_claim(&admitted,&admitted_thread);
    admitted_held=admitted_started&&wait_event(false);if(!admitted_held)release_gate();return 0;
}
static void admitted_case(void) {
    inputs[0]=loader("admitted",0);begin(1);arm(CC_TEST_AFTER_REDIRECT_BEFORE_ACCOUNT);
    run_job j={.index=0,.context={admitted_init,NULL,false}};pthread_t runner;
    bool started=pthread_create(&runner,NULL,run_worker,&j)==0;
    bool busy=started&&wait_event(true);release_gate();
    if(started&&pthread_join(runner,NULL)!=0)infra("runner join");
    if(admitted_started)join_claim(admitted_thread,&admitted,"admitted-claim");
    if(started){runs[run_count++]=(run_record){"admitted-run",j.status,0,j.module};}
    cc_module_snapshot m=observe(0,"admitted-closed");
    check(started&&admitted_started&&admitted_held&&busy&&gate.cm_busy&&!gate.failed,"close-observed-real-cm-contention");
    check(m.nhooks==1&&j.module.nhooks==1&&admitted.result.nhooks_after==1,"close-counts-published-claim");
    check(admitted.result.sdk_result==0&&published(&admitted.result)&&admitted.result.accounted,"admitted-receipt-retained");
    check(j.status==CC_OK&&!m.session_open&&cc_capture_session()==0,"actual-run-close-completes-after-join");
}
static cc_claim_result partial_first,partial_second;
static int partial_init(unsigned index,void *context) {(void)context;inits[index]++;partial_first=claim("first-publication",index,0,0);partial_second=claim("second-unlock-refusal",index,1,RC_F_TARGET_UNLOCK);return -23;}
static void partial_case(void) {
    inputs[0]=loader("partial-init",0);begin(1);entry_context c={partial_init,NULL,false};
    cc_status s=run(0,&c,"partial-failed-run");cc_module_snapshot m=observe(0,"partial-failed");
    check(partial_first.sdk_result==0&&published(&partial_first),"first-claim-published");
    check(partial_second.sdk_result==-1&&partial_second.installer_called&&partial_second.receipt.status==RC_TARGET_UNLOCK&&partial_second.receipt.target_protection.unlock.injected&&partial_second.receipt.target_protection.unlock.result==-1&&!partial_second.receipt.redirect_published,"second-prepublication-refusal-retained");
    check(published(&partial_first)&&no_original(1)&&partial_second.receipt.island_slot_burned,"partial-code-and-receipts-retained");
    check(m.nhooks==1&&s==CC_INIT_FAILED,"failed-init-retains-hook-count");
    check(m.phase==CC_FAILED&&m.init_returned&&m.init_result==-23&&!m.callbacks_retained&&!m.session_open&&m.handle_retained,"failed-init-callback-state-cleared-only");
}
static cc_claim_result warning_result;
static int warning_init(unsigned index,void *context){(void)context;inits[index]++;warning_result=claim("published-warning",index,0,RC_F_ISLAND_RELOCK|RC_F_TARGET_RELOCK);return 0;}
static void warning_case(void) {
    inputs[0]=loader("warning-init",0);begin(1);entry_context c={warning_init,NULL,false};
    cc_status s=run(0,&c,"warning-run");cc_module_snapshot m=observe(0,"warning-closed");
    check(warning_result.sdk_result==0,"warning-sdk-success");
    check(published(&warning_result)&&warning_result.receipt.status==RC_WARNINGS&&warning_result.receipt.island_protection.relock.injected&&warning_result.receipt.target_protection.relock.injected,"warning-actual-publication-retained");
    check(s==CC_OK&&m.nhooks==1&&warning_result.accounted,"warning-counted-once");
    check(rc_query_protection(bindings[0].target,16)==(PROT_READ|PROT_WRITE|PROT_EXEC),"warning-writable-route-never-entered");
}
static bool bad_slots,bad_hook,slots_untouched;static cc_claim_result custody_good;
static int custody_init(unsigned index,void *context) {
    (void)context;inits[index]++;void *temporary=NULL;unsigned t=cc_capture_session();
    cc_claim_result a=call_values("temporary-original",inputs[index].id,0,bindings[0].target,bindings[0].replacement,&temporary,caller(index),t,0,false);
    cc_claim_result b=call_values("inaccessible-identity",inputs[index].id,0,bindings[0].target,bindings[0].replacement,(void **)(uintptr_t)1,caller(index),t,0,false);
    cc_claim_result c=call_values("other-owned-slot",inputs[index].id,0,bindings[0].target,bindings[0].replacement,bindings[1].original,caller(index),t,0,false);
    cc_claim_result d=call_values("wrong-replacement",inputs[index].id,0,bindings[0].target,bindings[1].replacement,bindings[0].original,caller(index),t,0,false);
    bad_slots=declined(a)&&declined(b)&&declined(c)&&a.status==CC_CUSTODY_REFUSED&&b.status==CC_CUSTODY_REFUSED&&c.status==CC_CUSTODY_REFUSED;
    bad_hook=declined(d)&&d.status==CC_CUSTODY_REFUSED;
    slots_untouched=temporary==NULL&&untouched(0)&&untouched(1);
    custody_good=claim("known-fixed-descriptor",index,0,0);return 0;
}
static void custody_case(void) {
    inputs[0]=loader("fixed-custody",0);begin(1);entry_context c={custody_init,NULL,false};
    cc_status s=run(0,&c,"custody-run");cc_module_snapshot m=observe(0,"custody-closed");
    check(bad_slots,"wrong-original-identities-refuse-before-installer");
    check(bad_hook,"wrong-replacement-refuses-before-installer");
    check(slots_untouched,"refused-originals-and-targets-unchanged");
    check(s==CC_OK&&custody_good.sdk_result==0&&published(&custody_good)&&m.nhooks==1,"known-fixed-custody-admitted");
}
static cc_claim_result alias_result;
static int alias_init(unsigned index,void *context){(void)context;inits[index]++;alias_result=claim("community-alias",index,3,0);return 0;}
static void core_case(void) {
    inputs[0]=loader("community-alias",0);begin(1);
    rc_request r={bindings[0].replacement,bindings[0].original,0};rc_attempt a;memset(&a,0,sizeof a);
    rc_status cs=cc_install_core(0,&r,&a);core_attempts[core_count++]=a;
    entry_context c={alias_init,NULL,false};cc_status s=run(0,&c,"alias-run");cc_module_snapshot m=observe(0,"alias-closed");rc_metadata found;
    check(cs==RC_OK&&a.redirect_published&&a.emission.owner.kind==RC_OWNER_CORE&&a.emission.owner.id[0]==0,"core-publication-typed-and-retained");
    check(alias_result.sdk_result==-1&&!alias_result.installer_called&&alias_result.receipt.status==RC_CONFLICT&&!alias_result.receipt.installer_entered,"community-alias-refuses-before-installer");
    check(s==CC_OK&&m.nhooks==0&&!alias_result.accounted,"refused-alias-not-counted");
    check(no_original(3)&&(uintptr_t)__atomic_load_n(bindings[0].original,__ATOMIC_ACQUIRE)==a.emission.trampoline&&rc_lookup(a.emission.trampoline,&found)&&found.owner.kind==RC_OWNER_CORE,"common-ledger-preserves-first-owner");
}

/* Callback reader probe: a real ordinary thread must finish while entry/init
 * remains active. If it cannot, callback returns, then main joins it before
 * reporting failure. No lock is exposed or assumed signal safe. */
typedef struct {
    pthread_mutex_t mutex;pthread_cond_t cond;pthread_t thread;
    unsigned index;bool started,done,observed,got;cc_module_snapshot module;
} probe;
static probe probes[2]={
    {.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER},
    {.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER}
};
static void *probe_worker(void *p) {
    probe *q=p;bool got=cc_module_get(q->index,&q->module);
    pthread_mutex_lock(&q->mutex);q->got=got;q->done=true;
    pthread_cond_broadcast(&q->cond);pthread_mutex_unlock(&q->mutex);return NULL;
}
static void probe_callback(probe *q,unsigned index) {
    q->index=index;q->started=pthread_create(&q->thread,NULL,probe_worker,q)==0;
    if(!q->started)return;
    pthread_mutex_lock(&q->mutex);struct timespec end=deadline(5);
    while(!q->done){int s=pthread_cond_timedwait(&q->cond,&q->mutex,&end);if(s!=0)break;}
    q->observed=q->done;pthread_mutex_unlock(&q->mutex);
}
static int loader_init(unsigned index,void *context){(void)context;inits[index]++;probe_callback(&probes[1],index);return 0;}
static cc_descriptor loader_entry(unsigned index,void *context) {
    (void)context;entries[index]++;probe_callback(&probes[0],index);
    return (cc_descriptor){inputs[index].id,true,true,loader_init};
}
static void loader_inputs_case(void) {
    inputs[0]=loader("missing-handle",0);inputs[0].handle_present=false;
    inputs[1]=loader("failed-query",1);inputs[1].link_query_succeeded=false;
    inputs[2]=loader("missing-map",2);inputs[2].link_map_present=false;
    inputs[3]=loader("valid-zero-base",3);inputs[3].base=0;begin(4);
    bool refused[3];entry_context c={empty_init,NULL,false};
    for(unsigned i=0;i<3;++i){cc_status s=run(i,&c,"provenance-refusal");cc_module_snapshot m=observe(i,"provenance-refused");refused[i]=s==CC_RANGE_REFUSED&&!m.range_published&&!m.entry_attempted&&entries[i]==0;}
    cc_module_snapshot zero;memset(&zero,0,sizeof zero);cc_status s=cc_run_module(3,loader_entry,NULL,&zero);
    runs[run_count++]=(run_record){"valid-zero-run",s,3,zero};
    for(unsigned i=0;i<2;++i)if(probes[i].started&&pthread_join(probes[i].thread,NULL)!=0)infra("callback probe join");
    check(refused[0],"missing-handle-is-not-zero-base");
    check(refused[1],"failed-query-is-not-zero-base");
    check(refused[2],"missing-link-map-is-not-zero-base");
    check(s==CC_OK&&zero.range_published&&zero.lo==0&&zero.hi==0x1000&&zero.handle_retained&&zero.init_attempted,"explicit-valid-zero-base-accepted");
    check(probes[0].started&&probes[1].started&&probes[0].observed&&probes[1].observed&&probes[0].got&&probes[1].got,"entry-and-init-release-locks-before-callback");
}
static void loader_ranges_case(void) {
    inputs[0]=loader("wrong-object",0);inputs[0].object_matched=false;
    inputs[1]=loader("invalid-base",1);inputs[1].base_valid=false;
    inputs[2]=loader("overflow-range",2);inputs[2].base=UINTPTR_MAX-7;inputs[2].loads[0].memory_size=16;
    inputs[3]=loader("descriptor-refusal",3);begin(4);
    bool refused[3];entry_context c={empty_init,NULL,false};
    for(unsigned i=0;i<3;++i){cc_status s=run(i,&c,"range-refusal");cc_module_snapshot m=observe(i,"range-refused");refused[i]=s==CC_RANGE_REFUSED&&!m.range_published&&!m.entry_attempted;}
    c.reject_descriptor=true;cc_status s=run(3,&c,"descriptor-refusal");cc_module_snapshot m=observe(3,"descriptor-retained-range");
    check(refused[0],"unmatched-object-refuses-range");
    check(refused[1],"invalid-base-provenance-refuses");
    check(refused[2],"load-endpoint-overflow-refuses");
    check(s==CC_DESCRIPTOR_REFUSED&&m.range_published&&m.handle_retained&&m.phase==CC_DESCRIPTOR_FAILED&&m.entry_attempted&&m.entry_returned&&!m.init_attempted&&!m.session_used&&m.lo==inputs[3].base&&m.hi==inputs[3].base+0x1000,"descriptor-failure-retains-committed-range");
}
static cc_claim_result life_claims[2];static cc_status nested_run,nested_begin,nested_initialize;
static int lifecycle_init(unsigned index,void *context) {
    (void)context;inits[index]++;
    if(index==0){
        entry_context c={empty_init,NULL,false};cc_module_snapshot m;
        nested_run=cc_run_module(1,entry,&c,&m);
        nested_begin=cc_loader_begin(inputs,input_count);
        nested_initialize=cc_initialize(bindings[0].target,bindings[0].target+16);
        life_claims[0]=call_values("recursive-cm-claim",inputs[0].id,0,bindings[0].target,bindings[0].replacement,bindings[0].original,caller(0),cc_capture_session(),0,true);
        return 0;
    }
    life_claims[1]=claim("second-lifecycle-claim",index,1,0);return -29;
}
static void lifecycle_case(void) {
    inputs[0]=loader("life-success",0);inputs[1]=loader("life-failure",1);begin(2);
    entry_context c={lifecycle_init,NULL,false};cc_status a=run(0,&c,"life-first");
    unsigned before_entries=entries[0];cc_status repeated=run(0,&c,"life-repeat");cc_status b=run(1,&c,"life-second");
    cc_module_snapshot x=observe(0,"life-first-retained"),y=observe(1,"life-second-retained");
    cc_claim_result closed=claim("life-closed-claim",0,2,0);
    check(nested_run==CC_BUSY&&nested_begin==CC_ALREADY&&nested_initialize==CC_ALREADY,"in-progress-lifecycle-refuses-without-callback");
    check(repeated==CC_ALREADY&&entries[0]==before_entries&&entries[1]==1&&inits[0]==1&&inits[1]==1,"module-session-never-reopened");
    check(a==CC_OK&&b==CC_INIT_FAILED&&x.nhooks==1&&y.nhooks==1&&strcmp(x.id,y.id)!=0&&x.callbacks_retained&&!y.callbacks_retained,"sequential-identities-and-counts-retained");
    check(life_claims[0].sdk_result==0&&published(&life_claims[0])&&life_claims[1].sdk_result==0&&published(&life_claims[1]),"recursive-cm-without-writer-recursion");
    check(declined(closed)&&closed.status==CC_INIT_REFUSED&&cc_capture_session()==0,"closed-session-does-not-restart-installer");
}

static void js(FILE *f,const char *s) {
    fputc('"',f);
    for(const unsigned char *p=(const unsigned char *)s;*p;++p) {
        if(*p=='"' || *p=='\\') { fputc('\\',f);fputc(*p,f); }
        else if(*p<32) fprintf(f,"\\u%04x",(unsigned)*p);
        else fputc(*p,f);
    }
    fputc('"',f);
}
static void hex(FILE *f,const void *data,size_t length) {
    const unsigned char *p=data; fputc('"',f);
    for(size_t i=0;i<length;++i) fprintf(f,"%02x",p[i]);
    fputc('"',f);
}
static void raw_json(FILE *f,rc_raw r) {
    fprintf(f,"{\"attempted\":%d,\"returned\":%d,\"underlying_called\":%d,\"injected\":%d,\"errno_valid\":%d,\"result\":%" PRIdPTR ",\"error\":%d}",
        r.attempted,r.returned,r.underlying_called,r.injected,r.errno_valid,r.result,r.error);
}
static void span_json(FILE *f,rc_span s) { fprintf(f,"[%" PRIuPTR ",%" PRIuPTR "]",s.begin,s.end); }
static void metadata_json(FILE *f,const rc_metadata *m) {
    fprintf(f,"{\"owner\":{\"kind\":%u,\"process_id\":%" PRIu64 ",\"id\":",(unsigned)m->owner.kind,m->owner.process_id);
    js(f,m->owner.id);
    fprintf(f,"},\"binding\":%u,\"ncopy\":%u,\"out\":%u,\"target\":%" PRIuPTR ",\"replacement\":%" PRIuPTR ",\"original_slot\":%" PRIuPTR ",\"island\":%" PRIuPTR ",\"trampoline\":%" PRIuPTR ",\"continuation\":%" PRIuPTR ",\"target_exclusion\":",
        m->binding,m->ncopy,m->out,m->target,m->replacement,m->original_slot,m->island,m->trampoline,m->continuation);
    span_json(f,m->target_exclusion);fputs(",\"island_instructions\":",f);span_json(f,m->island_instructions);
    fputs(",\"island_storage\":",f);span_json(f,m->island_storage);
    fputs(",\"trampoline_instructions\":",f);span_json(f,m->trampoline_instructions);
    fputs(",\"trampoline_storage\":",f);span_json(f,m->trampoline_storage);
    fputs(",\"source_bytes\":",f);hex(f,m->source_words,16);fputc('}',f);
}
static void query_json(FILE *f,const rc_query *q) {
    fputs("{\"open\":",f);raw_json(f,q->open);fputs(",\"reads\":[",f);
    unsigned n=q->read_count<RC_READS?q->read_count:RC_READS;
    for(unsigned i=0;i<n;++i) { if(i)fputc(',',f);raw_json(f,q->reads[i]); }
    fputs("],\"close\":",f);raw_json(f,q->close);
    fprintf(f,",\"read_count\":%u,\"read_capacity_refused\":%d,\"returned\":%d,\"parser_result\":%d}",
        q->read_count,q->read_capacity_refused,q->returned,q->parser_result);
}
static void protection_json(FILE *f,const rc_protection *p) {
    fputs("{\"unlock\":",f);raw_json(f,p->unlock);fputs(",\"page_size\":",f);raw_json(f,p->page_size);
    fputs(",\"mutex_lock\":",f);raw_json(f,p->mutex_lock);fputs(",\"make_writable\":",f);raw_json(f,p->make_writable);
    fputs(",\"relock\":",f);raw_json(f,p->relock);fputs(",\"mutex_release\":",f);raw_json(f,p->mutex_release);
    fputs(",\"query\":",f);query_json(f,&p->query);
    fprintf(f,",\"token_valid\":%d,\"base\":%" PRIuPTR ",\"length\":%zu,\"saved_bits\":%d}",
        p->token_valid,p->base,p->length,p->saved_bits);
}
static void attempt_json(FILE *f,const rc_attempt *a) {
    fprintf(f,"{\"admitted\":%d,\"installer_entered\":%d,\"prepared\":%d,\"permit_validated\":%d,"
        "\"trampoline_prewrite_validated\":%d,\"metadata_published\":%d,\"original_published\":%d,\"redirect_published\":%d,"
        "\"original_saw_metadata\":%d,\"redirect_saw_original\":%d,\"island_slot_burned\":%d,"
        "\"nhooks_accounted\":%d,\"metadata_order\":%u,\"original_order\":%u,\"redirect_order\":%u,"
        "\"mapping_count\":%u,\"pool_index\":%u,\"pool_slot\":%u,\"pool_base\":%" PRIuPTR ",\"pool_length\":%zu,\"status\":%u,\"installer_result\":%d,\"emission\":",
        a->admitted,a->installer_entered,a->prepared,a->permit_validated,a->trampoline_prewrite_validated,a->metadata_published,
        a->original_published,a->redirect_published,a->original_saw_metadata,a->redirect_saw_original,
        a->island_slot_burned,a->nhooks_accounted,a->metadata_order,a->original_order,a->redirect_order,
        a->mapping_count,a->pool_index,a->pool_slot,a->pool_base,a->pool_length,(unsigned)a->status,a->installer_result);
    metadata_json(f,&a->emission);fputs(",\"mappings\":[",f);
    for(unsigned i=0;i<a->mapping_count && i<RC_MAPS;++i) {
        const rc_mapping *m=&a->mappings[i]; if(i)fputc(',',f);
        fprintf(f,"{\"kind\":%u,\"hint\":%" PRIuPTR ",\"address\":%" PRIuPTR ",\"length\":%zu,\"protection\":%d,"
            "\"held\":%d,\"accepted_pool\":%d,\"placement_rejected\":%d,\"cleanup_uncertain\":%d,\"allocation\":",
            (unsigned)m->kind,m->hint,m->address,m->length,m->protection,m->held,m->accepted_pool,m->placement_rejected,m->cleanup_uncertain);
        raw_json(f,m->allocation);fputs(",\"cleanup\":",f);raw_json(f,m->cleanup);fputc('}',f);
    }
    fputs("],\"initial_page_size\":",f);raw_json(f,a->initial_page_size);
    fputs(",\"seal\":",f);raw_json(f,a->seal);
    fputs(",\"island_protection\":",f);protection_json(f,&a->island_protection);
    fputs(",\"target_protection\":",f);protection_json(f,&a->target_protection);
    fprintf(f,",\"trampoline_cache_armed\":%d,\"trampoline_cache_returned\":%d,"
        "\"island_cache_armed\":%d,\"island_cache_returned\":%d,\"target_cache_armed\":%d,\"target_cache_returned\":%d,"
        "\"diagnostic_before\":%u,\"diagnostic_after\":%u,\"diagnostic_lost\":%u}",
        a->trampoline_cache_armed,a->trampoline_cache_returned,a->island_cache_armed,a->island_cache_returned,
        a->target_cache_armed,a->target_cache_returned,a->diagnostic_before,a->diagnostic_after,a->diagnostic_lost);
}


static void strings(FILE *f,const char *const *items,size_t n){fputc('[',f);for(size_t i=0;i<n;++i){if(i)fputc(',',f);js(f,items[i]);}fputc(']',f);}
static void report_json(FILE *f,const char *selector){
    fputs("{\"selector\":",f);js(f,selector);fputs(",\"check_names\":",f);strings(f,check_names,checks);
    fprintf(f,",\"checks\":%zu,\"failures\":%zu,\"failed_checks\":",checks,failures);strings(f,failed_checks,failures);fputc('}',f);
}
static void module_json(FILE *f,const cc_module_snapshot *m){
    fprintf(f,"{\"present\":%d,\"range_published\":%d,\"handle_retained\":%d,\"session_used\":%d,\"session_open\":%d,\"entry_attempted\":%d,\"entry_returned\":%d,\"descriptor_accepted\":%d,\"init_attempted\":%d,\"init_returned\":%d,\"callbacks_retained\":%d,\"id\":",
        m->present,m->range_published,m->handle_retained,m->session_used,m->session_open,m->entry_attempted,m->entry_returned,m->descriptor_accepted,m->init_attempted,m->init_returned,m->callbacks_retained);js(f,m->id);
    fprintf(f,",\"hooks_ok\":%d,\"firmware_any\":%d,\"process_id\":%" PRIu64 ",\"lo\":%" PRIuPTR ",\"hi\":%" PRIuPTR ",\"index\":%u,\"nhooks\":%u,\"init_result\":%d,\"phase\":%u,\"load_status\":%u}",
        m->hooks_ok,m->firmware_any,m->process_id,m->lo,m->hi,m->index,m->nhooks,m->init_result,(unsigned)m->phase,(unsigned)m->load_status);
}
static void claim_json(FILE *f,const cc_claim_result *r){
    fprintf(f,"{\"status\":%u,\"sdk_result\":%d,\"captured_session\":%u,\"resolved_session\":%u,\"module_index\":%u,\"nhooks_before\":%u,\"nhooks_after\":%u,\"installer_called\":%d,\"accounted\":%d,\"incomplete\":%d,\"receipt\":",
        (unsigned)r->status,r->sdk_result,r->captured_session,r->resolved_session,r->module_index,r->nhooks_before,r->nhooks_after,r->installer_called,r->accounted,r->incomplete);
    attempt_json(f,&r->receipt);fputc('}',f);
}
static void write_evidence(const char *path,const char *selector){
    FILE *f=fopen(path,"wx");if(!f)infra("exclusive evidence file");
    fputs("{\"schema\":1,\"selector\":",f);js(f,selector);fprintf(f,",\"page_size\":%ld,\"authored_inputs\":[",page_size);
    for(unsigned i=0;i<input_count;++i){const cc_loader_input *x=&inputs[i];if(i)fputc(',',f);fputs("{\"id\":",f);js(f,x->id);
        fprintf(f,",\"process_id\":%" PRIu64 ",\"handle_present\":%d,\"link_query_succeeded\":%d,\"link_map_present\":%d,\"object_matched\":%d,\"base_valid\":%d,\"base\":%" PRIuPTR ",\"load_count\":%u,\"hooks\":%d,\"firmware_any\":%d,\"loads\":[",
            x->process_id,x->handle_present,x->link_query_succeeded,x->link_map_present,x->object_matched,x->base_valid,x->base,x->load_count,x->hooks,x->firmware_any);
        for(unsigned j=0;j<x->load_count&&j<CC_LOADS;++j){if(j)fputc(',',f);fprintf(f,"{\"load\":%d,\"virtual_address\":%" PRIuPTR ",\"memory_size\":%zu}",x->loads[j].load,x->loads[j].virtual_address,x->loads[j].memory_size);}fputs("]}",f);}
    fputs("],\"bindings\":[",f);
    for(unsigned i=0;i<RC_BINDINGS;++i){if(i)fputc(',',f);int p=rc_query_protection(bindings[i].target,16);
        fprintf(f,"{\"id\":%u,\"target\":%" PRIuPTR ",\"replacement\":%" PRIuPTR ",\"original_slot\":%" PRIuPTR ",\"original_value\":%" PRIuPTR ",\"target_protection\":%d,\"before\":",
            i,bindings[i].target,(uintptr_t)bindings[i].replacement,(uintptr_t)bindings[i].original,(uintptr_t)__atomic_load_n(bindings[i].original,__ATOMIC_ACQUIRE),p);
        hex(f,before[i],16);fputs(",\"after\":",f);if(p>=0&&(p&PROT_READ))hex(f,(void *)bindings[i].target,16);else js(f,"");fputc('}',f);}
    fputs("],\"claims\":[",f);for(size_t i=0;i<claim_count;++i){if(i)fputc(',',f);fputs("{\"label\":",f);js(f,claims[i].label);fputs(",\"input\":{\"id\":",f);js(f,claims[i].id);
        fprintf(f,",\"binding\":%u,\"target\":%" PRIuPTR ",\"replacement\":%" PRIuPTR ",\"original\":%" PRIuPTR ",\"caller_ra\":%" PRIuPTR ",\"captured_session\":%u,\"faults\":%" PRIu64 ",\"recursive\":%d},\"result\":",
            claims[i].binding,claims[i].target,claims[i].replacement,claims[i].original,claims[i].caller_ra,claims[i].captured,claims[i].faults,claims[i].recursive);
        claim_json(f,&claims[i].result);fputc('}',f);}
    fputs("],\"core_attempts\":[",f);for(size_t i=0;i<core_count;++i){if(i)fputc(',',f);attempt_json(f,&core_attempts[i]);}
    fputs("],\"module_history\":[",f);for(size_t i=0;i<module_count;++i){if(i)fputc(',',f);fputs("{\"label\":",f);js(f,modules[i].label);fputs(",\"module\":",f);module_json(f,&modules[i].module);fputc('}',f);}
    fputs("],\"runs\":[",f);for(size_t i=0;i<run_count;++i){if(i)fputc(',',f);fputs("{\"label\":",f);js(f,runs[i].label);fprintf(f,",\"result\":%u,\"index\":%u,\"module\":",(unsigned)runs[i].result,runs[i].index);module_json(f,&runs[i].module);fputc('}',f);}
    fputs("],\"final_modules\":[",f);for(unsigned i=0;i<input_count;++i){cc_module_snapshot m;memset(&m,0,sizeof m);if(!cc_module_get(i,&m))infra("final module snapshot");if(i)fputc(',',f);module_json(f,&m);}
    fputs("],\"receipts\":[",f);
    for(unsigned i=0;i<RC_BINDINGS;++i){rc_attempt a;memset(&a,0,sizeof a);bool present=rc_snapshot(i,&a);if(i)fputc(',',f);
        fprintf(f,"{\"binding\":%u,\"present\":%d,\"attempt\":",i,present);if(present)attempt_json(f,&a);else fputs("null",f);
        int ip=present&&a.emission.island?rc_query_protection(a.emission.island,16):-1;
        size_t used=present&&a.emission.out<=4?4u*(a.emission.out+4u):0;
        int tp=present&&a.emission.trampoline&&used?rc_query_protection(a.emission.trampoline,used):-1;
        fprintf(f,",\"island_protection\":%d,\"trampoline_protection\":%d,\"island_bytes\":",ip,tp);
        if(present&&a.redirect_published&&ip>=0&&(ip&PROT_READ))hex(f,(void *)a.emission.island,16);else js(f,"");
        fputs(",\"trampoline_bytes\":",f);if(present&&a.redirect_published&&tp>=0&&(tp&PROT_READ))hex(f,(void *)a.emission.trampoline,used);else js(f,"");fputc('}',f);}
    fputs("],\"checkpoints\":[",f);for(size_t i=0;i<gate.count;++i){checkpoint *e=&gate.events[i];if(i)fputc(',',f);
        fprintf(f,"{\"domain\":%u,\"point\":%u,\"index\":%u,\"has_result\":%d,\"has_attempt\":%d,\"result\":",e->domain,e->point,e->index,e->has_result,e->has_attempt);
        if(e->has_result)claim_json(f,&e->result);else fputs("null",f);fputs(",\"attempt\":",f);if(e->has_attempt)attempt_json(f,&e->attempt);else fputs("null",f);fputc('}',f);}
    fprintf(f,"],\"scheduler\":{\"held\":%d,\"released\":%d,\"failed\":%d,\"cm_busy\":%d,\"writer_busy\":%d,\"holds\":%u,\"releases\":%u},\"entries\":[",gate.held,gate.released,gate.failed,gate.cm_busy,gate.writer_busy,gate.holds,gate.releases);
    for(unsigned i=0;i<CC_MODULES;++i){fprintf(f,"%s%u",i?",":"",entries[i]);}
    fputs("],\"inits\":[",f);
    for(unsigned i=0;i<CC_MODULES;++i){fprintf(f,"%s%u",i?",":"",inits[i]);}
    fputs("],\"target_hits\":[",f);
    for(unsigned i=0;i<4;++i){fprintf(f,"%s%lu",i?",":"",rc_target_hits[i]);}
    fputs("],\"replacement_hits\":[",f);
    for(unsigned i=0;i<4;++i){fprintf(f,"%s%lu",i?",":"",rc_replacement_hits[i]);}
    fputs("],\"callback_probes\":[",f);
    for(unsigned i=0;i<2;++i){probe *q=&probes[i];if(i)fputc(',',f);
        fprintf(f,"{\"index\":%u,\"started\":%d,\"done\":%d,\"observed_in_callback\":%d,\"got\":%d,\"module\":",q->index,q->started,q->done,q->observed,q->got);
        module_json(f,&q->module);fputc('}',f);}
    fprintf(f,"],\"active_session\":%u,\"report\":",cc_capture_session());report_json(f,selector);fputs("}\n",f);
    if(ferror(f)||fclose(f)!=0)infra("evidence write");
}
typedef struct {const char *name;void (*run)(void);} selector;
static const selector selectors[]={
    {"policy",policy_case},{"id32",id_case},{"queued-close",queued_case},{"admitted-close",admitted_case},
    {"partial-failed-init",partial_case},{"published-warning",warning_case},{"fixed-custody",custody_case},
    {"core-conflict",core_case},{"loader-inputs",loader_inputs_case},{"loader-ranges",loader_ranges_case},{"lifecycle",lifecycle_case}
};
int main(int argc,char **argv){
    if(argc!=3)infra("selector and evidence file required");
    const selector *s=NULL;for(size_t i=0;i<sizeof selectors/sizeof selectors[0];++i)if(strcmp(argv[1],selectors[i].name)==0)s=&selectors[i];
    if(!s)infra("unknown selector");
    page_size=sysconf(_SC_PAGESIZE);
    if(page_size!=4096)infra("selected page size");
    uintptr_t lo=UINTPTR_MAX,hi=0;
    for(unsigned i=0;i<RC_BINDINGS;++i){if(!rc_binding_get(i,&bindings[i]))infra("fixed descriptor");
        if(bindings[i].target<lo)lo=bindings[i].target;
        if(bindings[i].target>UINTPTR_MAX-16)infra("target endpoint");
        if(bindings[i].target+16>hi)hi=bindings[i].target+16;}
    if(cc_initialize(lo,hi)!=CC_OK)infra("composition initialize");
    for(unsigned i=0;i<RC_BINDINGS;++i){if(rc_query_protection(bindings[i].target,16)!=(PROT_READ|PROT_EXEC))infra("initial owned target");memcpy(before[i],(void *)bindings[i].target,16);}
    s->run();bool never=true;for(unsigned i=0;i<4;++i)never=never&&rc_target_hits[i]==0&&rc_replacement_hits[i]==0;
    check(never,"no-patched-target-invocation");
    write_evidence(argv[2],s->name);report_json(stdout,s->name);putchar('\n');return failures?1:0;
}
