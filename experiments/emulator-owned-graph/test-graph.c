/* SPDX-License-Identifier: GPL-2.0-only */
/* New graph composition only. Exact extracted handlers run in owned memory;
 * descriptors, usercopy and syscall replies are modeled. Successful sync
 * wrappers execute pthread; injected sync errors occur before pthread.
 * Entry inspection is synchronous and serialized, never concurrency proof. */
#include "kernel-shim.inc"
#include "candidate.inc"
#include "graph.h"
#include "sync.h"

static unsigned checks,failures;
static void ck(bool ok,const char *label)
{ checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);} }
static void pattern(uint8_t *p,unsigned seed)
{ for(unsigned i=0;i<152;i++)p[i]=(uint8_t)(seed+23*i); }
static bool zero_bytes(const void *p,size_t n)
{ const uint8_t *b=p;for(size_t i=0;i<n;i++)if(b[i])return false;return true; }
static bool state_equal(const struct mb_state *a,const struct mb_state *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->sequence==b->sequence &&
        a->flags==b->flags && a->crc_len==b->crc_len && !memcmp(a->frame,b->frame,152);
}
static bool call_equal(struct mbog_call a,struct mbog_call b)
{ return a.attempted==b.attempted && a.returned==b.returned && a.result==b.result; }
static bool broker_dispose_equal(const struct mbro_dispose_report *a,const struct mbro_dispose_report *b)
{
    return a->attempt_number==b->attempt_number && a->mutex_attempted==b->mutex_attempted &&
        a->mutex_returned==b->mutex_returned && a->mutex_result==b->mutex_result &&
        a->attr_attempted==b->attr_attempted && a->attr_returned==b->attr_returned &&
        a->attr_result==b->attr_result && a->completed==b->completed &&
        a->result==b->result && a->owner_released==b->owner_released;
}
static bool disposal_equal(const struct mbog_dispose_report *a,const struct mbog_dispose_report *b)
{
#define EQ(field) (a->field==b->field)
    bool same=EQ(attempt_number) && EQ(dispatch_attempted) && EQ(dispatch_returned) &&
        EQ(broker_attempted) && EQ(broker_returned) && EQ(broker_result) &&
        broker_dispose_equal(&a->broker_report,&b->broker_report) &&
        EQ(adapter_close.attempted) && EQ(adapter_close.returned) && EQ(adapter_close.result) &&
        EQ(adapter_close.report.attempted) && EQ(adapter_close.report.call.result) && EQ(adapter_close.report.call.error) &&
        EQ(claim_close.attempted) && EQ(claim_close.returned) && EQ(claim_close.result) &&
        EQ(claim_close.report.attempted) && EQ(claim_close.report.call.result) && EQ(claim_close.report.call.error) &&
        EQ(completed) && EQ(result) && EQ(graph_released) && EQ(uncertain_close_seen);
#undef EQ
    return same;
}
static bool setup_equal(const struct mbog_evidence *a,const struct mbog_evidence *b)
{
    for(unsigned i=0;i<MBOG_SETUP_STAGES;i++)if(!call_equal(a->setup[i],b->setup[i]))return false;
    return a->take_attempted==b->take_attempted && a->start_attempted==b->start_attempted &&
        a->terminal_stage==b->terminal_stage && a->terminal_result==b->terminal_result &&
        a->validated_post_valid==b->validated_post_valid &&
        state_equal(&a->validated_post,&b->validated_post);
}
static bool acquisition_equal(const struct mbog_evidence *a,const struct mbog_evidence *b)
{
    return setup_equal(a,b) && a->claim.phase==b->claim.phase &&
        a->claim.claim_outcome==b->claim.claim_outcome && a->claim.retained_fd==b->claim.retained_fd &&
        !memcmp(a->claim.preflight_wire,b->claim.preflight_wire,208) &&
        !memcmp(a->claim.claim_command,b->claim.claim_command,48) &&
        !memcmp(a->claim.postflight_wire,b->claim.postflight_wire,208) &&
        a->adapter.phase==b->adapter.phase && a->adapter.retained_fd==b->adapter.retained_fd &&
        !memcmp(a->adapter.validation_wire,b->adapter.validation_wire,208) &&
        a->broker.phase==b->broker.phase &&
        a->broker.initialize_attempted==b->broker.initialize_attempted &&
        a->broker.validation_attempted==b->broker.validation_attempted &&
        a->broker.validation_returned==b->broker.validation_returned &&
        a->broker.validation_result==b->broker.validation_result &&
        state_equal(&a->broker.validation_state,&b->broker.validation_state);
}
#define FD_COUNT 16
static struct file original,other;
static struct file *fds[FD_COUNT];
enum event { EV_STATE,EV_CLAIM,EV_DUP,EV_FRAME,EV_CLOSE,
             EV_ATTR_INIT,EV_ATTR_TYPE,EV_MUTEX_INIT,EV_MUTEX_DESTROY,EV_ATTR_DESTROY };
enum sync_kind { S_ATTR_INIT,S_ATTR_TYPE,S_MUTEX_INIT,S_MUTEX_DESTROY,S_ATTR_DESTROY,S_COUNT };
struct observation {
    enum event event;
    unsigned ordinal;
    int fd,rc;
    bool published,transferred;
    struct mbog_evidence e;
};
static struct observation observations[32];
static unsigned observation_count,states,claims,duplicates,frames,closes,native_calls,releases;
static unsigned sync_calls[S_COUNT],sync_real[S_COUNT];
static int sync_failure[S_COUNT],close_fds[8];
static struct mbog_graph *watch;
static struct mbog_graph **watch_slot;
static int *caller_slot;
static unsigned partial_state_on,alias_state_on;
static bool duplicate_failure,claim_uncertain,legacy_before_claim;
static int uncertain_close_fd;
static uint8_t frame_wire[200];

