/* SPDX-License-Identifier: GPL-2.0-only */
/* New retained-acquisition cases only. Frozen broker/wire TUs and seven exact
 * kernel methods execute under owned-memory fd/usercopy shims. The original
 * descriptor remains borrowed. Synchronous inspection inside a syscall fixture
 * is serialized same-thread observation, not concurrent/blocked-I/O proof. */
#include "kernel-shim.inc"
#include "candidate.inc"
#include "retained.h"
#include "guest_dispatch.h"

static unsigned checks, failures;
static void ck(bool ok, const char *label)
{ checks++; if (!ok) { failures++; fprintf(stderr,"FAIL %s\n",label); } }
static void pattern(uint8_t *p, unsigned seed)
{ for (unsigned i=0;i<152;i++) p[i]=(uint8_t)(seed+23*i); }
static bool zero_bytes(const void *p, size_t n)
{ const uint8_t *b=p; for (size_t i=0;i<n;i++) if (b[i]) return false; return true; }
static bool state_equal(const struct mb_state *a,const struct mb_state *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->sequence==b->sequence &&
        a->flags==b->flags && a->crc_len==b->crc_len && !memcmp(a->frame,b->frame,152);
}
static bool result_equal(struct mba_sys_result a,struct mba_sys_result b)
{ return a.result==b.result && a.error==b.error; }
static bool evidence_equal(const struct mbra_evidence *a,const struct mbra_evidence *b)
{
    return a->phase==b->phase && a->original_fd==b->original_fd &&
        state_equal(&a->expected,&b->expected) && a->descriptor_held==b->descriptor_held &&
        a->retained_fd==b->retained_fd && a->duplicate_attempted==b->duplicate_attempted &&
        a->duplicate_returned==b->duplicate_returned &&
        result_equal(a->duplicate_result,b->duplicate_result) &&
        a->validation_attempted==b->validation_attempted &&
        a->validation_returned==b->validation_returned &&
        result_equal(a->validation_result,b->validation_result) &&
        !memcmp(a->validation_wire,b->validation_wire,MBRA_STATE_BYTES) &&
        a->validated_state_valid==b->validated_state_valid &&
        state_equal(&a->validated_state,&b->validated_state) &&
        a->terminal_error==b->terminal_error;
}

#define FD_COUNT 32
static struct file original, replacement;
static struct file *fds[FD_COUNT];
static unsigned duplicates, states, mutations, native_calls, closes, releases;
static int duplicate_target, last_fd;
static bool force_duplicate, force_state_return, state_input_zero;
static struct mba_sys_result duplicate_reply, state_reply;
static bool force_close, close_saw_consumed;
static struct mba_sys_result close_reply;
static struct mbra_owner **watch_close_slot;
static unsigned malformed_wire;
static struct mbra_owner *watch;
static bool inspect_duplicate, inspect_state, duplicate_seen, state_seen;
static struct mbra_evidence at_duplicate, at_state;

