/* SPDX-License-Identifier: GPL-2.0-only */
/* New stream-owner composition only. Exact frozen graph fixture support supplies
 * the seven extracted driver methods and modeled controller descriptors. Only
 * the fixture syscall names are renamed for synchronous stream observations;
 * production graph/children keep their original ABI. No parent cases execute.
 * Stream bytes/fds/errors are modeled; pthread success paths remain real.
 * EPIPE is a returned script, not a SIGPIPE-survival or real-pipe test. */
#define mba_sys_ioctl fixture_mba_sys_ioctl
#define mba_sys_close fixture_mba_sys_close
#include "graph-support.inc"
#undef mba_sys_ioctl
#undef mba_sys_close
#include "stream.h"

enum stream_event { SE_READ,SE_WRITE,SE_NATIVE,SE_GRAPH_CLOSE,SE_STREAM_CLOSE };
struct stream_observation {
    enum stream_event event; unsigned ordinal; size_t length;
    int inspected,owner_published,caller_consumed;
    struct mbgs_evidence e;
};
static struct stream_observation stream_observations[128];
static unsigned stream_observation_count,read_count,write_count,stream_close_count;
static struct mbgs_owner *stream_watch;
static struct mbgs_owner **stream_watch_slot;
static int *transport_caller;
static struct mbog_graph **graph_caller;
static bool stream_live,frame_return_unknown,exercise_reentry,reentry_ok;
static int transport_fd,positive_error,stream_close_result,stream_close_error;
static size_t read_limit,write_limit,input_length,input_cursor,output_length;
static uint8_t input_bytes[2048],output_bytes[4096];
struct io_fault { unsigned on; ssize_t count; int error; size_t effects; };
static struct io_fault read_fault,write_fault;

