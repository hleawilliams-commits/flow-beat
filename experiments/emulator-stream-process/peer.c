/* SPDX-License-Identifier: GPL-2.0-only */
/* Offline child fixture only, never a guest service or endpoint.
 *
 * fd0 and fd1 are already-open unnamed duplex socketpair ends inherited from
 * the test parent: data and fixture-control respectively. The actual frozen
 * stream_native.c supplies every data read/write/close. Only modeled controller
 * ioctl returns may be faulted here; no stream syscall replacement is linked.
 *
 * The exact graph-support prefix supplies the frozen driver model and real
 * successful pthread operations. Its original/duplicate numbers (3/8) belong
 * to the model, not the process descriptor table. The transport is actual fd0.
 * No model selector or parent main is included or executed.
 *
 * READY precedes G and all data I/O. EOF/error retains the owner until explicit
 * D on the separate control channel. D is test-only independent accounting:
 * all serial fixture calls returned, modeled mutations are known, no external
 * native user exists, and the parent relinquished the data endpoint. Control
 * loss is a failed process, never disposal permission or successful evidence.
 * Process exit/kill loses this in-memory custody; no death recovery is claimed.
 *
 * Report protocol v1: fixed3072 bytes, explicitly big-endian, no native struct
 * serialization. Header32, metadata64*8, model-state200, records1120*2, pad88.
 * Commands are single bytes G then D. Report kinds1/2/3 are READY/TERMINAL/
 * CLEANUP. CLEANUP retains the pre-disposal snapshot as archived evidence;
 * the post-call owner pointer, disposal report and model liveness are current.
 */
#define mba_sys_ioctl fixture_mba_sys_ioctl
#include "graph-support.inc"
#undef mba_sys_ioctl
#include "stream.h"
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

enum peer_mode { PEER_NORMAL=0, PEER_UNKNOWN=1, PEER_LOST=2, PEER_PARTIAL=3 };
enum { REPORT_BYTES=3072, META_OFFSET=32, MODEL_OFFSET=544,
       CURRENT_OFFSET=744, LAST_OFFSET=1864, RECORD_BYTES=1120, MAX_STEPS=4096 };
_Static_assert(sizeof(ssize_t)<=sizeof(int64_t), "report signed count width");
_Static_assert(sizeof(size_t)<=sizeof(uint64_t), "report length width");
_Static_assert(MBW_MAX_REQUEST_SIZE==256 && MBW_MAX_RECORD_SIZE==668,
               "report frozen wire buffer sizes");
_Static_assert(META_OFFSET+64*8==MODEL_OFFSET &&
               MODEL_OFFSET+200==CURRENT_OFFSET &&
               CURRENT_OFFSET+RECORD_BYTES==LAST_OFFSET &&
               LAST_OFFSET+RECORD_BYTES+88==REPORT_BYTES, "report fixed layout");

static enum peer_mode mode;
static int sigpipe_ignored,shutdown_attempted,shutdown_result,shutdown_error;
static int dispose_attempted,dispose_result,last_step_result;
static uint64_t step_calls;
static struct mbgs_dispose_report dispose_report;

struct mba_sys_result mba_sys_ioctl(int fd,unsigned long operation,void *argument)
{
    struct mba_sys_result result=fixture_mba_sys_ioctl(fd,operation,argument);
    /* Synthetic lost successful native return, after the actual extracted
     * driver committed the FRAME. This is not a claimed Linux errno behavior. */
    if(mode==PEER_UNKNOWN && operation==AZ_OVERLAY_FRAME && result.result==0)
        return(struct mba_sys_result){-1,EIO};
    return result;
}

static int failure(const char *message)
{
    fprintf(stderr,"peer failure: %s\n",message);
    return 2;
}
static void put32(uint8_t *p,uint32_t value)
{
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(24-8*i));
}
static void put64(uint8_t *p,uint64_t value)
{
    for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(value>>(56-8*i));
}
#define SIGNED(value) ((uint64_t)(int64_t)(value))

