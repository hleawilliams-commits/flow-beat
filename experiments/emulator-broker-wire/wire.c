/* SPDX-License-Identifier: GPL-2.0-only */
#include "wire.h"
#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "wire bytes require eight bits");
_Static_assert(INT_MAX == INT32_MAX && INT_MIN == INT32_MIN, "broker errors require int32");
static const uint8_t magic[8] = {'A','Z','M','B','R','P','C','1'};
static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void put64(uint8_t *p, uint64_t v)
{
    put32(p,(uint32_t)(v>>32)); put32(p+4,(uint32_t)v);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p)<<32 | get32(p+4);
}
/* Both casts have a representable signed operand, including INT32_MIN. */
static int32_t gets32(const uint8_t *p)
{
    uint32_t v=get32(p);
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX-v);
}
static size_t known_length(uint32_t kind)
{
    switch(kind) {
    case MBW_ADOPT_REQUEST: return MBW_ADOPT_REQUEST_SIZE;
    case MBW_FRAME_REQUEST: return MBW_FRAME_REQUEST_SIZE;
    case MBW_ADOPT_RESPONSE: return MBW_ADOPT_RESPONSE_SIZE;
    case MBW_FRAME_RESPONSE: return MBW_FRAME_RESPONSE_SIZE;
    default: return 0;
    }
}
static int header_valid(const struct mbw_header *h)
{
    unsigned nonzero=0;
    if (!h || !known_length(h->kind) || h->length != known_length(h->kind) ||
        !h->exchange || h->exchange == UINT64_MAX) return 0;
    for (size_t i=0;i<sizeof(h->session);i++) nonzero |= h->session[i];
    return nonzero != 0;
}
static void put_header(uint8_t *p, const struct mbw_header *h)
{
    memcpy(p,magic,8); put32(p+8,1); put32(p+12,h->kind);
    put32(p+16,h->length); put32(p+20,0); memcpy(p+24,h->session,16);
    put64(p+40,h->exchange);
}
int mbw_decode_header(const uint8_t *p, size_t n, struct mbw_header *out)
{
    struct mbw_header h={0};
    if (out) memset(out,0,sizeof(*out));
    if (!p || !out || n != MBW_HEADER_SIZE || memcmp(p,magic,8) ||
        get32(p+8) != 1 || get32(p+20)) return MBW_INVALID;
    h.kind=get32(p+12); h.length=get32(p+16);
    memcpy(h.session,p+24,16); h.exchange=get64(p+40);
    if (!header_valid(&h)) return MBW_INVALID;
    *out=h; return MBW_OK;
}
static int decode_record(const uint8_t *p, size_t n, uint32_t kind, struct mbw_header *h)
{
    if (!p || n != known_length(kind) ||
        mbw_decode_header(p,MBW_HEADER_SIZE,h) != MBW_OK || h->kind != kind)
        return MBW_INVALID;
    return MBW_OK;
}
static int encode_ready(const struct mbw_header *h, uint32_t kind, const void *value,
                        uint8_t *out, size_t cap, size_t *written)
{
    if (written) *written=0;
    if (!written || !out || !value || !header_valid(h) || h->kind != kind)
        return MBW_INVALID;
    if (cap < known_length(kind)) return MBW_CAPACITY;
    return MBW_OK;
}
static void put_state(uint8_t *p, const struct mb_state *s)
{
    put64(p,s->instance[0]); put64(p+8,s->instance[1]); put64(p+16,s->run_id);
    put64(p+24,s->epoch); put64(p+32,s->sequence); put32(p+40,s->flags);
    put32(p+44,(uint32_t)s->crc_len); memcpy(p+48,s->frame,MB_FRAME_BYTES);
}
static void get_state(const uint8_t *p, struct mb_state *s)
{
    s->instance[0]=get64(p); s->instance[1]=get64(p+8); s->run_id=get64(p+16);
    s->epoch=get64(p+24); s->sequence=get64(p+32); s->flags=get32(p+40);
    s->crc_len=gets32(p+44); memcpy(s->frame,p+48,MB_FRAME_BYTES);
}
static void put_grant(uint8_t *p, const struct mb_grant *g)
{
    put64(p,g->instance[0]); put64(p+8,g->instance[1]); put64(p+16,g->run_id);
    put64(p+24,g->epoch); put64(p+32,g->producer_id);
}
static void get_grant(const uint8_t *p, struct mb_grant *g)
{
    g->instance[0]=get64(p); g->instance[1]=get64(p+8); g->run_id=get64(p+16);
    g->epoch=get64(p+24); g->producer_id=get64(p+32);
}
static void put_request(uint8_t *p, const struct mb_request *r)
{
    put_grant(p,&r->grant); put64(p+40,r->request_id); memcpy(p+48,r->frame,MB_FRAME_BYTES);
}
static void get_request(const uint8_t *p, struct mb_request *r)
{
    get_grant(p,&r->grant); r->request_id=get64(p+40); memcpy(r->frame,p+48,MB_FRAME_BYTES);
}
static void put_command(uint8_t *p, const struct mb_command *c)
{
    put64(p,c->instance[0]); put64(p+8,c->instance[1]); put64(p+16,c->run_id);
    put64(p+24,c->epoch); put64(p+32,c->sequence); put32(p+40,c->injected);
    memcpy(p+44,c->frame,MB_FRAME_BYTES);
}
static void get_command(const uint8_t *p, struct mb_command *c)
{
    c->instance[0]=get64(p); c->instance[1]=get64(p+8); c->run_id=get64(p+16);
    c->epoch=get64(p+24); c->sequence=get64(p+32); c->injected=get32(p+40);
    memcpy(c->frame,p+44,MB_FRAME_BYTES);
}
int mbw_encode_adopt_request(const struct mbw_header *h, const struct mbw_adopt_request *v,
                             uint8_t *out, size_t cap, size_t *written)
{
    int rc=encode_ready(h,MBW_ADOPT_REQUEST,v,out,cap,written); if(rc) return rc;
    put_header(out,h); put_state(out+48,&v->state); put64(out+248,v->producer_id);
    *written=MBW_ADOPT_REQUEST_SIZE; return MBW_OK;
}
int mbw_decode_adopt_request(const uint8_t *p, size_t n, struct mbw_header *h,
                             struct mbw_adopt_request *out)
{
    struct mbw_header parsed={0}; struct mbw_adopt_request v={0};
    if(h) memset(h,0,sizeof(*h)); if(out) memset(out,0,sizeof(*out));
    if(!h || !out || decode_record(p,n,MBW_ADOPT_REQUEST,&parsed)) return MBW_INVALID;
    get_state(p+48,&v.state); v.producer_id=get64(p+248); *h=parsed; *out=v; return MBW_OK;
}
int mbw_encode_frame_request(const struct mbw_header *h, const struct mb_request *v,
                             uint8_t *out, size_t cap, size_t *written)
{
    int rc=encode_ready(h,MBW_FRAME_REQUEST,v,out,cap,written); if(rc) return rc;
    put_header(out,h); put_request(out+48,v); *written=MBW_FRAME_REQUEST_SIZE; return MBW_OK;
}
int mbw_decode_frame_request(const uint8_t *p, size_t n, struct mbw_header *h, struct mb_request *out)
{
    struct mbw_header parsed={0}; struct mb_request v={0};
    if(h) memset(h,0,sizeof(*h)); if(out) memset(out,0,sizeof(*out));
    if(!h || !out || decode_record(p,n,MBW_FRAME_REQUEST,&parsed)) return MBW_INVALID;
    get_request(p+48,&v); *h=parsed; *out=v; return MBW_OK;
}
int mbw_encode_adopt_response(const struct mbw_header *h, const struct mbw_adopt_response *v,
                              uint8_t *out, size_t cap, size_t *written)
{
    int rc=encode_ready(h,MBW_ADOPT_RESPONSE,v,out,cap,written); if(rc) return rc;
    if(v->provenance>1) return MBW_INVALID;
    put_header(out,h); put32(out+48,(uint32_t)v->result); put32(out+52,v->provenance);
    put_state(out+56,&v->state); put_grant(out+256,&v->grant);
    *written=MBW_ADOPT_RESPONSE_SIZE; return MBW_OK;
}
int mbw_decode_adopt_response(const uint8_t *p, size_t n, struct mbw_header *h,
                              struct mbw_adopt_response *out)
{
    struct mbw_header parsed={0}; struct mbw_adopt_response v={0};
    if(h) memset(h,0,sizeof(*h)); if(out) memset(out,0,sizeof(*out));
    if(!h || !out || decode_record(p,n,MBW_ADOPT_RESPONSE,&parsed) || get32(p+52)>1)
        return MBW_INVALID;
    v.result=gets32(p+48); v.provenance=get32(p+52);
    get_state(p+56,&v.state); get_grant(p+256,&v.grant);
    *h=parsed; *out=v; return MBW_OK;
}
int mbw_encode_frame_response(const struct mbw_header *h, const struct mbw_frame_response *v,
                              uint8_t *out, size_t cap, size_t *written)
{
    int rc=encode_ready(h,MBW_FRAME_RESPONSE,v,out,cap,written); if(rc) return rc;
    if(v->provenance>1 || (unsigned)v->receipt.outcome>MB_OUTCOME_UNKNOWN ||
        (unsigned)v->receipt.operation>MB_OP_RESTORE) return MBW_INVALID;
    put_header(out,h); put32(out+48,(uint32_t)v->result); put32(out+52,v->provenance);
    put32(out+56,(uint32_t)v->receipt.outcome); put32(out+60,(uint32_t)v->receipt.operation);
    put32(out+64,(uint32_t)v->receipt.backend_error); put32(out+68,0);
    put_request(out+72,&v->receipt.request); put_command(out+272,&v->receipt.command);
    put_state(out+468,&v->receipt.expected_after); *written=MBW_FRAME_RESPONSE_SIZE; return MBW_OK;
}
int mbw_decode_frame_response(const uint8_t *p, size_t n, struct mbw_header *h,
                              struct mbw_frame_response *out)
{
    struct mbw_header parsed={0}; struct mbw_frame_response v={0};
    if(h) memset(h,0,sizeof(*h)); if(out) memset(out,0,sizeof(*out));
    if(!h || !out || decode_record(p,n,MBW_FRAME_RESPONSE,&parsed) || get32(p+52)>1 ||
        get32(p+56)>MB_OUTCOME_UNKNOWN || get32(p+60)>MB_OP_RESTORE || get32(p+68))
        return MBW_INVALID;
    v.result=gets32(p+48); v.provenance=get32(p+52);
    v.receipt.outcome=(enum mb_outcome)get32(p+56);
    v.receipt.operation=(enum mb_operation)get32(p+60);
    v.receipt.backend_error=gets32(p+64); get_request(p+72,&v.receipt.request);
    get_command(p+272,&v.receipt.command); get_state(p+468,&v.receipt.expected_after);
    *h=parsed; *out=v; return MBW_OK;
}
