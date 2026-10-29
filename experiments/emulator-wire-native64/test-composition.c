/* SPDX-License-Identifier: GPL-2.0-only */
/* New composition cases only: frozen wire/dispatcher/broker/adapter translation
 * units, and seven exact extracted kernel methods under owned-memory shims.
 * The fd table models references; native.c, Linux VFS, endpoints and a Rust host
 * do not execute here. A postcommit EIO is deliberately injected AFTER the
 * actual extracted mutation, not claimed to be a live driver outcome. */
#include "kernel-shim.inc"
#include "candidate.inc"
#include "guest_dispatch.h"
#include "adapter.h"

static unsigned checks, failures;
static void ck(bool ok, const char *label)
{ checks++; if (!ok) { failures++; fprintf(stderr, "FAIL %s\n", label); } }
static void pattern(uint8_t *b, unsigned seed)
{ for (unsigned i=0; i<152; i++) b[i]=(uint8_t)(seed+23*i); }
static bool state_equal(const struct mb_state *a, const struct mb_state *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->sequence==b->sequence &&
        a->flags==b->flags && a->crc_len==b->crc_len && !memcmp(a->frame,b->frame,152);
}
static bool grant_equal(const struct mb_grant *a, const struct mb_grant *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->producer_id==b->producer_id;
}

#define FD_COUNT 32
static struct file original, replacement;
static struct file *fds[FD_COUNT];
static unsigned dup_calls, state_calls, mutation_calls, close_calls, release_calls;
static unsigned native_calls;
static int last_fd;
static long last_native_result;
static unsigned long last_operation;
static uint8_t last_wire[208];
static size_t last_wire_length;
static bool postcommit_eio;