static void encode_record(uint8_t *out,const struct mbgs_record *r)
{
    uint64_t fields[24]={
        r->number,r->read_calls,r->write_calls,
        r->request_received,r->request_expected,r->response_length,r->response_sent,
        SIGNED(r->header_valid),
        SIGNED(r->last_read.attempted),SIGNED(r->last_read.returned),
        r->last_read.offset,r->last_read.requested,
        SIGNED(r->last_read.raw.count),SIGNED(r->last_read.raw.error),
        SIGNED(r->last_write.attempted),SIGNED(r->last_write.returned),
        r->last_write.offset,r->last_write.requested,
        SIGNED(r->last_write.raw.count),SIGNED(r->last_write.raw.error),
        SIGNED(r->dispatch.attempted),SIGNED(r->dispatch.returned),
        SIGNED(r->dispatch.result),r->dispatch.written
    };
    for(unsigned i=0;i<24;i++)put64(out+i*8,fields[i]);
    memcpy(out+192,r->request,256);
    memcpy(out+448,r->response,668);
    /* out[1116..1119] stays zero from the complete report initialization. */
}
static void encode_model(uint8_t *out)
{
    /* Pure model reads, between calls. No extra STATE, ioctl, usercopy or
     * acquisition is performed to construct this diagnostic state. */
    uint32_t flags=(mixer_fenced?AZ_OVERLAY_FENCED:0) |
        (mixer_owner?AZ_OVERLAY_ACTIVE:0) |
        (mixer_owner==&original?AZ_OVERLAY_CALLER_OWNER:0) |
        (has_inject[OWNED_BUS]?AZ_OVERLAY_INJECTED:0) |
        (allow_overlay_ownership?AZ_OVERLAY_ENABLED:0);
    put64(out,mixer_instance[0]);put64(out+8,mixer_instance[1]);
    put64(out+16,mixer_run);put64(out+24,mixer_epoch);put64(out+32,mixer_sequence);
    put32(out+40,flags);put32(out+44,(uint32_t)spi_crc_len[OWNED_BUS]);
    memcpy(out+48,has_inject[OWNED_BUS]?inject_pending[OWNED_BUS]:
           az_bus_frame[OWNED_BUS],152);
}
static int control_write(const uint8_t *bytes,size_t length)
{
    size_t at=0;
    /* Positive progress only, at most length calls. Errors/zero never retry. */
    while(at<length){
        ssize_t count=write(STDOUT_FILENO,bytes+at,length-at);
        if(count<=0 || (size_t)count>length-at)return -1;
        at+=(size_t)count;
    }
    return 0;
}
static int command(unsigned char expected)
{
    unsigned char value=0;
    ssize_t count=read(STDOUT_FILENO,&value,1);
    return count==1 && value==expected ? 0 : -1;
}
static int report(unsigned kind,const struct mbgs_evidence *e,
                  const struct mbgs_owner *owner,int archived)
{
    uint8_t bytes[REPORT_BYTES]={0};
    const struct mbog_dispose_report *g=&dispose_report.graph_report;
    uint64_t meta[64]={
        owner!=NULL,1,SIGNED(archived),0,SIGNED(last_step_result),
        SIGNED(e->phase),SIGNED(e->current_operation),SIGNED(e->active),
        SIGNED(e->taken),SIGNED(e->descriptor_held),SIGNED(e->stream_fd),
        SIGNED(e->graph_present),SIGNED(e->graph_evidence_valid),
        SIGNED(e->last_completed_valid),SIGNED(e->first_terminal),
        SIGNED(e->terminal_operation),SIGNED(e->disposal_started),
        SIGNED(e->uncertainty_seen),e->disposal_attempts,e->graph_finalize_attempts,
        SIGNED(e->graph.phase),SIGNED(e->graph.claim_present),
        SIGNED(e->graph.adapter_present),SIGNED(e->graph.broker_present),
        SIGNED(e->graph.dispatch_present),SIGNED(e->graph.disposal_started),
        SIGNED(e->graph.uncertain_close_seen),
        native_calls,states,claims,frames,duplicates,closes,
        fds[3]!=NULL,fds[8]!=NULL,mixer_owner==&original,mixer_fenced,
        SIGNED(sigpipe_ignored),SIGNED(shutdown_attempted),SIGNED(shutdown_result),
        SIGNED(shutdown_error),SIGNED(dispose_attempted),SIGNED(dispose_result),
        SIGNED(dispose_report.completed),SIGNED(dispose_report.result),
        SIGNED(dispose_report.owner_released),SIGNED(dispose_report.uncertainty_seen),
        SIGNED(dispose_report.graph_attempted),SIGNED(dispose_report.graph_returned),
        SIGNED(dispose_report.graph_result),SIGNED(dispose_report.stream_close.attempted),
        SIGNED(dispose_report.stream_close.returned),SIGNED(dispose_report.stream_close.descriptor),
        SIGNED(dispose_report.stream_close.raw.result),SIGNED(dispose_report.stream_close.raw.error),
        SIGNED(g->graph_released),SIGNED(g->broker_attempted),SIGNED(g->broker_returned),
        SIGNED(g->broker_result),SIGNED(g->adapter_close.report.call.result),
        SIGNED(g->adapter_close.report.call.error),SIGNED(g->claim_close.report.call.result),
        SIGNED(g->claim_close.report.call.error),step_calls
    };
    memcpy(bytes,"AZSPRP01",8);put32(bytes+8,1);put32(bytes+12,kind);
    put32(bytes+16,(uint32_t)mode);put32(bytes+20,REPORT_BYTES);
    for(unsigned i=0;i<64;i++)put64(bytes+META_OFFSET+i*8,meta[i]);
    encode_model(bytes+MODEL_OFFSET);
    encode_record(bytes+CURRENT_OFFSET,&e->current);
    encode_record(bytes+LAST_OFFSET,&e->last_completed);
    return control_write(bytes,sizeof bytes);
}
#undef SIGNED

