// SPDX-License-Identifier: GPL-3.0-only
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <functional>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
static void test_hook(unsigned,const void*,std::uint32_t);
static void tracked_read(void*,const void*,std::size_t);
#define TVT_TEST_POINT(point,transport,index) test_hook(static_cast<unsigned>(point),(transport),(index))
#define TC_READ_BYTES(dst,src,len) tracked_read((dst),(src),(len))
#define AZTASK_TEST
#include "../observer.cpp"
static std::atomic<unsigned> checks{0},failures{0},reads{0},bad_reads{0},tid_calls{0};
static thread_local const unsigned char* native_base=nullptr;
static thread_local long tid_value=777;
static thread_local std::function<void(unsigned,const void*,std::uint32_t)> hook;
static unsigned claims=0,starts=0,fw_reads=0,static_reads=0,logs=0,entry_logs=0,return_logs=0;
static unsigned claim_fail=0,claim_empty=0,fatal_claim=0,static_bad=0,sleep_calls=0;
static bool start_fail=false,start_fatal=false,log_suppress=false,log_reenter=false,init_reenter=false,sleep_eintr=false;
static const char* tag=eto::FIRMWARE;
static az_core_v1 core_fixture{};
static std::vector<std::string> lines;
static void check(bool ok,const char* name){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",name);}}
#define CHECK(x,name) check((x),(name))
static void test_hook(unsigned p,const void* t,std::uint32_t i){if(hook)hook(p,t,i);}
static bool at(unsigned p,tvt::HookPoint x){return p==static_cast<unsigned>(x);}
static void tracked_read(void* dst,const void* src,std::size_t n){
    const auto off=reinterpret_cast<uintptr_t>(src)-reinterpret_cast<uintptr_t>(native_base);
    if(!native_base||!((off==0&&n==8)||(off==0x10&&n==8)||(off==0x20&&n==8)||(off==0x28&&n==4)||(off==0x2c&&n==4)))++bad_reads;
    ++reads;std::memcpy(dst,src,n);errno=EILSEQ;
}
struct alignas(8) Task{unsigned char bytes[56];explicit Task(uint64_t no=0x12345678abcdef01ULL){
 std::memset(bytes,0xa5,56);const uint64_t v=TC_TASK_VPTR,id=0x8000000100000004ULL;const uint32_t d=0xffffffff,c=0x80000000;
 std::memcpy(bytes,&v,8);std::memcpy(bytes+0x10,&no,8);std::memcpy(bytes+0x20,&id,8);std::memcpy(bytes+0x28,&d,4);std::memcpy(bytes+0x2c,&c,4);
}};
static TaskCallContext before(Task& task,uintptr_t lr=TC_SUBMIT_LR){native_base=task.bytes;TaskCallContext c{};az_task_call_before(&c,0,reinterpret_cast<uintptr_t>(task.bytes),lr);native_base=nullptr;return c;}
static void execute(Task& task,uintptr_t lr=TC_EXECUTE_LR){native_base=task.bytes;az_task_exec_observe(reinterpret_cast<uintptr_t>(task.bytes),lr);native_base=nullptr;}
static bool zero_context(const TaskCallContext& c){TaskCallContext zero{};return !std::memcmp(&zero,&c,sizeof c);}
extern "C" void az_task_call_entry(void){}
extern "C" void az_task_exec_entry(void){}
static void native_submit_stub(){} static void native_execute_stub(){}
namespace eto {
static void test_static_read(void* out,uintptr_t a,size_t n){
 ++static_reads;
 if(a==SUBMIT_SLOT&&n==8)std::memcpy(out,&SUBMIT_ENTRY,n);
 else if(a==EXECUTE_SLOT&&n==8)std::memcpy(out,&EXECUTE_ENTRY,n);
 else if(a==SUBMIT_ENTRY&&n==16)std::memcpy(out,SUBMIT_WORDS,n);
 else if(a==EXECUTE_ENTRY&&n==16)std::memcpy(out,EXECUTE_WORDS,n);
 else{CHECK(false,"static/unexpected-range");std::memset(out,0,n);}
 if(static_bad==static_reads)static_cast<unsigned char*>(out)[0]^=1;
}
static long test_tid(void){++tid_calls;errno=ESRCH;return tid_value;}
static int test_sleep(struct timespec*){++sleep_calls;errno=sleep_eintr&&sleep_calls==1?EINTR:EIO;return -1;}
}
static const char* fw(){++fw_reads;return tag;}
static void logger(const char*,const char* format,...){
 ++logs;char buffer[1024];va_list a;va_start(a,format);std::vsnprintf(buffer,sizeof buffer,format,a);va_end(a);
 if(!std::strncmp(format,"ETO entry ",10))++entry_logs;
 if(!std::strncmp(format,"ETO return ",11))++return_logs;
 if(!log_suppress)lines.emplace_back(buffer);
 if(log_reenter&&!std::strncmp(format,"ETO entry ",10)){log_reenter=false;Task task(44);auto c=before(task);az_task_call_after(&c,44);}
 errno=ENOSPC;
}
static void invoke_during_claim(){
 TaskCallContext c;std::memset(&c,0xa5,sizeof c);const unsigned before_tid=tid_calls,before_reads=reads;
 errno=EDOM;az_task_call_before(&c,0,1,TC_SUBMIT_LR);az_task_exec_observe(1,TC_EXECUTE_LR);az_task_call_after(&c,42);
 CHECK(errno==EDOM&&zero_context(c),"claim/early-helper-unqualified-errno");
 CHECK(tid_calls==before_tid&&reads==before_reads,"claim/early-helper-no-tid-task-read");
}
static int start_thread(const char* id,const char*,void*(*fn)(void*),void* arg){
 CHECK(!std::strcmp(id,eto::ID)&&fn==eto::worker&&!arg,"init/worker-api");CHECK(!claims,"init/worker-before-claims");++starts;
 eto::WorkerState w;bool all_pending=true;for(unsigned i=0;i<125;++i)all_pending=eto::worker_cycle(w)&&all_pending;CHECK(all_pending,"init/pending-worker-stays");
 CHECK(w.polls==0&&w.quiet==0&&eto::state==eto::Installing,"init/pending-worker-no-close");
 if(init_reenter)CHECK(task_observer_init(&core_fixture)==-8,"init/reentrant-init-refused");
 if(start_fatal)fn(arg);return start_fail?-1:0;
}
static int claim(const char* id,uintptr_t address,void* entry,void** original){
 ++claims;CHECK(!std::strcmp(id,eto::ID)&&starts==1,"claim/after-worker");
 CHECK(eto::state==eto::Installing,"claim/not-ready");
 CHECK(claims==1?(address==eto::SUBMIT_ENTRY&&entry==reinterpret_cast<void*>(az_task_call_entry)&&original==&az_task_call_original):
 (address==eto::EXECUTE_ENTRY&&entry==reinterpret_cast<void*>(az_task_exec_entry)&&original==&az_task_exec_original),"claim/direct-permanent-original-slot");
 if(init_reenter)CHECK(task_observer_init(&core_fixture)==-8,"claim/reentrant-init-refused");
 eto::WorkerState w;CHECK(eto::worker_cycle(w)&&w.polls==0,"claim/worker-in-progress");
 if(claim_fail==claims)return -1;
 if(claim_empty!=claims)__atomic_store_n(original,claims==1?reinterpret_cast<void*>(native_submit_stub):reinterpret_cast<void*>(native_execute_stub),__ATOMIC_RELEASE);
 invoke_during_claim();
 if(fatal_claim==claims)eto::worker(nullptr);
 return 0;
}
static az_core_v1 mock_core(){az_core_v1 c{};c.abi=AZ_MOD_ABI;c.size=sizeof c;c.fw_tag=fw;c.log=logger;c.thread_start=start_thread;c.claim_hook=claim;return c;}
template<class P>static void wait_for(P p){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!p()){if(std::chrono::steady_clock::now()>deadline)std::abort();std::this_thread::yield();}}
static void successful_init(){errno=E2BIG;CHECK(task_observer_init(&core_fixture)==0,"init/success");CHECK(errno==E2BIG,"init/errno");CHECK(claims==2&&starts==1&&static_reads==4,"init/exact-preflight-claims");CHECK(eto::state==eto::Ready,"init/ready");CHECK(task_observer_init(&core_fixture)==-8,"init/one-shot");}
static void gates(){
 successful_init();TaskCallContext c;std::memset(&c,0xa5,sizeof c);unsigned n=tid_calls,r=reads;
 errno=ERANGE;az_task_call_before(&c,0,1,TC_SUBMIT_LR+4);az_task_exec_observe(1,TC_EXECUTE_LR+4);
 CHECK(zero_context(c)&&tid_calls==n&&reads==r&&errno==ERANGE,"helper/wrong-caller-no-reads");
 Task task;tid_value=-1;c=before(task);CHECK(zero_context(c)&&reads==r,"helper/failed-tid-no-task-read");
 tid_value=0;c=before(task);CHECK(zero_context(c)&&reads==r,"helper/zero-tid-no-task-read");
 tid_value=777;errno=EDOM;c=before(task);CHECK(c.invocation_id==1&&reads==r+5&&errno==EDOM,"helper/copied-entry-errno");
 execute(task);tc_context e{};auto* t=eto::published.load();CHECK(t->read_entry(1,e)&&e.phase==TC_EXECUTION,"helper/execution-copy");
 eto::close_capture();n=tid_calls;r=reads;TaskCallContext closed=before(task);execute(task);
 CHECK(zero_context(closed)&&tid_calls==n&&reads==r,"helper/closed-before-tid-read");
 errno=EILSEQ;az_task_call_after(&c,0x12345678abcdef01ULL);tc_return_record ret{};
 CHECK(t->read_return(0,ret)&&ret.native_result==0x12345678abcdef01ULL&&errno==EILSEQ,"helper/late-value-only-after-close");
 CHECK(!t->read_return(1,ret),"helper/execution-no-return");
 az_task_call_after(&c,2);CHECK(t->read_return(0,ret)&&ret.native_result==0x12345678abcdef01ULL,"helper/duplicate-no-rewrite");
 CHECK(!t->start(),"helper/no-rearm");
}
static void worker_records(bool suppressed){
 successful_init();eto::WorkerState w;Task task;auto c=before(task);execute(task);log_suppress=suppressed;unsigned base=logs;
 CHECK(eto::worker_cycle(w)&&w.entry_count==2&&w.return_count==0,"worker/copies-entry-independently");
 CHECK(w.log_attempts==2&&logs==base+2,"worker/attempt-not-delivery");
 for(unsigned i=0;i<6;++i)eto::worker_cycle(w);
 CHECK(eto::state==eto::Closed&&w.close_attempted,"worker/quiet-close-permanent");
 task_observer_tick();eto::worker_cycle(w);CHECK(eto::state==eto::Closed,"worker/heartbeat-no-rearm");
 az_task_call_after(&c,0);CHECK(eto::worker_cycle(w)&&w.return_count==1,"worker/late-return-copied");
 const unsigned attempts=w.log_attempts;for(unsigned i=0;i<20;++i)eto::worker_cycle(w);
 CHECK(w.log_attempts==attempts&&w.entry_count==2&&w.return_count==1,"worker/copied-records-not-retried");
 CHECK(eto::worker_cycles>20&&eto::worker_exit_reason==eto::WorkerRunning,"worker/closed-still-scanning");
 if(suppressed)CHECK(lines.size()==1,"worker/suppression-delivery-unknown");
 else{CHECK(entry_logs==2&&return_logs==1,"worker/one-attempt-each");for(const auto& line:lines)CHECK(line.find("task-address")==std::string::npos&&line.find("dispatcher")==std::string::npos,"worker/no-native-pointer-fields");}
}
static void poll_budget(){successful_init();eto::WorkerState w;for(unsigned i=0;i<eto::POLL_BUDGET;++i){task_observer_tick();eto::worker_cycle(w);}CHECK(eto::state==eto::Closed&&w.polls==eto::POLL_BUDGET&&w.quiet==0,"worker/poll-budget-not-wall-clock");}
static void fatal_worker(){successful_init();sleep_eintr=true;Task task;auto c=before(task);eto::worker(nullptr);CHECK(sleep_calls==2&&eto::state==eto::Closed&&eto::worker_exit_reason==eto::WorkerSleepFailed,"worker/eintr-then-fatal-close");az_task_call_after(&c,4);tc_return_record r{};CHECK(eto::published.load()->read_return(0,r)&&r.native_result==4,"worker/fatal-preserves-late-record");}
static void logging_limits(){
 successful_init();eto::WorkerState w;Task task;
 for(unsigned i=0;i<6;++i){tid_value=0;(void)before(task);task_observer_tick();eto::worker_cycle(w);}
 CHECK(w.diagnostics==eto::DIAGNOSTIC_BUDGET,"worker/diagnostic-attempt-limit");tid_value=777;
 for(unsigned i=0;i<16;++i){auto c=before(task);az_task_call_after(&c,i);}
 eto::worker_cycle(w);CHECK(eto::state==eto::Closed&&w.entry_count==16&&w.return_count==16,"worker/capacity-close-copies-all");
 CHECK(w.log_attempts==37,"worker/total-attempt-bound");const unsigned n=logs;
 for(unsigned i=0;i<30;++i)eto::worker_cycle(w);CHECK(logs==n,"worker/no-budget-retry");
}
static void reentry(){
 init_reenter=true;successful_init();Task task;TaskCallContext inner{};bool inside=false;
 hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::BeforeEntryPublish)&&!inside){inside=true;inner=before(task);az_task_call_after(&inner,2);inside=false;}};
 auto outer=before(task);hook={};az_task_call_after(&outer,1);CHECK(outer.invocation_id==1&&inner.invocation_id==2,"helper/reentry-private-contexts");
 eto::WorkerState w;log_reenter=true;eto::worker_cycle(w);CHECK(w.entry_count==3&&w.return_count==3,"worker/log-reentry-later-slot");
 CHECK(bad_reads==0,"helper/reentry-bounded-copy");
}
static void hole(){
 successful_init();std::atomic<bool> waiting{false},release{false};TaskCallContext first{};
 std::thread p([&]{Task task(10);hook=[&](unsigned h,const void*,std::uint32_t index){if(at(h,tvt::HookPoint::Reserved)&&index==0){waiting.store(true,std::memory_order_release);wait_for([&]{return release.load(std::memory_order_acquire);});}};first=before(task);hook={};});
 wait_for([&]{return waiting.load(std::memory_order_acquire);});Task next(20);auto second=before(next);az_task_call_after(&second,20);
 eto::WorkerState w;eto::worker_cycle(w);CHECK(!w.entry_copied[0]&&w.entry_copied[1]&&!w.return_copied[0]&&w.return_copied[1],"worker/skips-unpublished-hole");
 release.store(true,std::memory_order_release);p.join();eto::close_capture();az_task_call_after(&first,10);eto::worker_cycle(w);
 CHECK(w.entry_count==2&&w.return_count==2&&w.entry_copied[0]&&w.return_copied[0],"worker/revisits-hole-after-close");
}
static void concurrent(){
 successful_init();constexpr unsigned count=8;std::atomic<unsigned> held{0};std::atomic<bool> release{false},ack[count]{};std::vector<std::thread> ps;
 for(unsigned k=0;k<count;++k){ps.emplace_back([&,k]{Task task(100+k);hook=[&](unsigned p,const void*,std::uint32_t){if(at(p,tvt::HookPoint::Reserved)){held.fetch_add(1,std::memory_order_release);wait_for([&]{return release.load(std::memory_order_acquire);});}};auto c=before(task);hook={};CHECK(c.invocation_id==k+1,"concurrent/unique-context");az_task_call_after(&c,100+k);wait_for([&]{return ack[k].load(std::memory_order_acquire);});});wait_for([&]{return held.load(std::memory_order_acquire)==k+1;});}
 release.store(true,std::memory_order_release);eto::WorkerState w;
 wait_for([&]{task_observer_tick();eto::worker_cycle(w);for(unsigned i=0;i<count;++i)if(w.return_copied[i])ack[i].store(true,std::memory_order_release);return w.return_count==count;});
 for(auto& p:ps)p.join();CHECK(w.entry_count==count&&w.return_count==count&&reads==count*5,"concurrent/actual-helper-worker-publication");
 CHECK(entry_logs==count&&return_logs==count&&bad_reads==0,"concurrent/bounded-logging");
}
static void unmap_case(){
 successful_init();Task task;size_t size=static_cast<size_t>(sysconf(_SC_PAGESIZE));void* p=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(p!=MAP_FAILED,"lifetime/map");if(p==MAP_FAILED)return;
 std::memcpy(p,task.bytes,56);native_base=static_cast<unsigned char*>(p);TaskCallContext c{};az_task_call_before(&c,0,reinterpret_cast<uintptr_t>(p),TC_SUBMIT_LR);native_base=nullptr;
 CHECK(munmap(p,size)==0,"lifetime/unmap");az_task_call_after(&c,0xabcdef0199999999ULL);eto::WorkerState w;eto::worker_cycle(w);
 CHECK(reads==5&&w.entry_count==1&&w.return_count==1,"lifetime/no-task-after-read");
}
int main(int argc,char** argv){
 if(argc!=2)return 2;const std::string s=argv[1];core_fixture=mock_core();setenv("AZ_ENCODER_TASK_OBSERVER_EMULATOR","1",1);setenv("AZ_ENCODER_TASK_OBSERVER_RUN_ID","12345",1);
 int expected=0;bool failure_case=true;
 if(s=="env-missing"){unsetenv("AZ_ENCODER_TASK_OBSERVER_EMULATOR");expected=-9;}
 else if(s=="env-wrong"){setenv("AZ_ENCODER_TASK_OBSERVER_EMULATOR","true",1);expected=-9;}
 else if(s.rfind("run-",0)==0&&s!="run-max-valid"){const char* value=s=="run-zero"?"0":s=="run-sign"?"+1":s=="run-space"?" 1":s=="run-tail"?"1x":s=="run-overflow"?"18446744073709551616":"000000000000000000001";setenv("AZ_ENCODER_TASK_OBSERVER_RUN_ID",value,1);expected=-9;}
 else if(s=="sdk-null-core"){expected=-10;}
 else if(s=="sdk-abi"){core_fixture.abi=AZ_MOD_ABI+1;expected=-10;}
 else if(s=="sdk-null-fw"){core_fixture.fw_tag=nullptr;expected=-10;}
 else if(s=="sdk-null-claim"){core_fixture.claim_hook=nullptr;expected=-10;}
 else if(s=="sdk-null-thread"){core_fixture.thread_start=nullptr;expected=-10;}
 else if(s=="sdk-negative"){core_fixture.size=-1;expected=-10;}
 else if(s=="sdk-short"){core_fixture.size=offsetof(az_core_v1,thread_start);expected=-10;}
 else if(s=="sdk-null-log"){core_fixture.log=nullptr;expected=-10;}
 else if(s=="firmware-null"){tag=nullptr;expected=-10;}
 else if(s=="firmware-wrong"){tag="wrong";expected=-10;}
 else if(s.rfind("static-",0)==0){static_bad=static_cast<unsigned>(s.back()-'0');expected=-12;}
 else if(s=="thread-failure"){start_fail=true;expected=-13;}
 else if(s=="thread-fatal"){start_fatal=true;expected=-23;}
 else if(s=="first-failure"){claim_fail=1;expected=-20;}
 else if(s=="second-failure"){claim_fail=2;expected=-21;}
 else if(s=="first-empty"){claim_empty=1;expected=-20;}
 else if(s=="second-empty"){claim_empty=2;expected=-21;}
 else if(s=="first-worker-fatal"){fatal_claim=1;expected=-23;}
 else if(s=="second-worker-fatal"){fatal_claim=2;expected=-23;}
 else failure_case=false;
 if(failure_case){
  errno=EDOM;CHECK(task_observer_init(s=="sdk-null-core"?nullptr:&core_fixture)==expected,"failure/expected-code");CHECK(errno==EDOM&&eto::state==eto::Failed,"failure/closed-errno");
  CHECK(task_observer_init(&core_fixture)==-8,"failure/no-retry");invoke_during_claim();eto::WorkerState w;CHECK(!eto::worker_cycle(w),"failure/worker-exits");
  CHECK(!eto::published.load()||eto::published.load()->counters().reserved==0,"failure/no-reservation");
  if(expected==-9||expected==-10||expected==-12)CHECK(!claims&&!starts,"failure/no-installation");
  if(s=="second-failure"||s=="second-empty"||s=="first-worker-fatal"||s=="second-worker-fatal")CHECK(az_task_call_original==reinterpret_cast<void*>(native_submit_stub),"failure/first-original-retained");
  if(s=="second-worker-fatal")CHECK(az_task_exec_original==reinterpret_cast<void*>(native_execute_stub),"failure/second-original-retained");
 }else if(s=="gates")gates();else if(s=="run-max-valid"){setenv("AZ_ENCODER_TASK_OBSERVER_RUN_ID","18446744073709551615",1);gates();}else if(s=="worker")worker_records(false);else if(s=="suppressed-log")worker_records(true);else if(s=="budget")poll_budget();else if(s=="fatal-worker")fatal_worker();else if(s=="logging-limits")logging_limits();else if(s=="reentry")reentry();else if(s=="hole")hole();else if(s=="concurrent")concurrent();else if(s=="unmap")unmap_case();else return 2;
 CHECK(bad_reads==0,"all/no-unexpected-native-read");std::printf("scenario=%s checks=%u failures=%u\n",s.c_str(),checks.load(),failures.load());return failures?1:0;
}
