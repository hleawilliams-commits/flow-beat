/* SPDX-License-Identifier: GPL-2.0-only */
/* Focused original-descriptor/CLAIM owner cases. Exact extracted kernel methods
 * run in owned memory; fd and syscall replies are synthetic. Inspection at
 * syscall entry is same-thread serialized observation, not concurrency proof.
 * No open, native endpoint, duplicate, installation or recovery is performed. */
#include "kernel-shim.inc"
#include "candidate.inc"
#include "claim.h"

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
static bool result_equal(struct mba_sys_result a,struct mba_sys_result b)
{ return a.result==b.result && a.error==b.error; }
static bool evidence_equal(const struct mbco_evidence *a,const struct mbco_evidence *b)
{
    return a->phase==b->phase && a->plan.new_run==b->plan.new_run &&
        state_equal(&a->plan.expected_pre,&b->plan.expected_pre) &&
        state_equal(&a->expected_post,&b->expected_post) &&
        a->descriptor_held==b->descriptor_held && a->retained_fd==b->retained_fd &&
        a->preflight_attempted==b->preflight_attempted &&
        a->preflight_returned==b->preflight_returned &&
        result_equal(a->preflight_result,b->preflight_result) &&
        !memcmp(a->preflight_wire,b->preflight_wire,208) &&
        a->claim_attempted==b->claim_attempted && a->claim_returned==b->claim_returned &&
        result_equal(a->claim_result,b->claim_result) && !memcmp(a->claim_command,b->claim_command,48) &&
        a->claim_outcome==b->claim_outcome && a->claim_error==b->claim_error &&
        a->postflight_attempted==b->postflight_attempted &&
        a->postflight_returned==b->postflight_returned &&
        result_equal(a->postflight_result,b->postflight_result) &&
        !memcmp(a->postflight_wire,b->postflight_wire,208) &&
        a->validated_post_valid==b->validated_post_valid &&
        state_equal(&a->validated_post,&b->validated_post) &&
        a->terminal_error==b->terminal_error;
}
#define FD_COUNT 16
static struct file original,other;
static struct file *fds[FD_COUNT];
static unsigned duplicates,states,claims,other_io,closes,native_calls,releases;
static int last_fd;
static struct mbco_owner *watch;
static int *watch_caller_slot;
static struct mbco_owner **watch_close_slot;
static struct mbco_evidence at_state,at_claim;
static bool state_seen,claim_seen,state_saw_transferred,claim_saw_transferred,close_saw_consumed;
static bool force_claim,apply_claim,force_close,force_state,clobber_claim_argument;
static struct mba_sys_result claim_reply,close_reply,state_reply;
static long last_claim_native;
static unsigned corrupt_wire;
static uint8_t last_claim_wire[48];
static bool state_input_zero;
static void raw_close(int fd)
{
    assert(fd>=0 && fd<FD_COUNT && fds[fd]);
    struct file *f=fds[fd];fds[fd]=NULL;
    for(int i=0;i<FD_COUNT;i++)if(fds[i]==f)return;
    releases++;assert(!ctrl_release(NULL,f));
}
static void corrupt_state(struct az_overlay_state *s,unsigned which)
{
    switch(which){
    case 1:s->instance[1]^=1;break;
    case 2:s->run_id++;break;
    case 3:s->epoch++;break;
    case 4:s->sequence++;break;
    case 5:s->flags^=8;break;
    case 6:s->crc_len=95;break;
    case 7:s->frame[96]^=1;break;
    case 8:s->frame[97]^=1;break;
    case 9:s->frame[151]^=1;break;
    case 10:s->reserved=1;break;
    case 11:s->instance[0]^=1;break;
    case 12:s->version=1;break;
    default:break;
    }
}
struct mba_sys_result mba_sys_duplicate(int fd)
{ (void)fd;duplicates++;return(struct mba_sys_result){-1,ENOSYS}; }
struct mba_sys_result mba_sys_ioctl(int fd,unsigned long op,void *argument)
{
    last_fd=fd;assert(argument);
    if(op==AZ_OVERLAY_STATE){
        states++;state_input_zero=zero_bytes(argument,208);
        if(watch){assert(!mbco_inspect(watch,&at_state));state_seen=true;
            state_saw_transferred=watch_caller_slot && *watch_caller_slot==-1;}
    }else if(op==AZ_OVERLAY_CLAIM){
        claims++;memcpy(last_claim_wire,argument,48);
        if(watch){assert(!mbco_inspect(watch,&at_claim));claim_seen=true;
            claim_saw_transferred=watch_caller_slot && *watch_caller_slot==-1;}
        if(force_claim && !apply_claim)return claim_reply;
    }else other_io++;
    if(fd<0 || fd>=FD_COUNT || !fds[fd])return(struct mba_sys_result){-1,EBADF};
    native_calls++;long rc=ctrl_ioctl(fds[fd],(unsigned)op,(unsigned long)argument);
    if(op==AZ_OVERLAY_CLAIM){
        last_claim_native=rc;
        /* Scripted mutable-syscall fixture only; the real input-only handler
         * does not overwrite this command buffer. */
        if(clobber_claim_argument)memset(argument,0xa7,48);
        if(force_claim)return claim_reply;
    }
    if(op==AZ_OVERLAY_STATE){
        if(!rc)corrupt_state(argument,corrupt_wire);
        if(force_state)return state_reply;
    }
    return(struct mba_sys_result){rc<0?-1:(int)rc,rc<0?(int)-rc:0};
}
struct mba_sys_result mba_sys_close(int fd)
{
    closes++;if(watch_close_slot)close_saw_consumed=!*watch_close_slot;
    if(fd<0 || fd>=FD_COUNT || !fds[fd])return(struct mba_sys_result){-1,EBADF};
    raw_close(fd);return force_close?close_reply:(struct mba_sys_result){0,0};
}
static struct az_overlay_state raw_state(void)
{
    struct az_overlay_state s={0};
    assert(!ctrl_ioctl(&original,AZ_OVERLAY_STATE,(unsigned long)&s));return s;
}
static struct mb_state semantic(struct az_overlay_state s)
{
    struct mb_state out={.instance={s.instance[0],s.instance[1]},.run_id=s.run_id,
        .epoch=s.epoch,.sequence=s.sequence,.flags=s.flags,.crc_len=s.crc_len};
    memcpy(out.frame,s.frame,152);return out;
}
static void legacy(unsigned seed)
{
    uint8_t packet[153]={2};pattern(packet+1,seed);
    assert(ctrl_write(&other,(char*)packet,sizeof packet,NULL)==153);
}
static void reset(bool injected)
{
    from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;
    admin=1;allocation_failure=interrupted=0;partial_bytes=104;gate.waiting=gate.resume=0;
    memset(fds,0,sizeof fds);memset(&original,0,sizeof original);memset(&other,0,sizeof other);
    original.f_mode=other.f_mode=FMODE_WRITE;fds[3]=&original;fds[4]=&other;
    duplicates=states=claims=other_io=closes=native_calls=releases=0;last_fd=-1;
    watch=NULL;watch_caller_slot=NULL;watch_close_slot=NULL;
    state_seen=claim_seen=state_saw_transferred=claim_saw_transferred=close_saw_consumed=false;
    memset(&at_state,0,sizeof at_state);memset(&at_claim,0,sizeof at_claim);
    force_claim=apply_claim=force_close=force_state=clobber_claim_argument=false;
    claim_reply=close_reply=state_reply=(struct mba_sys_result){0,0};
    last_claim_native=0;corrupt_wire=0;memset(last_claim_wire,0,sizeof last_claim_wire);state_input_zero=false;
    memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);
    int crc[4]={28,38,96,38};memcpy(spi_crc_len,crc,sizeof crc);n_crc=4;
    mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;
    allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);
    random_wait_result=0;random_wait_calls=random_fill_calls=0;assert(!mixer_instance_init());testmode_held=0;
    if(injected)legacy(31);
}
static struct mbco_plan plan(void)
{ return(struct mbco_plan){.expected_pre=semantic(raw_state()),.new_run=UINT64_C(0x4142434445464748)}; }
static struct mbco_owner *create(const struct mbco_plan *p)
{ struct mbco_owner *o=NULL;assert(!mbco_create(p,&o) && o);return o; }
static struct mbco_evidence evidence(struct mbco_owner *o)
{ struct mbco_evidence e={0};assert(!mbco_inspect(o,&e));return e; }
static struct mbco_owner *taken(struct mbco_plan *p,int *caller)
{ struct mbco_owner *o=create(p);*caller=3;assert(!mbco_take(o,caller));return o; }
static bool no_post_value(struct mbco_owner *o)
{
    struct mb_state out;memset(&out,0xa5,sizeof out);
    return mbco_get_validated_post(o,&out)==EPERM && zero_bytes(&out,sizeof out);
}
static bool repeats(struct mbco_owner *o)
{
    struct mbco_evidence before=evidence(o);
    unsigned s=states,c=claims,n=native_calls,x=other_io;
    int a=mbco_preflight(o),b=mbco_claim(o),d=mbco_postflight(o);
    struct mbco_evidence after=evidence(o);
    return a==(before.preflight_attempted?EALREADY:EPERM) &&
        b==(before.claim_attempted?EALREADY:EPERM) &&
        d==(before.postflight_attempted?EALREADY:EPERM) &&
        states==s && claims==c && native_calls==n && other_io==x &&
        evidence_equal(&before,&after);
}
static void cleanup(struct mbco_owner **o)
{
    /* Serial test memory only. All observations precede this explicit disposal.
     * This is neither live recovery permission nor a proven teardown protocol. */
    watch=NULL;watch_caller_slot=NULL;watch_close_slot=NULL;force_close=false;
    force_claim=force_state=false;corrupt_wire=0;fail_from=fail_to=0;admin=1;
    if(o && *o){struct mbco_close_report r;assert(!mbco_dispose(o,&r) && !*o);}
    for(int fd=0;fd<FD_COUNT;fd++)if(fds[fd])raw_close(fd);
}
static bool unchanged_pre(const struct mbco_plan *p)
{ struct mb_state s=semantic(raw_state());return state_equal(&p->expected_pre,&s); }
static bool held_terminal(const struct mbco_evidence *e,int error)
{ return e->phase==MBCO_TERMINAL && e->terminal_error==error && e->descriptor_held && e->retained_fd==3; }

