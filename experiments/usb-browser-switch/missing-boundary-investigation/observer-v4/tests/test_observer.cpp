/* SPDX-License-Identifier: GPL-3.0-only */
#define UV4_TEST
#include "../observer.cpp"
#include <stdio.h>
#include <stdarg.h>
#include <thread>
#include <stdexcept>
using namespace uv4;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int parent_calls, event_calls, claimed;
static int claim_failure=-1, worker_failure, behavior, native_result;
static const char *tag=firmware;
static uintptr_t last_addresses[4];
static void *(*saved_worker)(void *);
static unsigned char *fixture_event, *fixture_control;
static unsigned nested_depth, nested_limit;
static int helper_calls, helper_behavior;
static int commit_native_event(void *self);
static int plain_helper(void *, unsigned);
static int plain_empty(void *, uint64_t);
static int empty_native_event(void *);
static int empty_behavior, empty_result, empty_calls;
static std::atomic<unsigned> freed_phase(0);
static unsigned reads_at_return;
template<class T> static void put(void *p,size_t off,T value) { memcpy((char *)p+off,&value,sizeof value); }
static void plain_parent(void *) { ++parent_calls; }
static int plain_event(void *self) {
    ++event_calls;
    if (behavior >= 20) return empty_native_event(self);
    if (behavior >= 8) return commit_native_event(self);
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
    int i=claimed++; CHECK(i<4); last_addresses[i]=addr;
    if(i==claim_failure)return -1;
    *out=addr==PARENT_SELECT?(void *)plain_parent:addr==EVENT_EXECUTE?(void *)plain_event:addr==POST_COMMIT_HELPER?(void *)plain_helper:(void *)plain_empty;
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
    put<uintptr_t>(fixture_event,0x28,0x123456);
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

static void native_commit(void *self, unsigned busy=0) {
    uintptr_t e=(uintptr_t)self, b=read<uintptr_t>(e+0x58);
    put<uint32_t>((void *)b,0x1e0,read<uint32_t>(e+0x98));
    put<uint16_t>((void *)b,0x1e4,read<uint16_t>(e+0x9c));
    put<uint8_t>((void *)b,0x1ba,(uint8_t)busy);
}
static int plain_helper(void *self, unsigned argument) {
    ++helper_calls;
    if(helper_behavior==1){delete[]fixture_event;fixture_event=NULL;}
    if(helper_behavior==2){delete[]fixture_control;fixture_control=NULL;}
    if(helper_behavior==3)throw std::runtime_error("native helper exception");
    if(helper_behavior==4){
        uintptr_t b=read<uintptr_t>((uintptr_t)self+0x58);
        put<uint16_t>((void *)b,0x1e4,0x0102); /* later native overwrite */
    }
    return 20+(int)argument;
}
static int commit_native_event(void *self) {
    uintptr_t e=(uintptr_t)self;
    if(behavior==14 && ++nested_depth<nested_limit) {
        uint16_t prior=read<uint16_t>(e+0x9c);
        put<uint16_t>(self,0x9c,prior==0x0102?0x0122:0x0102);
        event_hook(self);
        put<uint16_t>(self,0x9c,prior);
    }
    if(behavior!=9)native_commit(self,behavior==10?1:0);
    if(behavior==12)put<uint16_t>(self,0x9c,0x0102);
    test_post_commit_caller=behavior==11?POST_COMMIT_RETURN+4:POST_COMMIT_RETURN;
    int r=post_commit_hook(self,0);CHECK(r==20);
    if(behavior==14)--nested_depth;
    reads_at_return=test_native_reads.load();
    return native_result;
}
static Record commit_triplet(unsigned wanted, int result=0) {
    Record entry,commit,returned,extra;
    CHECK(pop(entry));CHECK(pop(commit));CHECK(pop(returned));CHECK(!pop(extra));
    CHECK(entry.kind==MEDIA_ENTRY);CHECK(entry.status==VALID);
    CHECK(commit.kind==COMMIT_ENTRY);CHECK(commit.status==wanted);
    CHECK(commit.correlation==entry.sequence);CHECK(same_media(commit.requested,entry.requested));
    CHECK(returned.kind==MEDIA_RESULT);CHECK(returned.correlation==entry.sequence);CHECK(returned.result==result);
    CHECK(test_native_reads.load()==reads_at_return);CHECK(active_scope==NULL);
    return commit;
}
static void test_commit_boundary() {
    helper_behavior=0;event_fixture();behavior=8;
    CHECK(event_hook(fixture_event)==0);Record r=commit_triplet(COMMITTED_AT_ENTRY);
    CHECK(r.before.id==0x0122 && !r.before_busy);free_fixture();
    event_fixture();behavior=9;CHECK(event_hook(fixture_event)==0);
    r=commit_triplet(COMMIT_MISMATCH);CHECK(r.before.id==0x0102);free_fixture();
    event_fixture();behavior=10;CHECK(event_hook(fixture_event)==0);
    r=commit_triplet(COMMIT_MISMATCH);CHECK(r.before_busy==1);free_fixture();
    event_fixture();behavior=12;CHECK(event_hook(fixture_event)==0);
    commit_triplet(CHANGED_CONTEXT);free_fixture();
    event_fixture();behavior=11;CHECK(event_hook(fixture_event)==0);entry_and_result(VALID);free_fixture();
    event_fixture();behavior=8;native_result=-1;CHECK(event_hook(fixture_event)==-1);
    commit_triplet(COMMITTED_AT_ENTRY,-1);free_fixture(); /* boundary + failure remain separate */
    event_fixture();behavior=8;helper_behavior=4;CHECK(event_hook(fixture_event)==0);
    r=commit_triplet(COMMITTED_AT_ENTRY);CHECK(r.before.id==0x0122);
    CHECK(read<uint16_t>((uintptr_t)fixture_control+0x10+0x1e4)==0x0102);free_fixture();
    for(int freed=1;freed<=2;++freed){
        event_fixture();behavior=8;helper_behavior=freed;CHECK(event_hook(fixture_event)==0);
        commit_triplet(COMMITTED_AT_ENTRY);free_fixture();
    }
    event_fixture();behavior=8;helper_behavior=3;bool caught=false;
    try{event_hook(fixture_event);}catch(const std::runtime_error&){caught=true;}
    CHECK(caught);CHECK(active_scope==NULL);Record entry,commit;
    CHECK(pop(entry));CHECK(pop(commit));CHECK(!pop(r));CHECK(commit.kind==COMMIT_ENTRY);
    CHECK(commit.correlation==entry.sequence);free_fixture();helper_behavior=0;
    /* Early gates must not inspect an arbitrary address supplied on a non-target path. */
    unsigned reads=test_native_reads.load();test_post_commit_caller=POST_COMMIT_RETURN;
    CHECK(post_commit_hook((void *)1,7)==27);CHECK(test_native_reads.load()==reads);
    Record scope_record={};scope_record.status=VALID;scope_record.event=1;scope_record.browser=1;
    { EventScope scope(scope_record);
      test_post_commit_caller=POST_COMMIT_RETURN+4;
      CHECK(post_commit_hook((void *)1,0)==20);CHECK(test_native_reads.load()==reads);
      test_post_commit_caller=POST_COMMIT_RETURN;
      CHECK(post_commit_hook((void *)2,0)==20);CHECK(test_native_reads.load()==reads);
      CHECK(post_commit_hook((void *)1,1)==21);CHECK(test_native_reads.load()==reads);
      test_tid=pid+1;CHECK(post_commit_hook((void *)1,0)==20);CHECK(test_native_reads.load()==reads);
      test_tid=pid;bad_thread.store(0);
    }
    CHECK(active_scope==NULL);CHECK(!pop(r));
}

static void test_concurrent_commit_free() {
    event_fixture();behavior=8;helper_behavior=0;test_after_original=after_original_barrier;
    std::thread destroyer([]{
        while(freed_phase.load(std::memory_order_acquire)!=1)std::this_thread::yield();
        delete[]fixture_event;delete[]fixture_control;fixture_event=fixture_control=NULL;
        freed_phase.store(2,std::memory_order_release);
    });
    CHECK(event_hook(fixture_event)==0);destroyer.join();test_after_original=NULL;
    commit_triplet(COMMITTED_AT_ENTRY);CHECK(freed_phase.load()==2);
}

static void test_commit_nested() {
    for(unsigned limit: {3u,9u}) {
        event_fixture();behavior=14;helper_behavior=0;nested_depth=0;nested_limit=limit;
        CHECK(event_hook(fixture_event)==0);CHECK(active_scope==NULL);CHECK(nested_depth==0);
        Record r;uint64_t seqs[10]={};Media requests[10]={};unsigned entries=0,commits=0,results=0;
        while(pop(r)) {
            if(r.kind==MEDIA_ENTRY){CHECK(entries<10);seqs[entries]=r.sequence;requests[entries++]=r.requested;}
            else if(r.kind==COMMIT_ENTRY){
                CHECK(r.status==COMMITTED_AT_ENTRY);bool matched=false;
                for(unsigned i=0;i<entries;++i)if(seqs[i]==r.correlation){matched=true;CHECK(same_media(requests[i],r.before));}
                CHECK(matched);++commits;
            }else{CHECK(r.kind==MEDIA_RESULT);++results;}
        }
        CHECK(entries==limit);CHECK(results==limit);CHECK(commits==(limit>MAX_SCOPE_DEPTH?MAX_SCOPE_DEPTH:limit));
        free_fixture();
    }
}
static void test_commit_owner() {
    event_fixture();native_commit(fixture_event);
    Record entry={};entry.status=VALID;entry.event=(uintptr_t)fixture_event;
    entry.browser=(uintptr_t)fixture_control+0x10;entry.requested=media((uintptr_t)fixture_event+0x98);entry.sequence=99;
    test_post_commit_caller=POST_COMMIT_RETURN;helper_behavior=0;
    { EventScope scope(entry);
      put<uintptr_t>(fixture_event,0,0);CHECK(post_commit_hook(fixture_event,0)==20);CHECK(one().status==BAD_EVENT);
      put<uintptr_t>(fixture_event,0,EVENT_VT);put<int32_t>(fixture_control,8,0);
      CHECK(post_commit_hook(fixture_event,0)==20);CHECK(one().status==BAD_OWNER);
      put<int32_t>(fixture_control,8,1);unsigned char replacement[0x2b0]={};
      put<uintptr_t>(replacement,0,BROWSER_CONTROL_VT);put<int32_t>(replacement,8,1);
      put<uintptr_t>(fixture_event,0x58,(uintptr_t)replacement+0x10);put<uintptr_t>(fixture_event,0x60,(uintptr_t)replacement);
      CHECK(post_commit_hook(fixture_event,0)==20);CHECK(one().status==CHANGED_CONTEXT);
    }
    CHECK(active_scope==NULL);free_fixture();
}

static unsigned reads_before_empty;
static uint64_t supplied_empty_argument;
static void *supplied_empty_agent;
static int plain_empty(void *agent, uint64_t packed) {
    ++empty_calls; CHECK(test_native_reads.load()==reads_before_empty);
    supplied_empty_agent=agent; supplied_empty_argument=packed;
    if(empty_behavior==1){delete[]fixture_event;fixture_event=NULL;}
    if(empty_behavior==2){delete[]fixture_control;fixture_control=NULL;}
    if(empty_behavior==3)throw std::runtime_error("native empty-path exception");
    return empty_result;
}
static int empty_native_event(void *self) {
    uintptr_t e=(uintptr_t)self;
    if(behavior==26 && ++nested_depth<nested_limit) {
        uint16_t prior=read<uint16_t>(e+0x9c);
        put<uint16_t>(self,0x9c,prior==0x0102?0x0122:0x0102);
        event_hook(self); put<uint16_t>(self,0x9c,prior);
    }
    uintptr_t agent=read<uintptr_t>(e+0x28);
    uint64_t packed=(uint64_t)read<uint32_t>(e+0x98) |
        ((uint64_t)read<uint16_t>(e+0x9c)<<32) | (UINT64_C(2)<<48);
    if(behavior==22)agent=1;
    if(behavior==23)packed^=UINT64_C(1)<<32;
    if(behavior==24)packed|=UINT64_C(1)<<60;
    if(behavior>=27 && behavior<=29){
        packed&=UINT64_C(0xff00ffffffffffff);
        packed|=(uint64_t)(behavior==27?0:behavior==28?1:255)<<48;
    }
    test_empty_request_caller=behavior==21?EMPTY_LIST_RETURN+4:EMPTY_LIST_RETURN;
    reads_before_empty=test_native_reads.load();
    int prior_calls=empty_calls;
    CHECK(empty_request_hook((void *)agent,packed)==empty_result);
    CHECK(empty_calls==prior_calls+1);CHECK(supplied_empty_agent==(void *)agent);
    CHECK(supplied_empty_argument==packed);CHECK(test_native_reads.load()==reads_before_empty);
    if(behavior==26)--nested_depth;
    reads_at_return=test_native_reads.load();return native_result;
}
static Record empty_quad(unsigned status,int helper_result) {
    Record entry,path,returned,outer,extra;
    CHECK(pop(entry));CHECK(pop(path));CHECK(pop(returned));CHECK(pop(outer));CHECK(!pop(extra));
    CHECK(entry.kind==MEDIA_ENTRY);CHECK(path.kind==EMPTY_PATH_ENTRY);
    CHECK(path.status==status);CHECK(path.correlation==entry.sequence);
    CHECK(same_media(path.requested,entry.requested));
    CHECK(returned.kind==EMPTY_PATH_RESULT);CHECK(returned.correlation==path.sequence);
    CHECK(returned.event_sequence==entry.sequence);CHECK(returned.result==helper_result);
    CHECK(returned.status==RESULT_ONLY);CHECK(!returned.event && !returned.browser && !returned.request_agent);
    CHECK(outer.kind==MEDIA_RESULT);CHECK(outer.correlation==entry.sequence);CHECK(outer.result==0);
    CHECK(test_native_reads.load()==reads_at_return);CHECK(active_scope==NULL);
    return path;
}
static void empty_scope_depth(unsigned depth) {
    Record r={};r.status=VALID;r.request_agent=1;r.requested={1,0x122};r.sequence=99;
    EventScope scope(r);
    if(depth>1)empty_scope_depth(depth-1);
    else {
        reads_before_empty=test_native_reads.load();
        CHECK(empty_request_hook((void *)1,UINT64_C(0x0002012200000001))==empty_result);
        CHECK(test_native_reads.load()==reads_before_empty);
    }
}
static void test_empty_boundary() {
    for(int result:{0,-1,47}) {
        event_fixture();behavior=20;empty_behavior=0;empty_result=result;
        CHECK(event_hook(fixture_event)==0);Record r=empty_quad(EMPTY_PATH_MEDIA_AGENT_MATCH_CATEGORY_UNKNOWN,result);
        CHECK(r.packed_request==UINT64_C(0x0002012200000001));free_fixture();
    }
    empty_result=0;
    /* Same MediaID/agent with different supplied category bytes is only a
     * partial match. Every opaque byte, zero included, survives unchanged. */
    for(int action:{27,28,29}) {
        event_fixture();behavior=action;empty_behavior=0;
        CHECK(event_hook(fixture_event)==0);
        Record r=empty_quad(EMPTY_PATH_MEDIA_AGENT_MATCH_CATEGORY_UNKNOWN,0);
        CHECK(((r.packed_request>>48)&255)==(unsigned)(action==27?0:action==28?1:255));
        free_fixture();
    }
    empty_result=0;
    for(int action:{22,23,24}) {
        event_fixture();behavior=action;CHECK(event_hook(fixture_event)==0);
        empty_quad(EMPTY_PATH_ARGUMENT_MISMATCH,0);free_fixture();
    }
    event_fixture();behavior=21;CHECK(event_hook(fixture_event)==0);entry_and_result(VALID);free_fixture();
    event_fixture();behavior=20;put<uintptr_t>(fixture_event,0,0);
    CHECK(event_hook(fixture_event)==0);entry_and_result(BAD_EVENT);free_fixture();
    for(int action:{1,2}) {
        event_fixture();behavior=20;empty_behavior=action;CHECK(event_hook(fixture_event)==0);
        empty_quad(EMPTY_PATH_MEDIA_AGENT_MATCH_CATEGORY_UNKNOWN,0);free_fixture();
    }
    event_fixture();behavior=20;empty_behavior=3;bool caught=false;
    try{event_hook(fixture_event);}catch(const std::runtime_error&){caught=true;}
    CHECK(caught);CHECK(active_scope==NULL);Record entry,path,extra;
    CHECK(pop(entry));CHECK(pop(path));CHECK(!pop(extra));CHECK(path.kind==EMPTY_PATH_ENTRY);
    CHECK(path.correlation==entry.sequence);free_fixture();empty_behavior=0;
    /* No active scope or wrong native caller/thread: forward without reading any
     * native object. Even the target path reads only its supplied scalar value. */
    reads_before_empty=test_native_reads.load();test_empty_request_caller=EMPTY_LIST_RETURN;
    CHECK(empty_request_hook((void *)1,0)==0);CHECK(test_native_reads.load()==reads_before_empty);CHECK(!pop(extra));
    Record record={};record.status=VALID;record.request_agent=1;record.requested={1,0x122};record.sequence=99;
    { EventScope scope(record);
      test_empty_request_caller=EMPTY_LIST_RETURN+4;
      CHECK(empty_request_hook((void *)1,0)==0);CHECK(!pop(extra));
      test_empty_request_caller=EMPTY_LIST_RETURN;test_tid=pid+1;
      CHECK(empty_request_hook((void *)1,0)==0);CHECK(!pop(extra));test_tid=pid;bad_thread.store(0);
      Record invalid={};invalid.status=BAD_EVENT;
      { EventScope inner(invalid);CHECK(empty_request_hook((void *)1,0)==0);CHECK(!pop(extra)); }
      CHECK(active_scope==&scope);
    }
    CHECK(active_scope==NULL);CHECK(test_native_reads.load()==reads_before_empty);
    empty_scope_depth(MAX_SCOPE_DEPTH+1);CHECK(active_scope==NULL);CHECK(!pop(extra));
}
static void test_empty_nested() {
    event_fixture();behavior=26;empty_behavior=0;empty_result=0;nested_depth=0;nested_limit=3;
    CHECK(event_hook(fixture_event)==0);CHECK(active_scope==NULL);CHECK(!nested_depth);
    Record r;uint64_t entries[3]={},paths[3]={};Media requests[3]={};unsigned en=0,pn=0,rn=0,outer=0;
    while(pop(r)) {
        if(r.kind==MEDIA_ENTRY){CHECK(en<3);entries[en]=r.sequence;requests[en++]=r.requested;}
        else if(r.kind==EMPTY_PATH_ENTRY){
            CHECK(pn<3);CHECK(r.status==EMPTY_PATH_MEDIA_AGENT_MATCH_CATEGORY_UNKNOWN);paths[pn++]=r.sequence;
            bool matched=false;for(unsigned i=0;i<en;++i)if(entries[i]==r.correlation){matched=true;CHECK(same_media(requests[i],r.requested));}
            CHECK(matched);
        }else if(r.kind==EMPTY_PATH_RESULT){CHECK(rn<3);CHECK(r.correlation==paths[rn++]);}
        else{CHECK(r.kind==MEDIA_RESULT);++outer;}
    }
    CHECK(en==3 && pn==3 && rn==3 && outer==3);free_fixture();
}
static void test_concurrent_empty_free() {
    event_fixture();behavior=20;empty_behavior=0;empty_result=0;test_after_original=after_original_barrier;
    std::thread destroyer([]{
        while(freed_phase.load(std::memory_order_acquire)!=1)std::this_thread::yield();
        delete[]fixture_event;delete[]fixture_control;fixture_event=fixture_control=NULL;
        freed_phase.store(2,std::memory_order_release);
    });
    CHECK(event_hook(fixture_event)==0);destroyer.join();test_after_original=NULL;
    empty_quad(EMPTY_PATH_MEDIA_AGENT_MATCH_CATEGORY_UNKNOWN,0);CHECK(freed_phase.load()==2);
}

int main(int argc,char **argv) {
    CHECK(argc==2);const char *scenario=argv[1];setenv("AZ_USB_OBSERVER_V4_EMULATOR","1",1);
    test_tid=(unsigned)getpid();az_core_v1 c=mock_core();
    if(!strcmp(scenario,"env")){unsetenv("AZ_USB_OBSERVER_V4_EMULATOR");CHECK(init(&c)==-9);CHECK(!claimed);}
    else if(!strcmp(scenario,"firmware")){tag="wrong";CHECK(init(&c)==-10);CHECK(!claimed);}
    else if(!strcmp(scenario,"prologue")){test_prologues=false;CHECK(init(&c)==-12);CHECK(!claimed);}
    else if(!strcmp(scenario,"worker")){worker_failure=1;CHECK(init(&c)==-13);CHECK(!claimed);}
    else if(!strncmp(scenario,"claim",5)){
        claim_failure=atoi(scenario+5);CHECK(init(&c)==-20-claim_failure);CHECK(state.load()==-1);
        if(claim_failure>0){parent_hook((void *)1);CHECK(parent_calls==1);}
        if(claim_failure>1){CHECK(event_hook((void *)1)==0);CHECK(event_calls==1);}
        if(claim_failure>2){CHECK(post_commit_hook((void *)1,0)==20);CHECK(helper_calls==1);}
        CHECK(!test_native_reads.load());
    }else{
        CHECK(init(&c)==0);CHECK(claimed==4);CHECK(saved_worker!=NULL);
        CHECK(last_addresses[0]==PARENT_SELECT);CHECK(last_addresses[1]==EVENT_EXECUTE);CHECK(last_addresses[2]==POST_COMMIT_HELPER);CHECK(last_addresses[3]==EMPTY_LIST_REQUEST);
        if(!strcmp(scenario,"normal")){test_parent();test_event();test_queue();}
        else if(!strcmp(scenario,"commit")){test_commit_boundary();test_commit_nested();test_commit_owner();}
        else if(!strcmp(scenario,"empty")){test_empty_boundary();test_empty_nested();}
        else if(!strcmp(scenario,"empty-concurrent-free")){test_concurrent_empty_free();}
        else if(!strcmp(scenario,"commit-concurrent-free")){test_concurrent_commit_free();}
        else if(!strcmp(scenario,"thread")){
            test_tid=pid+1;parent_hook((void *)1);CHECK(parent_calls==1);CHECK(bad_thread.load()==pid+1);
            CHECK(event_hook((void *)1)==0);CHECK(event_calls==1);Record r;CHECK(!pop(r));
        }else if(!strcmp(scenario,"concurrent-free")){
            test_concurrent_free();
        }else{CHECK(false);}
    }
    printf("%s: %u checks passed\n",scenario,checks);return 0;
}
