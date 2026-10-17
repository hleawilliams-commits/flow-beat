/* SPDX-License-Identifier: GPL-3.0-only */
#define UV2_TEST
#include "../observer.cpp"
#include <stdio.h>
#include <stdarg.h>
#include <thread>
#include <stdexcept>
using namespace uv2;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int parent_calls, event_calls, claimed;
static int claim_failure=-1, worker_failure, behavior, native_result;
static const char *tag=firmware;
static uintptr_t last_addresses[2];
static void *(*saved_worker)(void *);
static unsigned char *fixture_event, *fixture_control;
static unsigned nested_depth;
static std::atomic<unsigned> freed_phase(0);
static unsigned reads_at_return;
template<class T> static void put(void *p,size_t off,T value) { memcpy((char *)p+off,&value,sizeof value); }
static void plain_parent(void *) { ++parent_calls; }
static int plain_event(void *self) {
    ++event_calls;
    uintptr_t e=(uintptr_t)self;
    if (behavior==1) {
        uintptr_t b=read<uintptr_t>(e+0x58);
        put<uint32_t>((void *)b,0x1e0,read<uint32_t>(e+0x98));
        put<uint16_t>((void *)b,0x1e4,read<uint16_t>(e+0x9c));
        put<uint8_t>((void *)b,0x1ba,0);
    } else if (behavior==2) {
        delete[] fixture_event; fixture_event=NULL;
    } else if (behavior==3) {
        delete[] fixture_control; fixture_control=NULL;
    } else if (behavior==4) {
        put<uintptr_t>(self,0x58,0x10000); /* must reject without dereferencing replacement */
    } else if (behavior==5) {
        throw std::runtime_error("native exception");
    } else if (behavior==6) {
        if (++nested_depth<=4) event_hook(self);
        --nested_depth;
    } else if (behavior==7) {
        live.store(0);
    }
    reads_at_return=test_native_reads.load();
    return native_result;
}
static const char *fw() { return tag; }
static void logger(const char *,const char *,...) {}
static int start_thread(const char *,const char *,void *(*fn)(void *),void *) {
    saved_worker=fn; return worker_failure;
}
static int claim(const char *,uintptr_t addr,void *,void **out) {
    int i=claimed++; CHECK(i<2); last_addresses[i]=addr;
    if(i==claim_failure)return -1;
    *out=addr==PARENT_SELECT?(void *)plain_parent:(void *)plain_event;
    return 0;
}
static az_core_v1 mock_core() {
    az_core_v1 c={}; c.abi=AZ_MOD_ABI;c.size=sizeof c;c.fw_tag=fw;
    c.thread_start=start_thread;c.claim_hook=claim;c.log=logger;return c;
}
static void clear_queue() { Record r; while(pop(r)){} }
static Record one() { Record r; CHECK(pop(r));Record extra;CHECK(!pop(extra));return r; }
static void event_fixture() {
    fixture_event=new unsigned char[0xa0]();fixture_control=new unsigned char[0x2b0]();
    put<uintptr_t>(fixture_event,0,EVENT_VT);
    put<uintptr_t>(fixture_event,0x58,(uintptr_t)fixture_control+0x10);
    put<uintptr_t>(fixture_event,0x60,(uintptr_t)fixture_control);
    put<uintptr_t>(fixture_control,0,BROWSER_CONTROL_VT); put<int32_t>(fixture_control,8,1);
    put<uint32_t>(fixture_event,0x98,1);put<uint16_t>(fixture_event,0x9c,0x0122);
    put<uint32_t>(fixture_control,0x10+0x1e0,1);put<uint16_t>(fixture_control,0x10+0x1e4,0x0102);
    put<uint8_t>(fixture_control,0x10+0x1ba,1);
    behavior=0;native_result=0;live.store(1);
}
static void free_fixture() { delete[]fixture_event;delete[]fixture_control;fixture_event=fixture_control=NULL; }
static void test_parent() {
    alignas(8) unsigned char p[0x3b0]={}, c[0x158]={}, rows[0x50]={};
    put<uintptr_t>(p,0,PARENT_VT);put<uintptr_t>(p,0x198,LISTENER_VT);
    put<uintptr_t>(p,0x308,(uintptr_t)c);put<int32_t>(p,0x1e8,0);
    put<uintptr_t>(c,0,CHILD_VT);put<uintptr_t>(c,0x60,(uintptr_t)p+0x198);
    put<uintptr_t>(c,0x80,MODEL_VT);put<uintptr_t>(c,0x88,(uintptr_t)rows);
    put<int32_t>(c,0x90,2);put<int32_t>(c,0x98,2);
    for(unsigned i=0;i<2;++i) {
        put<uint32_t>(rows,i*0x28+4,1);put<uint8_t>(rows,i*0x28+8,1);
        put<uint16_t>(rows,i*0x28+0xc,i?0x0122:0x0102);
        put<uintptr_t>(rows,i*0x28+0x10,1); /* no label pointer is ever followed */
    }
    parent_hook(p); Record r=one();CHECK(r.status==VALID);CHECK(r.mode==0);CHECK(r.row_count==2);
    CHECK(r.rows[0].media==0x0102);CHECK(r.rows[1].media==0x0122);CHECK(parent_calls==1);
    put<int32_t>(p,0x1e8,1);parent_hook(p);r=one();CHECK(r.mode==1);CHECK(r.status==VALID);
    put<uintptr_t>(p,0x198,0);parent_hook(p);CHECK(one().status==BAD_PARENT);
    put<uintptr_t>(p,0x198,LISTENER_VT);put<uintptr_t>(c,0x60,0);parent_hook(p);CHECK(one().status==BAD_CHILD);
    put<uintptr_t>(c,0x60,(uintptr_t)p+0x198);put<uintptr_t>(c,0x80,0);parent_hook(p);CHECK(one().status==BAD_MODEL);
    put<uintptr_t>(c,0x80,MODEL_VT);put<int32_t>(c,0x98,17);parent_hook(p);CHECK(one().status==BAD_BOUNDS);
    put<int32_t>(c,0x98,-1);parent_hook(p);CHECK(one().status==BAD_BOUNDS);
    put<int32_t>(c,0x98,2);put<int32_t>(c,0x90,1);parent_hook(p);CHECK(one().status==BAD_BOUNDS);
    put<int32_t>(c,0x90,1025);parent_hook(p);CHECK(one().status==BAD_BOUNDS);
    put<int32_t>(c,0x90,0);put<int32_t>(c,0x98,0);put<uintptr_t>(c,0x88,0);parent_hook(p);r=one();CHECK(r.status==VALID);CHECK(r.row_count==0);
    CHECK(parent_calls==10);
}
static Record entry_and_result(unsigned entry_status, int result=0) {
    Record entry,r,extra;
    CHECK(pop(entry));CHECK(pop(r));CHECK(!pop(extra));
    CHECK(entry.kind==MEDIA_ENTRY);CHECK(entry.status==entry_status);
    CHECK(r.kind==MEDIA_RESULT);CHECK(r.correlation==entry.sequence);CHECK(r.result==result);
    CHECK(r.status==(result==0||result==1?RESULT_ONLY:NATIVE_FAILURE));
    CHECK(!r.event && !r.browser); /* result contains no live pointer */
    CHECK(test_native_reads.load()==reads_at_return);
    return entry;
}
static void test_event() {
    event_fixture();behavior=1;CHECK(event_hook(fixture_event)==0);Record r=entry_and_result(VALID);
    CHECK(r.requested.id==0x0122);CHECK(r.before.id==0x0102);CHECK(r.before_busy==1);
    behavior=0;native_result=1;CHECK(event_hook(fixture_event)==1);r=entry_and_result(VALID,1);
    CHECK(r.before.id==0x0122);CHECK(!r.before_busy);free_fixture();
    event_fixture();CHECK(event_hook(fixture_event)==0);r=entry_and_result(VALID);CHECK(r.before.id!=r.requested.id);free_fixture();
    event_fixture();behavior=1;native_result=-1;CHECK(event_hook(fixture_event)==-1);entry_and_result(VALID,-1);free_fixture();
    event_fixture();native_result=47;CHECK(event_hook(fixture_event)==47);entry_and_result(VALID,47);free_fixture();
    event_fixture();put<uintptr_t>(fixture_event,0,0);CHECK(event_hook(fixture_event)==0);entry_and_result(BAD_EVENT);free_fixture();
    event_fixture();put<uintptr_t>(fixture_control,0,0);CHECK(event_hook(fixture_event)==0);entry_and_result(BAD_OWNER);free_fixture();
    event_fixture();put<int32_t>(fixture_control,8,0);CHECK(event_hook(fixture_event)==0);entry_and_result(BAD_OWNER);free_fixture();
    event_fixture();put<uintptr_t>(fixture_event,0x58,1);CHECK(event_hook(fixture_event)==0);entry_and_result(BAD_OWNER);free_fixture();
    event_fixture();behavior=2;CHECK(event_hook(fixture_event)==0);entry_and_result(VALID);free_fixture();
    event_fixture();behavior=3;CHECK(event_hook(fixture_event)==0);entry_and_result(VALID);free_fixture();
    event_fixture();behavior=4;CHECK(event_hook(fixture_event)==0);entry_and_result(VALID);free_fixture();
    event_fixture();behavior=7;CHECK(event_hook(fixture_event)==0);entry_and_result(VALID);free_fixture();
    event_fixture();behavior=5;bool caught=false;try{event_hook(fixture_event);}catch(const std::runtime_error&){caught=true;}
    CHECK(caught);r=one();CHECK(r.kind==MEDIA_ENTRY);CHECK(r.status==VALID);free_fixture();
    event_fixture();behavior=6;CHECK(event_hook(fixture_event)==0);
    uint64_t entries[5]={};unsigned entry_count=0,result_count=0;
    while(pop(r)){
        if(r.kind==MEDIA_ENTRY){CHECK(entry_count<5);entries[entry_count++]=r.sequence;}
        else{CHECK(result_count<5);CHECK(r.kind==MEDIA_RESULT);CHECK(r.correlation==entries[4-result_count]);++result_count;}
    }
    CHECK(entry_count==5);CHECK(result_count==5);free_fixture();
}
static void after_original_barrier() {
    freed_phase.store(1,std::memory_order_release);
    while(freed_phase.load(std::memory_order_acquire)!=2)std::this_thread::yield();
}
static void test_concurrent_free() {
    event_fixture();test_after_original=after_original_barrier;
    std::thread destroyer([]{
        while(freed_phase.load(std::memory_order_acquire)!=1)std::this_thread::yield();
        delete[]fixture_event;delete[]fixture_control;fixture_event=fixture_control=NULL;
        freed_phase.store(2,std::memory_order_release);
    });
    /* Event and browser are freed from another thread AFTER the native original
     * has returned, BEFORE production result handling resumes. No notification,
     * observer lock, refcount pin or lifetime assumption protects these addresses. */
    CHECK(event_hook(fixture_event)==0);destroyer.join();test_after_original=NULL;
    Record r=entry_and_result(VALID);CHECK(r.before.id==0x0102);CHECK(freed_phase.load()==2);
}
static void test_queue() {
    Record r={};for(unsigned i=0;i<QUEUE_SIZE+2;++i)publish(r);
    CHECK(dropped.load()==3);unsigned n=0;while(pop(r))++n;CHECK(n==QUEUE_SIZE-1);
    std::atomic<bool> done(false);std::atomic<unsigned> received(0);
    std::thread consumer([&]{Record q;uint64_t prev=0;while(!done.load()||read_index.load()!=write_index.load()){
        if(pop(q)){if(q.sequence<=prev)abort();prev=q.sequence;received.fetch_add(1);}else std::this_thread::yield();}});
    for(unsigned i=0;i<10000;++i)publish(r);
    done.store(true);consumer.join();CHECK(received.load()>0);CHECK(read_index.load()==write_index.load());
}
int main(int argc,char **argv) {
    CHECK(argc==2);const char *scenario=argv[1];setenv("AZ_USB_OBSERVER_V2_EMULATOR","1",1);
    test_tid=(unsigned)getpid();az_core_v1 c=mock_core();
    if(!strcmp(scenario,"env")){unsetenv("AZ_USB_OBSERVER_V2_EMULATOR");CHECK(init(&c)==-9);CHECK(!claimed);}
    else if(!strcmp(scenario,"firmware")){tag="wrong";CHECK(init(&c)==-10);CHECK(!claimed);}
    else if(!strcmp(scenario,"prologue")){test_prologues=false;CHECK(init(&c)==-12);CHECK(!claimed);}
    else if(!strcmp(scenario,"worker")){worker_failure=1;CHECK(init(&c)==-13);CHECK(!claimed);}
    else if(!strncmp(scenario,"claim",5)){
        claim_failure=atoi(scenario+5);CHECK(init(&c)==-20-claim_failure);CHECK(state.load()==-1);
        if(claim_failure>0){parent_hook((void *)1);CHECK(parent_calls==1);}
        CHECK(!test_native_reads.load());
    }else{
        CHECK(init(&c)==0);CHECK(claimed==2);CHECK(saved_worker!=NULL);
        CHECK(last_addresses[0]==PARENT_SELECT);CHECK(last_addresses[1]==EVENT_EXECUTE);
        if(!strcmp(scenario,"normal")){test_parent();test_event();test_queue();}
        else if(!strcmp(scenario,"thread")){
            test_tid=pid+1;parent_hook((void *)1);CHECK(parent_calls==1);CHECK(bad_thread.load()==pid+1);
            CHECK(event_hook((void *)1)==0);CHECK(event_calls==1);Record r;CHECK(!pop(r));
        }else if(!strcmp(scenario,"concurrent-free")){
            test_concurrent_free();
        }else{CHECK(false);}
    }
    printf("%s: %u checks passed\n",scenario,checks);return 0;
}