static struct mbog_evidence evidence(struct mbog_graph *g)
{ struct mbog_evidence e={0};assert(!mbog_inspect(g,&e));return e; }
static void observe(enum event event,unsigned ordinal,int fd)
{
    if(!watch)return;
    assert(observation_count<32);
    struct observation *o=&observations[observation_count++];
    memset(o,0,sizeof(*o));o->event=event;o->ordinal=ordinal;o->fd=fd;
    o->published=watch_slot && *watch_slot==watch;
    o->transferred=caller_slot && *caller_slot==-1;
    o->rc=mbog_inspect(watch,&o->e);
}
static const struct observation *find_observation(enum event event,unsigned ordinal)
{
    for(unsigned i=0;i<observation_count;i++)
        if(observations[i].event==event && observations[i].ordinal==ordinal)return &observations[i];
    return NULL;
}
static bool observed_stage(enum event event,unsigned ordinal,enum mbog_stage stage)
{
    const struct observation *o=find_observation(event,ordinal);
    return o && !o->rc && o->published && o->transferred && o->e.current_stage==stage &&
        o->e.setup[stage].attempted && !o->e.setup[stage].returned;
}
static void raw_close(int fd)
{
    assert(fd>=0 && fd<FD_COUNT && fds[fd]);
    struct file *f=fds[fd];fds[fd]=NULL;
    for(int i=0;i<FD_COUNT;i++)if(fds[i]==f)return;
    releases++;assert(!ctrl_release(NULL,f));
}
static void legacy(unsigned seed)
{
    uint8_t packet[153]={2};pattern(packet+1,seed);
    assert(ctrl_write(&other,(char*)packet,sizeof packet,NULL)==153);
}
static void alias_frame(unsigned seed)
{
    struct az_overlay_frame f={.header={.version=2,
        .instance={mixer_instance[0],mixer_instance[1]},.run_id=mixer_run,
        .epoch=mixer_epoch,.sequence=mixer_sequence+1}};
    pattern(f.frame,seed);
    assert(!ctrl_ioctl(&original,AZ_OVERLAY_FRAME,(unsigned long)&f));
}
struct mba_sys_result mba_sys_duplicate(int fd)
{
    duplicates++;observe(EV_DUP,duplicates,fd);
    if(duplicate_failure)return(struct mba_sys_result){-1,EMFILE};
    if(fd<0 || fd>=FD_COUNT || !fds[fd])return(struct mba_sys_result){-1,EBADF};
    assert(!fds[8]);fds[8]=fds[fd];return(struct mba_sys_result){8,0};
}
struct mba_sys_result mba_sys_ioctl(int fd,unsigned long op,void *argument)
{
    assert(argument);
    if(op==AZ_OVERLAY_STATE){states++;observe(EV_STATE,states,fd);
        if(alias_state_on==states)alias_frame(87);
        if(partial_state_on==states)fail_to=to_calls+1;
    }else if(op==AZ_OVERLAY_CLAIM){claims++;observe(EV_CLAIM,claims,fd);
        if(legacy_before_claim)legacy(87);
    }else if(op==AZ_OVERLAY_FRAME){
        frames++;memcpy(frame_wire,argument,200);observe(EV_FRAME,frames,fd);
    }else assert(!"unexpected graph native operation");
    if(fd<0 || fd>=FD_COUNT || !fds[fd])return(struct mba_sys_result){-1,EBADF};
    native_calls++;
    long rc=ctrl_ioctl(fds[fd],(unsigned)op,(unsigned long)argument);
    if(op==AZ_OVERLAY_STATE && partial_state_on==states)fail_to=0;
    /* Modeled lost successful CLAIM return, not a claim about actual Linux. */
    if(op==AZ_OVERLAY_CLAIM && claim_uncertain && !rc)return(struct mba_sys_result){-1,EIO};
    return(struct mba_sys_result){rc<0?-1:(int)rc,rc<0?(int)-rc:0};
}
struct mba_sys_result mba_sys_close(int fd)
{
    assert(closes<8);close_fds[closes++]=fd;observe(EV_CLOSE,closes,fd);
    if(fd<0 || fd>=FD_COUNT || !fds[fd])return(struct mba_sys_result){-1,EBADF};
    raw_close(fd);
    /* Explicit fixture accounting knows this close took effect before EINTR. */
    return fd==uncertain_close_fd?(struct mba_sys_result){-1,EINTR}:(struct mba_sys_result){0,0};
}
static int sync_enter(enum sync_kind kind,enum event event)
{ sync_calls[kind]++;observe(event,sync_calls[kind],-1);return sync_failure[kind]; }
int mbrs_attr_init(pthread_mutexattr_t *p)
{
    int rc=sync_enter(S_ATTR_INIT,EV_ATTR_INIT);if(rc)return rc;
    sync_real[S_ATTR_INIT]++;return pthread_mutexattr_init(p);
}
int mbrs_attr_settype(pthread_mutexattr_t *p,int type)
{
    int rc=sync_enter(S_ATTR_TYPE,EV_ATTR_TYPE);if(rc)return rc;
    sync_real[S_ATTR_TYPE]++;return pthread_mutexattr_settype(p,type);
}
int mbrs_mutex_init(pthread_mutex_t *p,const pthread_mutexattr_t *a)
{
    int rc=sync_enter(S_MUTEX_INIT,EV_MUTEX_INIT);if(rc)return rc;
    sync_real[S_MUTEX_INIT]++;return pthread_mutex_init(p,a);
}
int mbrs_mutex_destroy(pthread_mutex_t *p)
{
    int rc=sync_enter(S_MUTEX_DESTROY,EV_MUTEX_DESTROY);if(rc)return rc;
    sync_real[S_MUTEX_DESTROY]++;return pthread_mutex_destroy(p);
}
int mbrs_attr_destroy(pthread_mutexattr_t *p)
{
    int rc=sync_enter(S_ATTR_DESTROY,EV_ATTR_DESTROY);if(rc)return rc;
    sync_real[S_ATTR_DESTROY]++;return pthread_mutexattr_destroy(p);
}
static struct az_overlay_state raw_state(void)
{
    struct az_overlay_state s={0};assert(!ctrl_ioctl(&original,AZ_OVERLAY_STATE,(unsigned long)&s));
    return s;
}
static struct mb_state semantic(struct az_overlay_state s)
{
    struct mb_state out={.instance={s.instance[0],s.instance[1]},.run_id=s.run_id,
        .epoch=s.epoch,.sequence=s.sequence,.flags=s.flags,.crc_len=s.crc_len};
    memcpy(out.frame,s.frame,152);return out;
}
static void reset(void)
{
    from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;
    admin=1;allocation_failure=interrupted=0;partial_bytes=104;gate.waiting=gate.resume=0;
    memset(fds,0,sizeof fds);memset(&original,0,sizeof original);memset(&other,0,sizeof other);
    original.f_mode=other.f_mode=FMODE_WRITE;fds[3]=&original;fds[4]=&other;
    states=claims=duplicates=frames=closes=native_calls=releases=observation_count=0;
    memset(sync_calls,0,sizeof sync_calls);memset(sync_real,0,sizeof sync_real);
    memset(sync_failure,0,sizeof sync_failure);memset(close_fds,0,sizeof close_fds);
    memset(observations,0,sizeof observations);memset(frame_wire,0,sizeof frame_wire);
    watch=NULL;watch_slot=NULL;caller_slot=NULL;partial_state_on=alias_state_on=0;
    duplicate_failure=claim_uncertain=legacy_before_claim=false;uncertain_close_fd=-1;
    memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);
    int crc[4]={28,38,96,38};memcpy(spi_crc_len,crc,sizeof crc);n_crc=4;
    mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;
    allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);
    random_wait_result=0;random_wait_calls=random_fill_calls=0;assert(!mixer_instance_init());testmode_held=0;
    legacy(31);
}
static struct mbog_plan plan(void)
{
    struct mbog_plan p={.claim={.expected_pre=semantic(raw_state()),
        .new_run=UINT64_C(0x4142434445464748)},.producer_id=99};
    for(unsigned i=0;i<16;i++)p.session[i]=(uint8_t)(0x51+i);return p;
}
static struct mb_state expected_post(const struct mbog_plan *p)
{
    struct mb_state s=p->claim.expected_pre;
    s.run_id=p->claim.new_run;s.epoch++;s.sequence=0;s.flags=MB_REQUIRED_FLAGS;return s;
}
static struct mbog_graph *create(const struct mbog_plan *p)
{ struct mbog_graph *g=NULL;assert(!mbog_create(p,&g) && g);return g; }
static struct mbog_graph *taken(struct mbog_plan *p,int *caller)
{
    struct mbog_graph *g=create(p);*caller=3;assert(!mbog_take(g,caller));return g;
}
static void watch_graph(struct mbog_graph **g,int *caller)
{ watch=*g;watch_slot=g;caller_slot=caller; }
static unsigned io_count(void)
{
    unsigned n=states+claims+duplicates+frames+closes;
    for(unsigned i=0;i<S_COUNT;i++)n+=sync_calls[i];return n;
}
static bool no_children(const struct mbog_evidence *e)
{ return !e->claim_present && !e->adapter_present && !e->broker_present && !e->dispatch_present; }
static bool no_exchange(struct mbog_graph *g)
{
    uint8_t request[1]={0},reply[1]={0};size_t written=9;unsigned before=io_count();
    return mbog_exchange(g,request,1,reply,1,&written)==MBWD_CLOSED && !written && io_count()==before;
}
static bool no_restart(struct mbog_graph *g)
{
    struct mbog_evidence before=evidence(g);unsigned io=io_count();
    int rc=mbog_start(g),alternate=4;int take_again=mbog_take(g,&alternate);
    struct mbog_evidence after=evidence(g);
    return rc==MBOG_STATE && take_again==MBOG_STATE && alternate==4 &&
        before.phase==after.phase && setup_equal(&before,&after) && io_count()==io;
}
static bool failed_at(const struct mbog_evidence *e,enum mbog_stage stage,int raw)
{
    return e->phase==MBOG_TERMINAL && e->terminal_stage==stage && e->terminal_result==raw &&
        e->setup[stage].attempted && e->setup[stage].returned && e->setup[stage].result==raw;
}
static void cleanup(struct mbog_graph **g)
{
    /* All serial fixture calls have returned. We independently account for
     * modeled mutation/close outcomes, remove PRE-call sync refusals, and
     * explicitly retire remaining children. No live recovery is authorized. */
    watch=NULL;watch_slot=NULL;caller_slot=NULL;partial_state_on=alias_state_on=0;
    duplicate_failure=claim_uncertain=legacy_before_claim=false;uncertain_close_fd=-1;
    memset(sync_failure,0,sizeof sync_failure);fail_from=fail_to=0;admin=1;
    if(g && *g){
        struct mbog_dispose_report r;int rc=mbog_dispose(g,&r);
        assert(rc==MBOG_OK || rc==MBOG_UNCERTAIN);
        if(r.graph_released)*g=NULL;
        if(*g){struct mbog_evidence e=evidence(*g);assert(no_children(&e));
            assert(!mbog_finalize(g) && !*g);}
    }
    for(int fd=0;fd<FD_COUNT;fd++)if(fds[fd])raw_close(fd);
}
/* Exactly one final functional oracle per selector. Observations precede
 * synthetic externally-quiescent cleanup; assertions are not control oracles. */
