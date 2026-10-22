// SPDX-License-Identifier: GPL-3.0-only
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>
#include <unwind.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdint>
#include <cstddef>
using U=std::uint64_t;
static thread_local const unsigned char *read_base=nullptr;
static std::atomic<unsigned> reads{0},forbidden{0};
static void tracked_read(void *dst,const void *src,std::size_t n) {
 const U p=reinterpret_cast<U>(src),b=reinterpret_cast<U>(read_base);
 const U off=p>=b?p-b:UINT64_MAX;
 if(!((off==0&&n==8)||(off==0x10&&n==8)||(off==0x20&&n==8)||
      (off==0x28&&n==4)||(off==0x2c&&n==4)))++forbidden;
 ++reads;std::memcpy(dst,src,n);
}
#define TC_READ_BYTES tracked_read
#include "../task-value-transport/transport.h"
#include "../task-call-return-abi/context.h"
static_assert(sizeof(tc_context)==sizeof(TaskCallContext),"context size reuse");
static_assert(offsetof(tc_context,values)==offsetof(TaskCallContext,values)&&
 offsetof(tc_context,run_id)==offsetof(TaskCallContext,run_id)&&
 offsetof(tc_context,invocation_id)==offsetof(TaskCallContext,invocation_id)&&
 offsetof(tc_context,thread_observation)==offsetof(TaskCallContext,thread_observation)&&
 offsetof(tc_context,phase)==offsetof(TaskCallContext,phase)&&
 offsetof(tc_context,status)==offsetof(TaskCallContext,status)&&
 offsetof(tc_context,reserved)==offsetof(TaskCallContext,reserved),"all context offsets");
static_assert(sizeof(tc_values)==sizeof(TaskValues),"payload size");
static_assert(offsetof(tc_values,task_no)==offsetof(TaskValues,taskno)&&
 offsetof(tc_values,id)==offsetof(TaskValues,id)&&
 offsetof(tc_values,delta_bits)==offsetof(TaskValues,delta_bits)&&
 offsetof(tc_values,cumulative_bits)==offsetof(TaskValues,cumulative_bits),"payload offsets");
