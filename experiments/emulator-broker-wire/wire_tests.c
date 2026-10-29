/* SPDX-License-Identifier: GPL-2.0-only */
/* New seam tests only. Actual frozen broker is a separate translation unit.
 * MBW_TEST injects faults AFTER its real mb_submit return; it does not pretend
 * to induce an operating-system mutex failure or establish native receipts. */
#include "guest_dispatch.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef MBW_TEST
#error "unit binary requires -DMBW_TEST; production and fixture omit it"
#endif
static unsigned checks, failures;
#define CHECK(value,label) do { checks++; if(!(value)) { failures++; fprintf(stderr,"FAIL %s\n",label); } } while(0)
static const uint64_t producer=UINT64_C(0x4142434445464748);
static const uint8_t label[16]={0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60};
static struct mb_state seed(void)
{
    struct mb_state s={0}; s.instance[0]=UINT64_C(0x0102030405060708);
    s.instance[1]=UINT64_C(0x1112131415161718); s.run_id=UINT64_C(0x2122232425262728);
    s.epoch=UINT64_C(0x3132333435363738); s.sequence=7; s.flags=31; s.crc_len=96;
    for(size_t i=0;i<MB_FRAME_BYTES;i++) s.frame[i]=(uint8_t)(i*3+7);
    return s;
}
static struct mb_grant grant_for(const struct mb_state *s)
{
    struct mb_grant g={{s->instance[0],s->instance[1]},s->run_id,s->epoch,producer}; return g;
}
static int same_state(const struct mb_state *a, const struct mb_state *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->sequence==b->sequence &&
        a->flags==b->flags && a->crc_len==b->crc_len && !memcmp(a->frame,b->frame,152);
}
static int same_grant(const struct mb_grant *a, const struct mb_grant *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->producer_id==b->producer_id;
}
static int same_request(const struct mb_request *a, const struct mb_request *b)
{
    return same_grant(&a->grant,&b->grant) && a->request_id==b->request_id && !memcmp(a->frame,b->frame,152);
}
static int same_command(const struct mb_command *a, const struct mb_command *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->sequence==b->sequence &&
        a->injected==b->injected && !memcmp(a->frame,b->frame,152);
}
/* Independent golden construction uses byte loops, no production codec helper. */
static void be(uint8_t *p, uint64_t v, size_t n)
{
    for(size_t i=0;i<n;i++) p[i]=(uint8_t)(v >> ((n-1-i)*8));
}
static void golden_header(uint8_t *p, uint32_t kind, size_t n, uint64_t exchange)
{
    memset(p,0,n); memcpy(p,"AZMBRPC1",8); be(p+8,1,4); be(p+12,kind,4);
    be(p+16,n,4); memcpy(p+24,label,16); be(p+40,exchange,8);
}
static void golden_state(uint8_t *p, const struct mb_state *s)
{
    be(p,s->instance[0],8); be(p+8,s->instance[1],8); be(p+16,s->run_id,8);
    be(p+24,s->epoch,8); be(p+32,s->sequence,8); be(p+40,s->flags,4);
    be(p+44,(uint32_t)s->crc_len,4); memcpy(p+48,s->frame,152);
}
static void golden_grant(uint8_t *p, const struct mb_grant *g)
{
    be(p,g->instance[0],8); be(p+8,g->instance[1],8); be(p+16,g->run_id,8);
    be(p+24,g->epoch,8); be(p+32,g->producer_id,8);
}
static void golden_request(uint8_t *p, const struct mb_request *r)
{
    golden_grant(p,&r->grant); be(p+40,r->request_id,8); memcpy(p+48,r->frame,152);
}
static void golden_command(uint8_t *p, const struct mb_command *c)
{
    be(p,c->instance[0],8); be(p+8,c->instance[1],8); be(p+16,c->run_id,8);
    be(p+24,c->epoch,8); be(p+32,c->sequence,8); be(p+40,c->injected,4);
    memcpy(p+44,c->frame,152);
}
static struct mbw_header header(uint32_t kind, size_t length, uint64_t exchange)
{
    struct mbw_header h={0}; h.kind=kind; h.length=(uint32_t)length;
    memcpy(h.session,label,16); h.exchange=exchange; return h;
}
static struct mb_request request_for(const struct mb_state *s, uint64_t id)
{
    struct mb_request r={0}; r.grant=grant_for(s); r.request_id=id;
    for(size_t i=0;i<152;i++) r.frame[i]=(uint8_t)(i*7+19);
    return r;
}
static struct mb_receipt positive_for(const struct mb_state *s, const struct mb_request *r)
{
    struct mb_receipt v={0}; v.outcome=MB_OUTCOME_COMMITTED; v.operation=MB_OP_FRAME; v.request=*r;
    v.command.instance[0]=s->instance[0]; v.command.instance[1]=s->instance[1];
    v.command.run_id=s->run_id; v.command.epoch=s->epoch; v.command.sequence=s->sequence+1;
    v.command.injected=1; memcpy(v.command.frame,r->frame,152);
    v.expected_after=*s; v.expected_after.sequence++; memcpy(v.expected_after.frame,r->frame,152); return v;
}
struct fake { struct mb_state actual; unsigned snapshots, submits; enum mb_outcome outcome; int error, snapshot_error; };
struct fixture { struct fake fake; struct mb_broker *broker; struct mbw_dispatch *dispatch; };
static int snapshot(void *p, struct mb_state *out)
{
    struct fake *f=p; f->snapshots++; *out=f->actual; return f->snapshot_error;
}
static struct mb_backend_reply submit(void *p, enum mb_operation op, const struct mb_command *c)
{
    struct fake *f=p; f->submits++;
    if(op != MB_OP_FRAME) return (struct mb_backend_reply){MB_OUTCOME_UNKNOWN,22};
    if(f->outcome==MB_OUTCOME_COMMITTED && !f->error) {
        f->actual.sequence=c->sequence; memcpy(f->actual.frame,c->frame,152);
    }
    return (struct mb_backend_reply){f->outcome,f->error};
}
static int shim_api_error, shim_fault=-1;
static struct mbw_dispatch *observe_dispatch;
static struct mbw_diagnostic before_submit;
static void alter_receipt(struct mb_receipt *r, int at)
{
    switch(at) {
    case 0: r->request.grant.instance[0]^=1; return;
    case 1: r->request.grant.instance[1]^=1; return;
    case 2: r->request.grant.run_id^=1; return;
    case 3: r->request.grant.epoch^=1; return;
    case 4: r->request.grant.producer_id^=1; return;
    case 5: r->request.request_id^=1; return;
    case 158: r->command.instance[0]^=1; return;
    case 159: r->command.instance[1]^=1; return;
    case 160: r->command.run_id^=1; return;
    case 161: r->command.epoch^=1; return;
    case 162: r->command.sequence^=1; return;
    case 163: r->command.injected=0; return;
    case 316: r->expected_after.instance[0]^=1; return;
    case 317: r->expected_after.instance[1]^=1; return;
    case 318: r->expected_after.run_id^=1; return;
    case 319: r->expected_after.epoch^=1; return;
    case 320: r->expected_after.sequence^=1; return;
    case 321: r->expected_after.flags^=8; return;
    case 322: r->expected_after.crc_len^=1; return;
    case 475: r->operation=MB_OP_ROTATE; return;
    case 476: r->outcome=MB_OUTCOME_NONE; return;
    case 477: r->backend_error=5; return;
    default: break;
    }
    if(at>=6 && at<158) r->request.frame[at-6]^=1;
    if(at>=164 && at<316) r->command.frame[at-164]^=1;
    if(at>=323 && at<475) r->expected_after.frame[at-323]^=1;
}
int mbw_test_submit(struct mb_broker *broker, const struct mb_request *request, struct mb_receipt *out)
{
    /* Test seam observes same-thread local journal before actual broker call;
     * no production callback or concurrent caller is granted reentry. */
    if(observe_dispatch) (void)mbw_dispatch_inspect(observe_dispatch,&before_submit);
    int rc=mb_submit(broker,request,out);
    if(shim_fault>=0) alter_receipt(out,shim_fault);
    return shim_api_error ? MB_ERR_MUTEX : rc;
}
static int make(struct fixture *f, struct mb_state s)
{
    memset(f,0,sizeof(*f)); f->fake.actual=s; f->fake.outcome=MB_OUTCOME_COMMITTED;
    struct mb_backend b={&f->fake,snapshot,submit};
    int rc=mb_create(&b,&s,&f->broker); CHECK(rc==MB_OK,"fixture-broker-create");
    if(rc != MB_OK) return 0;
    rc=mbw_dispatch_create(f->broker,label,&s,producer,&f->dispatch);
    CHECK(rc==MBWD_OK,"fixture-dispatch-create");
    if(rc != MBWD_OK) { mb_destroy(f->broker); f->broker=NULL; return 0; }
    return 1;
}
static void drop(struct fixture *f)
{
    mbw_dispatch_destroy(f->dispatch); mb_destroy(f->broker); memset(f,0,sizeof(*f));
}
static size_t adopt_bytes(const struct mb_state *s, uint8_t *out)
{
    golden_header(out,MBW_ADOPT_REQUEST,256,1); golden_state(out+48,s); be(out+248,producer,8); return 256;
}
static size_t frame_bytes(const struct mb_state *s, uint64_t exchange, uint64_t id, uint8_t *out)
{
    struct mb_request r=request_for(s,id); golden_header(out,MBW_FRAME_REQUEST,248,exchange);
    golden_request(out+48,&r); return 248;
}
static int adopt(struct fixture *f)
{
    uint8_t input[256],out[668]; size_t n=0;
    adopt_bytes(&f->fake.actual,input);
    int rc=mbw_dispatch_exchange(f->dispatch,input,256,out,sizeof(out),&n);
    CHECK(rc==MBWD_OK && n==296,"fixture-adopt"); return rc==MBWD_OK && n==296;
}
static void test_codec_golden(void)
{
    uint8_t actual[668], expected[668]; size_t n=0; struct mbw_header h,decoded_h;
    struct mb_state s=seed(); struct mbw_adopt_request a={s,producer},ad={0};
    h=header(MBW_ADOPT_REQUEST,256,1); adopt_bytes(&s,expected);
    CHECK(mbw_encode_adopt_request(&h,&a,actual,sizeof(actual),&n)==0 && n==256,"adopt-encode-size");
    CHECK(!memcmp(actual,expected,256),"adopt-independent-bytes");
    CHECK(mbw_decode_adopt_request(expected,256,&decoded_h,&ad)==0 && same_state(&ad.state,&s) &&
          ad.producer_id==producer && decoded_h.exchange==1,"adopt-independent-decode");
    struct mb_request request=request_for(&s,1),rd={0};
    h=header(MBW_FRAME_REQUEST,248,2); frame_bytes(&s,2,1,expected);
    CHECK(mbw_encode_frame_request(&h,&request,actual,sizeof(actual),&n)==0 && n==248,"frame-encode-size");
    CHECK(!memcmp(actual,expected,248),"frame-independent-bytes");
    CHECK(mbw_decode_frame_request(expected,248,&decoded_h,&rd)==0 && same_request(&request,&rd),"frame-independent-decode");
    struct mbw_adopt_response ar={0},ard={0}; ar.result=INT32_MIN; ar.provenance=1; ar.state=s;
    ar.state.crc_len=-97; ar.grant=grant_for(&s); h=header(MBW_ADOPT_RESPONSE,296,1);
    golden_header(expected,MBW_ADOPT_RESPONSE,296,1); be(expected+48,UINT32_C(0x80000000),4);
    be(expected+52,1,4); golden_state(expected+56,&ar.state); golden_grant(expected+256,&ar.grant);
    CHECK(mbw_encode_adopt_response(&h,&ar,actual,sizeof(actual),&n)==0 && n==296,"adopt-response-encode");
    CHECK(!memcmp(actual,expected,296),"adopt-response-independent-bytes");
    CHECK(mbw_decode_adopt_response(expected,296,&decoded_h,&ard)==0 && ard.result==INT32_MIN &&
          ard.provenance==1 && same_state(&ar.state,&ard.state) && same_grant(&ar.grant,&ard.grant),"adopt-response-independent-decode");
    struct mbw_frame_response fr={0},fd={0}; fr.result=MB_ERR_MUTEX; fr.provenance=1;
    fr.receipt=positive_for(&s,&request); fr.receipt.backend_error=INT32_MIN;
    h=header(MBW_FRAME_RESPONSE,668,2); golden_header(expected,MBW_FRAME_RESPONSE,668,2);
    be(expected+48,UINT32_C(0xfffffffd),4); be(expected+52,1,4); be(expected+56,1,4);
    be(expected+60,1,4); be(expected+64,UINT32_C(0x80000000),4);
    golden_request(expected+72,&request); golden_command(expected+272,&fr.receipt.command);
    golden_state(expected+468,&fr.receipt.expected_after);
    CHECK(mbw_encode_frame_response(&h,&fr,actual,sizeof(actual),&n)==0 && n==668,"frame-response-encode");
    CHECK(!memcmp(actual,expected,668),"frame-response-independent-bytes");
    CHECK(mbw_decode_frame_response(expected,668,&decoded_h,&fd)==0 && fd.result==-3 && fd.provenance==1 &&
          fd.receipt.backend_error==INT32_MIN && fd.receipt.outcome==MB_OUTCOME_COMMITTED &&
          fd.receipt.operation==MB_OP_FRAME && same_request(&fd.receipt.request,&request) &&
          same_command(&fd.receipt.command,&fr.receipt.command) &&
          same_state(&fd.receipt.expected_after,&fr.receipt.expected_after),"frame-response-independent-decode");
    CHECK(fd.receipt.restore.restore_id==0,"restore-absent-zero");
}
static void test_codec_invalid(void)
{
    uint8_t bytes[669],copy[669],out[668]; struct mb_state s=seed();
    struct mbw_header h={0}; struct mbw_adopt_request a={0}; adopt_bytes(&s,bytes);
    for(size_t n=0;n<256;n++) CHECK(mbw_decode_adopt_request(bytes,n,&h,&a)==MBW_INVALID,"adopt-truncated-refused");
    CHECK(mbw_decode_adopt_request(bytes,257,&h,&a)==MBW_INVALID,"adopt-trailing-refused");
    const size_t positions[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23};
    for(size_t i=0;i<sizeof(positions)/sizeof(positions[0]);i++) {
        memcpy(copy,bytes,256); copy[positions[i]]^=0x40;
        CHECK(mbw_decode_adopt_request(copy,256,&h,&a)==MBW_INVALID,"invalid-header-refused");
    }
    memcpy(copy,bytes,256); memset(copy+24,0,16);
    CHECK(mbw_decode_adopt_request(copy,256,&h,&a)==MBW_INVALID,"zero-session-refused");
    memcpy(copy,bytes,256); memset(copy+40,0,8);
    CHECK(mbw_decode_adopt_request(copy,256,&h,&a)==MBW_INVALID,"zero-exchange-refused");
    memset(copy+40,255,8);
    CHECK(mbw_decode_adopt_request(copy,256,&h,&a)==MBW_INVALID,"max-exchange-refused");
    a.state=s; a.producer_id=producer; h=header(MBW_ADOPT_REQUEST,256,1); size_t written=99;
    memset(out,0x6d,sizeof(out));
    CHECK(mbw_encode_adopt_request(&h,&a,out,255,&written)==MBW_CAPACITY && written==0 && out[0]==0x6d,"encode-capacity-no-write");
    h.kind=99; CHECK(mbw_encode_adopt_request(&h,&a,out,sizeof(out),&written)==MBW_INVALID && written==0,"encode-kind-refused");
    struct mb_request request=request_for(&s,1); struct mbw_frame_response fr={0},decoded={0};
    fr.receipt=positive_for(&s,&request); h=header(MBW_FRAME_RESPONSE,668,2);
    CHECK(mbw_encode_frame_response(&h,&fr,bytes,sizeof(bytes),&written)==0,"invalid-response-setup");
    const size_t response_positions[]={52,55,56,59,60,63,68,69,70,71};
    for(size_t i=0;i<sizeof(response_positions)/sizeof(response_positions[0]);i++) {
        memcpy(copy,bytes,668); copy[response_positions[i]]|=0x80;
        CHECK(mbw_decode_frame_response(copy,668,&h,&decoded)==MBW_INVALID,"response-enum-reserved-refused");
    }
    for(size_t n=0;n<668;n++) CHECK(mbw_decode_frame_response(bytes,n,&h,&decoded)==MBW_INVALID,"response-truncated-refused");
    CHECK(mbw_decode_frame_response(bytes,669,&h,&decoded)==MBW_INVALID,"response-trailing-refused");
}
static void test_construct(void)
{
    struct fixture f; struct mb_state s=seed(); if(!make(&f,s)) return;
    CHECK(f.fake.snapshots==1 && f.fake.submits==0,"construct-local-inspection-only");
    struct mbw_dispatch *d=NULL; struct mb_state changed=s; changed.frame[151]^=1;
    CHECK(mbw_dispatch_create(f.broker,label,&changed,producer,&d)==MBWD_BROKER && !d,"constructor-complete-correspondence");
    changed=s; changed.sequence=UINT64_MAX;
    CHECK(mbw_dispatch_create(f.broker,label,&changed,producer,&d)==MBWD_INVALID && !d,"constructor-max-sequence");
    uint8_t zero[16]={0}; CHECK(mbw_dispatch_create(f.broker,zero,&s,producer,&d)==MBWD_INVALID && !d,"constructor-zero-label");
    CHECK(mbw_dispatch_create(f.broker,label,&s,0,&d)==MBWD_INVALID && !d,"constructor-zero-producer");
    changed=s; changed.flags=23; CHECK(mbw_dispatch_create(f.broker,label,&changed,producer,&d)==MBWD_INVALID,"constructor-injected-required");
    changed=s; changed.instance[0]=changed.instance[1]=0;
    CHECK(mbw_dispatch_create(f.broker,label,&changed,producer,&d)==MBWD_INVALID,"constructor-instance-required");
    struct mb_grant g={0}; CHECK(mb_adopt(f.broker,s.frame,producer,&g)==MB_OK,"constructor-alias-adopt-setup");
    CHECK(mbw_dispatch_create(f.broker,label,&s,producer,&d)==MBWD_BROKER && !d,"constructor-already-adopted-refused");
    mbw_dispatch_destroy(f.dispatch); f.dispatch=NULL; struct mb_status status={0};
    CHECK(mb_inspect(f.broker,&status)==MB_OK && status.admission_open,"destroy-does-not-quiesce-borrowed-broker");
    drop(&f);
}
static void test_adopt(void)
{
    struct mb_state s=seed();
    for(size_t at=48;at<256;at++) {
        struct fixture f; if(!make(&f,s)) return; uint8_t input[256],out[668]; size_t n=9;
        adopt_bytes(&s,input); input[at]^=1;
        CHECK(mbw_dispatch_exchange(f.dispatch,input,256,out,sizeof(out),&n)==MBWD_CONFLICT && !n,"adopt-all-seed-bytes-bound");
        CHECK(f.fake.snapshots==1 && f.fake.submits==0,"adopt-mismatch-before-broker");
        struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
        CHECK(d.phase==MBWD_TERMINAL && d.request_length==0,"adopt-mismatch-preserves-unattempted-journal"); drop(&f);
    }
    struct fixture f; if(!make(&f,s)) return;
    f.fake.actual.frame[151]^=1; uint8_t input[256],out[668]; size_t n=0; adopt_bytes(&s,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,256,out,sizeof(out),&n)==0 && n==296,"adopt-broker-failure-response");
    struct mbw_header h={0}; struct mbw_adopt_response r={0};
    CHECK(mbw_decode_adopt_response(out,n,&h,&r)==0 && r.result==MB_ERR_STATE && r.grant.producer_id==0,"adopt-fresh-snapshot-failure-preserved");
    struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
    CHECK(d.phase==MBWD_TERMINAL && d.broker_result==MB_ERR_STATE && d.returned_valid,"adopt-failure-terminal-journal"); drop(&f);
}
static void test_journal(void)
{
    struct fixture f; struct mb_state s=seed(); if(!make(&f,s)) return;
    if(!adopt(&f)) { drop(&f); return; }
    uint8_t input[248],out[668],expected[668]; size_t n=0;
    frame_bytes(&s,2,1,input); observe_dispatch=f.dispatch; memset(&before_submit,0,sizeof(before_submit));
    int rc=mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n); observe_dispatch=NULL;
    CHECK(before_submit.phase==MBWD_FRAME_ARMED && !before_submit.returned_valid && !before_submit.response_length,
          "journal-armed-before-broker");
    CHECK(before_submit.last_exchange==2 && before_submit.request_length==248 && !memcmp(input,before_submit.request,248),
          "journal-full-request-before-broker");
    CHECK(rc==0 && n==668 && f.fake.submits==1,"one-actual-frame-call");
    struct mb_request request=request_for(&s,1); struct mb_receipt r=positive_for(&s,&request);
    golden_header(expected,MBW_FRAME_RESPONSE,668,2); be(expected+56,1,4); be(expected+60,1,4);
    golden_request(expected+72,&request); golden_command(expected+272,&r.command); golden_state(expected+468,&r.expected_after);
    CHECK(!memcmp(out,expected,668),"actual-broker-receipt-canonical");
    struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
    CHECK(d.returned_valid && d.broker_result==MB_OK && d.returned_receipt.outcome==MB_OUTCOME_COMMITTED &&
          same_request(&d.returned_receipt.request,&request),"journal-full-return-retained");
    CHECK(d.phase==MBWD_READY && d.next_request_id==2 && same_state(&d.expected,&r.expected_after),"positive-advances-once");
    CHECK(f.fake.snapshots==2,"submit-does-not-readopt-or-snapshot"); drop(&f);
}
static void test_cache(void)
{
    struct fixture f; struct mb_state s=seed(); if(!make(&f,s)) return;
    uint8_t input[256],out[668],first[668]; size_t n=0,first_n=0; adopt_bytes(&s,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,256,first,sizeof(first),&first_n)==0,"cache-adopt-initial");
    CHECK(mbw_dispatch_exchange(f.dispatch,input,256,out,sizeof(out),&n)==0 && n==first_n && out[55]==1,"cache-adopt-provenance");
    first[55]=1; CHECK(!memcmp(out,first,n) && f.fake.snapshots==2,"cache-adopt-no-redispatch");
    frame_bytes(&s,2,1,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,first,sizeof(first),&first_n)==0,"cache-frame-initial");
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==0 && n==668 && out[55]==1,"cache-frame-provenance");
    first[55]=1; CHECK(!memcmp(out,first,n) && f.fake.submits==1,"cache-frame-no-redispatch");
    input[247]^=1;
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==MBWD_CONFLICT && !n,"cache-conflict-closes");
    struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
    CHECK(d.phase==MBWD_TERMINAL && d.response[55]==0 && d.returned_receipt.request.frame[151]!=(uint8_t)input[247],"cache-conflict-preserves-original");
    input[247]^=1;
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==0 && n==668 && out[55]==1 &&
          f.fake.submits==1,"cache-retrieval-after-closure");
    frame_bytes(&s,3,2,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==MBWD_CLOSED && !n && f.fake.submits==1,"cache-closure-no-new-frame");
    mbw_dispatch_inspect(f.dispatch,&d); CHECK(d.first_refusal==MBWD_CONFLICT,"first-refusal-preserved"); drop(&f);
}
static void test_request_policy(void)
{
    struct mb_state s=seed();
    for(size_t at=48;at<96;at++) {
        struct fixture f; if(!make(&f,s)) return; if(!adopt(&f)) { drop(&f); return; }
        uint8_t input[248],out[668]; size_t n=9; frame_bytes(&s,2,1,input); input[at]^=1;
        CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==MBWD_CONFLICT && !n,"frame-all-grant-id-bytes-bound");
        CHECK(f.fake.submits==0,"frame-mismatch-before-broker"); drop(&f);
    }
    for(int mode=0;mode<9;mode++) {
        struct fixture f; if(!make(&f,s)) return; if(!adopt(&f)) { drop(&f); return; }
        uint8_t input[257],out[668]; size_t len=frame_bytes(&s,2,1,input),n=9,cap=sizeof(out);
        switch(mode) {
        case 0: input[24]^=1; break;
        case 1: be(input+40,3,8); break;
        case 2: be(input+40,UINT64_MAX,8); break;
        case 3: be(input+88,UINT64_MAX,8); break;
        case 4: len=adopt_bytes(&s,input); be(input+40,2,8); break;
        case 5: len--; break;
        case 6: input[len++]=0; break;
        case 7: cap=667; break;
        case 8: be(input+40,0,8); break;
        }
        CHECK(mbw_dispatch_exchange(f.dispatch,input,len,out,cap,&n)!=0 && !n && f.fake.submits==0,"invalid-next-request-before-broker");
        struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
        CHECK(d.phase==MBWD_TERMINAL && d.last_exchange==1 && d.response_length==296,"invalid-next-preserves-adopt-journal"); drop(&f);
    }
}
static void test_outcomes(void)
{
    struct mb_state s=seed();
    for(int kind=0;kind<2;kind++) {
        struct fixture f; if(!make(&f,s)) return; if(!adopt(&f)) { drop(&f); return; }
        f.fake.outcome=kind ? MB_OUTCOME_UNKNOWN : MB_OUTCOME_REJECTED; f.fake.error=5;
        uint8_t input[248],out[668]; size_t n=0; frame_bytes(&s,2,1,input);
        CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==0 && n==668,"noncommitted-response-returned");
        struct mbw_header h={0}; struct mbw_frame_response r={0};
        CHECK(mbw_decode_frame_response(out,n,&h,&r)==0 && r.result==MB_OK &&
              r.receipt.outcome==f.fake.outcome && r.receipt.backend_error==5,"noncommitted-exact-outcome");
        struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
        CHECK(d.phase==MBWD_TERMINAL && d.next_request_id==1 && same_state(&d.expected,&s),"noncommitted-no-positive-advance");
        CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==0 && out[55]==1 && f.fake.submits==1,"noncommitted-cached-no-call");
        frame_bytes(&s,3,2,input);
        CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==MBWD_CLOSED && !n && f.fake.submits==1,"noncommitted-next-blocked"); drop(&f);
    }
}
static void test_receipt_fields(void)
{
    struct mb_state s=seed();
    for(int fault=0;fault<478;fault++) {
        struct fixture f; if(!make(&f,s)) return; if(!adopt(&f)) { drop(&f); return; }
        uint8_t input[248],out[668]; size_t n=7; frame_bytes(&s,2,1,input);
        shim_fault=fault; int rc=mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n); shim_fault=-1;
        CHECK(rc==MBWD_RECEIPT && !n && f.fake.submits==1,"receipt-full-correspondence-refused");
        struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
        CHECK(d.returned_valid && d.broker_result==MB_OK && d.phase==MBWD_TERMINAL &&
              !d.response_length && same_state(&d.expected,&s) && d.next_request_id==1,"bad-return-retained-without-advance");
        CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==MBWD_CLOSED && !n && f.fake.submits==1,
              "bad-return-no-usable-cache-or-redispatch"); drop(&f);
    }
}
static void test_api_error(void)
{
    struct fixture f; struct mb_state s=seed(); if(!make(&f,s)) return; if(!adopt(&f)) { drop(&f); return; }
    uint8_t input[248],out[668]; size_t n=0; frame_bytes(&s,2,1,input);
    shim_api_error=1; int rc=mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n); shim_api_error=0;
    struct mbw_header h={0}; struct mbw_frame_response r={0};
    CHECK(rc==0 && n==668 && mbw_decode_frame_response(out,n,&h,&r)==0,"api-error-response-decodes");
    CHECK(r.result==MB_ERR_MUTEX && r.receipt.outcome==MB_OUTCOME_COMMITTED && r.receipt.command.sequence==8,
          "api-error-positive-receipt-distinct");
    struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
    CHECK(d.broker_result==MB_ERR_MUTEX && d.returned_receipt.outcome==MB_OUTCOME_COMMITTED &&
          d.phase==MBWD_TERMINAL && same_state(&d.expected,&s) && d.next_request_id==1,"api-error-no-acceptance");
    CHECK(f.fake.actual.sequence==8 && f.fake.submits==1,"api-error-may-follow-real-commit");
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==0 && out[55]==1 && f.fake.submits==1,"api-error-historical-cache");
    frame_bytes(&s,3,2,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==MBWD_CLOSED && !n && f.fake.submits==1,"api-error-blocks-later-write");
    drop(&f);
    /* Actual API refusal without the seam: an external alias closed admission. */
    if(!make(&f,s)) return; if(!adopt(&f)) { drop(&f); return; }
    CHECK(mb_quiesce(f.broker)==MB_OK,"api-alias-quiesce-setup"); frame_bytes(&s,2,1,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==0 &&
          mbw_decode_frame_response(out,n,&h,&r)==0 && r.result==MB_ERR_CLOSED &&
          r.receipt.outcome==MB_OUTCOME_NONE && f.fake.submits==0,"actual-broker-api-refusal-preserved"); drop(&f);
}
static void test_exhaustion(void)
{
    struct fixture f; struct mb_state s=seed(); s.sequence=UINT64_MAX-1;
    if(!make(&f,s)) return; if(!adopt(&f)) { drop(&f); return; }
    uint8_t input[248],out[668]; size_t n=0; frame_bytes(&s,2,1,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==0 && n==668 &&
          f.fake.actual.sequence==UINT64_MAX,"last-sequence-commit-exact");
    struct mbw_diagnostic d={0}; mbw_dispatch_inspect(f.dispatch,&d);
    CHECK(d.expected.sequence==UINT64_MAX && d.next_request_id==2,"last-sequence-retained-no-wrap");
    frame_bytes(&s,3,2,input);
    CHECK(mbw_dispatch_exchange(f.dispatch,input,248,out,sizeof(out),&n)==MBWD_CONFLICT && !n &&
          f.fake.submits==1,"sequence-exhaustion-before-broker"); drop(&f);
}
struct testcase { const char *name; void (*run)(void); };
static const struct testcase cases[]={
    {"codec-golden",test_codec_golden},{"codec-invalid",test_codec_invalid},
    {"construct",test_construct},{"adopt",test_adopt},{"journal",test_journal},
    {"cache",test_cache},{"request-policy",test_request_policy},{"outcomes",test_outcomes},
    {"receipt-fields",test_receipt_fields},{"api-error",test_api_error},{"exhaustion",test_exhaustion}
};
int main(int argc, char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--list")) {
        for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) puts(cases[i].name);
        return 0;
    }
    if(argc>2) { fprintf(stderr,"invalid test arguments\n"); return 2; }
    int found=0;
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        if(argc==1 || !strcmp(argv[1],"all") || !strcmp(argv[1],cases[i].name)) {
            found=1; cases[i].run();
        }
    }
    if(!found) { fprintf(stderr,"unknown test case\n"); return 2; }
    printf("checks=%u failures=%u\n",checks,failures); return failures ? 1 : 0;
}