static struct mbgs_evidence stream_evidence(struct mbgs_owner *owner)
{ struct mbgs_evidence e={0};assert(!mbgs_inspect(owner,&e));return e; }
static bool record_equal(const struct mbgs_record *a,const struct mbgs_record *b)
{
    return a->number==b->number && a->read_calls==b->read_calls && a->write_calls==b->write_calls &&
        a->request_received==b->request_received && a->request_expected==b->request_expected &&
        a->response_length==b->response_length && a->response_sent==b->response_sent &&
        a->header_valid==b->header_valid && a->header.kind==b->header.kind &&
        a->header.length==b->header.length && a->header.exchange==b->header.exchange &&
        !memcmp(a->header.session,b->header.session,16) &&
        a->last_read.attempted==b->last_read.attempted && a->last_read.returned==b->last_read.returned &&
        a->last_read.offset==b->last_read.offset && a->last_read.requested==b->last_read.requested &&
        a->last_read.raw.count==b->last_read.raw.count && a->last_read.raw.error==b->last_read.raw.error &&
        a->last_write.attempted==b->last_write.attempted && a->last_write.returned==b->last_write.returned &&
        a->last_write.offset==b->last_write.offset && a->last_write.requested==b->last_write.requested &&
        a->last_write.raw.count==b->last_write.raw.count && a->last_write.raw.error==b->last_write.raw.error &&
        a->dispatch.attempted==b->dispatch.attempted && a->dispatch.returned==b->dispatch.returned &&
        a->dispatch.result==b->dispatch.result && a->dispatch.written==b->dispatch.written &&
        !memcmp(a->request,b->request,sizeof a->request) && !memcmp(a->response,b->response,sizeof a->response);
}
static unsigned total_calls(void)
{ return io_count()+read_count+write_count+stream_close_count; }
static void observe_stream(enum stream_event event,unsigned ordinal,size_t length)
{
    if(!stream_watch)return;
    assert(stream_observation_count<128);
    struct stream_observation *o=&stream_observations[stream_observation_count++];
    memset(o,0,sizeof *o);o->event=event;o->ordinal=ordinal;o->length=length;
    o->owner_published=stream_watch_slot && *stream_watch_slot==stream_watch;
    o->caller_consumed=transport_caller && *transport_caller==-1 && graph_caller && !*graph_caller;
    o->inspected=mbgs_inspect(stream_watch,&o->e);
    if(exercise_reentry){
        struct mbgs_evidence before=o->e,after;
        struct mbgs_owner *same=stream_watch;
        struct mbgs_dispose_report report={0};int fd=15;struct mbog_graph *none=NULL;
        unsigned count=total_calls();
        bool ok=mbgs_step(same)==MBGS_BUSY && mbgs_next(same)==MBGS_BUSY &&
            mbgs_take(same,&fd,&none)==MBGS_BUSY && fd==15 && !none &&
            mbgs_dispose(&same,&report)==MBGS_BUSY && same==stream_watch &&
            report.completed && report.result==MBGS_BUSY &&
            mbgs_finalize_graph(same)==MBGS_BUSY && mbgs_finalize(&same)==MBGS_BUSY &&
            same==stream_watch && !mbgs_inspect(same,&after) &&
            before.active && after.active && before.phase==after.phase &&
            before.current_operation==after.current_operation &&
            record_equal(&before.current,&after.current) &&
            before.disposal_started==after.disposal_started && total_calls()==count;
        reentry_ok=ok&&reentry_ok;
    }
}
static const struct stream_observation *stream_observation(enum stream_event event,unsigned ordinal)
{
    for(unsigned i=0;i<stream_observation_count;i++)
        if(stream_observations[i].event==event && stream_observations[i].ordinal==ordinal)
            return &stream_observations[i];
    return NULL;
}
struct mba_sys_result mba_sys_ioctl(int fd,unsigned long operation,void *argument)
{
    observe_stream(SE_NATIVE,native_calls+1,operation);
    struct mba_sys_result r=fixture_mba_sys_ioctl(fd,operation,argument);
    /* A test-only lost native success return after the actual extracted FRAME. */
    if(frame_return_unknown && operation==AZ_OVERLAY_FRAME && !r.result)
        return(struct mba_sys_result){-1,EIO};
    return r;
}
struct mba_sys_result mba_sys_close(int fd)
{
    observe_stream(SE_GRAPH_CLOSE,closes+1,(size_t)fd);
    return fixture_mba_sys_close(fd);
}
struct mbgs_io_result mbgs_sys_read(int fd,void *buffer,size_t n)
{
    assert(stream_live && fd==transport_fd && n>0 && n<=256);
    read_count++;observe_stream(SE_READ,read_count,n);
    size_t amount=input_length-input_cursor;
    if(amount>n)amount=n;
    if(read_fault.on==read_count){
        if(amount>read_fault.effects)amount=read_fault.effects;
        memcpy(buffer,input_bytes+input_cursor,amount);input_cursor+=amount;
        return(struct mbgs_io_result){read_fault.count,read_fault.error};
    }
    if(amount>read_limit)amount=read_limit;
    memcpy(buffer,input_bytes+input_cursor,amount);input_cursor+=amount;
    return(struct mbgs_io_result){(ssize_t)amount,positive_error};
}
struct mbgs_io_result mbgs_sys_write(int fd,const void *buffer,size_t n)
{
    assert(stream_live && fd==transport_fd && n>0 && n<=668);
    write_count++;observe_stream(SE_WRITE,write_count,n);
    size_t amount=n;
    if(write_fault.on==write_count){
        if(amount>write_fault.effects)amount=write_fault.effects;
        assert(amount<=sizeof output_bytes-output_length);
        memcpy(output_bytes+output_length,buffer,amount);output_length+=amount;
        return(struct mbgs_io_result){write_fault.count,write_fault.error};
    }
    if(amount>write_limit)amount=write_limit;
    assert(amount<=sizeof output_bytes-output_length);
    memcpy(output_bytes+output_length,buffer,amount);output_length+=amount;
    return(struct mbgs_io_result){(ssize_t)amount,positive_error};
}
struct mbgs_close_result mbgs_sys_close(int fd)
{
    assert(stream_live && fd==transport_fd);stream_close_count++;
    observe_stream(SE_STREAM_CLOSE,stream_close_count,(size_t)fd);
    /* Fixture accounting deliberately knows the modeled close consumed its fd,
     * including scripted EINTR/abnormal returns. This is not Linux close proof. */
    stream_live=false;
    return(struct mbgs_close_result){stream_close_result,stream_close_error};
}
static void stream_reset(void)
{
    reset();read_count=write_count=stream_close_count=stream_observation_count=0;
    stream_watch=NULL;stream_watch_slot=NULL;transport_caller=NULL;graph_caller=NULL;
    stream_live=true;transport_fd=12;positive_error=stream_close_result=stream_close_error=0;
    read_limit=256;write_limit=668;input_length=input_cursor=output_length=0;
    read_fault=(struct io_fault){0};write_fault=(struct io_fault){0};
    frame_return_unknown=exercise_reentry=false;reentry_ok=true;
    memset(input_bytes,0,sizeof input_bytes);memset(output_bytes,0,sizeof output_bytes);
    memset(stream_observations,0,sizeof stream_observations);
}
static struct mbog_graph *ready_graph(struct mbog_plan *p)
{
    *p=plan();int fd=3;struct mbog_graph *g=create(p);
    assert(!mbog_take(g,&fd) && fd==-1 && !mbog_start(g));return g;
}
static struct mbgs_owner *new_owner(void)
{ struct mbgs_owner *o=NULL;assert(!mbgs_create(&o) && o);return o; }
static void watch_owner(struct mbgs_owner **owner,int *fd,struct mbog_graph **graph)
{ stream_watch=*owner;stream_watch_slot=owner;transport_caller=fd;graph_caller=graph; }
static struct mbgs_owner *owned(struct mbog_plan *p)
{
    struct mbog_graph *g=ready_graph(p);struct mbgs_owner *o=new_owner();int fd=transport_fd;
    assert(!mbgs_take(o,&fd,&g) && fd==-1 && !g);return o;
}
static void input_set(const uint8_t *bytes,size_t length)
{ assert(length<=sizeof input_bytes);memcpy(input_bytes,bytes,length);input_length=length;input_cursor=0; }
static void stream_cleanup(struct mbgs_owner **owner,struct mbog_graph **untransferred)
{
    /* All modeled callbacks have returned. Only this explicit fixture boundary
     * supplies independent quiescence and accounts for scripted close effects. */
    stream_watch=NULL;stream_watch_slot=NULL;transport_caller=NULL;graph_caller=NULL;
    watch=NULL;watch_slot=NULL;caller_slot=NULL;exercise_reentry=false;
    uncertain_close_fd=-1;stream_close_result=stream_close_error=0;
    memset(sync_failure,0,sizeof sync_failure);
    if(owner && *owner){
        for(unsigned i=0;i<4 && *owner;i++){
            struct mbgs_dispose_report r={0};int rc=mbgs_dispose(owner,&r);
            assert(rc==MBGS_OK || rc==MBGS_UNCERTAIN);
            if(r.owner_released)*owner=NULL;
            if(*owner){
                struct mbgs_evidence e=stream_evidence(*owner);
                if(e.graph_present && no_children(&e.graph))assert(!mbgs_finalize_graph(*owner));
                e=stream_evidence(*owner);
                if(!e.graph_present && !e.descriptor_held)assert(!mbgs_finalize(owner));
            }
        }
        assert(!*owner);
    }
    if(stream_live)stream_live=false; /* externally retained/untransferred fixture fd */
    cleanup(untransferred);
}
static void net32(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(24-8*i)); }
static void net64(uint8_t *p,uint64_t n)
{ for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(56-8*i)); }
static struct mbw_header request_header(const struct mbog_plan *p,uint32_t kind,size_t length,uint64_t exchange)
{
    struct mbw_header h={.kind=kind,.length=(uint32_t)length,.exchange=exchange};
    memcpy(h.session,p->session,16);return h;
}
static size_t adopt_request(const struct mbog_plan *p,uint8_t *bytes)
{
    struct mbw_adopt_request a={.state=expected_post(p),.producer_id=p->producer_id};
    struct mbw_header h=request_header(p,MBW_ADOPT_REQUEST,256,1);size_t n=0;
    assert(!mbw_encode_adopt_request(&h,&a,bytes,256,&n));return n;
}
static size_t frame_request(const struct mbog_plan *p,uint8_t *bytes)
{
    struct mb_state s=expected_post(p);
    struct mb_request f={.grant={.instance={s.instance[0],s.instance[1]},.run_id=s.run_id,
        .epoch=s.epoch,.producer_id=p->producer_id},.request_id=1};
    pattern(f.frame,97);struct mbw_header h=request_header(p,MBW_FRAME_REQUEST,248,2);size_t n=0;
    assert(!mbw_encode_frame_request(&h,&f,bytes,256,&n));return n;
}
static void state_wire(uint8_t *p,const struct mb_state *s)
{
    net64(p,s->instance[0]);net64(p+8,s->instance[1]);net64(p+16,s->run_id);net64(p+24,s->epoch);
    net64(p+32,s->sequence);net32(p+40,s->flags);net32(p+44,(uint32_t)s->crc_len);memcpy(p+48,s->frame,152);
}
static bool exact_frame_reply(const struct mbog_plan *p,const uint8_t *request,const uint8_t *reply,size_t n)
{
    struct mb_state s=expected_post(p);s.sequence++;memcpy(s.frame,request+96,152);
    uint8_t expected[668]={0};memcpy(expected,"AZMBRPC1",8);net32(expected+8,1);
    net32(expected+12,MBW_FRAME_RESPONSE);net32(expected+16,668);memcpy(expected+24,p->session,16);
    memcpy(expected+40,request+40,8);net32(expected+56,MB_OUTCOME_COMMITTED);net32(expected+60,MB_OP_FRAME);
    memcpy(expected+72,request+48,200);net64(expected+272,s.instance[0]);net64(expected+280,s.instance[1]);
    net64(expected+288,s.run_id);net64(expected+296,s.epoch);net64(expected+304,s.sequence);
    net32(expected+312,1);memcpy(expected+316,s.frame,152);state_wire(expected+468,&s);
    return n==668 && !memcmp(reply,expected,668);
}
static int drive_until(struct mbgs_owner *o,enum mbgs_phase phase)
{
    int rc=MBGS_OK;
    for(unsigned i=0;i<256;i++){
        struct mbgs_evidence e=stream_evidence(o);
        if(e.phase==phase || e.phase==MBGS_TERMINAL)return rc;
        rc=mbgs_step(o);
        if(rc<0)return rc;
    }
    return MBGS_STATE; /* bounded even for a mutant with no progress */
}
static void prepare_frame_write(struct mbgs_owner *o,const struct mbog_plan *p,uint8_t *frame)
{
    uint8_t adopt[256];input_set(adopt,adopt_request(p,adopt));
    assert(drive_until(o,MBGS_WRITTEN)==MBGS_RECORD_WRITTEN);
    assert(!mbgs_next(o));input_set(frame,frame_request(p,frame));
    assert(drive_until(o,MBGS_WRITE)==MBGS_PROGRESS);
}
static bool terminal_stable(struct mbgs_owner *o,int first)
{
    struct mbgs_evidence a=stream_evidence(o);unsigned calls=total_calls();
    int step=mbgs_step(o),next=mbgs_next(o);struct mbgs_evidence b=stream_evidence(o);
    return step==MBGS_CLOSED && next==MBGS_CLOSED && b.phase==MBGS_TERMINAL &&
        b.first_terminal==first && a.first_terminal==first &&
        a.terminal_operation==b.terminal_operation && record_equal(&a.current,&b.current) &&
        total_calls()==calls && b.descriptor_held && b.graph_present && !stream_close_count && !closes;
}
static bool record_armed(const struct stream_observation *o,enum mbgs_operation op)
{
    return o && !o->inspected && o->owner_published && o->caller_consumed &&
        o->e.active && o->e.current_operation==op && o->e.descriptor_held &&
        o->e.graph_present && !o->e.graph.broker.backend.context &&
        !o->e.graph.broker.backend.snapshot && !o->e.graph.broker.backend.submit;
}