static void transfer_case(void)
{
    reset(true);struct mbco_plan p=plan(),saved=p;struct mbco_owner *o=create(&p);
    p.new_run++;p.expected_pre.frame[151]^=1;
    int bad=-1;struct mbco_evidence before=evidence(o);
    int invalid=mbco_take(o,&bad);struct mbco_evidence unchanged=evidence(o);
    fds[0]=fds[3];fds[3]=NULL;int caller=0;int got=mbco_take(o,&caller);
    int other_slot=4;int again=mbco_take(o,&other_slot);
    struct mbco_evidence held=evidence(o);
    watch=o;watch_caller_slot=&caller;fail_to=to_calls+1;
    int pre=mbco_preflight(o);fail_to=0;struct mbco_evidence failed=evidence(o);
    bool ok=invalid==EINVAL && bad==-1 && evidence_equal(&before,&unchanged) &&
        !got && caller==-1 && again==EALREADY && other_slot==4 &&
        held.phase==MBCO_FD_RETAINED && held.descriptor_held && held.retained_fd==0 &&
        held.plan.new_run==saved.new_run && state_equal(&held.plan.expected_pre,&saved.expected_pre) &&
        pre==EFAULT && failed.descriptor_held && failed.retained_fd==0 && state_saw_transferred &&
        fds[0]==&original && states==1 && !claims && !duplicates && !other_io && !closes &&
        no_post_value(o) && repeats(o);
    cleanup(&o);
    for(unsigned which=0;which<12;which++){
        reset(true);p=plan();int retained=3;
        if(which==0)p.new_run=0;
        if(which==1)p.expected_pre.run_id=1;
        if(which==2)p.expected_pre.epoch=1;
        if(which==3)p.expected_pre.sequence=1;
        if(which==4){p.expected_pre.flags=25;p.expected_pre.epoch=1;}
        if(which==5){p.expected_pre.flags=25;p.expected_pre.run_id=1;}
        if(which==6){p.expected_pre.flags=25;p.expected_pre.run_id=1;p.expected_pre.epoch=UINT64_MAX;}
        if(which==7)p.expected_pre.instance[0]=p.expected_pre.instance[1]=0;
        if(which==8)p.expected_pre.crc_len=95;
        if(which==9)p.expected_pre.flags=31;
        if(which==10)p.expected_pre.flags|=32;
        if(which==11)p.expected_pre.flags=0;
        o=(void*)(uintptr_t)1;int invalid_plan=mbco_create(&p,&o);
        ok=(invalid_plan==EINVAL && !o && retained==3 && fds[3]==&original &&
            !states && !claims && !duplicates && !closes)&&ok;
        if(o!=(void*)(uintptr_t)1)cleanup(&o);else cleanup(NULL);
    }
    ck(ok,"claim_transfer_keeps_fd_zero_and_plan_before_any_io");
}
static void pre_arm_case(void)
{
    reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
    struct az_overlay_state raw=raw_state();watch=o;watch_caller_slot=&caller;
    int rc=mbco_preflight(o);struct mbco_evidence e=evidence(o);
    bool ok=!rc && state_seen && state_saw_transferred && at_state.phase==MBCO_PREFLIGHT_ARMED &&
        at_state.descriptor_held && at_state.retained_fd==3 && at_state.preflight_attempted &&
        !at_state.preflight_returned && zero_bytes(at_state.preflight_wire,208) &&
        e.phase==MBCO_PRE_MATCHED && e.preflight_returned && !e.preflight_result.result &&
        !memcmp(e.preflight_wire,&raw,208) && state_input_zero &&
        states==1 && native_calls==1 && !claims && !duplicates && !other_io && !closes &&
        no_post_value(o);
    cleanup(&o);ck(ok,"claim_prestate_is_armed_after_original_transfer_before_state_call");
}
static void pre_failure_case(void)
{
    bool ok=true;
    for(unsigned mode=0;mode<2;mode++){
        reset(true);struct az_overlay_state raw=raw_state();struct mbco_plan p=plan();
        if(mode)p.expected_pre.frame[151]^=1;
        int caller;struct mbco_owner *o=taken(&p,&caller);
        if(!mode)fail_to=to_calls+1;
        int rc=mbco_preflight(o);fail_to=0;struct mbco_evidence e=evidence(o);
        uint8_t bytes[208]={0};memcpy(bytes,&raw,mode?208:104);int error=mode?ESTALE:EFAULT;
        bool one=rc==error && held_terminal(&e,error) && e.preflight_attempted &&
            e.preflight_returned && !memcmp(e.preflight_wire,bytes,208) &&
            e.preflight_result.result==(mode?0:-1) && e.preflight_result.error==(mode?0:EFAULT) &&
            !e.claim_attempted && e.claim_outcome==MB_OUTCOME_NONE && !e.postflight_attempted &&
            no_post_value(o) && repeats(o) && states==1 && !claims && !other_io && !duplicates &&
            !closes && !mixer_owner && !mixer_fenced && fds[3]==&original;
        ok=one&&ok;cleanup(&o);
    }
    ck(ok,"claim_failed_prestate_keeps_raw_evidence_without_claim_or_close");
}
static void claim_arm_case(void)
{
    reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
    assert(!mbco_preflight(o));struct mbco_evidence pre=evidence(o);
    watch=o;watch_caller_slot=&caller;force_claim=apply_claim=true;claim_reply=(struct mba_sys_result){0,EINTR};
    clobber_claim_argument=true;
    int rc=mbco_claim(o);struct mbco_evidence e=evidence(o);
    struct az_overlay_command command={.version=2,.instance={p.expected_pre.instance[0],p.expected_pre.instance[1]},
        .run_id=p.new_run,.epoch=p.expected_pre.epoch,.sequence=0};
    bool ok=!rc && claim_seen && claim_saw_transferred && at_claim.phase==MBCO_CLAIM_ARMED &&
        at_claim.descriptor_held && at_claim.retained_fd==3 && at_claim.claim_attempted &&
        !at_claim.claim_returned && !memcmp(at_claim.claim_command,&command,48) &&
        !memcmp(last_claim_wire,&command,48) && !memcmp(e.claim_command,&command,48) &&
        !memcmp(at_claim.preflight_wire,pre.preflight_wire,208) &&
        e.phase==MBCO_CLAIM_COMMITTED && e.claim_returned &&
        result_equal(e.claim_result,claim_reply) && e.claim_outcome==MB_OUTCOME_COMMITTED &&
        !e.claim_error && !e.postflight_attempted && !last_claim_native &&
        mixer_owner==&original && mixer_fenced && states==1 && claims==1 &&
        !duplicates && !other_io && !closes && no_post_value(o);
    cleanup(&o);ck(ok,"claim_intent_and_exact_command_are_retained_before_committing_syscall");
}
static void input_fault_case(void)
{
    reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
    assert(!mbco_preflight(o));partial_bytes=24;fail_from=from_calls+1;
    int rc=mbco_claim(o);fail_from=0;struct mbco_evidence e=evidence(o);
    bool ok=rc==EFAULT && held_terminal(&e,EFAULT) && e.claim_returned &&
        e.claim_result.result==-1 && e.claim_result.error==EFAULT &&
        e.claim_outcome==MB_OUTCOME_REJECTED && e.claim_error==EFAULT &&
        !e.postflight_attempted && unchanged_pre(&p) && no_post_value(o) && repeats(o) &&
        states==1 && claims==1 && !duplicates && !other_io && !closes && fds[3]==&original;
    cleanup(&o);ck(ok,"claim_actual_partial_input_is_definite_rejection_without_acquisition");
}
static void busy_case(void)
{
    bool ok=true;
    for(unsigned same=0;same<2;same++){
        reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
        assert(!mbco_preflight(o));
        struct az_overlay_command competing={.version=2,.instance={p.expected_pre.instance[0],p.expected_pre.instance[1]},
            .run_id=99,.epoch=p.expected_pre.epoch};
        struct file *winner=same?&original:&other;
        assert(!ctrl_ioctl(winner,AZ_OVERLAY_CLAIM,(unsigned long)&competing));
        struct mb_state won=semantic(raw_state());int rc=mbco_claim(o);struct mbco_evidence e=evidence(o);
        struct mb_state after=semantic(raw_state());
        bool one=rc==EBUSY && held_terminal(&e,EBUSY) && e.claim_outcome==MB_OUTCOME_REJECTED &&
            e.claim_error==EBUSY && e.claim_result.result==-1 && e.claim_result.error==EBUSY &&
            !e.postflight_attempted && mixer_owner==winner && mixer_fenced &&
            state_equal(&won,&after) && no_post_value(o) && repeats(o) &&
            states==1 && claims==1 && !duplicates && !other_io && !closes;
        ok=one&&ok;cleanup(&o);
    }
    ck(ok,"claim_busy_rejects_this_attempt_without_claiming_original_is_unowned");
}
static void legacy_race_case(void)
{
    reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
    assert(!mbco_preflight(o));legacy(97);struct mb_state raced=semantic(raw_state());
    int claim=mbco_claim(o);struct az_overlay_state post=raw_state();
    int rc=mbco_postflight(o);struct mbco_evidence e=evidence(o);
    bool ok=!claim && rc==ESTALE && held_terminal(&e,ESTALE) &&
        e.claim_outcome==MB_OUTCOME_COMMITTED && !e.claim_error && !e.claim_result.result &&
        e.postflight_returned && !e.postflight_result.result && !memcmp(e.postflight_wire,&post,208) &&
        !e.validated_post_valid && !memcmp(post.frame,raced.frame,152) &&
        memcmp(post.frame,p.expected_pre.frame,152) && mixer_owner==&original && mixer_fenced &&
        post.run_id==p.new_run && post.epoch==p.expected_pre.epoch+1 && post.sequence==0 &&
        no_post_value(o) && repeats(o) && states==2 && claims==1 && native_calls==3 &&
        !duplicates && !other_io && !closes && fds[3]==&original;
    cleanup(&o);ck(ok,"claim_legacy_race_keeps_committed_owned_fenced_mismatch");
}
static void unknown_case(void)
{
    reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
    assert(!mbco_preflight(o));force_claim=apply_claim=true;claim_reply=(struct mba_sys_result){-1,EIO};
    int rc=mbco_claim(o);struct mbco_evidence before=evidence(o);
    struct mb_state actual=semantic(raw_state());int post=mbco_postflight(o);
    struct mbco_evidence after=evidence(o);
    bool ok=rc==EIO && held_terminal(&before,EIO) && result_equal(before.claim_result,claim_reply) &&
        before.claim_outcome==MB_OUTCOME_UNKNOWN && before.claim_error==EIO &&
        !last_claim_native && mixer_owner==&original && mixer_fenced &&
        state_equal(&actual,&before.expected_post) && post==EPERM &&
        !after.postflight_attempted && evidence_equal(&before,&after) &&
        no_post_value(o) && repeats(o) && states==1 && claims==1 && native_calls==2 &&
        !duplicates && !other_io && !closes && fds[3]==&original;
    cleanup(&o);ck(ok,"claim_postcommit_eio_stays_unknown_without_poststate_promotion");
}
static void classification_case(void)
{
    const struct mba_sys_result replies[]={{-1,EACCES},{1,0},{-2,EFAULT}};
    const int errors[]={EACCES,EPROTO,EPROTO};bool ok=true;
    for(unsigned apply=0;apply<2;apply++)for(unsigned i=0;i<sizeof replies/sizeof *replies;i++){
        reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
        assert(!mbco_preflight(o));force_claim=true;apply_claim=apply;claim_reply=replies[i];
        int rc=mbco_claim(o);struct mbco_evidence e=evidence(o);
        bool one=rc==errors[i] && held_terminal(&e,errors[i]) &&
            result_equal(e.claim_result,replies[i]) && e.claim_outcome==MB_OUTCOME_UNKNOWN &&
            e.claim_error==errors[i] && !e.postflight_attempted &&
            (apply?mixer_owner==&original && mixer_fenced:!mixer_owner && !mixer_fenced) &&
            no_post_value(o) && repeats(o) && states==1 && claims==1 &&
            native_calls==1+apply && !other_io && !duplicates && !closes;
        ok=one&&ok;cleanup(&o);
    }
    ck(ok,"claim_nonhandler_or_malformed_returns_remain_unknown_before_or_after_commit");
}
static void post_fault_case(void)
{
    reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
    assert(!mbco_preflight(o) && !mbco_claim(o));struct az_overlay_state raw=raw_state();
    watch=o;watch_caller_slot=&caller;fail_to=to_calls+1;
    int rc=mbco_postflight(o);fail_to=0;struct mbco_evidence e=evidence(o);
    uint8_t bytes[208]={0};memcpy(bytes,&raw,104);
    bool ok=rc==EFAULT && held_terminal(&e,EFAULT) &&
        state_seen && state_saw_transferred && at_state.phase==MBCO_POSTFLIGHT_ARMED &&
        at_state.postflight_attempted && !at_state.postflight_returned &&
        at_state.claim_outcome==MB_OUTCOME_COMMITTED && at_state.descriptor_held &&
        e.postflight_returned && e.postflight_result.result==-1 && e.postflight_result.error==EFAULT &&
        !memcmp(e.postflight_wire,bytes,208) && e.claim_outcome==MB_OUTCOME_COMMITTED &&
        !e.claim_error && !e.claim_result.result && !e.validated_post_valid &&
        mixer_owner==&original && mixer_fenced && no_post_value(o) && repeats(o) &&
        states==2 && claims==1 && !duplicates && !other_io && !closes;
    cleanup(&o);ck(ok,"claim_partial_poststate_preserves_committed_claim_and_raw_failed_copy");
}
static void post_mismatch_case(void)
{
    bool ok=true;
    for(unsigned which=1;which<=12;which++){
        reset(true);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
        assert(!mbco_preflight(o) && !mbco_claim(o));struct az_overlay_state raw=raw_state();
        corrupt_wire=which;corrupt_state(&raw,which);
        int rc=mbco_postflight(o);struct mbco_evidence e=evidence(o);
        int error=(which==6 || which==10 || which==12)?EPROTO:ESTALE;
        bool one=rc==error && held_terminal(&e,error) && e.postflight_returned &&
            !e.postflight_result.result && !memcmp(e.postflight_wire,&raw,208) &&
            e.claim_outcome==MB_OUTCOME_COMMITTED && !e.claim_error && !e.claim_result.result &&
            !e.validated_post_valid && mixer_owner==&original && mixer_fenced &&
            no_post_value(o) && repeats(o) && states==2 && claims==1 &&
            !duplicates && !other_io && !closes;
        ok=one&&ok;cleanup(&o);
    }
    ck(ok,"claim_poststate_checks_complete_tuple_mode_crc_and_tail_without_erasing_commit");
}
static void idle_case(void)
{
    reset(false);struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
    int a=mbco_preflight(o),b=mbco_claim(o),c=mbco_postflight(o);
    struct mbco_evidence e=evidence(o);struct mb_state out={0};int exported=mbco_get_validated_post(o,&out);
    struct mb_state actual=semantic(raw_state());
    bool ok=!a && !b && !c && !exported && p.expected_pre.flags==16 && out.flags==23 &&
        e.phase==MBCO_READY && e.validated_post_valid && state_equal(&out,&e.expected_post) &&
        state_equal(&out,&actual) && !memcmp(out.frame,p.expected_pre.frame,152) &&
        e.claim_outcome==MB_OUTCOME_COMMITTED && out.run_id==p.new_run &&
        out.epoch==p.expected_pre.epoch+1 && !out.sequence && out.crc_len==96 &&
        !has_inject[2] && repeats(o) && states==2 && claims==1 && native_calls==3 &&
        !duplicates && !other_io && !closes;
    cleanup(&o);ck(ok,"claim_idle_success_preserves_mode_and_bytes_without_automatic_frame");
}
static void fenced_case(void)
{
    bool ok=true;
    for(unsigned injected=0;injected<2;injected++){
        reset(injected);struct mb_state s=semantic(raw_state());
        struct az_overlay_command prior={.version=2,.instance={s.instance[0],s.instance[1]},.run_id=9};
        assert(!ctrl_ioctl(&other,AZ_OVERLAY_CLAIM,(unsigned long)&prior));
        prior.epoch=1;prior.sequence=1;
        assert(!ctrl_ioctl(&other,AZ_OVERLAY_RELEASE,(unsigned long)&prior));
        /* Boundary seed only: no claim that 2^64 native operations ran. */
        mixer_sequence=UINT64_MAX;
        struct mbco_plan p=plan();int caller;struct mbco_owner *o=taken(&p,&caller);
        int a=mbco_preflight(o),b=mbco_claim(o),c=mbco_postflight(o);
        struct mbco_evidence e=evidence(o);struct mb_state out={0};int exported=mbco_get_validated_post(o,&out);
        bool one=!a && !b && !c && !exported && p.expected_pre.flags==(injected?25u:17u) &&
            p.expected_pre.sequence==UINT64_MAX && p.expected_pre.run_id==9 &&
            out.flags==(injected?31u:23u) && out.epoch==2 && !out.sequence &&
            out.run_id==p.new_run && !memcmp(out.frame,p.expected_pre.frame,152) &&
            e.phase==MBCO_READY && e.claim_outcome==MB_OUTCOME_COMMITTED &&
            repeats(o) && states==2 && claims==1 && !duplicates && !other_io && !closes;
        ok=one&&ok;cleanup(&o);
    }
    ck(ok,"claim_fenced_unowned_prestate_accepts_old_max_sequence_without_unfencing");
}
static void disposal_case(void)
{
    bool ok=true;
    for(unsigned mode=0;mode<5;mode++){
        reset(true);struct mbco_plan p=plan();struct mbco_owner *o=create(&p);int caller=3;
        if(mode)assert(!mbco_take(o,&caller));
        if(mode>=2){if(mode==2)fail_to=to_calls+1;int pre=mbco_preflight(o);fail_to=0;assert(pre==(mode==2?EFAULT:0));}
        if(mode>=3){force_claim=apply_claim=true;claim_reply=(struct mba_sys_result){mode==3?-1:0,mode==3?EIO:0};
            int c=mbco_claim(o);assert(c==(mode==3?EIO:0));if(mode==4)assert(!mbco_postflight(o));}
        unsigned calls=states+claims+other_io;
        bool held=mode!=0;
        force_close=mode==3;close_reply=(struct mba_sys_result){-1,EINTR};
        watch_close_slot=&o;struct mbco_close_report r,again;
        int rc=mbco_dispose(&o,&r),twice=mbco_dispose(&o,&again);
        bool one=!rc && !twice && !o && r.attempted==held && closes==(held?1u:0u) &&
            (held?close_saw_consumed && !fds[3] && caller==-1:!close_saw_consumed && fds[3]==&original && caller==3) &&
            (mode==3?result_equal(r.call,close_reply):!r.call.result && !r.call.error) &&
            !again.attempted && !again.call.result && !again.call.error &&
            states+claims+other_io==calls && !duplicates &&
            (mode>=3?mixer_fenced && !mixer_owner:true);
        ok=one&&ok;watch_close_slot=NULL;cleanup(NULL);
    }
    ck(ok,"claim_disposal_consumes_only_transferred_original_once_without_recovery");
}
int main(int argc,char **argv)
{
    alarm(30);const char *which=argc>1?argv[1]:"all";bool all=!strcmp(which,"all"),selected=false;
#define RUN(name,call) do{if(all || !strcmp(which,name)){selected=true;call;}}while(0)
    RUN("transfer",transfer_case());RUN("pre-arm",pre_arm_case());RUN("pre-failure",pre_failure_case());
    RUN("claim-arm",claim_arm_case());RUN("claim-input-fault",input_fault_case());RUN("busy",busy_case());
    RUN("legacy-race",legacy_race_case());RUN("unknown",unknown_case());RUN("classification",classification_case());
    RUN("post-fault",post_fault_case());RUN("post-mismatch",post_mismatch_case());RUN("idle",idle_case());
    RUN("fenced",fenced_case());RUN("disposal",disposal_case());
#undef RUN
    if(!selected)return 2;
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