// context.h declares old C++ names only; copy between live object types using memcpy.
using Transport=tvt::Transport<16>;
static Transport *active=nullptr;
struct alignas(8) Task {unsigned char bytes[56];};
struct alignas(16) Registers {U x[19],lr,sp,nzcv,fpcr,fpsr;U q[8][2];};
static_assert(sizeof(Registers)==320&&offsetof(Registers,q)==192,"register fixture offsets");
extern "C" {
void *az_task_call_original=nullptr,*az_task_exec_original=nullptr;
Registers fixture_register_capture{};
U fixture_expected_registers[19]{},fixture_expected_sp=0;
alignas(16) U fixture_expected_vectors[8][2]{};
U fixture_submit(U,const void*);U fixture_submit_bad(U,const void*);
U fixture_execute(const void*);U fixture_execute_bad(const void*);
U fixture_invoke_registers();void fixture_register_target();
extern char fixture_submit_lr[],fixture_submit_bad_lr[],fixture_execute_lr[],fixture_execute_bad_lr[],fixture_invoke_registers_lr[];
extern char az_task_exec_entry[],az_task_exec_entry_end[],fixture_invoke_registers_end[];
}
static unsigned checks=0,failures=0,scenarios=0;
static const char *scenario="init";
#define CHECK(x,name) do{++checks;if(!(x)){++failures;std::printf("FAIL %s/%s\n",scenario,name);}}while(0)
static std::atomic<unsigned> before_count{0},after_count{0},exec_observe_count{0},native_submit_count{0},native_exec_count{0};
static std::atomic<unsigned> helper_bad{0},order_index{0};
struct Event {unsigned kind;tc_context ctx;U actual_lr,result;bool accepted;};
static Event events[512];
static U last_exec_lr=0,last_exec_task=0;
static unsigned trace_mask=0,trace_stage=0;
static U trace_ips[3][64];static unsigned trace_count[3]{};
static _Unwind_Reason_Code trace_frame(_Unwind_Context*c,void*) noexcept {
 if(trace_count[trace_stage]==64)return _URC_END_OF_STACK;
 trace_ips[trace_stage][trace_count[trace_stage]++]=_Unwind_GetIP(c);return _URC_NO_REASON;
}
static void trace(unsigned stage){if(trace_mask&(1u<<stage)){trace_stage=stage;_Unwind_Backtrace(trace_frame,nullptr);}}
static bool trace_has(unsigned stage,U low,U high){for(unsigned i=0;i<trace_count[stage];++i)if(trace_ips[stage][i]>=low&&trace_ips[stage][i]<high)return true;return false;}
static void event(unsigned kind,const tc_context&ctx,U lr,U result,bool accepted) noexcept {
 const unsigned i=order_index.fetch_add(1);if(i>=512)std::terminate();events[i]={kind,ctx,lr,result,accepted};
}
static U mapped_lr(U lr,unsigned phase) noexcept {
 // Explicit fixture-only mapping. Unknown labels stay unknown, never a catch-all success.
 if(phase==TC_SUBMISSION&&lr==reinterpret_cast<U>(fixture_submit_lr))return TC_SUBMIT_LR;
 if(phase==TC_EXECUTION&&lr==reinterpret_cast<U>(fixture_execute_lr))return TC_EXECUTE_LR;
 return 0;
}
static void init_task(Task&task,U no,U id=0xabcdef0187654321ULL,std::uint32_t delta=0xffffffffu,std::uint32_t cumulative=0x80000000u){
 std::memset(&task,0xa5,sizeof task);const U vptr=TC_TASK_VPTR;
 std::memcpy(task.bytes,&vptr,8);std::memcpy(task.bytes+0x10,&no,8);std::memcpy(task.bytes+0x20,&id,8);
 std::memcpy(task.bytes+0x28,&delta,4);std::memcpy(task.bytes+0x2c,&cumulative,4);
}
static thread_local bool helper_nested=false;
static bool reenter_before=false,reenter_after=false;
static U nested_result=0;
static constexpr U HIGH=0xfedcba9876543210ULL;
extern "C" void az_task_call_before(TaskCallContext *raw,U box,U task,U lr) noexcept {
 const int saved=errno;const TaskCallContext zero{};
 if(std::memcmp(raw,&zero,sizeof zero))++helper_bad;
 ++before_count;tc_context c{};read_base=reinterpret_cast<const unsigned char*>(task);
 const bool accepted=active->begin(&c,TC_SUBMISSION,mapped_lr(lr,TC_SUBMISSION),reinterpret_cast<const void*>(task),17);read_base=nullptr;
 std::memcpy(raw,&c,sizeof c);event(1,c,lr,box,accepted);trace(0);
 if(reenter_before&&!helper_nested){helper_nested=true;Task nested;init_task(nested,HIGH-1);errno=41;nested_result=fixture_submit(0,&nested);helper_nested=false;}
 errno=91; (void)saved; // Actual submission assembly must restore caller errno.
}
extern "C" void az_task_call_after(const TaskCallContext *raw,U result) noexcept {
 ++after_count;tc_context c;std::memcpy(&c,raw,sizeof c);
 const bool accepted=active->finish(c,result);event(3,c,0,result,accepted);trace(2);
 if(reenter_after&&!helper_nested){helper_nested=true;Task nested;init_task(nested,HIGH-1);errno=41;nested_result=fixture_submit(0,&nested);helper_nested=false;}
 errno=92; // Actual submission assembly must restore native errno.
}
extern "C" void fixture_exec_capture(U task,U lr) noexcept {
 const int saved=errno;++exec_observe_count;last_exec_lr=lr;last_exec_task=task;
 tc_context c{};read_base=reinterpret_cast<const unsigned char*>(task);
 const bool accepted=active->begin(&c,TC_EXECUTION,mapped_lr(lr,TC_EXECUTION),reinterpret_cast<const void*>(task),23);read_base=nullptr;
 event(2,c,lr,0,accepted);trace(1);errno=93;errno=saved;
}
static bool check_midcall=false,mid_submit=false,mid_execute=false;
static unsigned mode=0;static bool do_execute=false,unmap_mode=false,close_mode=false,throw_mode=false;
static std::size_t page_size=0;static int unmapped=-1;
static std::atomic<unsigned> errno_bad{0};
static std::atomic<unsigned> destroyed{0};struct Guard{~Guard(){++destroyed;}};struct NativeError{U token;};
static U native_execute(const void*task){
 ++native_exec_count;if(errno!=41&&errno!=74)++errno_bad;
 if(check_midcall){tc_context c{};tc_return_record r{};mid_execute=active->read_entry(1,c)&&c.phase==TC_EXECUTION&&!active->read_return(0,r);}
 Guard g;if(throw_mode){errno=74;throw NativeError{HIGH};}
 // No task dereference: rejected/null input forwarding is safe in this synthetic callee.
 errno=73;return task?0x8000000000000001ULL:0;
}
static U native_submit(U depth,const void*task){
 ++native_submit_count;if(errno!=41)++errno_bad;Guard guard;
 if(check_midcall){tc_context c{};tc_return_record r{};mid_submit=active->read_entry(0,c)&&c.phase==TC_SUBMISSION&&!active->read_return(0,r);}
 U result=HIGH;
 if(mode==1&&depth){Task nested;init_task(nested,HIGH-depth,77+depth);result=fixture_submit(depth-1,&nested)+1;}
 else if(do_execute){errno=41;(void)fixture_execute(task);}
 if(close_mode)active->close();
 if(unmap_mode)unmapped=munmap(const_cast<void*>(task),page_size);
 if(mode==2)result=0;
 if(mode==3)result=HIGH-17;
 errno=73;return result;
}
template<class F> static void set_target(void **where,F f){__atomic_store_n(where,reinterpret_cast<void*>(f),__ATOMIC_RELEASE);}
static void start(const char*name,Transport&t,bool enabled=true){
 scenario=name;++scenarios;active=&t;if(enabled)CHECK(t.start(),"start-once");
 order_index=0;reads=0;forbidden=0;do_execute=false;unmap_mode=false;close_mode=false;throw_mode=false;mode=0;
 check_midcall=false;mid_submit=false;mid_execute=false;
 reenter_before=false;reenter_after=false;set_target(&az_task_call_original,native_submit);set_target(&az_task_exec_original,native_execute);
}
static bool read_entry(unsigned slot,tc_context&out){return active->read_entry(slot,out);}
static bool read_return(unsigned slot,tc_return_record&out){return active->read_return(slot,out);}
static void ordinary_and_unmap(){
 Transport t(11);start("entry-execution-return",t);Task task;init_task(task,HIGH);unsigned char original[56];std::memcpy(original,&task,56);
 do_execute=true;check_midcall=true;errno=41;const unsigned ns=native_submit_count,ne=native_exec_count;
 CHECK(fixture_submit(0,&task)==HIGH,"full-native-result");CHECK(errno==73,"native-errno");
 CHECK(native_submit_count==ns+1&&native_exec_count==ne+1,"each-native-once");
 CHECK(mid_submit,"submission-entry-visible-before-native-return");CHECK(mid_execute,"execution-entry-visible-before-submission-return");
 CHECK(order_index==3&&events[0].kind==1&&events[1].kind==2&&events[2].kind==3,"execute-before-return-order");
 CHECK(events[0].actual_lr==reinterpret_cast<U>(fixture_submit_lr)&&events[1].actual_lr==reinterpret_cast<U>(fixture_execute_lr),"actual-caller-lrs");
 tc_context a{},b{};tc_return_record r{};CHECK(read_entry(0,a)&&read_entry(1,b)&&read_return(0,r),"independent-records-present");
 CHECK(!read_return(1,r),"executor-no-return-record");CHECK(a.phase==TC_SUBMISSION&&b.phase==TC_EXECUTION,"distinct-phases");
 CHECK(a.invocation_id!=b.invocation_id&&a.run_id==b.run_id,"distinct-observer-invocations");
 CHECK(tc_candidate(&a,&b)==TC_SAME_VALUES_ONLY,"value-only-candidate");CHECK(read_return(0,r)&&r.native_result==HIGH&&r.relation==TC_RETURN_SAME_NONZERO_VALUE,"full-qualified-bits");
 CHECK(a.values.id==0xabcdef0187654321ULL&&a.values.delta_bits==0xffffffffu&&a.values.cumulative_bits==0x80000000u,"exact-payload-widths");
 CHECK(reads==10&&forbidden==0,"only-five-reads-per-capture");CHECK(!std::memcmp(original,&task,56),"native-storage-unmodified");
 Transport u(12);start("unmap-before-return",u);page_size=static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
 void*page=mmap(nullptr,page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(page!=MAP_FAILED,"mapped-task");if(page==MAP_FAILED)std::terminate();
 init_task(*static_cast<Task*>(page),HIGH);do_execute=true;unmap_mode=true;errno=41;
 CHECK(fixture_submit(0,page)==HIGH&&unmapped==0,"original-unmaps-before-post");CHECK(errno==73,"unmap-return-errno");
 CHECK(read_return(0,r)&&r.entry.values.task_no==HIGH&&r.entry.values.id==0xabcdef0187654321ULL,"post-uses-copied-values");
 CHECK(reads==10&&forbidden==0,"no-post-task-read");
}
static void gates(){
 Task task;init_task(task,HIGH);tc_context c{};tc_return_record r{};
 {Transport t(21);start("wrong-caller",t);errno=41;unsigned n=native_submit_count;
 CHECK(fixture_submit_bad(0,reinterpret_cast<void*>(1))==HIGH,"rejected-submit-forwarded");CHECK(native_submit_count==n+1&&reads==0,"bad-caller-no-read-once");
 CHECK(!read_entry(0,c)&&!read_return(0,r),"bad-caller-no-qualified-record");CHECK(events[0].actual_lr==reinterpret_cast<U>(fixture_submit_bad_lr),"unmapped-submit-lr-retained");
 errno=41;n=native_exec_count;CHECK(fixture_execute_bad(reinterpret_cast<void*>(1))==0x8000000000000001ULL,"rejected-executor-forwarded");
 CHECK(native_exec_count==n+1&&reads==0,"bad-executor-no-read-once");CHECK(last_exec_lr==reinterpret_cast<U>(fixture_execute_bad_lr),"unmapped-executor-lr-retained");}
 {Transport t(22);start("null-task",t);errno=41;unsigned n=native_submit_count;
 CHECK(fixture_submit(0,nullptr)==HIGH&&native_submit_count==n+1,"null-submit-once");errno=41;n=native_exec_count;
 CHECK(fixture_execute(nullptr)==0&&native_exec_count==n+1,"null-executor-once");CHECK(reads==0&&!read_return(0,r),"null-no-read-or-qualified-return");}
 {Transport t(23);start("disabled",t,false);errno=41;unsigned n=native_submit_count;
 CHECK(fixture_submit(0,reinterpret_cast<void*>(1))==HIGH&&native_submit_count==n+1,"disabled-submit-once");errno=41;n=native_exec_count;
 CHECK(fixture_execute(reinterpret_cast<void*>(1))==0x8000000000000001ULL&&native_exec_count==n+1,"disabled-executor-once");CHECK(reads==0,"disabled-no-task-read");}
 {Transport t(24);start("wrong-type",t);Task wrong=task;wrong.bytes[0]^=1;errno=41;unsigned n=native_submit_count;
 CHECK(fixture_submit(0,&wrong)==HIGH&&native_submit_count==n+1,"wrong-type-forwarded");CHECK(reads==1&&!read_return(0,r),"wrong-type-vptr-only");}
 {Transport t(25);start("capacity",t);for(unsigned i=0;i<16;++i){errno=41;CHECK(fixture_submit(0,&task)==HIGH,"fill-forwards");}
 const unsigned count=reads;errno=41;unsigned n=native_submit_count;
 CHECK(fixture_submit(0,reinterpret_cast<void*>(1))==HIGH&&native_submit_count==n+1,"full-submit-once");errno=41;n=native_exec_count;
 CHECK(fixture_execute(reinterpret_cast<void*>(1))==0x8000000000000001ULL&&native_exec_count==n+1,"full-executor-once");CHECK(reads==count,"full-no-further-task-read");
 for(unsigned i=0;i<16;++i)CHECK(read_entry(i,c)&&read_return(i,r),"published-slots-retained");}
 {Transport t(26);start("close-with-outstanding",t);close_mode=true;errno=41;CHECK(fixture_submit(0,&task)==HIGH,"close-still-forwards");
 CHECK(read_entry(0,c)&&read_return(0,r)&&r.native_result==HIGH,"return-publishes-after-close");CHECK(!t.start(),"no-reopen");}
 for(unsigned m=2;m<=3;++m){Transport t(27+m);start(m==2?"zero-return":"mismatch-return",t);mode=m;errno=41;
 const U expected=m==2?0:HIGH-17;CHECK(fixture_submit(0,&task)==expected,"ambiguous-result-forwarded");
 CHECK(read_return(0,r)&&r.native_result==expected&&r.relation==(m==2?TC_RETURN_ZERO:TC_RETURN_VALUE_MISMATCH),"ambiguous-return-only");}
}
static void reentry_and_throw(){
 Task task;init_task(task,HIGH);tc_context c{};tc_return_record r{};
 {Transport t(31);start("nested-native",t);mode=1;errno=41;CHECK(fixture_submit(2,&task)==HIGH+2,"nested-result");
 CHECK(order_index==6&&events[0].kind==1&&events[1].kind==1&&events[2].kind==1&&events[3].kind==3,"nested-publication-order");
 for(unsigned i=0;i<3;++i){CHECK(read_entry(i,c)&&read_return(i,r),"nested-both-records");CHECK(r.entry.invocation_id==c.invocation_id&&r.entry.values.task_no==c.values.task_no,"nested-local-context");CHECK(r.native_result==HIGH+2-i,"nested-result-per-invocation");}}
 for(unsigned which=0;which<2;++which){Transport t(33+which);start(which?"after-reentry":"before-reentry",t);reenter_before=!which;reenter_after=which;errno=41;
 CHECK(fixture_submit(0,&task)==HIGH&&nested_result==HIGH,"helper-reentry-results");CHECK(read_return(0,r)&&r.entry.values.task_no==HIGH,"outer-context-retained");
 CHECK(read_return(1,r)&&r.entry.values.task_no==HIGH-1,"inner-context-retained");CHECK(errno==73,"reentry-errno");}
 {Transport t(35);start("exception-bypasses-after",t);mode=1;do_execute=true;throw_mode=true;errno=41;destroyed=0;
 const unsigned before=before_count,after=after_count,exec=exec_observe_count;bool caught=false;
 try{Guard outer;(void)fixture_submit(2,&task);}catch(const NativeError&e){caught=e.token==HIGH;}
 CHECK(caught,"one-native-exception-caught");CHECK(destroyed==5,"five-destructors");
 CHECK(before_count==before+3&&after_count==after&&exec_observe_count==exec+1,"three-submissions-no-after");CHECK(errno==74,"exception-errno");
 for(unsigned i=0;i<4;++i)CHECK(read_entry(i,c)&&!read_return(i,r),"entries-remain-no-return");
 mode=0;do_execute=false;throw_mode=false;errno=41;CHECK(fixture_submit(0,&task)==HIGH&&read_return(4,r),"usable-after-unwind");}
}
static void executor_registers(){
 Transport t(41);start("one-input-executor-abi",t,false);
 for(unsigned i=0;i<19;++i)fixture_expected_registers[i]=0xabcd000000000100ULL+i;
 for(unsigned i=0;i<8;++i)for(unsigned j=0;j<2;++j)fixture_expected_vectors[i][j]=0x1234567800000000ULL+i*17+j;
 set_target(&az_task_exec_original,fixture_register_target);trace_mask=2;errno=41;
 CHECK(fixture_invoke_registers()==0x54329876ULL,"unknown-native-return-untouched");trace_mask=0;
 CHECK(errno==41,"executor-helper-errno-preserved");CHECK(last_exec_task==fixture_expected_registers[0],"executor-one-input-full-x0");
 CHECK(last_exec_lr==reinterpret_cast<U>(fixture_invoke_registers_lr),"executor-original-lr-register");
 for(unsigned i=0;i<19;++i)if(i!=16)CHECK(fixture_register_capture.x[i]==fixture_expected_registers[i],"native-input-register-preserved");
 CHECK(fixture_register_capture.x[16]==reinterpret_cast<U>(fixture_register_target),"x16-tail-scratch");
 CHECK(fixture_register_capture.lr==reinterpret_cast<U>(fixture_invoke_registers_lr)&&fixture_register_capture.sp==fixture_expected_sp,"native-original-lr-sp");
 CHECK(fixture_register_capture.nzcv==0x60000000&&fixture_register_capture.fpcr==0x400000&&fixture_register_capture.fpsr==1,"flags-fp-preserved");
 CHECK(!std::memcmp(fixture_register_capture.q,fixture_expected_vectors,sizeof fixture_expected_vectors),"vectors-preserved");
 CHECK(trace_has(1,reinterpret_cast<U>(az_task_exec_entry),reinterpret_cast<U>(az_task_exec_entry_end)),"live-executor-cfi-frame");
 CHECK(trace_has(1,reinterpret_cast<U>(fixture_invoke_registers),reinterpret_cast<U>(fixture_invoke_registers_end)),"live-executor-cfi-caller");
 CHECK(reads==0,"abi-stress-no-invalid-task-read");
}
static unsigned concurrent_records=0;
static void concurrent(){
 Transport t(51);start("concurrent-submission",t);std::atomic<unsigned> errors{0};std::thread threads[4];
 for(unsigned k=0;k<4;++k)threads[k]=std::thread([k,&errors]{for(unsigned j=0;j<2;++j){Task task;init_task(task,HIGH-k*2-j,100+k*2+j);errno=41;if(fixture_submit(0,&task)!=HIGH||errno!=73)++errors;}});
 for(auto&t:threads)t.join();
 CHECK(errors==0,"concurrent-native-results");
 bool seen[8]{};
 for(unsigned i=0;i<16;++i){
  tc_context c{};tc_return_record r{};const bool have=read_entry(i,c),post=read_return(i,r);
  CHECK(have==post,"concurrent-entry-return-conservation");
  CHECK(!have||(c.invocation_id==i+1&&r.entry.invocation_id==c.invocation_id),"unique-local-invocation");
  CHECK(!post||r.native_result==HIGH,"concurrent-full-return");
  const U id=c.values.id;const bool valid=id>=100&&id<108;
  CHECK(!have||valid,"concurrent-value-range");
  CHECK(!have||(valid&&!seen[id-100]),"concurrent-value-unique");
  if(have){++concurrent_records;if(valid)seen[id-100]=true;}
 }
 const auto counts=t.counters();
 CHECK(concurrent_records>=1&&concurrent_records<=8,"bounded-concurrent-reservations");
 CHECK(counts.attempted==8&&counts.reserved==concurrent_records&&counts.entries==concurrent_records&&counts.returns==concurrent_records,"quiescent-publication-counts");
 CHECK(counts.contention+concurrent_records==8&&counts.capacity==0&&counts.gated==0&&counts.capture_rejected==0,"quiescent-contention-conservation");
 CHECK(reads==5*concurrent_records&&forbidden==0,"concurrent-copy-ranges");
}

int main(){
 CHECK(sizeof(void*)==8&&sizeof(tc_context)==64,"arm64-context-layout");ordinary_and_unmap();gates();reentry_and_throw();executor_registers();concurrent();
 CHECK(helper_bad==0,"zeroed-stack-context");CHECK(errno_bad==0,"native-entry-errno");
 CHECK(before_count==native_submit_count,"all-submissions-forward-once");
 // Register target is assembly-only and does not increment the C++ target counter.
 CHECK(exec_observe_count==native_exec_count+1,"all-executors-forward-once");
 std::printf("ARM64 integration: %u passed, %u failed; scenarios=%u; before=%u; after=%u; executor=%u; native-submit=%u; native-execute=%u; concurrent-records=%u\n",checks-failures,failures,scenarios,before_count.load(),after_count.load(),exec_observe_count.load(),native_submit_count.load(),native_exec_count.load(),concurrent_records);
 return failures?1:0;
}