static bool stream_dispose_equal(const struct mbgs_dispose_report *a,const struct mbgs_dispose_report *b)
{
    return a->attempt_number==b->attempt_number && a->graph_attempted==b->graph_attempted &&
        a->graph_returned==b->graph_returned && a->graph_result==b->graph_result &&
        disposal_equal(&a->graph_report,&b->graph_report) &&
        a->stream_close.attempted==b->stream_close.attempted &&
        a->stream_close.returned==b->stream_close.returned &&
        a->stream_close.descriptor==b->stream_close.descriptor &&
        a->stream_close.raw.result==b->stream_close.raw.result &&
        a->stream_close.raw.error==b->stream_close.raw.error &&
        a->completed==b->completed && a->result==b->result &&
        a->owner_released==b->owner_released && a->uncertainty_seen==b->uncertainty_seen;
}
static void retain_case(void)
{
    bool ok=true;
    for(unsigned i=0;i<5;i++){
        stream_reset();struct mbog_plan p;struct mbog_graph *g;
        if(i==4){p=plan();g=create(&p);}else g=ready_graph(&p);
        if(i==3){
            uint8_t request[256],reply[668];size_t written=0;
            assert(!mbog_exchange(g,request,adopt_request(&p,request),reply,sizeof reply,&written));
        }
        struct mbgs_owner *o=new_owner();struct mbgs_evidence before=stream_evidence(o);
        int fd=i==0?3:i==1?8:i==2?-1:12;transport_fd=fd;
        struct mbog_graph *original_graph=g;unsigned calls=total_calls();
        int rc=mbgs_take(o,&fd,&g);struct mbgs_evidence after=stream_evidence(o);
        int expected=i<3?MBGS_INVALID:MBGS_STATE;
        bool one=rc==expected && fd==transport_fd && g==original_graph &&
            before.phase==MBGS_ALLOCATED && !before.taken && !before.descriptor_held &&
            !before.graph_present && before.stream_fd==-1 && after.phase==MBGS_ALLOCATED &&
            !after.taken && !after.descriptor_held && !after.graph_present &&
            record_equal(&before.current,&after.current) && total_calls()==calls;
        ok=one&&ok;stream_cleanup(&o,&g);
    }
    stream_reset();struct mbog_plan p;struct mbog_graph *g=ready_graph(&p);
    struct mbgs_owner *o=new_owner();transport_fd=0;int fd=0;
    unsigned before=total_calls();int rc=mbgs_take(o,&fd,&g);
    struct mbgs_evidence e=stream_evidence(o);int other_fd=15;struct mbog_graph *none=NULL;
    int again=mbgs_take(o,&other_fd,&none);struct mbgs_evidence after=stream_evidence(o);
    ok=(!rc && fd==-1 && !g && e.phase==MBGS_HEADER && e.taken && e.descriptor_held &&
        e.stream_fd==0 && e.graph_present && e.graph_evidence_valid &&
        e.current.number==1 && e.current.request_expected==48 && !e.current.request_received &&
        !e.graph.broker.backend.context && !e.graph.broker.backend.snapshot &&
        !e.graph.broker.backend.submit && again==MBGS_STATE && other_fd==15 && !none &&
        record_equal(&e.current,&after.current) && total_calls()==before)&&ok;
    stream_cleanup(&o,&g);
    ck(ok,"stream_retains_fresh_graph_and_fd_zero_without_io");
}
static void fragments_case(void)
{
    stream_reset();struct mbog_plan p;struct mbog_graph *g=ready_graph(&p);
    struct mbgs_owner *o=new_owner();int fd=transport_fd;watch_owner(&o,&fd,&g);
    assert(!mbgs_take(o,&fd,&g));uint8_t requests[504];
    assert(adopt_request(&p,requests)==256 && frame_request(&p,requests+256)==248);
    input_set(requests,sizeof requests);read_limit=23;write_limit=37;positive_error=EIO;
    bool ok=true;int rc=0;
    for(unsigned record=0;record<2;record++){
        for(unsigned i=0;i<256;i++){
            struct mbgs_evidence before=stream_evidence(o);
            if(before.phase==MBGS_WRITTEN || before.phase==MBGS_TERMINAL)break;
            unsigned calls=total_calls();rc=mbgs_step(o);
            struct mbgs_evidence after=stream_evidence(o);
            bool one=total_calls()==calls+1 && rc>=0;
            if(before.phase==MBGS_HEADER || before.phase==MBGS_BODY)
                one=one && after.current.read_calls==before.current.read_calls+1 &&
                    !after.current.dispatch.attempted && !after.current.write_calls;
            if(before.phase==MBGS_DISPATCH)
                one=one && after.current.dispatch.attempted && after.current.dispatch.returned &&
                    after.current.dispatch.result==MBWD_OK && !after.current.response_sent;
            ok=one&&ok;
        }
        struct mbgs_evidence e=stream_evidence(o);unsigned calls=total_calls(),reads=read_count;
        int repeat=mbgs_step(o);struct mbgs_evidence repeated=stream_evidence(o);
        bool one=e.phase==MBGS_WRITTEN && rc==MBGS_RECORD_WRITTEN &&
            repeat==MBGS_RECORD_WRITTEN && record_equal(&e.current,&repeated.current) &&
            total_calls()==calls && read_count==reads &&
            input_cursor==(record?504:256) && e.current.response_sent==e.current.response_length &&
            e.current.request_received==(record?248:256) &&
            e.current.dispatch.written==(record?668:296) && !e.first_terminal &&
            !memcmp(e.current.request,requests+(record?256:0),record?248:256);
        ok=one&&ok;
        if(!record){
            int next=mbgs_next(o);struct mbgs_evidence advanced=stream_evidence(o);
            ok=(!next && total_calls()==calls && advanced.phase==MBGS_HEADER &&
                advanced.last_completed_valid && record_equal(&advanced.last_completed,&e.current) &&
                advanced.current.number==2 && advanced.current.request_expected==48 &&
                !advanced.current.request_received && !advanced.current.read_calls &&
                !advanced.current.write_calls && !advanced.current.dispatch.attempted &&
                zero_bytes(advanced.current.request,256) && zero_bytes(advanced.current.response,668))&&ok;
        }
    }
    struct mbgs_evidence done=stream_evidence(o);
    struct mb_state actual=semantic(raw_state()),expected=expected_post(&p);
    expected.sequence=1;memcpy(expected.frame,requests+256+96,152);
    const struct stream_observation *r=stream_observation(SE_READ,1);
    const struct stream_observation *w=stream_observation(SE_WRITE,1);
    const struct stream_observation *d=stream_observation(SE_NATIVE,6);
    ok=(states==5 && claims==1 && duplicates==1 && frames==1 && native_calls==7 &&
        state_equal(&actual,&expected) && output_length==964 &&
        exact_frame_reply(&p,requests+256,output_bytes+296,668) &&
        !memcmp(done.current.response,output_bytes+296,668) &&
        record_armed(r,MBGS_OP_READ) && r->e.current.last_read.attempted &&
        !r->e.current.last_read.returned && r->e.current.last_read.requested==48 &&
        !r->e.current.last_read.offset && record_armed(w,MBGS_OP_WRITE) &&
        w->e.current.last_write.attempted && !w->e.current.last_write.returned &&
        w->e.current.last_write.requested==296 && record_armed(d,MBGS_OP_DISPATCH) &&
        d->e.current.dispatch.attempted && !d->e.current.dispatch.returned &&
        d->e.current.request_received==256 && !d->e.current.response_length)&&ok;
    stream_cleanup(&o,&g);
    ck(ok,"stream_fragments_once_per_step_and_archives_only_on_explicit_next");
}
static void header_refusal_case(void)
{
    bool ok=true;
    for(unsigned i=0;i<6;i++){
        stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);
        uint8_t bytes[256];assert(adopt_request(&p,bytes)==256);
        if(i==0)bytes[0]^=1;
        if(i==1)net32(bytes+8,2);
        if(i==2)bytes[20]=1;
        if(i==3){net32(bytes+12,MBW_ADOPT_RESPONSE);net32(bytes+16,296);}
        if(i==4)net32(bytes+16,UINT32_MAX);
        if(i==5)net32(bytes+16,248);
        input_set(bytes,sizeof bytes);unsigned native=native_calls;int rc=mbgs_step(o);
        struct mbgs_evidence e=stream_evidence(o);
        bool one=rc==MBGS_PROTOCOL && e.phase==MBGS_TERMINAL && !e.current.header_valid &&
            e.current.request_received==48 && e.current.request_expected==48 &&
            input_cursor==48 && read_count==1 && !write_count && native_calls==native &&
            !e.current.dispatch.attempted && !memcmp(e.current.request,bytes,48) &&
            terminal_stable(o,MBGS_PROTOCOL);
        ok=one&&ok;struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    }
    ck(ok,"stream_rejects_noncanonical_header_before_body_or_dispatch");
}
static void read_eof_case(void)
{
    bool ok=true;const size_t lengths[]={0,13,65};
    for(unsigned i=0;i<3;i++){
        stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);
        uint8_t bytes[256];assert(adopt_request(&p,bytes)==256);input_set(bytes,lengths[i]);
        int rc=drive_until(o,MBGS_TERMINAL);struct mbgs_evidence e=stream_evidence(o);
        bool one=rc==MBGS_EOF && e.current.request_received==lengths[i] &&
            e.current.last_read.attempted && e.current.last_read.returned &&
            e.current.last_read.raw.count==0 && e.current.last_read.offset==lengths[i] &&
            !memcmp(e.current.request,bytes,lengths[i]) && !e.current.dispatch.attempted &&
            !write_count && states==4 && !frames && terminal_stable(o,MBGS_EOF);
        ok=one&&ok;struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    }
    stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);uint8_t bytes[256];
    input_set(bytes,adopt_request(&p,bytes));assert(drive_until(o,MBGS_WRITTEN)==MBGS_RECORD_WRITTEN);
    struct mbgs_record completed=stream_evidence(o).current;assert(!mbgs_next(o));
    int rc=mbgs_step(o);struct mbgs_evidence e=stream_evidence(o);
    ok=(rc==MBGS_EOF && e.last_completed_valid && record_equal(&completed,&e.last_completed) &&
        e.current.number==2 && !e.current.request_received && !e.current.dispatch.attempted &&
        terminal_stable(o,MBGS_EOF))&&ok;
    struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    ck(ok,"stream_eof_retains_partial_and_previous_completed_record_without_cleanup");
}
static void read_errors_case(void)
{
    bool ok=true;const int errors[]={EINTR,EAGAIN,EIO};
    for(unsigned i=0;i<6;i++){
        stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);uint8_t bytes[256];
        input_set(bytes,adopt_request(&p,bytes));size_t credited=i<3?11:48;
        read_limit=credited;assert(mbgs_step(o)==MBGS_PROGRESS);
        read_fault=(struct io_fault){.on=2,.count=-1,.error=errors[i%3],.effects=5};
        unsigned calls=total_calls();int rc=mbgs_step(o);struct mbgs_evidence e=stream_evidence(o);
        bool one=rc==MBGS_IO && total_calls()==calls+1 && e.current.request_received==credited &&
            input_cursor==credited+5 && e.current.last_read.raw.count==-1 &&
            e.current.last_read.raw.error==errors[i%3] && e.current.last_read.returned &&
            e.current.last_read.offset==credited && e.current.last_read.requested==(i<3?37:208) &&
            !memcmp(e.current.request,bytes,credited+5) && !e.current.dispatch.attempted &&
            !write_count && terminal_stable(o,MBGS_IO);
        ok=one&&ok;struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    }
    ck(ok,"stream_read_errors_keep_raw_effects_without_credit_retry_or_dispatch");
}
static void read_abnormal_case(void)
{
    bool ok=true;const ssize_t counts[]={-2,49};
    for(unsigned i=0;i<2;i++){
        stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);uint8_t bytes[256];
        input_set(bytes,adopt_request(&p,bytes));
        read_fault=(struct io_fault){.on=1,.count=counts[i],.error=EBUSY,.effects=7};
        int rc=mbgs_step(o);struct mbgs_evidence e=stream_evidence(o);
        bool one=rc==MBGS_COUNT && !e.current.request_received && input_cursor==7 &&
            e.current.last_read.requested==48 && e.current.last_read.raw.count==counts[i] &&
            e.current.last_read.raw.error==EBUSY && !memcmp(e.current.request,bytes,7) &&
            !e.current.dispatch.attempted && !write_count && terminal_stable(o,MBGS_COUNT);
        ok=one&&ok;struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    }
    ck(ok,"stream_impossible_read_counts_preserve_bounded_bytes_and_zero_credit");
}
static void write_errors_case(void)
{
    bool ok=true;const int errors[]={EINTR,EAGAIN,EPIPE};
    for(unsigned i=0;i<3;i++){
        stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);uint8_t request[256];
        prepare_frame_write(o,&p,request);struct mbgs_record before=stream_evidence(o).current;
        write_limit=17;assert(mbgs_step(o)==MBGS_PROGRESS);
        write_fault=(struct io_fault){.on=write_count+1,.count=-1,.error=errors[i],.effects=5};
        int rc=mbgs_step(o);struct mbgs_evidence e=stream_evidence(o);struct mb_state actual=semantic(raw_state());
        bool one=rc==MBGS_IO && e.current.response_sent==17 && output_length==296+22 &&
            e.current.last_write.offset==17 && e.current.last_write.requested==651 &&
            e.current.last_write.returned && e.current.last_write.raw.count==-1 &&
            e.current.last_write.raw.error==errors[i] && e.current.response_length==668 &&
            !memcmp(e.current.response,before.response,668) && !memcmp(e.current.request,request,248) &&
            !memcmp(output_bytes+296,before.response,22) && frames==1 && actual.sequence==1 &&
            !memcmp(actual.frame,request+96,152) && terminal_stable(o,MBGS_IO);
        ok=one&&ok;struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    }
    ck(ok,"stream_failed_response_write_retains_committed_evidence_without_replay");
}
static void write_abnormal_case(void)
{
    bool ok=true;const ssize_t counts[]={0,-2,669};
    for(unsigned i=0;i<3;i++){
        stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);uint8_t request[256];
        prepare_frame_write(o,&p,request);
        write_fault=(struct io_fault){.on=write_count+1,.count=counts[i],.error=EBUSY,.effects=0};
        int expected=i?MBGS_COUNT:MBGS_NO_PROGRESS;int rc=mbgs_step(o);
        struct mbgs_evidence e=stream_evidence(o);
        bool one=rc==expected && e.current.response_length==668 && !e.current.response_sent &&
            e.current.last_write.requested==668 && e.current.last_write.raw.count==counts[i] &&
            e.current.last_write.raw.error==EBUSY && e.current.last_write.returned &&
            exact_frame_reply(&p,request,e.current.response,668) && output_length==296 &&
            frames==1 && terminal_stable(o,expected);
        ok=one&&ok;struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    }
    ck(ok,"stream_zero_or_impossible_write_counts_are_terminal_without_resend");
}
static void dispatch_refusal_case(void)
{
    stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);
    uint8_t request[256];assert(adopt_request(&p,request)==256);request[247]^=1;
    input_set(request,sizeof request);assert(drive_until(o,MBGS_DISPATCH)==MBGS_PROGRESS);
    struct mbgs_evidence before=stream_evidence(o);unsigned calls=total_calls();
    int rc=mbgs_step(o);struct mbgs_evidence e=stream_evidence(o);
    bool ok=rc==MBGS_GRAPH && e.current.dispatch.attempted && e.current.dispatch.returned &&
        e.current.dispatch.result!=MBWD_OK && !e.current.dispatch.written &&
        !e.current.response_length && !e.current.response_sent && !write_count &&
        e.current.request_received==256 && !memcmp(e.current.request,request,256) &&
        !before.current.dispatch.attempted && total_calls()==calls &&
        states==4 && !frames && e.graph.last_exchange.attempted &&
        e.graph.last_exchange.returned && terminal_stable(o,MBGS_GRAPH);
    struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    ck(ok,"stream_retains_actual_dispatch_refusal_and_never_writes_or_replays");
}
static void unknown_response_case(void)
{
    stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);uint8_t request[256];
    frame_return_unknown=true;prepare_frame_write(o,&p,request);
    struct mbgs_evidence before=stream_evidence(o);int rc=drive_until(o,MBGS_WRITTEN);
    struct mbgs_evidence e=stream_evidence(o);struct mbw_header h={0};struct mbw_frame_response response={0};
    int decoded=mbw_decode_frame_response(e.current.response,e.current.response_length,&h,&response);
    struct mb_state actual=semantic(raw_state());unsigned calls=total_calls();
    int again=mbgs_step(o);
    bool ok=rc==MBGS_RECORD_WRITTEN && again==MBGS_RECORD_WRITTEN && total_calls()==calls &&
        e.phase==MBGS_WRITTEN && !e.first_terminal && e.current.response_sent==668 &&
        e.current.dispatch.result==MBWD_OK && !decoded && response.result==MB_OK &&
        response.receipt.outcome==MB_OUTCOME_UNKNOWN && response.receipt.backend_error==EIO &&
        response.provenance==0 && frames==1 && actual.sequence==1 &&
        !memcmp(actual.frame,request+96,152) && e.graph.dispatch.phase==MBWD_TERMINAL &&
        !memcmp(e.current.response,before.current.response,668) &&
        output_length==964 && !memcmp(output_bytes+296,e.current.response,668);
    struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    ck(ok,"stream_complete_local_write_preserves_unknown_after_modeled_native_commit");
}
static void reentry_case(void)
{
    stream_reset();struct mbog_plan p;struct mbog_graph *g=ready_graph(&p);
    struct mbgs_owner *o=new_owner();int fd=transport_fd;watch_owner(&o,&fd,&g);
    assert(!mbgs_take(o,&fd,&g));exercise_reentry=true;
    uint8_t bytes[256];input_set(bytes,adopt_request(&p,bytes));
    int rc=drive_until(o,MBGS_WRITTEN);struct mbgs_evidence done=stream_evidence(o);
    struct mbgs_dispose_report report={0};int disposed=mbgs_dispose(&o,&report);
    bool slot_consumed=!o;if(report.owner_released)o=NULL;
    const struct stream_observation *close=stream_observation(SE_STREAM_CLOSE,1);
    bool ok=rc==MBGS_RECORD_WRITTEN && done.phase==MBGS_WRITTEN &&
        !disposed && slot_consumed && !o && report.owner_released && reentry_ok &&
        read_count==2 && write_count==1 && states==5 && !frames &&
        closes==2 && stream_close_count==1 && close && close->e.active &&
        close->e.current_operation==MBGS_OP_STREAM_CLOSE && !close->e.descriptor_held &&
        close->e.stream_fd==-1 && !close->e.graph_present && close->e.graph_evidence_valid &&
        close->e.stream_close.attempted && !close->e.stream_close.returned;
    stream_cleanup(&o,&g);
    ck(ok,"stream_same_thread_inspection_and_busy_reentry_preserve_inflight_custody");
}
static void dispose_order_case(void)
{
    bool ok=true;
    for(unsigned i=0;i<2;i++){
        stream_reset();struct mbog_plan p;struct mbog_graph *g=ready_graph(&p);
        struct mbgs_owner *o=new_owner();int fd=transport_fd;watch_owner(&o,&fd,&g);
        assert(!mbgs_take(o,&fd,&g));sync_failure[i?S_ATTR_DESTROY:S_MUTEX_DESTROY]=EBUSY;
        struct mbgs_dispose_report first={0},last={0};int rc=mbgs_dispose(&o,&first);
        struct mbgs_evidence held=stream_evidence(o);unsigned count=total_calls();
        int step=mbgs_step(o),next=mbgs_next(o);
        bool one=rc==MBGS_GRAPH && first.completed && first.graph_attempted &&
            first.graph_returned && first.graph_result==MBOG_CHILD && !first.stream_close.attempted &&
            held.disposal_started && held.phase==MBGS_DISPOSING && held.graph_present &&
            held.descriptor_held && held.stream_fd==transport_fd && stream_live &&
            held.graph.claim_present && held.graph.adapter_present && held.graph.broker_present &&
            !held.graph.dispatch_present && !closes && !stream_close_count &&
            held.graph.broker.attr_held && held.graph.broker.mutex_held==!i &&
            stream_dispose_equal(&held.first_disposal,&first) &&
            step==MBGS_CLOSED && next==MBGS_CLOSED && total_calls()==count;
        sync_failure[i?S_ATTR_DESTROY:S_MUTEX_DESTROY]=0;
        int finished=mbgs_dispose(&o,&last);bool slot_consumed=!o;if(last.owner_released)o=NULL;
        const struct stream_observation *close=stream_observation(SE_STREAM_CLOSE,1);
        one=one && !finished && slot_consumed && !o && last.owner_released && last.attempt_number==2 &&
            last.graph_report.broker_report.mutex_attempted==!i &&
            sync_calls[S_MUTEX_DESTROY]==(i?1u:2u) &&
            sync_real[S_MUTEX_DESTROY]==1 && sync_real[S_ATTR_DESTROY]==1 &&
            closes==2 && close_fds[0]==8 && close_fds[1]==3 && stream_close_count==1 &&
            close && !close->e.graph_present && close->e.graph_evidence_valid &&
            !close->e.descriptor_held && close->e.stream_fd==-1 && close->e.active &&
            stream_dispose_equal(&close->e.first_disposal,&first);
        ok=one&&ok;stream_cleanup(&o,&g);
    }
    ck(ok,"stream_disposal_stops_on_broker_failure_then_retires_graph_before_transport");
}
static void close_uncertain_case(void)
{
    stream_reset();struct mbog_plan p;struct mbgs_owner *o=owned(&p);
    uncertain_close_fd=8;struct mbgs_dispose_report first={0},second={0},third={0},refused={0};
    int a=mbgs_dispose(&o,&first);struct mbgs_evidence e1=stream_evidence(o);
    bool ok=a==MBGS_UNCERTAIN && e1.graph_present && e1.descriptor_held &&
        e1.uncertainty_seen && e1.graph.claim_present && !e1.graph.adapter_present &&
        closes==1 && close_fds[0]==8 && !stream_close_count && stream_live;
    unsigned calls=total_calls();int premature=mbgs_finalize_graph(o);
    struct mbgs_evidence denied=stream_evidence(o);
    ok=(premature==MBGS_GRAPH && denied.graph_present && denied.last_graph_finalize.returned &&
        denied.last_graph_finalize.result==MBOG_STATE && total_calls()==calls)&&ok;
    /* The fixture independently accounts for the scripted close, then retries
     * graph retirement. No prior descriptor number is ever closed again. */
    uncertain_close_fd=-1;int b=mbgs_dispose(&o,&second);struct mbgs_evidence e2=stream_evidence(o);
    ok=(b==MBGS_UNCERTAIN && e2.graph_present && no_children(&e2.graph) &&
        e2.descriptor_held && !stream_close_count && closes==2 &&
        stream_dispose_equal(&e2.first_disposal,&first) &&
        stream_dispose_equal(&e2.last_disposal,&second))&&ok;
    calls=total_calls();int f=mbgs_finalize_graph(o);struct mbgs_evidence empty=stream_evidence(o);
    ok=(!f && total_calls()==calls && !empty.graph_present && empty.graph_evidence_valid &&
        empty.uncertainty_seen && empty.descriptor_held &&
        empty.last_graph_finalize.returned && empty.last_graph_finalize.result==MBOG_OK)&&ok;
    int c=mbgs_dispose(&o,&third);struct mbgs_evidence e3=stream_evidence(o);
    calls=total_calls();int d=mbgs_dispose(&o,&refused);struct mbgs_evidence e4=stream_evidence(o);
    ok=(c==MBGS_UNCERTAIN && !e3.graph_present && !e3.descriptor_held && e3.uncertainty_seen &&
        stream_close_count==1 && closes==2 && e3.stream_close.returned &&
        e3.stream_close.raw.result==0 && d==MBGS_UNCERTAIN && total_calls()==calls &&
        stream_dispose_equal(&e3.first_disposal,&first) &&
        stream_dispose_equal(&e3.last_disposal,&third) &&
        stream_dispose_equal(&e4.first_disposal,&first) &&
        stream_dispose_equal(&e4.last_disposal,&third) &&
        e4.disposal_attempts==e3.disposal_attempts)&&ok;
    int finalized=mbgs_finalize(&o);ok=(!finalized && !o && total_calls()==calls)&&ok;
    struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    for(unsigned i=0;i<2;i++){
        stream_reset();o=owned(&p);stream_close_result=i?1:-1;stream_close_error=i?EBUSY:EINTR;
        struct mbgs_dispose_report report={0};int rc=mbgs_dispose(&o,&report);
        struct mbgs_evidence e=stream_evidence(o);calls=total_calls();
        struct mbgs_dispose_report repeated={0};int again=mbgs_dispose(&o,&repeated);
        struct mbgs_evidence after=stream_evidence(o);
        bool one=rc==MBGS_UNCERTAIN && !e.graph_present && !e.descriptor_held &&
            e.stream_fd==-1 && e.stream_close.attempted && e.stream_close.returned &&
            e.stream_close.raw.result==stream_close_result && e.stream_close.raw.error==stream_close_error &&
            e.uncertainty_seen && stream_close_count==1 && closes==2 && !stream_live &&
            again==MBGS_UNCERTAIN && total_calls()==calls &&
            stream_dispose_equal(&e.first_disposal,&after.first_disposal) &&
            stream_dispose_equal(&e.last_disposal,&after.last_disposal);
        ok=one&&ok;stream_cleanup(&o,&none);
    }
    ck(ok,"stream_uncertain_graph_and_transport_closes_require_explicit_finalization_without_retry");
}
static void partial_dispose_case(void)
{
    bool ok=true;
    for(unsigned i=0;i<3;i++){
        stream_reset();struct mbog_plan p;struct mbgs_owner *o=i?owned(&p):new_owner();
        if(i==2)assert(mbgs_step(o)==MBGS_EOF);
        struct mbgs_evidence before=stream_evidence(o);
        bool one=!stream_close_count && !closes && stream_live &&
            (i?before.graph_present && before.descriptor_held:!before.graph_present && !before.descriptor_held);
        struct mbgs_dispose_report report={0},again={0};int rc=mbgs_dispose(&o,&report);
        bool slot_consumed=!o;if(report.owner_released)o=NULL;
        one=one && !rc && slot_consumed && !o && report.owner_released && report.completed &&
            report.graph_attempted==!!i && report.stream_close.attempted==!!i &&
            closes==(i?2u:0u) && stream_close_count==(i?1u:0u);
        unsigned calls=total_calls();int repeat=mbgs_dispose(&o,&again);
        one=one && !repeat && again.completed && !again.attempt_number &&
            !again.graph_attempted && !again.stream_close.attempted && total_calls()==calls;
        ok=one&&ok;struct mbog_graph *none=NULL;stream_cleanup(&o,&none);
    }
    ck(ok,"stream_empty_and_partial_owners_only_release_resources_on_explicit_disposal");
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    struct {const char *name;void (*run)(void);} cases[]={
        {"retain",retain_case},{"fragments",fragments_case},{"header-refusal",header_refusal_case},
        {"read-eof",read_eof_case},{"read-errors",read_errors_case},{"read-abnormal",read_abnormal_case},
        {"write-errors",write_errors_case},{"write-abnormal",write_abnormal_case},
        {"dispatch-refusal",dispatch_refusal_case},{"unknown-response",unknown_response_case},
        {"reentry",reentry_case},{"dispose-order",dispose_order_case},
        {"close-uncertain",close_uncertain_case},{"partial-dispose",partial_dispose_case}
    };
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++)if(!strcmp(argv[1],cases[i].name)){
        cases[i].run();printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
    }
    return 2;
}