static void raw_close(int fd)
{
    assert(fd>=0 && fd<FD_COUNT && fds[fd]);
    struct file *f=fds[fd];fds[fd]=NULL;
    for(int i=0;i<FD_COUNT;i++) if(fds[i]==f) return;
    releases++;assert(!ctrl_release(NULL,f));
}
static void corrupt_state(struct az_overlay_state *s,unsigned which)
{
    switch(which) {
    case 1:s->version=1;break;
    case 2:s->reserved=1;break;
    case 3:s->epoch=0;break;
    case 4:s->flags=0;break;
    case 5:s->crc_len=95;break;
    default:break;
    }
}
struct mba_sys_result mba_sys_duplicate(int fd)
{
    duplicates++;
    if(inspect_duplicate) {
        assert(watch && !mbra_inspect(watch,&at_duplicate));duplicate_seen=true;
    }
    if(force_duplicate && duplicate_reply.result<0) return duplicate_reply;
    if(fd<0 || fd>=FD_COUNT || !fds[fd]) return (struct mba_sys_result){-1,EBADF};
    int target=force_duplicate?duplicate_reply.result:duplicate_target;
    if(target!=fd) {
        assert(target>=0 && target<FD_COUNT && !fds[target]);fds[target]=fds[fd];
    }
    return (struct mba_sys_result){target,force_duplicate?duplicate_reply.error:0};
}
struct mba_sys_result mba_sys_ioctl(int fd,unsigned long op,void *argument)
{
    last_fd=fd;
    assert(argument);
    if(op==AZ_OVERLAY_STATE) {
        states++;state_input_zero=zero_bytes(argument,sizeof(struct az_overlay_state));
        if(inspect_state) {
            assert(watch && !mbra_inspect(watch,&at_state));state_seen=true;
        }
    } else {
        assert(op==AZ_OVERLAY_FRAME || op==AZ_OVERLAY_ROTATE || op==AZ_OVERLAY_RESTORE);
        mutations++;
    }
    if(fd<0 || fd>=FD_COUNT || !fds[fd]) return (struct mba_sys_result){-1,EBADF};
    native_calls++;
    long rc=ctrl_ioctl(fds[fd],(unsigned)op,(unsigned long)argument);
    if(op==AZ_OVERLAY_STATE && !rc) corrupt_state(argument,malformed_wire);
    if(op==AZ_OVERLAY_STATE && force_state_return) return state_reply;
    return (struct mba_sys_result){rc<0?-1:(int)rc,rc<0?(int)-rc:0};
}
struct mba_sys_result mba_sys_close(int fd)
{
    closes++;
    if(watch_close_slot) close_saw_consumed=!*watch_close_slot;
    if(fd<0 || fd>=FD_COUNT || !fds[fd]) return (struct mba_sys_result){-1,EBADF};
    raw_close(fd);
    return force_close?close_reply:(struct mba_sys_result){0,0};
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
static void reset(void)
{
    from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;
    admin=1;allocation_failure=interrupted=0;partial_bytes=104;gate.waiting=gate.resume=0;
    memset(fds,0,sizeof fds);memset(&original,0,sizeof original);memset(&replacement,0,sizeof replacement);
    original.f_mode=replacement.f_mode=FMODE_WRITE;fds[3]=&original;
    duplicates=states=mutations=native_calls=closes=releases=0;duplicate_target=8;last_fd=-1;
    force_duplicate=force_state_return=state_input_zero=false;
    duplicate_reply=state_reply=close_reply=(struct mba_sys_result){0,0};malformed_wire=0;
    force_close=close_saw_consumed=false;watch_close_slot=NULL;
    watch=NULL;inspect_duplicate=inspect_state=duplicate_seen=state_seen=false;
    memset(&at_duplicate,0,sizeof at_duplicate);memset(&at_state,0,sizeof at_state);
    memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);
    int crc[4]={28,38,96,38};memcpy(spi_crc_len,crc,sizeof crc);n_crc=4;
    mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;
    allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);
    random_wait_result=0;random_wait_calls=random_fill_calls=0;assert(!mixer_instance_init());testmode_held=0;
    uint8_t packet[153]={2};pattern(packet+1,31);
    assert(ctrl_write(&replacement,(char *)packet,sizeof packet,NULL)==153);
    struct az_overlay_command claim={.version=2,.instance={mixer_instance[0],mixer_instance[1]},.run_id=7};
    assert(!ctrl_ioctl(&original,AZ_OVERLAY_CLAIM,(unsigned long)&claim));
}
static struct mbra_owner *create(const struct mb_state *s)
{
    struct mbra_owner *owner=NULL;
    assert(!mbra_create(3,s,&owner) && owner);return owner;
}
static struct mbra_evidence evidence(struct mbra_owner *owner)
{
    struct mbra_evidence out={0};assert(!mbra_inspect(owner,&out));return out;
}
static bool backend_refused(struct mbra_owner *owner)
{
    struct mb_backend b;memset(&b,0xa5,sizeof b);
    return mbra_get_backend(owner,&b)==EPERM && zero_bytes(&b,sizeof b);
}
static void cleanup(struct mbra_owner **owner)
{
    /* Explicit test-only destruction of the synchronous, externally quiescent
     * fixture. This neither grants production teardown permission nor restores
     * an uncertain live controller. No remaining broker/callback is used. */
    watch=NULL;inspect_duplicate=inspect_state=false;force_state_return=false;
    malformed_wire=0;fail_to=fail_from=0;admin=1;force_close=false;watch_close_slot=NULL;
    if(owner && *owner) {
        struct mba_close_report report;
        assert(!mbra_dispose(owner,&report) && !*owner);
    }
    for(int fd=0;fd<FD_COUNT;fd++) if(fds[fd]) raw_close(fd);
}
static bool retained_failure(const struct mbra_evidence *e,int error)
{
    return e->phase==MBRA_TERMINAL && e->terminal_error==error &&
        e->descriptor_held && e->retained_fd==8 && e->duplicate_attempted &&
        e->duplicate_returned && e->duplicate_result.result==8 &&
        e->validation_attempted && e->validation_returned && !e->validated_state_valid;
}
/* Each selector has exactly one final functional check after cleanup. Setup
 * asserts and sanitizer failures cannot satisfy a strict control oracle. */
