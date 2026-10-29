/* SPDX-License-Identifier: GPL-2.0-only */
/* Offline synthetic broker fixture. stdin/stdout pipes only; no endpoint opens.
 * Fault modes alter results only after the actual dispatcher/broker operation.
 * Normal exit destroys local objects after synchronous calls have returned;
 * synthetic state destruction is not evidence about a live adapter's disposal. */
#include "guest_dispatch.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct backend { struct mb_state state; enum mb_outcome outcome; };
static int snapshot(void *p, struct mb_state *out)
{
    *out=((struct backend *)p)->state; return 0;
}
static struct mb_backend_reply submit(void *p, enum mb_operation op, const struct mb_command *c)
{
    struct backend *b=p;
    if(op != MB_OP_FRAME) return (struct mb_backend_reply){MB_OUTCOME_UNKNOWN,EINVAL};
    if(b->outcome==MB_OUTCOME_COMMITTED) {
        b->state.sequence=c->sequence; memcpy(b->state.frame,c->frame,MB_FRAME_BYTES);
        return (struct mb_backend_reply){MB_OUTCOME_COMMITTED,0};
    }
    return (struct mb_backend_reply){b->outcome,5};
}
static int read_full(uint8_t *out, size_t n)
{
    size_t at=0;
    while(at<n) {
        ssize_t got=read(STDIN_FILENO,out+at,n-at);
        if(got==0) return at==0 ? 0 : -1;
        if(got<0) return -1;
        at+=(size_t)got;
    }
    return 1;
}
static int write_full(const uint8_t *bytes, size_t n)
{
    size_t at=0;
    while(at<n) {
        ssize_t sent=write(STDOUT_FILENO,bytes+at,n-at);
        if(sent<=0) return -1;
        at+=(size_t)sent;
    }
    return 0;
}
int main(int argc, char **argv)
{
    const char *mode=argc==1 ? "commit" : argv[1];
    if(argc>2 || (strcmp(mode,"commit") && strcmp(mode,"reject") && strcmp(mode,"unknown") &&
        strcmp(mode,"eof-after-commit") && strcmp(mode,"cached") && strcmp(mode,"api-error"))) {
        fprintf(stderr,"invalid fixture mode\n"); return 2;
    }
    struct backend fake={0};
    fake.state.instance[0]=UINT64_C(0x0102030405060708);
    fake.state.instance[1]=UINT64_C(0x1112131415161718);
    fake.state.run_id=UINT64_C(0x2122232425262728); fake.state.epoch=UINT64_C(0x3132333435363738);
    fake.state.sequence=7; fake.state.flags=MB_REQUIRED_FLAGS; fake.state.crc_len=96;
    for(size_t i=0;i<MB_FRAME_BYTES;i++) fake.state.frame[i]=(uint8_t)(i*3+7);
    fake.outcome=!strcmp(mode,"reject") ? MB_OUTCOME_REJECTED :
                 !strcmp(mode,"unknown") ? MB_OUTCOME_UNKNOWN : MB_OUTCOME_COMMITTED;
    uint8_t session[16]; for(size_t i=0;i<16;i++) session[i]=(uint8_t)(0x51+i);
    struct mb_backend backend={&fake,snapshot,submit}; struct mb_broker *broker=NULL;
    struct mbw_dispatch *dispatch=NULL;
    if(mb_create(&backend,&fake.state,&broker) != MB_OK ||
        mbw_dispatch_create(broker,session,&fake.state,UINT64_C(0x4142434445464748),&dispatch) != MBWD_OK) {
        fprintf(stderr,"fixture construction failed\n"); mb_destroy(broker); return 2;
    }
    int result=0;
    for(;;) {
        uint8_t input[MBW_MAX_REQUEST_SIZE], output[MBW_MAX_RECORD_SIZE];
        struct mbw_header h={0}; size_t written=0;
        int got=read_full(input,MBW_HEADER_SIZE);
        if(!got) break;
        if(got<0 || mbw_decode_header(input,MBW_HEADER_SIZE,&h) ||
            (h.kind != MBW_ADOPT_REQUEST && h.kind != MBW_FRAME_REQUEST) ||
            h.length>sizeof(input) || read_full(input+MBW_HEADER_SIZE,h.length-MBW_HEADER_SIZE) != 1) {
            fprintf(stderr,"fixture request truncated or invalid\n"); result=2; break;
        }
        if(mbw_dispatch_exchange(dispatch,input,h.length,output,sizeof(output),&written) != MBWD_OK) {
            fprintf(stderr,"fixture dispatch refused\n"); result=2; break;
        }
        if(h.kind==MBW_FRAME_REQUEST) {
            if(!strcmp(mode,"eof-after-commit")) break;
            if(!strcmp(mode,"cached") &&
                mbw_dispatch_exchange(dispatch,input,h.length,output,sizeof(output),&written) != MBWD_OK) {
                fprintf(stderr,"fixture cached retrieval refused\n"); result=2; break;
            }
            if(!strcmp(mode,"api-error")) {
                /* Test-only post-dispatch wire fault: real positive receipt,
                 * synthetic MB_ERR_MUTEX, not an induced pthread unlock failure. */
                output[48]=0xff; output[49]=0xff; output[50]=0xff; output[51]=0xfd;
            }
        }
        if(write_full(output,written)) { fprintf(stderr,"fixture reply write failed\n"); result=2; break; }
    }
    mbw_dispatch_destroy(dispatch); mb_destroy(broker); return result;
}