static int select_mode(int argc,char **argv)
{
    if(argc!=2)return -1;
    if(!strcmp(argv[1],"normal"))mode=PEER_NORMAL;
    else if(!strcmp(argv[1],"unknown"))mode=PEER_UNKNOWN;
    else if(!strcmp(argv[1],"lost-reply"))mode=PEER_LOST;
    else if(!strcmp(argv[1],"partial-request"))mode=PEER_PARTIAL;
    else return -1;
    return 0;
}

int main(int argc,char **argv)
{
    struct mbog_graph *graph=NULL;
    struct mbgs_owner *owner=NULL;
    struct mbgs_evidence held={0};
    int original_fd=3,data_fd=STDIN_FILENO;
    if(select_mode(argc,argv))return failure("invalid mode");
    /* Fixture process policy only. Production stream_native.c remains unchanged.
     * This permits the lost-reply socket write to return EPIPE. It does not
     * establish any policy for a future emulator/guest process. */
    if(signal(SIGPIPE,SIG_IGN)==SIG_ERR)return failure("SIGPIPE policy");
    sigpipe_ignored=1;
    reset();
    struct mbog_plan graph_plan=plan();
    if(mbog_create(&graph_plan,&graph)!=MBOG_OK ||
       mbog_take(graph,&original_fd)!=MBOG_OK || original_fd!=-1 ||
       mbog_start(graph)!=MBOG_OK || mbgs_create(&owner)!=MBGS_OK ||
       mbgs_take(owner,&data_fd,&graph)!=MBGS_OK || data_fd!=-1 || graph)
        return failure("construction");
    if(mbgs_inspect(owner,&held)!=MBGS_OK)return failure("ready inspect");
    if(report(1,&held,owner,0))return failure("READY report");
    if(command('G'))return failure("GO control lost or invalid");

    int terminal=0;
    for(unsigned i=0;i<MAX_STEPS;i++){
        if(mbgs_inspect(owner,&held)!=MBGS_OK)return failure("step inspect");
        if(mode==PEER_LOST && !shutdown_attempted &&
           held.phase==MBGS_WRITE && held.current.header.kind==MBW_FRAME_REQUEST){
            /* Explicit fixture fault only: make the response unobservable after
             * actual model commit, while retaining owner and its positive bytes.
             * The next actual production write must return its own raw result. */
            shutdown_attempted=1;
            shutdown_result=shutdown(STDIN_FILENO,SHUT_WR);
            shutdown_error=shutdown_result==-1?errno:0;
            if(shutdown_result!=0)return failure("write shutdown");
        }
        last_step_result=mbgs_step(owner);
        step_calls++;
        if(mbgs_inspect(owner,&held)!=MBGS_OK)return failure("returned inspect");
        if(last_step_result<0){terminal=1;break;}
        if(last_step_result==MBGS_RECORD_WRITTEN){
            /* This is an explicit fixture decision between completed records,
             * not automatic work performed by the production stream owner. */
            if(mbgs_next(owner)!=MBGS_OK)return failure("next record");
        }else if(last_step_result!=MBGS_PROGRESS)return failure("unexpected progress");
    }
    if(!terminal)return failure("step bound exhausted");
    if(report(2,&held,owner,0))return failure("TERMINAL report");
    if(command('D'))return failure("dispose control lost or invalid");
    /* No step occurs between terminal report and D. All modeled operations have
     * returned; D confirms the parent's independent fixture accounting. Unknown
     * native return is fully accounted for by this model, not inferred from EOF.
     * No fault reset, raw resource close, retry or finalization is used here. */
    dispose_attempted=1;
    dispose_result=mbgs_dispose(&owner,&dispose_report);
    if(report(3,&held,owner,1))return failure("CLEANUP report");
    if(dispose_result!=MBGS_OK || owner || !dispose_report.completed ||
       !dispose_report.owner_released || fds[3] || fds[8] ||
       !dispose_report.graph_returned || !dispose_report.graph_report.graph_released ||
       !dispose_report.stream_close.attempted || !dispose_report.stream_close.returned ||
       dispose_report.stream_close.raw.result!=0)
        return failure("explicit cleanup incomplete");
    /* Fixture-only other model file fd4 is static memory, never graph custody or
     * an OS fd. It remains distinct from the actual control descriptor fd1. */
    if(close(STDOUT_FILENO)!=0)return failure("control close");
    return 0;
}