static void allocated_case(void)
{
    reset();struct mb_state supplied=semantic(raw_state()),saved=supplied;
    struct mbra_owner *owner=create(&supplied);struct mbra_evidence before=evidence(owner);
    supplied.instance[0]^=1;supplied.frame[151]^=1;
    int early=mbra_validate(owner);struct mbra_evidence after=evidence(owner);
    bool ok=before.phase==MBRA_ALLOCATED && before.original_fd==3 &&
        state_equal(&before.expected,&saved) && state_equal(&after.expected,&saved) &&
        !before.descriptor_held && before.retained_fd==-1 &&
        !before.duplicate_attempted && !before.validation_attempted &&
        !before.terminal_error && early==EPERM && evidence_equal(&before,&after) &&
        backend_refused(owner) && !duplicates && !states && !mutations && !closes;
    cleanup(&owner);ck(ok,"retained_allocate_copies_plan_without_io_or_early_validation");
}
static void invalid_case(void)
{
    reset();struct mb_state good=semantic(raw_state());bool ok=true;
    for(unsigned which=0;which<5;which++) {
        struct mb_state supplied=good;struct mbra_owner *owner=(void *)(uintptr_t)1;
        const struct mb_state *p=&supplied;int fd=3;
        if(which==0)fd=-1;if(which==1)p=NULL;if(which==2)supplied.flags=0;
        if(which==3)supplied.crc_len=95;if(which==4)supplied.run_id=0;
        int rc=mbra_create(fd,p,&owner);
        ok=(rc==EINVAL && !owner && !duplicates && !states && !closes) && ok;
        if(owner!=(void *)(uintptr_t)1) cleanup(&owner);
        else cleanup(NULL);
        if(which<4) reset();
    }
    ck(ok,"retained_invalid_plan_never_allocates_or_acquires_descriptor");
}
static void duplicate_arm_case(void)
{
    reset();struct mb_state s=semantic(raw_state());struct mbra_owner *owner=create(&s);
    watch=owner;inspect_duplicate=true;int rc=mbra_duplicate(owner);
    struct mbra_evidence after=evidence(owner);
    bool ok=!rc && duplicate_seen && at_duplicate.phase==MBRA_DUPLICATE_ARMED &&
        at_duplicate.duplicate_attempted && !at_duplicate.duplicate_returned &&
        !at_duplicate.descriptor_held && at_duplicate.retained_fd==-1 &&
        state_equal(&at_duplicate.expected,&s) && after.phase==MBRA_DUPLICATE_RETAINED &&
        after.duplicate_returned && after.duplicate_result.result==8 &&
        after.descriptor_held && after.retained_fd==8 && fds[8]==&original &&
        backend_refused(owner) && duplicates==1 && !states && !mutations && !closes;
    cleanup(&owner);ck(ok,"retained_duplicate_is_armed_before_call_and_owned_before_validation");
}
static void duplicate_error_case(void)
{
    const struct mba_sys_result replies[]={{-1,EMFILE},{-1,0},{-2,EIO},{3,0}};
    const int errors[]={EMFILE,EPROTO,EPROTO,EPROTO};bool ok=true;
    for(unsigned i=0;i<sizeof replies/sizeof *replies;i++) {
        reset();struct mb_state s=semantic(raw_state());struct mbra_owner *owner=create(&s);
        force_duplicate=true;duplicate_reply=replies[i];int rc=mbra_duplicate(owner);
        struct mbra_evidence first=evidence(owner);
        int repeat=mbra_duplicate(owner),validation=mbra_validate(owner);
        struct mbra_evidence after=evidence(owner);
        bool one=rc==errors[i] && first.phase==MBRA_TERMINAL && first.terminal_error==errors[i] &&
            first.duplicate_attempted && first.duplicate_returned &&
            result_equal(first.duplicate_result,replies[i]) && !first.descriptor_held &&
            first.retained_fd==-1 && !first.validation_attempted && !first.validation_returned &&
            repeat==EALREADY && validation==EPERM && evidence_equal(&first,&after) &&
            backend_refused(owner) && duplicates==1 && !states && !mutations && !closes &&
            fds[3]==&original && !fds[8] && !releases;
        ok=one&&ok;cleanup(&owner);
    }
    ck(ok,"retained_failed_or_impossible_duplicate_keeps_owner_and_raw_result");
}
static void fd_zero_case(void)
{
    reset();struct mb_state s=semantic(raw_state());struct az_overlay_state raw=raw_state();
    struct mbra_owner *owner=create(&s);force_duplicate=true;duplicate_reply=(struct mba_sys_result){0,EINTR};
    int duplicate=mbra_duplicate(owner);struct mbra_evidence held=evidence(owner);
    raw_close(3);fds[3]=&replacement;
    force_state_return=true;state_reply=(struct mba_sys_result){0,EIO};
    int rc=mbra_validate(owner);struct mbra_evidence e=evidence(owner);
    struct mb_backend b={0};int exported=mbra_get_backend(owner,&b);
    bool ok=!duplicate && held.descriptor_held && held.retained_fd==0 &&
        held.duplicate_result.result==0 && held.duplicate_result.error==EINTR &&
        !rc && !exported && b.context==owner && b.snapshot && b.submit &&
        e.phase==MBRA_READY && e.descriptor_held && e.retained_fd==0 &&
        result_equal(e.validation_result,state_reply) && e.validated_state_valid &&
        state_equal(&e.validated_state,&s) && !memcmp(e.validation_wire,&raw,208) &&
        duplicates==1 && states==1 && native_calls==1 && !mutations && !closes &&
        last_fd==0 && fds[0]==&original && fds[3]==&replacement && !releases;
    cleanup(&owner);ck(ok,"retained_fd_zero_survives_original_reuse_and_stale_errno");
}
static void state_arm_case(void)
{
    reset();struct mb_state s=semantic(raw_state());struct mbra_owner *owner=create(&s);
    assert(!mbra_duplicate(owner));watch=owner;inspect_state=true;
    int rc=mbra_validate(owner);struct mbra_evidence after=evidence(owner);
    bool ok=!rc && state_seen && at_state.phase==MBRA_VALIDATE_ARMED &&
        at_state.validation_attempted && !at_state.validation_returned &&
        at_state.descriptor_held && at_state.retained_fd==8 &&
        at_state.duplicate_returned && at_state.duplicate_result.result==8 &&
        state_equal(&at_state.expected,&s) && zero_bytes(at_state.validation_wire,208) &&
        !at_state.validated_state_valid && state_input_zero && after.phase==MBRA_READY &&
        duplicates==1 && states==1 && native_calls==1 && !mutations && !closes;
    cleanup(&owner);ck(ok,"retained_state_is_armed_with_descriptor_before_syscall");
}
static void partial_case(void)
{
    reset();struct az_overlay_state raw=raw_state();struct mb_state s=semantic(raw);
    struct mbra_owner *owner=create(&s);assert(!mbra_duplicate(owner));
    fail_to=to_calls+1;int rc=mbra_validate(owner);fail_to=0;
    struct mbra_evidence e=evidence(owner);uint8_t expected[208]={0};memcpy(expected,&raw,104);
    bool ok=rc==EFAULT && retained_failure(&e,EFAULT) &&
        e.validation_result.result==-1 && e.validation_result.error==EFAULT &&
        !memcmp(e.validation_wire,expected,208) && !zero_bytes(e.validation_wire,104) &&
        zero_bytes(e.validation_wire+104,104) && backend_refused(owner) &&
        duplicates==1 && states==1 && native_calls==1 && !mutations && !closes &&
        fds[8]==&original && !releases;
    cleanup(&owner);ck(ok,"retained_partial_state_fault_keeps_exact_observed_bytes_and_descriptor");
}
static void mismatch_case(void)
{
    bool ok=true;
    for(unsigned which=0;which<10;which++) {
        reset();struct az_overlay_state raw=raw_state();struct mb_state supplied=semantic(raw);
        if(which==0)supplied.instance[0]^=1;if(which==1)supplied.instance[1]^=1;
        if(which==2)supplied.run_id++;if(which==3)supplied.epoch++;
        if(which==4)supplied.sequence++;if(which==5)supplied.flags=MB_OWNED_FLAGS;
        if(which==6)supplied.frame[0]^=1;if(which==7)supplied.frame[96]^=1;
        if(which==8)supplied.frame[97]^=1;if(which==9)supplied.frame[151]^=1;
        struct mbra_owner *owner=create(&supplied);assert(!mbra_duplicate(owner));
        int rc=mbra_validate(owner);struct mbra_evidence e=evidence(owner);
        bool one=rc==ESTALE && retained_failure(&e,ESTALE) &&
            state_equal(&e.expected,&supplied) && !memcmp(e.validation_wire,&raw,208) &&
            !e.validation_result.result && !e.validation_result.error &&
            backend_refused(owner) && duplicates==1 && states==1 && native_calls==1 &&
            !mutations && !closes && fds[8]==&original && !releases;
        ok=one&&ok;cleanup(&owner);
    }
    ck(ok,"retained_full_state_disagreement_keeps_plan_raw_state_and_custody");
}
static void malformed_case(void)
{
    bool ok=true;
    for(unsigned which=1;which<=5;which++) {
        reset();struct az_overlay_state raw=raw_state();struct mb_state s=semantic(raw);
        struct mbra_owner *owner=create(&s);assert(!mbra_duplicate(owner));
        malformed_wire=which;corrupt_state(&raw,which);
        int rc=mbra_validate(owner);struct mbra_evidence e=evidence(owner);
        bool one=rc==EPROTO && retained_failure(&e,EPROTO) &&
            !e.validation_result.result && !memcmp(e.validation_wire,&raw,208) &&
            backend_refused(owner) && duplicates==1 && states==1 && native_calls==1 &&
            !mutations && !closes && fds[8]==&original;
        ok=one&&ok;cleanup(&owner);
    }
    ck(ok,"retained_malformed_state_preserves_raw_header_and_shape_failure");
}
static void positive_case(void)
{
    reset();struct az_overlay_state raw=raw_state();struct mb_state s=semantic(raw);
    struct mbra_owner *owner=create(&s);assert(!mbra_duplicate(owner));
    force_state_return=true;state_reply=(struct mba_sys_result){1,EINTR};
    int rc=mbra_validate(owner);struct mbra_evidence e=evidence(owner);
    bool ok=rc==EPROTO && retained_failure(&e,EPROTO) &&
        result_equal(e.validation_result,state_reply) && !memcmp(e.validation_wire,&raw,208) &&
        backend_refused(owner) && duplicates==1 && states==1 && native_calls==1 &&
        !mutations && !closes && fds[8]==&original;
    cleanup(&owner);ck(ok,"retained_unexpected_state_return_keeps_successful_copy_without_readiness");
}
static void repeat_failure_case(void)
{
    reset();struct mb_state s=semantic(raw_state());s.frame[151]^=1;
    struct mbra_owner *owner=create(&s);assert(!mbra_duplicate(owner));
    int first=mbra_validate(owner);struct mbra_evidence before=evidence(owner);
    int validate=mbra_validate(owner),duplicate=mbra_duplicate(owner);
    bool refused=backend_refused(owner);struct mbra_evidence after=evidence(owner);
    bool ok=first==ESTALE && validate==EALREADY && duplicate==EALREADY && refused &&
        retained_failure(&before,ESTALE) && evidence_equal(&before,&after) &&
        duplicates==1 && states==1 && native_calls==1 && !mutations && !closes;
    cleanup(&owner);ck(ok,"retained_failed_validation_cannot_retry_or_replace_first_evidence");
}
static void repeat_ready_case(void)
{
    reset();struct az_overlay_state raw=raw_state();struct mb_state s=semantic(raw);
    struct mbra_owner *owner=create(&s);assert(!mbra_duplicate(owner));
    force_state_return=true;state_reply=(struct mba_sys_result){0,EIO};
    int validated=mbra_validate(owner);struct mbra_evidence before=evidence(owner);
    int validate=mbra_validate(owner),duplicate=mbra_duplicate(owner);
    struct mb_backend b={0};int exported=mbra_get_backend(owner,&b);
    struct mbra_evidence after=evidence(owner);
    bool ok=!validated && before.phase==MBRA_READY && before.validated_state_valid &&
        state_equal(&before.validated_state,&s) && !memcmp(before.validation_wire,&raw,208) &&
        result_equal(before.validation_result,state_reply) && validate==EALREADY &&
        duplicate==EALREADY && !exported && b.context==owner && b.snapshot && b.submit &&
        evidence_equal(&before,&after) && duplicates==1 && states==1 &&
        native_calls==1 && !mutations && !closes;
    cleanup(&owner);ck(ok,"retained_ready_refusals_preserve_one_validation_and_exact_journal");
}
static void disposal_case(void)
{
    bool ok=true;
    /* All resources here are synchronous fixture memory. The uncertain-close
     * variant actually removes the modeled duplicate and then returns EINTR;
     * it establishes report/call-count behavior, not live close resolution. */
    for(unsigned which=0;which<5;which++) {
        reset();struct mb_state s=semantic(raw_state());
        if(which==2)s.frame[151]^=1;
        struct mbra_owner *owner=create(&s);
        if(which==1) {
            force_duplicate=true;duplicate_reply=(struct mba_sys_result){3,0};
            assert(mbra_duplicate(owner)==EPROTO);
        } else if(which>=2) {
            assert(!mbra_duplicate(owner));
            int rc=mbra_validate(owner);assert(rc==(which==2?ESTALE:0));
        }
        bool held=which>=2;
        struct mbra_evidence before=evidence(owner);
        force_close=which==4;close_reply=(struct mba_sys_result){-1,EINTR};
        watch_close_slot=&owner;struct mba_close_report report,repeat;
        int rc=mbra_dispose(&owner,&report);
        int again=mbra_dispose(&owner,&repeat);
        bool one=!rc && !again && !owner && before.descriptor_held==held &&
            report.attempted==held && closes==(held?1u:0u) &&
            (held ? close_saw_consumed && !fds[8] : !close_saw_consumed) &&
            (which==4 ? result_equal(report.call,close_reply) :
                        !report.call.result && !report.call.error) &&
            !repeat.attempted && !repeat.call.result && !repeat.call.error &&
            fds[3]==&original && !releases && mixer_owner==&original;
        ok=one&&ok;watch_close_slot=NULL;cleanup(NULL);
    }
    ck(ok,"retained_disposal_consumes_once_and_closes_only_its_held_duplicate");
}
static struct mbw_header header(uint32_t kind,uint32_t length,uint64_t exchange)
{
    struct mbw_header h={.kind=kind,.length=length,.exchange=exchange};
    for(unsigned i=0;i<16;i++)h.session[i]=(uint8_t)(0x51+i);return h;
}
static void broker_wire_case(void)
{
    reset();struct az_overlay_state raw=raw_state();struct mb_state s=semantic(raw);
    struct mbra_owner *owner=create(&s);assert(!mbra_duplicate(owner));
    assert(!mbra_validate(owner));struct mbra_evidence acquisition=evidence(owner);
    struct mb_backend backend={0};int exported=mbra_get_backend(owner,&backend);
    struct mb_broker *broker=NULL;struct mbw_dispatch *dispatch=NULL;
    int created=exported?MB_ERR_INVALID:mb_create(&backend,&s,&broker);
    struct mbw_header h=header(MBW_ADOPT_REQUEST,MBW_ADOPT_REQUEST_SIZE,1);
    int constructed=created?MBWD_BROKER:mbw_dispatch_create(broker,h.session,&s,
        UINT64_C(0x4142434445464748),&dispatch);
    unsigned before_adopt_states=states,before_adopt_native=native_calls;
    uint8_t request[256]={0},reply[668]={0};size_t request_n=0,reply_n=0;
    struct mbw_adopt_request ar={.state=s,.producer_id=UINT64_C(0x4142434445464748)};
    assert(!mbw_encode_adopt_request(&h,&ar,request,sizeof request,&request_n));
    int adopted=constructed?MBWD_CLOSED:mbw_dispatch_exchange(dispatch,request,request_n,reply,sizeof reply,&reply_n);
    struct mbw_adopt_response adoption={0};struct mbw_header decoded={0};
    int adopt_decoded=mbw_decode_adopt_response(reply,reply_n,&decoded,&adoption);
    unsigned before_frame_states=states,before_frame_native=native_calls;
    struct mb_request fr={.grant=adoption.grant,.request_id=1};pattern(fr.frame,97);
    h=header(MBW_FRAME_REQUEST,MBW_FRAME_REQUEST_SIZE,2);
    assert(!mbw_encode_frame_request(&h,&fr,request,sizeof request,&request_n));
    int submitted=constructed?MBWD_CLOSED:mbw_dispatch_exchange(dispatch,request,request_n,reply,sizeof reply,&reply_n);
    struct mbw_frame_response response={0};
    int frame_decoded=mbw_decode_frame_response(reply,reply_n,&decoded,&response);
    struct mb_state actual=semantic(raw_state());struct mbra_evidence after=evidence(owner);
    struct mb_state target=s;target.sequence++;memcpy(target.frame,fr.frame,152);
    bool ok=!exported && !created && !constructed && before_adopt_states==2 &&
        before_adopt_native==2 && !adopted && !adopt_decoded && !adoption.result &&
        !adoption.provenance && state_equal(&adoption.state,&s) &&
        before_frame_states==3 && before_frame_native==3 && !submitted && !frame_decoded &&
        !response.result && !response.provenance &&
        response.receipt.outcome==MB_OUTCOME_COMMITTED &&
        response.receipt.operation==MB_OP_FRAME && !response.receipt.backend_error &&
        response.receipt.request.request_id==1 && response.receipt.command.sequence==s.sequence+1 &&
        !memcmp(response.receipt.request.frame,fr.frame,152) &&
        !memcmp(response.receipt.command.frame,fr.frame,152) &&
        state_equal(&response.receipt.expected_after,&target) && state_equal(&actual,&target) &&
        acquisition.phase==MBRA_READY && !memcmp(acquisition.validation_wire,&raw,208) &&
        evidence_equal(&acquisition,&after) && duplicates==1 && states==3 && native_calls==4 &&
        mutations==1 && last_fd==8 && !closes && fds[8]==&original;
    if(dispatch)mbw_dispatch_destroy(dispatch);
    if(broker)mb_destroy(broker);
    cleanup(&owner);ck(ok,"retained_ready_backend_composes_exact_broker_wire_frame_without_journal_rewrite");
}
int main(int argc,char **argv)
{
    alarm(30);const char *which=argc>1?argv[1]:"all";bool all=!strcmp(which,"all"),selected=false;
#define RUN(name,call) do{if(all || !strcmp(which,name)){selected=true;call;}}while(0)
    RUN("allocated",allocated_case());RUN("invalid",invalid_case());
    RUN("duplicate-arm",duplicate_arm_case());RUN("duplicate-errors",duplicate_error_case());
    RUN("fd-zero",fd_zero_case());RUN("state-arm",state_arm_case());RUN("partial-state",partial_case());
    RUN("mismatch",mismatch_case());RUN("malformed",malformed_case());RUN("positive-state",positive_case());
    RUN("repeat-failure",repeat_failure_case());RUN("repeat-ready",repeat_ready_case());
    RUN("broker-wire",broker_wire_case());RUN("disposal",disposal_case());
#undef RUN
    if(!selected)return 2;
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