static void model_close(int fd)
{
    assert(fd>=0 && fd<FD_COUNT && fds[fd]);
    struct file *f=fds[fd]; fds[fd]=NULL;
    for (int i=0; i<FD_COUNT; i++) if (fds[i]==f) return;
    release_calls++; assert(!ctrl_release(NULL,f));
}
struct mba_sys_result mba_sys_duplicate(int fd)
{
    dup_calls++;
    if (fd<0 || fd>=FD_COUNT || !fds[fd]) return (struct mba_sys_result){-1,EBADF};
    assert(!fds[8]); fds[8]=fds[fd];
    return (struct mba_sys_result){8,0};
}
struct mba_sys_result mba_sys_ioctl(int fd, unsigned long op, void *argument)
{
    last_fd=fd; last_operation=op;
    switch (op) {
    case AZ_OVERLAY_STATE: last_wire_length=sizeof(struct az_overlay_state); state_calls++; break;
    case AZ_OVERLAY_FRAME: case AZ_OVERLAY_ROTATE:
        last_wire_length=sizeof(struct az_overlay_frame); mutation_calls++; break;
    case AZ_OVERLAY_RESTORE: last_wire_length=sizeof(struct az_overlay_restore); mutation_calls++; break;
    default: assert(!"unexpected adapter ioctl"); last_wire_length=0;
    }
    assert(argument);
    memset(last_wire,0,sizeof last_wire); memcpy(last_wire,argument,last_wire_length);
    if (fd<0 || fd>=FD_COUNT || !fds[fd]) return (struct mba_sys_result){-1,EBADF};
    native_calls++;
    last_native_result=ctrl_ioctl(fds[fd],(unsigned)op,(unsigned long)argument);
    if (postcommit_eio && op!=AZ_OVERLAY_STATE && !last_native_result)
        return (struct mba_sys_result){-1,EIO};
    return (struct mba_sys_result){last_native_result<0 ? -1 : (int)last_native_result,
                                  last_native_result<0 ? (int)-last_native_result : 0};
}
struct mba_sys_result mba_sys_close(int fd)
{
    close_calls++;
    if (fd<0 || fd>=FD_COUNT || !fds[fd]) return (struct mba_sys_result){-1,EBADF};
    model_close(fd); return (struct mba_sys_result){0,0};
}
static struct mb_state native_state(void)
{
    struct az_overlay_state s={0};
    assert(!ctrl_ioctl(&original,AZ_OVERLAY_STATE,(unsigned long)&s));
    assert(s.version==2 && !s.reserved);
    struct mb_state out={.instance={s.instance[0],s.instance[1]},.run_id=s.run_id,
        .epoch=s.epoch,.sequence=s.sequence,.flags=s.flags,.crc_len=s.crc_len};
    memcpy(out.frame,s.frame,152); return out;
}
static void reset(bool injected)
{
    from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;
    admin=1; allocation_failure=interrupted=0; partial_bytes=104;
    gate.waiting=gate.resume=0;
    memset(fds,0,sizeof fds); memset(&original,0,sizeof original);
    memset(&replacement,0,sizeof replacement);
    original.f_mode=replacement.f_mode=FMODE_WRITE; fds[3]=&original;
    dup_calls=state_calls=mutation_calls=close_calls=release_calls=native_calls=0;
    last_fd=-1; last_native_result=0; last_operation=0; last_wire_length=0; postcommit_eio=false;
    memset(last_wire,0,sizeof last_wire);
    memset(inject_pending,0,sizeof inject_pending); memset(has_inject,0,sizeof has_inject);
    int crc[4]={28,38,96,38}; memcpy(spi_crc_len,crc,sizeof crc); n_crc=4;
    mixer_fenced=false; mixer_owner=NULL; mixer_run=mixer_epoch=mixer_sequence=0;
    allow_overlay_ownership=true; memset(mixer_instance,0,sizeof mixer_instance);
    random_wait_result=0; random_wait_calls=random_fill_calls=0;
    assert(!mixer_instance_init()); testmode_held=0;
    if (injected) {
        uint8_t packet[153]={2}; pattern(packet+1,31);
        assert(ctrl_write(&replacement,(char *)packet,sizeof packet,NULL)==153);
    }
    struct az_overlay_command claim={.version=2,
        .instance={mixer_instance[0],mixer_instance[1]},.run_id=7};
    assert(!ctrl_ioctl(&original,AZ_OVERLAY_CLAIM,(unsigned long)&claim));
}
struct fixture {
    struct mba_adapter *adapter;
    struct mb_broker *broker;
    struct mbw_dispatch *dispatch;
    struct mb_state seed;
    struct mb_grant grant;
    uint8_t session[16];
};
static void foundation(struct fixture *f, bool injected)
{
    reset(injected); memset(f,0,sizeof(*f)); f->seed=native_state();
    for (unsigned i=0; i<16; i++) f->session[i]=(uint8_t)(0x51+i);
    struct mba_attach_report report;
    assert(!mba_attach(3,&f->seed,&f->adapter,&report) && f->adapter);
    struct mb_backend backend={0}; assert(!mba_get_backend(f->adapter,&backend));
    assert(!mb_create(&backend,&f->seed,&f->broker) && f->broker);
    f->grant=(struct mb_grant){.instance={f->seed.instance[0],f->seed.instance[1]},
        .run_id=f->seed.run_id,.epoch=f->seed.epoch,.producer_id=0x4142434445464748ULL};
}
static void begin(struct fixture *f)
{
    foundation(f,true);
    assert(!mbw_dispatch_create(f->broker,f->session,&f->seed,f->grant.producer_id,&f->dispatch));
}
static void cleanup(struct fixture *f)
{
    /* Only owned test memory and modeled descriptors are destroyed after all
     * synchronous calls finish. This is not permission to dispose an uncertain
     * live adapter, cancel a command, restore an overlay or reopen a gate. */
    postcommit_eio=false; admin=1; fail_from=fail_to=0;
    if (f->dispatch) { mbw_dispatch_destroy(f->dispatch); f->dispatch=NULL; }
    if (f->broker) { mb_destroy(f->broker); f->broker=NULL; }
    if (f->adapter) {
        struct mba_close_report report;
        assert(!mba_dispose(&f->adapter,&report) && !f->adapter && report.attempted);
    }
    for (int fd=0; fd<FD_COUNT; fd++) if (fds[fd]) model_close(fd);
}
static struct mbw_header header(const struct fixture *f, uint32_t kind, uint32_t length, uint64_t id)
{
    struct mbw_header h={.kind=kind,.length=length,.exchange=id};
    memcpy(h.session,f->session,16); return h;
}
static size_t adopt_record(const struct fixture *f, uint8_t *bytes)
{
    struct mbw_header h=header(f,MBW_ADOPT_REQUEST,256,1);
    struct mbw_adopt_request request={.state=f->seed,.producer_id=f->grant.producer_id};
    size_t n=0; assert(!mbw_encode_adopt_request(&h,&request,bytes,256,&n) && n==256); return n;
}
static size_t frame_record(const struct fixture *f, uint8_t *bytes,
                           uint64_t exchange, uint64_t id, unsigned seed)
{
    struct mbw_header h=header(f,MBW_FRAME_REQUEST,248,exchange);
    struct mb_request request={.grant=f->grant,.request_id=id}; pattern(request.frame,seed);
    size_t n=0; assert(!mbw_encode_frame_request(&h,&request,bytes,256,&n) && n==248); return n;
}
static int exchange(struct fixture *f, const uint8_t *request, size_t n,
                    uint8_t *reply, size_t *length)
{
    memset(reply,0xa5,668); *length=0;
    return mbw_dispatch_exchange(f->dispatch,request,n,reply,668,length);
}
static void adopted(struct fixture *f)
{
    uint8_t request[256],reply[668]; size_t n=adopt_record(f,request),length;
    assert(!exchange(f,request,n,reply,&length));
    struct mbw_header h; struct mbw_adopt_response r;
    assert(!mbw_decode_adopt_response(reply,length,&h,&r));
    assert(!r.result && !r.provenance && grant_equal(&r.grant,&f->grant));
}
static void alias_frame(unsigned seed)
{
    struct az_overlay_frame frame={.header={.version=2,
        .instance={mixer_instance[0],mixer_instance[1]},.run_id=mixer_run,
        .epoch=mixer_epoch,.sequence=mixer_sequence+1}};
    pattern(frame.frame,seed);
    assert(!ctrl_ioctl(&original,AZ_OVERLAY_FRAME,(unsigned long)&frame));
}
static struct mbw_diagnostic diagnostic(struct fixture *f)
{ struct mbw_diagnostic d; assert(!mbw_dispatch_inspect(f->dispatch,&d)); return d; }
static struct mb_status broker_status(struct fixture *f)
{ struct mb_status s; assert(!mb_inspect(f->broker,&s)); return s; }
/* Independent canonical response oracle, never codec/struct-memory roundtrip. */
static void put32(uint8_t *p,uint32_t n)
{ for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(24-8*i)); }
static void put64(uint8_t *p,uint64_t n)
{ for (unsigned i=0;i<8;i++) p[i]=(uint8_t)(n>>(56-8*i)); }
static void state_bytes(uint8_t *p,const struct mb_state *s)
{
    put64(p,s->instance[0]);put64(p+8,s->instance[1]);put64(p+16,s->run_id);
    put64(p+24,s->epoch);put64(p+32,s->sequence);put32(p+40,s->flags);
    put32(p+44,(uint32_t)s->crc_len);memcpy(p+48,s->frame,152);
}
static bool full_response(const struct fixture *f,const uint8_t *request,const uint8_t *reply,
                          size_t n,unsigned outcome,int error,unsigned provenance)
{
    uint8_t expected[668]={0}; memcpy(expected,"AZMBRPC1",8);
    put32(expected+8,1);put32(expected+12,0x80000002U);put32(expected+16,668);
    memcpy(expected+24,f->session,16);memcpy(expected+40,request+40,8);
    put32(expected+52,provenance);put32(expected+56,outcome);put32(expected+60,1);
    put32(expected+64,(uint32_t)error);memcpy(expected+72,request+48,200);
    struct mb_state after=f->seed;after.sequence=f->seed.sequence+1;
    memcpy(after.frame,request+96,152);
    put64(expected+272,after.instance[0]);put64(expected+280,after.instance[1]);
    put64(expected+288,after.run_id);put64(expected+296,after.epoch);
    put64(expected+304,after.sequence);put32(expected+312,1);
    memcpy(expected+316,after.frame,152);state_bytes(expected+468,&after);
    return n==668 && !memcmp(reply,expected,668);
}
static bool native_frame_wire(const struct fixture *f,const uint8_t *request)
{
    struct az_overlay_frame frame={.header={.version=2,.instance={f->seed.instance[0],
        f->seed.instance[1]},.run_id=f->seed.run_id,.epoch=f->seed.epoch,
        .sequence=f->seed.sequence+1}};
    memcpy(frame.frame,request+96,152);
    return last_operation==AZ_OVERLAY_FRAME && last_wire_length==200 &&
        !memcmp(last_wire,&frame,200);
}
static bool target_state(const struct fixture *f,const uint8_t *request,const struct mb_state *s)
{
    struct mb_state expected=f->seed;expected.sequence++;memcpy(expected.frame,request+96,152);
    return state_equal(&expected,s);
}

