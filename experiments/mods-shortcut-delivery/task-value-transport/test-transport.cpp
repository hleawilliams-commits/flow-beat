// SPDX-License-Identifier: GPL-3.0-only
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
static void test_hook(unsigned,const void*,std::uint32_t);
static void tracked_read(void*,const void*,std::size_t);
#define TVT_TEST_POINT(point,transport,index) test_hook(static_cast<unsigned>(point),(transport),(index))
#define TC_READ_BYTES(dst,src,len) tracked_read((dst),(src),(len))
#include "transport.h"
static std::atomic<unsigned> checks{0},failures{0},bad_reads{0};
static thread_local std::function<void(unsigned,const void*,std::uint32_t)> hook;
static thread_local const unsigned char* native_base=nullptr;
static thread_local unsigned reads=0;
static void check(bool ok,const char* name) {
    checks.fetch_add(1,std::memory_order_relaxed);
    if (!ok) { failures.fetch_add(1,std::memory_order_relaxed); std::fprintf(stderr,"FAIL %s\n",name); }
}
#define CHECK(x,name) check((x),(name))
static void test_hook(unsigned point,const void* transport,std::uint32_t index) {
    if (hook) hook(point,transport,index);
    errno=EILSEQ;
}
static bool at(unsigned p,tvt::HookPoint want) { return p==static_cast<unsigned>(want); }
static void tracked_read(void* dst,const void* src,std::size_t n) {
    const auto offset=reinterpret_cast<std::uintptr_t>(src)-reinterpret_cast<std::uintptr_t>(native_base);
    if (!native_base || !((offset==0 && n==8)||(offset==0x10 && n==8)||
        (offset==0x20 && n==8)||(offset==0x28 && n==4)||(offset==0x2c && n==4)))
        bad_reads.fetch_add(1,std::memory_order_relaxed);
    ++reads; std::memcpy(dst,src,n); errno=EILSEQ;
}
struct alignas(8) Task {
    unsigned char bytes[56];
    explicit Task(std::uint64_t number=UINT64_C(0x1234567887654321),std::uint64_t id=4) {
        std::memset(bytes,0xa5,sizeof bytes);
        const std::uint64_t vp=TC_TASK_VPTR; const std::uint32_t delta=0xffffffff,cumulative=0x80000000;
        std::memcpy(bytes,&vp,8);std::memcpy(bytes+0x10,&number,8);std::memcpy(bytes+0x20,&id,8);
        std::memcpy(bytes+0x28,&delta,4);std::memcpy(bytes+0x2c,&cumulative,4);
    }
};
template<std::size_t N> static bool begin(tvt::Transport<N>& t,tc_context& c,Task& task,std::uint64_t thread=7,std::uint32_t phase=TC_SUBMISSION) {
    native_base=task.bytes;reads=0;
    return t.begin(&c,phase,phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR,task.bytes,thread);
}
template<class Predicate> static void wait_for(Predicate pred) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while (!pred()) {
        if (std::chrono::steady_clock::now()>end) { std::fputs("FATAL schedule timeout\n",stderr);std::abort(); }
        std::this_thread::yield();
    }
}
static void gate_and_rejection() {
    native_base=nullptr;reads=0;
    tvt::Transport<8> t(31);tc_context c{}; c.invocation_id=99; errno=EDOM;
    CHECK(!t.begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,reinterpret_cast<void*>(8),7),"gate/disabled");
    CHECK(c.invocation_id==0 && c.phase==0 && reads==0,"gate/disabled-unqualified-no-read");
    CHECK(errno==EDOM,"gate/errno-disabled"); CHECK(t.start(),"gate/start");CHECK(!t.start(),"gate/no-restart");
    CHECK(!t.begin(&c,8,TC_SUBMIT_LR,reinterpret_cast<void*>(8),7),"gate/bad-phase");
    CHECK(!t.begin(&c,TC_SUBMISSION,TC_SUBMIT_LR+4,reinterpret_cast<void*>(8),7),"gate/bad-caller");
    CHECK(t.counters().reserved==0 && reads==0,"gate/prefilter-no-reserve-read");
    CHECK(!t.begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,nullptr,7),"capture/null");
    CHECK(c.invocation_id==0 && c.phase==0,"capture/null-out-unqualified");
    tc_context rejection{};CHECK(t.read_entry(0,rejection)&&rejection.status==TC_NULL_TASK&&rejection.invocation_id==1,"capture/rejection-is-published");
    CHECK(!t.finish(rejection,1),"capture/rejection-cannot-finish");
    CHECK(!t.begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,reinterpret_cast<void*>(9),7),"capture/alignment");
    CHECK(!t.begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,reinterpret_cast<void*>(UINTPTR_MAX-7),7),"capture/address-overflow");
    CHECK(reads==0,"capture/early-reject-no-read");
    Task wrong;std::memset(wrong.bytes,0,8);CHECK(!begin(t,c,wrong),"capture/type");
    CHECK(reads==1&&c.invocation_id==0&&c.phase==0,"capture/type-vptr-only");
    Task task;errno=ERANGE;CHECK(begin(t,c,task),"capture/valid");
    CHECK(errno==ERANGE&&reads==5,"capture/exact-reads-errno");
    CHECK(c.invocation_id==5&&c.run_id==31&&c.thread_observation==7,"capture/slot-derived-identity");
    CHECK(!t.finish(tc_context{},1),"capture/zero-context-rejected");
    auto counts=t.counters();CHECK(counts.prefilter_rejected==2&&counts.capture_rejected==4&&counts.reserved==5&&counts.entries==5,"capture/counters-after-serial-work");
    tc_context stale=c;tc_return_record old{};old.native_result=123;
    CHECK(!t.read_entry(7,stale)&&tvt::same_context(stale,c),"read/false-preserves-entry");
    CHECK(!t.read_return(0,old)&&old.native_result==123,"read/false-preserves-return");
    CHECK(!t.read_entry(8,stale)&&!t.read_return(8,old),"read/bounds");
    t.close();CHECK(!t.start(),"gate/close-permanent");native_base=nullptr;reads=0;
    CHECK(!t.begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,reinterpret_cast<void*>(8),7)&&c.invocation_id==0&&reads==0,"gate/closed-no-read");
    tvt::Transport<1> zero(0);CHECK(!zero.start(),"scope/zero-run-no-start");
}
static void returns_and_value_limits() {
    tvt::Transport<10> t(41),other(42);CHECK(t.start()&&other.start(),"return/start");
    Task task;tc_context submission{},execution{};
    CHECK(begin(t,submission,task),"return/submission-entry");
    CHECK(begin(t,execution,task,8,TC_EXECUTION),"return/executor-before-return");
    tc_return_record result{};CHECK(!t.read_return(0,result),"return/missing-stays-absent");
    CHECK(tc_candidate(&submission,&execution)==TC_SAME_VALUES_ONLY,"value/equal-is-only-candidate");
    CHECK(!t.finish(execution,task.bytes[0]),"return/execution-has-no-after");
    auto altered=submission;altered.values.id++;CHECK(!t.finish(altered,3),"return/wrong-payload-same-id");
    altered=submission;altered.reserved=1;CHECK(!t.finish(altered,3),"return/wrong-reserved");
    CHECK(!other.finish(submission,3),"return/run-mismatch");
    CHECK(other.counters().reserved==0&&other.counters().return_scope_mismatch==1,"return/run-mismatch-no-slot-write");
    t.close();errno=E2BIG;CHECK(t.finish(submission,submission.values.task_no),"return/after-close");
    CHECK(errno==E2BIG&&t.read_return(0,result),"return/errno-and-ready");
    CHECK(result.native_result==UINT64_C(0x1234567887654321)&&result.relation==TC_RETURN_SAME_NONZERO_VALUE,"return/full-width");
    CHECK(!t.finish(submission,0),"return/duplicate");
    CHECK(t.read_return(0,result)&&result.native_result==submission.values.task_no,"return/immutable-after-duplicate");
    tvt::Transport<8> w(43);CHECK(w.start(),"value/wrap-start");
    const std::uint64_t numbers[]={0,UINT64_MAX,1,UINT64_MAX};
    tc_context cs[4];
    for (unsigned i=0;i<4;i++) { Task a(numbers[i]);CHECK(begin(w,cs[i],a),"value/number-captured");CHECK(cs[i].values.task_no==numbers[i]&&cs[i].invocation_id==i+1,"value/native-number-not-observer-id"); }
    CHECK(w.finish(cs[0],0)&&w.read_return(0,result)&&result.relation==TC_RETURN_ZERO,"value/zero-remains-zero");
    CHECK(w.finish(cs[1],1)&&w.read_return(1,result)&&result.relation==TC_RETURN_VALUE_MISMATCH,"value/wrap-mismatch");
    CHECK(cs[1].values.task_no==cs[3].values.task_no&&cs[1].invocation_id!=cs[3].invocation_id,"value/duplicate-native-number-distinct-invocations");
}
static void entry_hole_and_gate_race() {
    tvt::Transport<3> t(51);CHECK(t.start(),"hole/start");
    std::atomic<bool> held{false},release{false};tc_context first{},second{};
    std::thread a([&]{ Task task(100);hook=[&](unsigned p,const void*,std::uint32_t i){if(at(p,tvt::HookPoint::Reserved)&&i==0){held.store(true,std::memory_order_release);wait_for([&]{return release.load(std::memory_order_acquire);});}};CHECK(begin(t,first,task),"hole/first-success");hook={}; });
    wait_for([&]{return held.load(std::memory_order_acquire);});Task next(200);
    CHECK(begin(t,second,next),"hole/later-success");tc_context read{};
    CHECK(!t.read_entry(0,read),"hole/writing-unreadable");
    CHECK(t.read_entry(1,read)&&read.values.task_no==200&&read.invocation_id==2,"hole/later-ready-visible");
    release.store(true,std::memory_order_release);a.join();
    CHECK(t.read_entry(0,read)&&read.values.task_no==100,"hole/revisit-earlier");
    tvt::Transport<2> g(52);CHECK(g.start(),"close-race/start");held=false;release=false;tc_context c{};
    std::thread b([&]{ Task task(300);hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::GatePassed)){held.store(true,std::memory_order_release);wait_for([&]{return release.load(std::memory_order_acquire);});}};CHECK(begin(g,c,task),"close-race/passed-open-may-publish");hook={}; });
    wait_for([&]{return held.load(std::memory_order_acquire);});g.close();
    CHECK(g.counters().reserved==0,"close-race/close-does-not-wait-for-reservation");
    release.store(true,std::memory_order_release);b.join();
    CHECK(g.read_entry(0,read)&&read.values.task_no==300,"close-race/late-entry-visible");
    CHECK(g.finish(c,300),"close-race/late-return-allowed");
    CHECK(!begin(g,second,next),"close-race/new-attempt-gated");
}
static void duplicate_finish_and_return_hole() {
    tvt::Transport<2> t(61);CHECK(t.start(),"duplicate/start");Task task;tc_context c{};CHECK(begin(t,c,task),"duplicate/entry");
    std::atomic<bool> held{false},release{false};
    std::thread a([&]{hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::BeforeReturnPublish)){held.store(true,std::memory_order_release);wait_for([&]{return release.load(std::memory_order_acquire);});}};CHECK(t.finish(c,UINT64_C(0xffffffff00000001)),"duplicate/first-finishes");hook={};});
    wait_for([&]{return held.load(std::memory_order_acquire);});tc_context entry{};tc_return_record r{};
    CHECK(t.read_entry(0,entry)&&tvt::same_context(entry,c),"duplicate/entry-independent");
    CHECK(!t.read_return(0,r),"duplicate/return-writing-hidden");
    CHECK(!t.finish(c,2),"duplicate/concurrent-after-rejected");
    t.close();release.store(true,std::memory_order_release);a.join();
    CHECK(t.read_return(0,r)&&r.native_result==UINT64_C(0xffffffff00000001),"duplicate/first-result-retained");
    CHECK(t.counters().returns==1&&t.counters().return_duplicate==1,"duplicate/one-winner");
}
static void reservation_contention(bool last) {
    tvt::Transport<2> t(last?72:71);CHECK(t.start(),"reservation/start");Task task;tc_context initial{};
    if(last) CHECK(begin(t,initial,task),"reservation/fill-first");
    std::atomic<unsigned> waiting{0},success{0},sum_reads{0};std::atomic<bool> release{false};
    auto worker=[&]{Task local;tc_context c{};hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::BeforeReserve)){waiting.fetch_add(1,std::memory_order_release);wait_for([&]{return release.load(std::memory_order_acquire);});}};
        const bool ok=begin(t,c,local);if(ok) success.fetch_add(1,std::memory_order_relaxed);
        CHECK(ok ? c.invocation_id!=0 : c.invocation_id==0,"reservation/rejected-context-unqualified");
        sum_reads.fetch_add(reads,std::memory_order_relaxed);hook={};};
    std::thread a(worker),b(worker);wait_for([&]{return waiting.load(std::memory_order_acquire)==2;});release.store(true,std::memory_order_release);a.join();b.join();
    const auto counts=t.counters();CHECK(success==1&&sum_reads==5,"reservation/single-try-one-winner-no-loser-read");
    CHECK(last?(counts.capacity==1&&counts.contention==0&&counts.reserved==2):(counts.contention==1&&counts.capacity==0&&counts.reserved==1),"reservation/failure-classification");
    if(last) { reads=0;tc_context c{};CHECK(!t.begin(&c,TC_SUBMISSION,TC_SUBMIT_LR,reinterpret_cast<void*>(8),1)&&reads==0&&c.invocation_id==0,"reservation/full-no-read");CHECK(t.finish(initial,1),"reservation/return-does-not-free");CHECK(!begin(t,c,task)&&t.counters().reserved==2,"reservation/no-recycling"); }
}
static void concurrent_publication() {
    constexpr unsigned n=16;tvt::Transport<n> t(81);CHECK(t.start(),"concurrent/start");
    std::atomic<unsigned> held{0};std::atomic<bool> release{false};
    std::atomic<bool> ack_entry[n]{},ack_return[n]{};std::vector<std::thread> producers;
    for(unsigned k=0;k<n;k++) {
        producers.emplace_back([&,k]{Task task(1000+k,2000+k);tc_context c{};
            hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::Reserved)){held.fetch_add(1,std::memory_order_release);wait_for([&]{return release.load(std::memory_order_acquire);});}};
            CHECK(begin(t,c,task,3000+k),"concurrent/begin");hook={};CHECK(reads==5,"concurrent/exact-entry-reads");
            wait_for([&]{return ack_entry[k].load(std::memory_order_acquire);});
            CHECK(t.finish(c,UINT64_C(0x100000000)+k),"concurrent/finish");
            wait_for([&]{return ack_return[k].load(std::memory_order_acquire);});
        });
        // Reservation order is deliberately scheduled; payload and return writes
        // run concurrently. Slot readiness alone publishes payload to the reader.
        wait_for([&]{return held.load(std::memory_order_acquire)==k+1;});
    }
    release.store(true,std::memory_order_release);
    bool entries[n]{},returns[n]{};unsigned done_entries=0,done_returns=0;
    wait_for([&]{
        for(unsigned i=0;i<n;i++) {
            tc_context c{};tc_return_record r{};
            if(!entries[i]&&t.read_entry(i,c)) {
                CHECK(c.invocation_id==i+1&&c.run_id==81&&c.thread_observation==3000+i,"concurrent/entry-identity");
                CHECK(c.values.task_no==1000+i&&c.values.id==2000+i&&c.values.delta_bits==UINT32_MAX&&c.values.cumulative_bits==0x80000000,"concurrent/entry-payload");
                entries[i]=true;++done_entries;ack_entry[i].store(true,std::memory_order_release);
            }
            if(!returns[i]&&t.read_return(i,r)) {
                CHECK(r.entry.invocation_id==i+1&&r.entry.values.task_no==1000+i&&r.native_result==UINT64_C(0x100000000)+i,"concurrent/return-payload");
                CHECK(r.relation==TC_RETURN_VALUE_MISMATCH,"concurrent/return-not-completion");
                returns[i]=true;++done_returns;ack_return[i].store(true,std::memory_order_release);
            }
        }
        return done_entries==n&&done_returns==n;
    });
    for(auto& p:producers)p.join();auto counts=t.counters();
    CHECK(counts.reserved==n&&counts.entries==n&&counts.returns==n,"concurrent/quiescent-counts");
}
static void unmapped_after_and_nested() {
    tvt::Transport<4> t(91);CHECK(t.start(),"lifetime/start");
    const long page=sysconf(_SC_PAGESIZE);void* p=mmap(nullptr,static_cast<std::size_t>(page),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(p!=MAP_FAILED,"lifetime/mmap");if(p==MAP_FAILED)return;
    Task task;std::memcpy(p,task.bytes,56);native_base=static_cast<unsigned char*>(p);reads=0;tc_context outer{},inner{};
    CHECK(t.begin(&outer,TC_SUBMISSION,TC_SUBMIT_LR,p,1),"lifetime/outer-entry");
    CHECK(begin(t,inner,task,2),"lifetime/nested-entry-without-guard");
    CHECK(munmap(p,static_cast<std::size_t>(page))==0,"lifetime/unmap-before-return");native_base=nullptr;reads=0;
    CHECK(t.finish(inner,9)&&t.finish(outer,8),"lifetime/reverse-return-order");
    CHECK(reads==0&&outer.invocation_id!=inner.invocation_id,"lifetime/no-native-after-read");
    tc_return_record r{};CHECK(t.read_return(0,r)&&r.native_result==8&&r.entry.values.task_no==UINT64_C(0x1234567887654321),"lifetime/value-survives-unmap");
}
// Serial, reentrant polling schedules used also by isolated strict controls.
static void publication_controls() {
    tvt::Transport<1> t(101);CHECK(t.start(),"control/start");Task task;tc_context c{};
    hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::BeforeEntryCopy)){tc_context read{};CHECK(!t.read_entry(0,read),"control/entry-must-wait-for-copy");}};
    CHECK(begin(t,c,task),"control/begin");hook={};
    hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::BeforeReturnCopy)){tc_return_record r{};CHECK(!t.read_return(0,r),"control/return-must-wait-for-copy");}};
    CHECK(t.finish(c,UINT64_C(0xabcdef0112345678)),"control/finish");hook={};tc_return_record r{};
    CHECK(t.read_return(0,r)&&r.native_result==UINT64_C(0xabcdef0112345678),"control/full-result");
    CHECK(!t.finish(c,3),"control/duplicate-rejected");
    CHECK(t.read_return(0,r)&&r.native_result==UINT64_C(0xabcdef0112345678),"control/duplicate-cannot-rewrite");
}
int main(int argc,char**) {
    publication_controls();
    if(argc==1) { gate_and_rejection();returns_and_value_limits();entry_hole_and_gate_race();duplicate_finish_and_return_hole();reservation_contention(false);reservation_contention(true);concurrent_publication();unmapped_after_and_nested(); }
    CHECK(bad_reads==0,"read/no-native-pointer-graph-read");
    std::printf("checks=%u failures=%u scenarios=%u\n",checks.load(),failures.load(),argc==1?9:1);
    return failures.load()?1:0;
}