static void retain_case(void)
{
    reset();struct mbog_plan p=plan(),saved=p;struct mbog_graph *g=create(&p);
    p.producer_id++;p.session[0]^=1;p.claim.expected_pre.frame[151]^=1;
    struct mbog_evidence before=evidence(g);int early=mbog_start(g);
    struct mbog_evidence unchanged=evidence(g);
    fds[0]=fds[3];fds[3]=NULL;int caller=0;int rc=mbog_take(g,&caller);
    struct mbog_evidence held=evidence(g);int alternate=4;int repeated_take=mbog_take(g,&alternate);
    struct mbog_evidence repeated=evidence(g);
    bool ok=repeated_take==MBOG_STATE && alternate==4 && setup_equal(&held,&repeated) &&
        held.phase==repeated.phase && held.claim.retained_fd==repeated.claim.retained_fd &&
        before.phase==MBOG_ALLOCATED && no_children(&before) && !io_count() &&
        before.plan.producer_id==saved.producer_id && !memcmp(before.plan.session,saved.session,16) &&
        state_equal(&before.plan.claim.expected_pre,&saved.claim.expected_pre) &&
        early==MBOG_STATE && setup_equal(&before,&unchanged) && !rc && caller==-1 &&
        held.phase==MBOG_RETAINED && held.claim_present && held.claim_evidence_valid &&
        held.claim.phase==MBCO_FD_RETAINED && held.claim.descriptor_held &&
        held.claim.retained_fd==0 && fds[0]==&original && !held.adapter_present &&
        !held.broker_present && !held.dispatch_present && !io_count() && no_exchange(g);
    cleanup(&g);
    for(unsigned i=0;i<4;i++){
        reset();p=plan();int caller_unchanged=3;
        if(i==0)p.claim.expected_pre.flags=16;
        if(i==1){p.claim.expected_pre.flags=17;p.claim.expected_pre.run_id=2;p.claim.expected_pre.epoch=1;}
        if(i==2)p.producer_id=0;
        if(i==3)memset(p.session,0,16);
        g=(void*)(uintptr_t)1;int rejected=mbog_create(&p,&g);
        bool one=rejected==MBOG_INVALID && !g && caller_unchanged==3 && fds[3]==&original && !io_count();
        ok=one&&ok;if(g==(void*)(uintptr_t)1)g=NULL;cleanup(&g);
    }
    ck(ok,"graph_captures_plan_and_original_before_any_io");
}
static void armed_chain_case(void)
{
    reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
    watch_graph(&g,&caller);int rc=mbog_start(g);struct mbog_evidence e=evidence(g);
    bool ok=!rc && e.phase==MBOG_READY && e.claim_present && e.adapter_present &&
        e.broker_present && e.dispatch_present && e.claim.phase==MBCO_READY &&
        e.adapter.phase==MBRA_READY && e.broker.phase==MBRO_READY &&
        e.dispatch.phase==MBWD_RETAINED && states==4 && claims==1 && duplicates==1 &&
        native_calls==5 && !frames && !closes &&
        observed_stage(EV_STATE,1,MBOG_STAGE_PREFLIGHT) &&
        observed_stage(EV_CLAIM,1,MBOG_STAGE_CLAIM) &&
        observed_stage(EV_STATE,2,MBOG_STAGE_POSTFLIGHT) &&
        observed_stage(EV_DUP,1,MBOG_STAGE_DUPLICATE) &&
        observed_stage(EV_STATE,3,MBOG_STAGE_ADAPTER_VALIDATE) &&
        observed_stage(EV_ATTR_INIT,1,MBOG_STAGE_BROKER_INITIALIZE) &&
        observed_stage(EV_ATTR_TYPE,1,MBOG_STAGE_BROKER_INITIALIZE) &&
        observed_stage(EV_MUTEX_INIT,1,MBOG_STAGE_BROKER_INITIALIZE) &&
        observed_stage(EV_STATE,4,MBOG_STAGE_BROKER_VALIDATE);
    for(unsigned i=1;i<MBOG_SETUP_STAGES;i++)
        ok=(e.setup[i].attempted && e.setup[i].returned && !e.setup[i].result)&&ok;
    for(unsigned i=0;i<observation_count;i++){
        const struct observation *o=&observations[i];
        bool lower=o->e.claim_present && o->e.claim_evidence_valid && o->e.claim.descriptor_held;
        if(o->event==EV_DUP)lower=lower && o->e.adapter_present &&
            o->e.adapter.phase==MBRA_DUPLICATE_ARMED && !o->e.adapter.descriptor_held;
        if(o->event==EV_STATE && o->ordinal==3)lower=lower && o->e.adapter_present &&
            o->e.adapter.descriptor_held && o->e.adapter.phase==MBRA_VALIDATE_ARMED;
        if(o->event>=EV_ATTR_INIT || (o->event==EV_STATE && o->ordinal==4))
            lower=lower && o->e.adapter_present && o->e.adapter.phase==MBRA_READY &&
                o->e.broker_present && !o->e.dispatch_present &&
                !o->e.broker.backend.context && !o->e.broker.backend.snapshot && !o->e.broker.backend.submit;
        if(o->event==EV_ATTR_INIT)lower=lower && o->e.broker.phase==MBRO_ATTR_INIT_ARMED;
        if(o->event==EV_ATTR_TYPE)lower=lower && o->e.broker.phase==MBRO_ATTR_SETTYPE_ARMED && o->e.broker.attr_held;
        if(o->event==EV_MUTEX_INIT)lower=lower && o->e.broker.phase==MBRO_MUTEX_INIT_ARMED && o->e.broker.attr_held;
        if(o->event==EV_STATE && o->ordinal==4)lower=lower &&
            o->e.broker.phase==MBRO_VALIDATE_ARMED && o->e.broker.mutex_held;
        ok=lower&&ok;
    }
    ok=no_restart(g)&&ok;cleanup(&g);
    ck(ok,"graph_retains_each_child_before_its_native_or_sync_call");
}
static void preflight_stop_case(void)
{
    reset();struct mbog_plan p=plan();struct az_overlay_state raw=raw_state();int caller;
    struct mbog_graph *g=taken(&p,&caller);partial_state_on=1;
    int rc=mbog_start(g);struct mbog_evidence e=evidence(g);
    bool ok=rc==MBOG_CHILD && failed_at(&e,MBOG_STAGE_PREFLIGHT,EFAULT) &&
        e.claim_present && e.claim.descriptor_held && e.claim.retained_fd==3 &&
        !memcmp(e.claim.preflight_wire,&raw,104) && zero_bytes(e.claim.preflight_wire+104,104) &&
        !e.adapter_present && !e.broker_present && !e.dispatch_present &&
        states==1 && !claims && !duplicates && !closes && !frames && fds[3]==&original &&
        no_restart(g) && no_exchange(g);
    cleanup(&g);ck(ok,"graph_preflight_failure_keeps_original_and_stops_acquisition");
}
static void claim_unknown_case(void)
{
    reset();struct mbog_plan p=plan();struct mb_state post=expected_post(&p);int caller;
    struct mbog_graph *g=taken(&p,&caller);claim_uncertain=true;
    int rc=mbog_start(g);struct mbog_evidence e=evidence(g);struct mb_state actual=semantic(raw_state());
    bool ok=rc==MBOG_CHILD && failed_at(&e,MBOG_STAGE_CLAIM,EIO) &&
        e.claim_present && e.claim.claim_outcome==MB_OUTCOME_UNKNOWN &&
        e.claim.claim_result.result==-1 && e.claim.claim_result.error==EIO &&
        !e.claim.postflight_attempted && !e.setup[MBOG_STAGE_POSTFLIGHT].attempted &&
        !e.validated_post_valid && !e.adapter_present && !e.broker_present && !e.dispatch_present &&
        states==1 && claims==1 && !duplicates && !closes && !frames &&
        mixer_owner==&original && mixer_fenced && state_equal(&post,&actual) &&
        no_restart(g) && no_exchange(g);
    cleanup(&g);ck(ok,"graph_uncertain_claim_keeps_native_ownership_without_promotion");
}
static void postflight_stop_case(void)
{
    reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
    legacy_before_claim=true;int rc=mbog_start(g);struct mbog_evidence e=evidence(g);
    struct mb_state actual=semantic(raw_state()),post=expected_post(&p);pattern(post.frame,87);
    bool ok=rc==MBOG_CHILD && failed_at(&e,MBOG_STAGE_POSTFLIGHT,ESTALE) &&
        e.claim_present && e.claim.claim_outcome==MB_OUTCOME_COMMITTED &&
        e.claim.claim_returned && !e.claim.claim_result.result && e.claim.postflight_returned &&
        !e.claim.validated_post_valid && !e.adapter_present && !e.broker_present && !e.dispatch_present &&
        states==2 && claims==1 && !duplicates && !closes && !frames &&
        state_equal(&actual,&post) && mixer_owner==&original && mixer_fenced &&
        no_restart(g) && no_exchange(g);
    cleanup(&g);ck(ok,"graph_postclaim_mismatch_preserves_commit_and_stops_before_duplicate");
}
static void duplicate_stop_case(void)
{
    reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
    duplicate_failure=true;watch_graph(&g,&caller);int rc=mbog_start(g);struct mbog_evidence e=evidence(g);
    bool ok=rc==MBOG_CHILD && failed_at(&e,MBOG_STAGE_DUPLICATE,EMFILE) &&
        observed_stage(EV_DUP,1,MBOG_STAGE_DUPLICATE) &&
        e.claim_present && e.claim.phase==MBCO_READY && e.adapter_present &&
        e.adapter.phase==MBRA_TERMINAL && e.adapter.duplicate_returned &&
        e.adapter.duplicate_result.result==-1 && e.adapter.duplicate_result.error==EMFILE &&
        !e.adapter.descriptor_held && !e.adapter.validation_attempted &&
        !e.broker_present && !e.dispatch_present && states==2 && claims==1 && duplicates==1 &&
        !closes && !frames && fds[3]==&original && !fds[8] && no_restart(g) && no_exchange(g);
    cleanup(&g);ck(ok,"graph_duplicate_failure_keeps_claim_and_adapter_without_cleanup");
}
static void adapter_state_stop_case(void)
{
    reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
    partial_state_on=3;int rc=mbog_start(g);struct mbog_evidence e=evidence(g);
    struct az_overlay_state raw=raw_state();
    bool ok=rc==MBOG_CHILD && failed_at(&e,MBOG_STAGE_ADAPTER_VALIDATE,EFAULT) &&
        e.claim_present && e.adapter_present && e.adapter.descriptor_held && e.adapter.retained_fd==8 &&
        e.adapter.validation_returned && e.adapter.validation_result.error==EFAULT &&
        !memcmp(e.adapter.validation_wire,&raw,104) && zero_bytes(e.adapter.validation_wire+104,104) &&
        !e.broker_present && !e.dispatch_present && states==3 && claims==1 && duplicates==1 &&
        fds[3]==&original && fds[8]==&original && !closes && !frames &&
        no_restart(g) && no_exchange(g);
    cleanup(&g);ck(ok,"graph_adapter_validation_failure_keeps_both_descriptors");
}
static void broker_init_stop_case(void)
{
    reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
    sync_failure[S_MUTEX_INIT]=EAGAIN;int rc=mbog_start(g);struct mbog_evidence e=evidence(g);
    bool ok=rc==MBOG_CHILD && failed_at(&e,MBOG_STAGE_BROKER_INITIALIZE,MB_ERR_MUTEX) &&
        e.claim_present && e.adapter_present && e.broker_present && !e.dispatch_present &&
        e.broker.attr_held && !e.broker.mutex_held && e.broker.mutex_init_returned &&
        e.broker.mutex_init_result==EAGAIN && !e.broker.validation_attempted &&
        !e.broker.backend.context && !e.broker.backend.snapshot && !e.broker.backend.submit &&
        states==3 && claims==1 && duplicates==1 && !closes && !frames &&
        sync_calls[S_ATTR_INIT]==1 && sync_calls[S_ATTR_TYPE]==1 && sync_calls[S_MUTEX_INIT]==1 &&
        !sync_real[S_MUTEX_INIT] && !sync_calls[S_MUTEX_DESTROY] && !sync_calls[S_ATTR_DESTROY] &&
        fds[3]==&original && fds[8]==&original && no_restart(g) && no_exchange(g);
    cleanup(&g);ck(ok,"graph_broker_initialization_failure_keeps_all_lower_resources");
}
static void broker_state_stop_case(void)
{
    reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
    alias_state_on=4;int rc=mbog_start(g);struct mbog_evidence e=evidence(g);
    struct mb_state actual=semantic(raw_state());
    bool ok=rc==MBOG_CHILD && failed_at(&e,MBOG_STAGE_BROKER_VALIDATE,MB_ERR_STATE) &&
        e.claim_present && e.adapter_present && e.broker_present && !e.dispatch_present &&
        e.broker.attr_held && e.broker.mutex_held && e.broker.validation_returned &&
        !e.broker.validation_result && !e.broker.validation_matched &&
        state_equal(&e.broker.validation_state,&actual) && actual.sequence==1 &&
        e.validated_post_valid && e.validated_post.sequence==0 &&
        states==4 && claims==1 && duplicates==1 && !frames && !closes &&
        !sync_calls[S_MUTEX_DESTROY] && !sync_calls[S_ATTR_DESTROY] &&
        no_restart(g) && no_exchange(g);
    cleanup(&g);ck(ok,"graph_broker_snapshot_divergence_prevents_dispatcher_creation");
}
static struct mbw_header header(const struct mbog_plan *p,uint32_t kind,uint32_t n,uint64_t exchange)
{
    struct mbw_header h={.kind=kind,.length=n,.exchange=exchange};
    memcpy(h.session,p->session,16);return h;
}
static void put32(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(24-8*i)); }
static void put64(uint8_t *p,uint64_t n)
{ for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(56-8*i)); }
static void state_bytes(uint8_t *p,const struct mb_state *s)
{
    put64(p,s->instance[0]);put64(p+8,s->instance[1]);put64(p+16,s->run_id);
    put64(p+24,s->epoch);put64(p+32,s->sequence);put32(p+40,s->flags);
    put32(p+44,(uint32_t)s->crc_len);memcpy(p+48,s->frame,152);
}
static bool frame_response(const struct mbog_plan *p,const struct mb_state *seed,
                           const uint8_t *request,const uint8_t *reply,size_t n,unsigned provenance)
{
    uint8_t expected[668]={0};memcpy(expected,"AZMBRPC1",8);
    put32(expected+8,1);put32(expected+12,0x80000002U);put32(expected+16,668);
    memcpy(expected+24,p->session,16);memcpy(expected+40,request+40,8);
    put32(expected+52,provenance);put32(expected+56,MB_OUTCOME_COMMITTED);put32(expected+60,MB_OP_FRAME);
    memcpy(expected+72,request+48,200);struct mb_state after=*seed;after.sequence++;
    memcpy(after.frame,request+96,152);put64(expected+272,after.instance[0]);put64(expected+280,after.instance[1]);
    put64(expected+288,after.run_id);put64(expected+296,after.epoch);put64(expected+304,after.sequence);
    put32(expected+312,1);memcpy(expected+316,after.frame,152);state_bytes(expected+468,&after);
    return n==668 && !memcmp(reply,expected,668);
}
static void ready_wire_case(void)
{
    reset();struct mbog_plan p=plan();struct mb_state seed=expected_post(&p);int caller;
    struct mbog_graph *g=taken(&p,&caller);int started=mbog_start(g);struct mbog_evidence before=evidence(g);
    struct mbw_header h=header(&p,MBW_ADOPT_REQUEST,256,1),parsed;
    struct mbw_adopt_request adopt={.state=seed,.producer_id=p.producer_id};
    uint8_t request[256]={0},reply[668]={0};size_t n=0,written=0;
    assert(!mbw_encode_adopt_request(&h,&adopt,request,sizeof request,&n));
    int ar=mbog_exchange(g,request,n,reply,sizeof reply,&written);struct mbw_adopt_response a={0};
    int ad=mbw_decode_adopt_response(reply,written,&parsed,&a);
    struct mb_request frame={.grant={.instance={seed.instance[0],seed.instance[1]},
        .run_id=seed.run_id,.epoch=seed.epoch,.producer_id=p.producer_id},.request_id=1};
    pattern(frame.frame,97);h=header(&p,MBW_FRAME_REQUEST,248,2);
    assert(!mbw_encode_frame_request(&h,&frame,request,sizeof request,&n));
    int fr=mbog_exchange(g,request,n,reply,sizeof reply,&written);
    bool positive=frame_response(&p,&seed,request,reply,written,0);
    struct mb_state actual=semantic(raw_state()),target=seed;target.sequence++;memcpy(target.frame,frame.frame,152);
    struct az_overlay_frame expected_wire={.header={.version=2,.instance={seed.instance[0],seed.instance[1]},
        .run_id=seed.run_id,.epoch=seed.epoch,.sequence=1}};
    memcpy(expected_wire.frame,frame.frame,152);
    /* Close dispatcher by a malformed record, then retrieve only its exact
     * historical cache. Graph Ready must not erase that diagnostic route. */
    uint8_t malformed[256];memcpy(malformed,request,n);malformed[0]^=1;
    int bad=mbog_exchange(g,malformed,n,reply,sizeof reply,&written);
    int cached=mbog_exchange(g,request,n,reply,sizeof reply,&written);
    struct mbog_evidence after=evidence(g);
    bool ok=!started && ar==MBWD_OK && ad==MBW_OK && !a.result && !a.provenance &&
        state_equal(&a.state,&seed) && a.grant.producer_id==p.producer_id &&
        fr==MBWD_OK && positive && state_equal(&actual,&target) &&
        !memcmp(frame_wire,&expected_wire,200) && bad==MBWD_INVALID && cached==MBWD_OK &&
        frame_response(&p,&seed,request,reply,written,1) &&
        after.phase==MBOG_READY && after.dispatch.phase==MBWD_TERMINAL &&
        after.dispatch.first_refusal==MBWD_INVALID && after.dispatch.response[55]==0 &&
        acquisition_equal(&before,&after) && after.last_exchange.attempted &&
        after.last_exchange.returned && !after.last_exchange.result &&
        states==5 && claims==1 && duplicates==1 && frames==1 && native_calls==7 && !closes;
    cleanup(&g);ck(ok,"graph_ready_exchange_uses_the_full_frozen_native_chain");
}
static void dispose_order_case(void)
{
    bool ok=true;
    for(unsigned attr_failure=0;attr_failure<2;attr_failure++){
        reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
        assert(!mbog_start(g));watch_graph(&g,&caller);
        sync_failure[attr_failure?S_ATTR_DESTROY:S_MUTEX_DESTROY]=attr_failure?EINVAL:EBUSY;
        struct mbog_dispose_report first;int rc=mbog_dispose(&g,&first);struct mbog_evidence e=evidence(g);
        const struct observation *o=find_observation(EV_MUTEX_DESTROY,1);
        const struct observation *a=find_observation(EV_ATTR_DESTROY,1);
        bool kept=rc==MBOG_CHILD && g && e.disposal_started && !e.dispatch_present &&
            e.dispatch_evidence_valid && e.broker_present && e.adapter_present && e.claim_present &&
            e.broker.attr_held && e.broker.mutex_held==(!attr_failure) &&
            first.dispatch_attempted && first.dispatch_returned && first.broker_attempted &&
            first.broker_returned && first.broker_result==MB_ERR_MUTEX &&
            first.broker_report.mutex_result==(attr_failure?0:EBUSY) &&
            first.broker_report.attr_attempted==(int)attr_failure &&
            first.broker_report.attr_result==(attr_failure?EINVAL:0) &&
            !first.adapter_close.attempted && !first.claim_close.attempted && !closes &&
            sync_calls[S_ATTR_DESTROY]==attr_failure &&
            o && !o->rc && o->e.current_stage==MBOG_STAGE_BROKER_DISPOSE &&
            !o->e.dispatch_present && o->e.broker_present && o->e.adapter_present && o->e.claim_present &&
            o->e.broker.phase==MBRO_DISPOSING &&
            (!attr_failure || (a && !a->rc && a->e.broker_present && !a->e.broker.mutex_held &&
                a->e.broker.attr_held && a->e.claim_present && a->e.adapter_present)) &&
            no_exchange(g) && mbog_finalize(&g)==MBOG_STATE;
        memset(sync_failure,0,sizeof sync_failure);struct mbog_dispose_report second;
        int retried=mbog_dispose(&g,&second);bool consumed=!g;
        const struct observation *dup=find_observation(EV_CLOSE,1),*orig=find_observation(EV_CLOSE,2);
        bool one=kept && !retried && consumed && second.graph_released &&
            !second.dispatch_attempted && second.broker_attempted && second.broker_returned &&
            second.broker_report.owner_released &&
            second.broker_report.mutex_attempted==(!attr_failure) &&
            closes==2 && close_fds[0]==8 && close_fds[1]==3 &&
            sync_calls[S_MUTEX_DESTROY]==(attr_failure?1u:2u) && sync_real[S_MUTEX_DESTROY]==1 &&
            sync_calls[S_ATTR_DESTROY]==(attr_failure?2u:1u) && sync_real[S_ATTR_DESTROY]==1 &&
            dup && !dup->rc && dup->e.current_stage==MBOG_STAGE_ADAPTER_DISPOSE &&
            !dup->e.adapter_present && dup->e.adapter_evidence_valid && dup->e.claim_present &&
            !dup->e.broker_present && dup->e.adapter_close.attempted && !dup->e.adapter_close.returned &&
            orig && !orig->rc && orig->e.current_stage==MBOG_STAGE_CLAIM_DISPOSE &&
            !orig->e.claim_present && orig->e.claim_evidence_valid && orig->e.claim_close.attempted &&
            !orig->e.claim_close.returned;
        ok=one&&ok;if(second.graph_released)g=NULL;
        cleanup(&g);
    }
    ck(ok,"graph_broker_disposal_failure_stops_before_descriptor_teardown");
}
static void close_uncertain_case(void)
{
    bool ok=true;
    for(unsigned which=0;which<2;which++){
        reset();struct mbog_plan p=plan();int caller;struct mbog_graph *g=taken(&p,&caller);
        assert(!mbog_start(g));uncertain_close_fd=which?3:8;
        struct mbog_dispose_report first;int rc=mbog_dispose(&g,&first);struct mbog_evidence e=evidence(g);
        bool one=rc==MBOG_UNCERTAIN && g && e.uncertain_close_seen && e.disposal_started &&
            !e.adapter_present && !e.broker_present && !e.dispatch_present &&
            e.claim_present==(!which) && e.adapter_close.attempted && e.adapter_close.returned &&
            e.adapter_close.report.attempted && !e.adapter_close.result &&
            closes==(which?2u:1u) && close_fds[0]==8 && !fds[8] &&
            e.adapter_close.report.call.result==(which?0:-1) &&
            e.adapter_close.report.call.error==(which?0:EINTR) &&
            (which?(e.claim_close.returned && e.claim_close.report.call.result==-1 &&
                    e.claim_close.report.call.error==EINTR && !fds[3]):
                   (!e.claim_close.attempted && fds[3]==&original)) &&
            no_exchange(g);
        if(!which && e.claim_present)one=(mbog_finalize(&g)==MBOG_STATE)&&one;
        uncertain_close_fd=-1;struct mbog_dispose_report second;int again=mbog_dispose(&g,&second);
        struct mbog_evidence after=evidence(g);
        bool retained=again==MBOG_UNCERTAIN && g && no_children(&after) &&
            after.uncertain_close_seen && after.adapter_close.report.call.result==(which?0:-1) &&
            after.adapter_close.report.call.error==(which?0:EINTR) &&
            closes==2 && close_fds[1]==3 && !second.adapter_close.attempted &&
            second.claim_close.attempted==(!which) && !fds[3] &&
            e.first_disposal_valid && e.last_disposal_valid &&
            after.first_disposal_valid && after.last_disposal_valid &&
            disposal_equal(&e.first_disposal,&first) &&
            disposal_equal(&after.first_disposal,&e.first_disposal) &&
            after.first_disposal.result==MBOG_UNCERTAIN && after.first_disposal.uncertain_close_seen &&
            after.disposal_attempts==(which?1u:2u) && second.attempt_number==(which?0u:2u) &&
            (which?disposal_equal(&after.last_disposal,&e.last_disposal):
                   disposal_equal(&after.last_disposal,&second));
        unsigned io=io_count();int finalized=mbog_finalize(&g);
        ok=(one && retained && !finalized && !g && io_count()==io)&&ok;
        cleanup(&g);
    }
    ck(ok,"graph_uncertain_close_is_retained_and_never_retried");
}
static void partial_dispose_case(void)
{
    bool ok=true;
    for(unsigned taken_child=0;taken_child<2;taken_child++){
        reset();struct mbog_plan p=plan();struct mbog_graph *g=create(&p);int caller=3;
        if(taken_child)assert(!mbog_take(g,&caller));
        watch_graph(&g,&caller);struct mbog_dispose_report r;int rc=mbog_dispose(&g,&r);bool consumed=!g;
        const struct observation *o=find_observation(EV_CLOSE,1);
        bool one=!rc && consumed && r.graph_released && r.completed && !r.result &&
            !r.dispatch_attempted && !r.broker_attempted && !r.adapter_close.attempted &&
            r.claim_close.attempted==(int)taken_child && closes==taken_child &&
            !states && !claims && !duplicates && !frames &&
            !sync_calls[S_ATTR_INIT] && !sync_calls[S_MUTEX_DESTROY] &&
            (taken_child?(caller==-1 && !fds[3] && o && !o->rc &&
                o->e.current_stage==MBOG_STAGE_CLAIM_DISPOSE && !o->e.claim_present &&
                o->e.claim_evidence_valid && o->e.claim.descriptor_held &&
                o->e.claim_close.attempted && !o->e.claim_close.returned):
                (caller==3 && fds[3]==&original));
        ok=one&&ok;if(r.graph_released)g=NULL;cleanup(&g);
    }
    ck(ok,"graph_partial_disposal_closes_only_transferred_resources");
}
int main(int argc,char **argv)
{
    alarm(30);const char *which=argc>1?argv[1]:"all";bool all=!strcmp(which,"all"),selected=false;
#define RUN(name,call) do{if(all || !strcmp(which,name)){selected=true;call;}}while(0)
    RUN("retain",retain_case());RUN("armed-chain",armed_chain_case());RUN("preflight-stop",preflight_stop_case());
    RUN("claim-unknown",claim_unknown_case());RUN("postflight-stop",postflight_stop_case());
    RUN("duplicate-stop",duplicate_stop_case());RUN("adapter-state-stop",adapter_state_stop_case());
    RUN("broker-init-stop",broker_init_stop_case());RUN("broker-state-stop",broker_state_stop_case());
    RUN("ready-wire",ready_wire_case());RUN("dispose-order",dispose_order_case());
    RUN("close-uncertain",close_uncertain_case());RUN("partial-dispose",partial_dispose_case());
#undef RUN
    if(!selected)return 2;printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