/* Exactly one functional oracle per selector, always after fixture cleanup.
 * Assertions are setup/cleanup invariants, never accepted control failures. */
static void constructor_case(void)
{
    struct fixture f; foundation(&f,true);
    unsigned states=state_calls,native=native_calls,duplicates=dup_calls;
    int rc=mbw_dispatch_create(f.broker,f.session,&f.seed,f.grant.producer_id,&f.dispatch);
    struct mb_status s=broker_status(&f);
    bool ok=!rc && f.dispatch && state_calls==states && native_calls==native &&
        dup_calls==duplicates && state_calls==2 && native_calls==2 && dup_calls==1 &&
        !mutation_calls && !close_calls && !s.grant_issued &&
        !s.admission_open && !s.unresolved && s.next_request_id==1;
    if (f.dispatch) {struct mbw_diagnostic d=diagnostic(&f);
        ok=ok && d.phase==MBWD_RETAINED && !d.last_exchange && !d.returned_valid &&
           state_equal(&d.seed,&f.seed);}
    cleanup(&f); ck(ok,"wire_constructor_retains_local_broker_without_native_adoption");
}
static void idle_case(void)
{
    struct fixture f; foundation(&f,false); unsigned calls=native_calls;
    int rc=mbw_dispatch_create(f.broker,f.session,&f.seed,f.grant.producer_id,&f.dispatch);
    struct mb_status s=broker_status(&f);
    bool ok=rc==MBWD_INVALID && !f.dispatch && f.seed.flags==23 &&
        native_calls==calls && !s.grant_issued && !s.admission_open && !close_calls;
    cleanup(&f);ck(ok,"wire_requires_injected_seed_even_when_adapter_broker_accept_owned_idle");
}
static void constructor_drift_case(void)
{
    struct fixture f;foundation(&f,true);struct mb_state wrong=f.seed;wrong.frame[151]^=1;
    unsigned calls=native_calls;
    int rc=mbw_dispatch_create(f.broker,f.session,&wrong,f.grant.producer_id,&f.dispatch);
    struct mb_status s=broker_status(&f);
    bool ok=rc==MBWD_BROKER && !f.dispatch && native_calls==calls &&
        state_equal(&s.expected,&f.seed) && !s.grant_issued && !s.admission_open;
    cleanup(&f);ck(ok,"wire_constructor_checks_complete_retained_broker_seed");
}
static void adoption_case(void)
{
    struct fixture f;begin(&f);uint8_t request[256],reply[668];size_t n=adopt_record(&f,request),length;
    unsigned states=state_calls,calls=native_calls;
    int rc=exchange(&f,request,n,reply,&length);
    struct mbw_header h;struct mbw_adopt_response r;
    int decoded=mbw_decode_adopt_response(reply,length,&h,&r);
    struct mbw_diagnostic d=diagnostic(&f);struct mb_status s=broker_status(&f);
    uint8_t expected_state[200];state_bytes(expected_state,&f.seed);
    bool ok=!rc && !decoded && !r.result && !r.provenance && h.exchange==1 &&
        grant_equal(&r.grant,&f.grant) && state_equal(&r.state,&f.seed) &&
        length==296 && !memcmp(reply+56,expected_state,200) && state_calls==states+1 &&
        native_calls==calls+1 && state_calls==3 && native_calls==3 &&
        dup_calls==1 && last_fd==8 && !mutation_calls && !close_calls &&
        d.phase==MBWD_READY && d.returned_valid && !d.broker_result &&
        s.grant_issued && s.admission_open && !s.unresolved && s.next_request_id==1;
    cleanup(&f);ck(ok,"wire_adopt_uses_fresh_bound_native_state_and_exact_grant");
}
static void frame_case(void)
{
    struct fixture f;begin(&f);adopted(&f);
    uint8_t request[256],reply[668];size_t n=frame_record(&f,request,2,1,97),length;
    unsigned states=state_calls,calls=native_calls;
    int rc=exchange(&f,request,n,reply,&length);
    bool wire=native_frame_wire(&f,request);struct mb_state actual=native_state();
    struct mbw_diagnostic d=diagnostic(&f);struct mb_status s=broker_status(&f);
    bool ok=!rc && full_response(&f,request,reply,length,MB_OUTCOME_COMMITTED,0,0) &&
        wire && target_state(&f,request,&actual) && state_calls==states && native_calls==calls+1 &&
        mutation_calls==1 && last_fd==8 && !close_calls && d.phase==MBWD_READY &&
        d.next_request_id==2 && state_equal(&d.expected,&actual) &&
        !s.unresolved && s.admission_open && s.next_request_id==2 &&
        state_equal(&s.expected,&actual) && actual.crc_len==96 &&
        actual.frame[96]==request[192] && actual.frame[97]==request[193] &&
        actual.frame[151]==request[247];
    cleanup(&f);ck(ok,"wire_frame_commits_exact_raw_native_and_canonical_response_bytes");
}
static void fd_case(void)
{
    struct fixture f;begin(&f);
    model_close(3);fds[3]=&replacement;
    uint8_t adopt[256],reply[668],request[256];size_t length;
    int ar=exchange(&f,adopt,adopt_record(&f,adopt),reply,&length);
    struct mbw_header h;struct mbw_adopt_response a;
    int decoded=mbw_decode_adopt_response(reply,length,&h,&a);
    size_t n=frame_record(&f,request,2,1,99);int rc=exchange(&f,request,n,reply,&length);
    struct mb_state actual=native_state();
    bool ok=!ar && !decoded && !a.result && !rc &&
        full_response(&f,request,reply,length,MB_OUTCOME_COMMITTED,0,0) &&
        target_state(&f,request,&actual) && last_fd==8 && fds[8]==&original &&
        fds[3]==&replacement && mixer_owner==&original && !release_calls &&
        dup_calls==1 && mutation_calls==1 && !close_calls;
    cleanup(&f);ck(ok,"wire_adopt_and_frame_keep_duplicate_file_after_original_number_reuse");
}
static void adopt_divergence_case(void)
{
    struct fixture f;begin(&f);alias_frame(77);struct mb_state alias=native_state();
    uint8_t request[256],reply[668],cached[668],next[256];size_t n=adopt_record(&f,request),length,cached_n;
    unsigned states=state_calls,calls=native_calls;
    int rc=exchange(&f,request,n,reply,&length);
    struct mbw_header h;struct mbw_adopt_response a;
    int decoded=mbw_decode_adopt_response(reply,length,&h,&a);
    int duplicate=exchange(&f,request,n,cached,&cached_n);
    size_t next_n=frame_record(&f,next,2,1,98),unused;
    int closed=exchange(&f,next,next_n,reply,&unused);
    struct mbw_diagnostic d=diagnostic(&f);struct mb_status s=broker_status(&f);
    struct mb_state actual=native_state();
    bool ok=!rc && !decoded && a.result==MB_ERR_STATE && !a.provenance &&
        !duplicate && cached_n==296 && cached[55]==1 && closed==MBWD_CLOSED && !unused &&
        states+1==state_calls && calls+1==native_calls && !mutation_calls && !close_calls &&
        !s.grant_issued && !s.admission_open && d.phase==MBWD_TERMINAL &&
        d.broker_result==MB_ERR_STATE && state_equal(&actual,&alias);
    cleanup(&f);ck(ok,"wire_adoption_rejects_actual_alias_state_drift_after_constructor");
}
static void denial(bool copy_fault)
{
    struct fixture f;begin(&f);adopted(&f);
    uint8_t request[256],reply[668],cached[668],next[256];size_t n=frame_record(&f,request,2,1,101),length,cached_n;
    struct mb_state before=native_state();unsigned states=state_calls,calls=native_calls;
    if (copy_fault) {partial_bytes=104;fail_from=from_calls+1;} else admin=0;
    int rc=exchange(&f,request,n,reply,&length);long kernel_result=last_native_result;
    fail_from=0;admin=1;int error=copy_fault?EFAULT:EPERM;
    struct mb_state after=native_state();struct mb_status s=broker_status(&f);
    int duplicate=exchange(&f,request,n,cached,&cached_n);
    size_t next_n=frame_record(&f,next,3,2,103),unused;
    int closed=exchange(&f,next,next_n,cached,&unused);
    struct mbw_diagnostic d=diagnostic(&f);
    bool ok=!rc && full_response(&f,request,reply,length,MB_OUTCOME_REJECTED,error,0) &&
        kernel_result==-error && state_equal(&before,&after) &&
        !duplicate && cached_n==668 && closed==MBWD_CLOSED && !unused &&
        state_calls==states && native_calls==calls+1 && mutation_calls==1 && !close_calls &&
        !s.unresolved && !s.admission_open && s.next_request_id==2 &&
        d.phase==MBWD_TERMINAL && d.next_request_id==1 && d.returned_valid &&
        d.returned_receipt.outcome==MB_OUTCOME_REJECTED;
    cleanup(&f);
    ck(ok,copy_fault ? "wire_actual_partial_input_efault_is_exact_definite_rejection" :
                       "wire_actual_permission_denial_is_exact_definite_rejection");
}
static void unknown_case(void)
{
    struct fixture f;begin(&f);adopted(&f);
    uint8_t request[256],reply[668],cached[668],next[256];size_t n=frame_record(&f,request,2,1,107),length,cached_n;
    unsigned states=state_calls,calls=native_calls;postcommit_eio=true;
    int rc=exchange(&f,request,n,reply,&length);postcommit_eio=false;
    long committed=last_native_result;struct mb_state actual=native_state();
    struct mb_status s=broker_status(&f);
    int duplicate=exchange(&f,request,n,cached,&cached_n);
    bool cache=!duplicate && full_response(&f,request,cached,cached_n,MB_OUTCOME_UNKNOWN,EIO,1);
    size_t next_n=frame_record(&f,next,3,2,109),unused;
    int closed=exchange(&f,next,next_n,cached,&unused);
    struct mbw_diagnostic d=diagnostic(&f);
    bool ok=!rc && full_response(&f,request,reply,length,MB_OUTCOME_UNKNOWN,EIO,0) &&
        !committed && target_state(&f,request,&actual) && cache && closed==MBWD_CLOSED && !unused &&
        s.unresolved && !s.admission_open && s.next_request_id==2 &&
        state_equal(&s.expected,&f.seed) && d.phase==MBWD_TERMINAL &&
        state_equal(&d.expected,&f.seed) && d.next_request_id==1 &&
        d.returned_receipt.outcome==MB_OUTCOME_UNKNOWN &&
        state_calls==states && native_calls==calls+1 && mutation_calls==1 && !close_calls;
    cleanup(&f);ck(ok,"wire_actual_commit_then_eio_retains_unknown_without_replay_or_disposal");
}
static void lost_case(void)
{
    struct fixture f;begin(&f);adopted(&f);
    uint8_t request[256],discarded[668],cached[668];size_t n=frame_record(&f,request,2,1,113),length,cached_n;
    int first=exchange(&f,request,n,discarded,&length);
    unsigned calls=native_calls,states=state_calls,mutations=mutation_calls;
    /* The first returned response is discarded by this synthetic transport.
     * Retrieval is historical; no C replacement for Rust host policy exists. */
    int second=exchange(&f,request,n,cached,&cached_n);
    struct mbw_diagnostic d=diagnostic(&f);struct mb_state actual=native_state();
    bool ok=!first && !second && full_response(&f,request,cached,cached_n,MB_OUTCOME_COMMITTED,0,1) &&
        full_response(&f,request,d.response,d.response_length,MB_OUTCOME_COMMITTED,0,0) &&
        target_state(&f,request,&actual) && calls==native_calls && states==state_calls &&
        mutations==mutation_calls && mutations==1 && !close_calls && d.last_exchange==2;
    cleanup(&f);ck(ok,"wire_lost_positive_response_cache_is_historical_without_second_native_call");
}
static void conflict_case(void)
{
    struct fixture f;begin(&f);adopted(&f);
    uint8_t request[256],reply[668],changed[256],cached[668];size_t n=frame_record(&f,request,2,1,117),length,cached_n;
    assert(!exchange(&f,request,n,reply,&length));
    struct mb_state before=native_state();struct mbw_diagnostic prior=diagnostic(&f);
    memcpy(changed,request,n);changed[247]^=1;unsigned calls=native_calls;
    int conflict=exchange(&f,changed,n,reply,&length);
    struct mbw_diagnostic after=diagnostic(&f);
    int historical=exchange(&f,request,n,cached,&cached_n);struct mb_state actual=native_state();
    bool ok=conflict==MBWD_CONFLICT && !length && after.phase==MBWD_TERMINAL &&
        after.first_refusal==MBWD_CONFLICT && after.request_length==prior.request_length &&
        !memcmp(after.request,prior.request,prior.request_length) &&
        after.response_length==prior.response_length && !memcmp(after.response,prior.response,prior.response_length) &&
        !historical && full_response(&f,request,cached,cached_n,MB_OUTCOME_COMMITTED,0,1) &&
        native_calls==calls && mutation_calls==1 && state_equal(&actual,&before) && !close_calls;
    cleanup(&f);ck(ok,"wire_changed_duplicate_tail_closes_without_overwriting_prior_commit_or_redispatch");
}
static void alias_cache_case(void)
{
    struct fixture f;begin(&f);adopted(&f);
    uint8_t request[256],reply[668],cached[668];size_t n=frame_record(&f,request,2,1,121),length,cached_n;
    assert(!exchange(&f,request,n,reply,&length));
    alias_frame(123);struct mb_state actual=native_state();unsigned calls=native_calls;
    int rc=exchange(&f,request,n,cached,&cached_n);struct mbw_diagnostic d=diagnostic(&f);
    bool ok=!rc && full_response(&f,request,cached,cached_n,MB_OUTCOME_COMMITTED,0,1) &&
        actual.sequence==f.seed.sequence+2 && !target_state(&f,request,&actual) &&
        !state_equal(&actual,&d.expected) && native_calls==calls && mutation_calls==1 &&
        d.expected.sequence==f.seed.sequence+1 && d.phase==MBWD_READY && !close_calls;
    cleanup(&f);ck(ok,"wire_cache_does_not_claim_current_state_or_exclude_same_capability_alias");
}
static void alias_next_case(void)
{
    struct fixture f;begin(&f);adopted(&f);
    uint8_t first[256],next[256],reply[668];size_t n=frame_record(&f,first,2,1,125),length;
    assert(!exchange(&f,first,n,reply,&length));alias_frame(127);
    struct mb_state alias=native_state();unsigned states=state_calls,calls=native_calls;
    n=frame_record(&f,next,3,2,129);int rc=exchange(&f,next,n,reply,&length);
    struct mbw_header h;struct mbw_frame_response r;
    int decoded=mbw_decode_frame_response(reply,length,&h,&r);
    struct mb_state actual=native_state();struct mb_status s=broker_status(&f);
    struct mbw_diagnostic d=diagnostic(&f);
    bool ok=!rc && !decoded && !r.result && !r.provenance &&
        r.receipt.outcome==MB_OUTCOME_REJECTED && r.receipt.backend_error==ESTALE &&
        r.receipt.request.request_id==2 && r.receipt.command.sequence==f.seed.sequence+2 &&
        !memcmp(r.receipt.request.frame,next+96,152) &&
        state_equal(&actual,&alias) && state_calls==states && native_calls==calls+1 &&
        mutation_calls==2 && !close_calls && !s.unresolved && !s.admission_open &&
        s.next_request_id==3 && d.phase==MBWD_TERMINAL && d.next_request_id==2;
    cleanup(&f);ck(ok,"wire_next_frame_after_alias_is_native_stale_not_retagged_or_retried");
}
int main(int argc,char **argv)
{
    alarm(30);const char *which=argc>1?argv[1]:"all";bool all=!strcmp(which,"all");bool selected=false;
#define RUN(name,call) do { if(all || !strcmp(which,name)){selected=true;call;} } while(0)
    RUN("constructor",constructor_case());RUN("idle",idle_case());
    RUN("constructor-drift",constructor_drift_case());RUN("adopt",adoption_case());
    RUN("frame",frame_case());RUN("fd-reuse",fd_case());RUN("adopt-divergence",adopt_divergence_case());
    RUN("permission",denial(false));RUN("copy-fault",denial(true));RUN("postcommit-unknown",unknown_case());
    RUN("lost-response",lost_case());RUN("conflict",conflict_case());
    RUN("alias-cache",alias_cache_case());RUN("alias-next",alias_next_case());
#undef RUN
    if(!selected)return 2;
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
