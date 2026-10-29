/* SPDX-License-Identifier: GPL-2.0-only */
#include "guest_dispatch.h"
#include <stdlib.h>
#include <string.h>

struct mbw_dispatch { struct mb_broker *broker; struct mbw_diagnostic journal; };
#ifdef MBW_TEST
/* Unit-only fault seam: implementation calls the actual frozen mb_submit before
 * altering its return for coverage. No callback indirection in production. */
extern int mbw_test_submit(struct mb_broker *, const struct mb_request *, struct mb_receipt *);
#define dispatch_submit mbw_test_submit
#else
#define dispatch_submit mb_submit
#endif
static int state_equal(const struct mb_state *a, const struct mb_state *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->sequence==b->sequence &&
        a->flags==b->flags && a->crc_len==b->crc_len && !memcmp(a->frame,b->frame,MB_FRAME_BYTES);
}
static int grant_equal(const struct mb_grant *a, const struct mb_grant *b)
{
    return a->instance[0]==b->instance[0] && a->instance[1]==b->instance[1] &&
        a->run_id==b->run_id && a->epoch==b->epoch && a->producer_id==b->producer_id;
}
static int request_equal(const struct mb_request *a, const struct mb_request *b)
{
    return grant_equal(&a->grant,&b->grant) && a->request_id==b->request_id &&
        !memcmp(a->frame,b->frame,MB_FRAME_BYTES);
}
static int seed_valid(const struct mb_state *s, uint64_t producer)
{
    return s && (s->instance[0] || s->instance[1]) && s->run_id && s->epoch && producer &&
        s->sequence != UINT64_MAX && s->flags==MB_REQUIRED_FLAGS && s->crc_len==96;
}
static struct mb_grant seed_grant(const struct mb_state *s, uint64_t producer)
{
    struct mb_grant g={{s->instance[0],s->instance[1]},s->run_id,s->epoch,producer}; return g;
}
static int close_dispatch(struct mbw_dispatch *d, int reason)
{
    d->journal.phase=MBWD_TERMINAL;
    if (!d->journal.first_refusal) d->journal.first_refusal=reason;
    return reason;
}
int mbw_dispatch_create(struct mb_broker *broker, const uint8_t session[16],
                        const struct mb_state *seed, uint64_t producer, struct mbw_dispatch **out)
{
    struct mb_status status={0}; unsigned nonzero=0;
    if(out) *out=NULL;
    if(!out || !broker || !session || !seed_valid(seed,producer)) return MBWD_INVALID;
    for(size_t i=0;i<16;i++) nonzero |= session[i];
    if(!nonzero) return MBWD_INVALID;
    int rc=mb_inspect(broker,&status);
    if(rc != MB_OK || !state_equal(seed,&status.expected) || status.unresolved ||
        status.admission_open || status.grant_issued || status.next_request_id != 1)
        return MBWD_BROKER;
    struct mbw_dispatch *d=calloc(1,sizeof(*d));
    if(!d) return MBWD_NOMEM;
    d->broker=broker; d->journal.phase=MBWD_RETAINED;
    memcpy(d->journal.session,session,16); d->journal.seed=*seed; d->journal.expected=*seed;
    d->journal.producer_id=producer; d->journal.next_request_id=1;
    d->journal.grant=seed_grant(seed,producer); *out=d; return MBWD_OK;
}
void mbw_dispatch_destroy(struct mbw_dispatch *d) { free(d); }
int mbw_dispatch_inspect(const struct mbw_dispatch *d, struct mbw_diagnostic *out)
{
    if(!out) return MBWD_INVALID;
    memset(out,0,sizeof(*out)); if(!d) return MBWD_INVALID;
    *out=d->journal; return MBWD_OK;
}
static void arm(struct mbw_dispatch *d, const uint8_t *bytes, size_t n,
                 uint64_t exchange, enum mbw_phase phase)
{
    struct mbw_diagnostic *j=&d->journal;
    memset(j->request,0,sizeof(j->request)); memcpy(j->request,bytes,n);
    j->request_length=n; j->last_exchange=exchange; j->response_length=0;
    memset(j->response,0,sizeof(j->response)); j->returned_valid=0; j->broker_result=0;
    memset(&j->returned_grant,0,sizeof(j->returned_grant));
    memset(&j->returned_receipt,0,sizeof(j->returned_receipt)); j->phase=phase;
}
static int frame_corresponds(const struct mbw_diagnostic *j, const struct mb_request *request,
                              const struct mb_receipt *receipt)
{
    struct mb_state after=j->expected;
    const struct mb_command *c=&receipt->command;
    after.sequence++; after.flags=MB_REQUIRED_FLAGS; memcpy(after.frame,request->frame,MB_FRAME_BYTES);
    return receipt->operation==MB_OP_FRAME && request_equal(request,&receipt->request) &&
        c->instance[0]==j->expected.instance[0] && c->instance[1]==j->expected.instance[1] &&
        c->run_id==j->expected.run_id && c->epoch==j->expected.epoch &&
        c->sequence==j->expected.sequence+1 && c->injected==1 &&
        !memcmp(c->frame,request->frame,MB_FRAME_BYTES) && state_equal(&after,&receipt->expected_after);
}
static int dispatch_adopt(struct mbw_dispatch *d, const uint8_t *bytes, size_t n,
                           const struct mbw_header *h)
{
    struct mbw_header parsed={0}, response_header=*h;
    struct mbw_adopt_request request={0}; struct mbw_adopt_response response={0};
    struct mbw_diagnostic *j=&d->journal;
    if(mbw_decode_adopt_request(bytes,n,&parsed,&request) ||
        !state_equal(&request.state,&j->seed) || request.producer_id != j->producer_id)
        return close_dispatch(d,MBWD_CONFLICT);
    arm(d,bytes,n,h->exchange,MBWD_ADOPT_ARMED);
    int rc=mb_adopt(d->broker,request.state.frame,request.producer_id,&response.grant);
    /* Publish exact returned evidence before validation or serialization. */
    j->broker_result=rc; j->returned_grant=response.grant; j->returned_valid=1;
    if(rc==MB_OK && !grant_equal(&response.grant,&j->grant))
        return close_dispatch(d,MBWD_RECEIPT);
    response.result=rc; response.state=j->seed; response_header.kind=MBW_ADOPT_RESPONSE;
    response_header.length=MBW_ADOPT_RESPONSE_SIZE;
    if(mbw_encode_adopt_response(&response_header,&response,j->response,sizeof(j->response),&j->response_length))
        return close_dispatch(d,MBWD_RECEIPT);
    if(rc==MB_OK) j->phase=MBWD_READY; else close_dispatch(d,MBWD_BROKER);
    return MBWD_OK;
}
static int dispatch_frame(struct mbw_dispatch *d, const uint8_t *bytes, size_t n,
                           const struct mbw_header *h)
{
    struct mbw_header parsed={0}, response_header=*h;
    struct mb_request request={0}; struct mbw_frame_response response={0};
    struct mbw_diagnostic *j=&d->journal;
    if(mbw_decode_frame_request(bytes,n,&parsed,&request) ||
        !grant_equal(&request.grant,&j->grant) || request.request_id != j->next_request_id ||
        !request.request_id || request.request_id==UINT64_MAX || j->expected.sequence==UINT64_MAX)
        return close_dispatch(d,MBWD_CONFLICT);
    arm(d,bytes,n,h->exchange,MBWD_FRAME_ARMED);
    int rc=dispatch_submit(d->broker,&request,&response.receipt);
    j->broker_result=rc; j->returned_receipt=response.receipt; j->returned_valid=1;
    /* Any API error is preserved as such, even with a fully positive receipt.
     * A caller must not reinterpret unlock failure as a definite noncommit. */
    if(rc==MB_OK) {
        int outcome_valid=(response.receipt.outcome==MB_OUTCOME_COMMITTED && response.receipt.backend_error==0) ||
            (response.receipt.outcome==MB_OUTCOME_REJECTED && response.receipt.backend_error>0) ||
            (response.receipt.outcome==MB_OUTCOME_UNKNOWN && response.receipt.backend_error>=0);
        if(!outcome_valid || !frame_corresponds(j,&request,&response.receipt))
            return close_dispatch(d,MBWD_RECEIPT);
    }
    response.result=rc; response_header.kind=MBW_FRAME_RESPONSE; response_header.length=MBW_FRAME_RESPONSE_SIZE;
    if(mbw_encode_frame_response(&response_header,&response,j->response,sizeof(j->response),&j->response_length))
        return close_dispatch(d,MBWD_RECEIPT);
    if(rc==MB_OK && response.receipt.outcome==MB_OUTCOME_COMMITTED) {
        j->expected=response.receipt.expected_after; j->next_request_id++; j->phase=MBWD_READY;
    } else close_dispatch(d,MBWD_BROKER);
    return MBWD_OK;
}
int mbw_dispatch_exchange(struct mbw_dispatch *d, const uint8_t *bytes, size_t n,
                          uint8_t *out, size_t capacity, size_t *written)
{
    struct mbw_header h={0};
    if(written) *written=0;
    if(!d) return MBWD_INVALID;
    if(!bytes || !out || !written || n<MBW_HEADER_SIZE ||
        mbw_decode_header(bytes,MBW_HEADER_SIZE,&h) || n != h.length ||
        (h.kind != MBW_ADOPT_REQUEST && h.kind != MBW_FRAME_REQUEST) ||
        memcmp(h.session,d->journal.session,16)) return close_dispatch(d,MBWD_INVALID);
    struct mbw_diagnostic *j=&d->journal;
    size_t response_size=h.kind==MBW_ADOPT_REQUEST ? MBW_ADOPT_RESPONSE_SIZE : MBW_FRAME_RESPONSE_SIZE;
    if(capacity < response_size) return close_dispatch(d,MBWD_CAPACITY);
    if(j->last_exchange && h.exchange==j->last_exchange) {
        if(n != j->request_length || memcmp(bytes,j->request,n)) return close_dispatch(d,MBWD_CONFLICT);
        if(!j->response_length) return close_dispatch(d,MBWD_CLOSED);
        memcpy(out,j->response,j->response_length);
        /* Stored canonical response remains provenance0; only copied retrieval is historical. */
        out[55]=1; *written=j->response_length; return MBWD_OK;
    }
    if(j->phase==MBWD_TERMINAL || j->phase==MBWD_ADOPT_ARMED || j->phase==MBWD_FRAME_ARMED)
        return close_dispatch(d,MBWD_CLOSED);
    if(!j->last_exchange) {
        if(h.exchange != 1 || h.kind != MBW_ADOPT_REQUEST || j->phase != MBWD_RETAINED)
            return close_dispatch(d,MBWD_CONFLICT);
    } else if(j->last_exchange >= UINT64_MAX-1 || h.exchange != j->last_exchange+1 ||
              h.kind != MBW_FRAME_REQUEST || j->phase != MBWD_READY) {
        return close_dispatch(d,MBWD_CONFLICT);
    }
    int rc=h.kind==MBW_ADOPT_REQUEST ? dispatch_adopt(d,bytes,n,&h) : dispatch_frame(d,bytes,n,&h);
    if(rc != MBWD_OK) return rc;
    memcpy(out,j->response,j->response_length); *written=j->response_length; return MBWD_OK;
}
