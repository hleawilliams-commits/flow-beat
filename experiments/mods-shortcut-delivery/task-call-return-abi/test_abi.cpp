// SPDX-License-Identifier: GPL-3.0-only
#include "context.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unwind.h>
#include <sys/mman.h>
#include <unistd.h>
using U=std::uint64_t;
using V=U __attribute__((vector_size(16)));
struct Pair {U a,b;}; struct Large {U a,b,c,d,e;}; struct Hfa {double a,b,c,d;};
struct alignas(16) Registers {U x[19],lr,sp,nzcv,fpcr,fpsr;U q[8][2];};
static_assert(sizeof(Registers)==320&&offsetof(Registers,q)==192,"assembly offsets");
extern "C" {
void *az_task_call_original=nullptr;
Registers fixture_input{},fixture_output{};
U fixture_expected_registers[19]{};
alignas(16) U fixture_expected_vectors[8][2]{};
U fixture_expected_sp=0;
extern char az_task_call_entry[],az_task_call_entry_end[],az_task_call_native_return[];
extern char fixture_invoke_registers_end[];
U fixture_invoke_registers();void fixture_register_target();
U fixture_entry_integer(U,U);Pair fixture_entry_pair(U,U);Large fixture_entry_large(U,U);
Hfa fixture_entry_hfa(U,U);V fixture_entry_vector(U,U);
U fixture_entry_nested(U,U);U fixture_entry_throw(U,U);
}
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
struct Event {TaskCallContext before,after; U box,task,lr,result;int before_errno,after_errno;bool returned;};
static Event events[256];
static std::mutex lock;
static std::atomic<unsigned> observed{0},posted{0},native_calls{0},zero_errors{0},context_errors{0};
static bool reenter_before=false,reenter_after=false,destroy_mode=false;
static thread_local unsigned helper_depth=0;
static U nested_before_result=0,nested_after_result=0;
static std::atomic<int> native_seen_errno{0};
static std::uintptr_t trace_ips[3][64];static unsigned trace_counts[3]{};
static unsigned trace_mask=0,trace_stage=0;
static _Unwind_Reason_Code trace_frame(_Unwind_Context *c,void*) noexcept{
 if(trace_counts[trace_stage]==64)return _URC_END_OF_STACK;
 trace_ips[trace_stage][trace_counts[trace_stage]++]=_Unwind_GetIP(c);return _URC_NO_REASON;
}
static void trace(unsigned stage){if(trace_mask&(1u<<stage)){trace_stage=stage;_Unwind_Backtrace(trace_frame,nullptr);}}
static bool contains(unsigned stage,std::uintptr_t low,std::uintptr_t high){
 for(unsigned i=0;i<trace_counts[stage];++i) {
  if(trace_ips[stage][i]>=low&&trace_ips[stage][i]<high)return true;
 }
 return false;
}
extern "C" void fixture_before(TaskCallContext *ctx,U box,U task,U lr) noexcept{
 const int saved=errno;
 const TaskCallContext zero{};
 if(std::memcmp(ctx,&zero,sizeof zero))++zero_errors;
 const unsigned i=observed.fetch_add(1);
 if(i>=256)std::terminate();
 // These are synthetic values, not pointer encodings or native memory reads.
 ctx->values={0xfabc000000000000ULL+i,0x1234000000000000ULL+i,0xfffffffeu,700u+i};
 if(destroy_mode)ctx->values=*reinterpret_cast<const TaskValues*>(task); // Synthetic owned page, before only.
 ctx->run_id=0x76543210fedcba98ULL;ctx->invocation_id=i+1;ctx->thread_observation=0;
 ctx->phase=1;ctx->status=2;ctx->reserved=0;
 {std::lock_guard<std::mutex>g(lock);events[i]={*ctx,{},box,task,lr,0,saved,0,false};}
 trace(0);
 if(reenter_before&&!helper_depth){++helper_depth;nested_before_result=fixture_entry_integer(31,41);--helper_depth;}
 errno=91;
}
extern "C" void fixture_after(const TaskCallContext *ctx,U result) noexcept{
 const int saved=errno;
 const unsigned i=static_cast<unsigned>(ctx->invocation_id-1);
 if(i>=256)std::terminate();
 {std::lock_guard<std::mutex>g(lock);
  if(std::memcmp(ctx,&events[i].before,sizeof *ctx))++context_errors;
  events[i].after=*ctx;events[i].result=result;events[i].after_errno=saved;events[i].returned=true;}
 ++posted;trace(2);
 if(reenter_after&&!helper_depth){++helper_depth;nested_after_result=fixture_entry_integer(51,61);--helper_depth;}
 errno=92;
}
extern "C" void fixture_native_record(){++native_calls;native_seen_errno=errno;trace(1);errno=73;}
template<class F> static void target(F f){__atomic_store_n(&az_task_call_original,reinterpret_cast<void*>(f),__ATOMIC_RELEASE);}
static U value(U a,U b){return (a^0xfedcba9876543210ULL)+3*b;}
static U integer(U a,U b){++native_calls;native_seen_errno=errno;errno=73;return value(a,b);}
static Pair pair(U a,U b){++native_calls;return {a^0xfedcba9876543210ULL,b^0x123456789abcdef0ULL};}
static Large large(U a,U b){++native_calls;return {a,b,a+b,a^b,0xdeadbeefcafef00dULL};}
static Hfa hfa(U a,U b){++native_calls;return {double(a)+.25,double(b)+.5,8.75,9.125};}
static V vector(U a,U b){++native_calls;return V{a,b};}
static U nested(U depth,U b){++native_calls;if(depth)return 1+fixture_entry_nested(depth-1,b);return b;}
struct Error {U a,b;};static unsigned destroyed=0;struct Guard{~Guard(){++destroyed;}};
static U throwing(U depth,U token){++native_calls;Guard guard;if(depth)return fixture_entry_throw(depth-1,token);errno=74;throw Error{depth,token};}
static U exception_caller(){Guard guard;return fixture_entry_throw(2,0xfedcba9876543210ULL);}
static std::size_t page_size=0;static int unmap_result=-1;
static U destroy_task(U,U task){++native_calls;unmap_result=munmap(reinterpret_cast<void*>(task),page_size);errno=75;return 0x8000000000000001ULL;}
int main(){
 CHECK(sizeof(void*)==8);CHECK(sizeof(TaskCallContext)==64);
 for(unsigned i=0;i<19;++i)fixture_expected_registers[i]=0xabcd000000000100ULL+i;
 for(unsigned i=0;i<8;++i)for(unsigned j=0;j<2;++j)fixture_expected_vectors[i][j]=0x1234567800000000ULL+17*i+j;
 target(fixture_register_target);trace_mask=7;errno=37;
 CHECK(fixture_invoke_registers()==0xfedcba9876543210ULL);trace_mask=0;
 CHECK(errno==73);CHECK(native_seen_errno==37);
 CHECK(observed==1&&posted==1&&native_calls==1);
 for(unsigned i=0;i<19;++i)if(i!=16)CHECK(fixture_input.x[i]==fixture_expected_registers[i]);
 CHECK(fixture_input.x[16]==reinterpret_cast<U>(fixture_register_target));
 CHECK(fixture_input.sp+416==fixture_expected_sp);CHECK((fixture_input.sp&15)==0);
 CHECK(fixture_input.lr==reinterpret_cast<U>(az_task_call_native_return));
 CHECK(fixture_input.nzcv==0x60000000&&fixture_input.fpcr==0x400000&&fixture_input.fpsr==1);
 CHECK(!std::memcmp(fixture_input.q,fixture_expected_vectors,sizeof fixture_expected_vectors));
 CHECK(events[0].box==fixture_expected_registers[0]&&events[0].task==fixture_expected_registers[1]);
 CHECK(events[0].lr==fixture_output.lr&&events[0].lr!=0);
 CHECK(events[0].result==0xfedcba9876543210ULL);
 CHECK(events[0].before_errno==37&&events[0].after_errno==73);
 for(unsigned i=0;i<19;++i)CHECK(fixture_output.x[i]==(i==0?0xfedcba9876543210ULL:i==1?0x0123456789abcdefULL:0x700+i));
 CHECK(fixture_output.sp==fixture_expected_sp);
 CHECK(fixture_output.nzcv==0x90000000&&fixture_output.fpcr==0x800000&&fixture_output.fpsr==0x10);
 for(unsigned i=0;i<8;++i)for(unsigned j=0;j<2;++j)CHECK(fixture_output.q[i][j]==0x5a5a5a5a5a5a5a5aULL);
 for(unsigned i=0;i<3;++i){
  CHECK(contains(i,reinterpret_cast<U>(az_task_call_entry),reinterpret_cast<U>(az_task_call_entry_end)));
  CHECK(contains(i,reinterpret_cast<U>(fixture_invoke_registers),reinterpret_cast<U>(fixture_invoke_registers_end)));
  CHECK(trace_counts[i]>=4);
 }
 target(integer);unsigned base=observed;
 const U a=0xf000000000000123ULL,b=0x80000000fedcba98ULL;errno=38;
 CHECK(fixture_entry_integer(a,b)==value(a,b));CHECK(errno==73&&native_seen_errno==38);
 CHECK(events[base].box==a&&events[base].task==b&&events[base].result==value(a,b));
 target(pair);Pair p=fixture_entry_pair(a,b);CHECK(p.a==(a^0xfedcba9876543210ULL)&&p.b==(b^0x123456789abcdef0ULL));
 target(large);Large l=fixture_entry_large(a,b);CHECK(l.a==a&&l.b==b&&l.c==a+b&&l.d==(a^b)&&l.e==0xdeadbeefcafef00dULL);
 target(hfa);Hfa h=fixture_entry_hfa(5,6);CHECK(h.a==5.25&&h.b==6.5&&h.c==8.75&&h.d==9.125);
 target(vector);V v=fixture_entry_vector(a,b);CHECK(v[0]==a&&v[1]==b);
 target(nested);base=observed;unsigned post_base=posted;
 CHECK(fixture_entry_nested(3,0x8000000000000010ULL)==0x8000000000000013ULL);
 CHECK(observed==base+4&&posted==post_base+4);
 for(unsigned i=0;i<4;++i){CHECK(events[base+i].box==3-i);CHECK(events[base+i].result==0x8000000000000013ULL-i);CHECK(events[base+i].returned);}
 target(integer);base=observed;reenter_before=true;
 CHECK(fixture_entry_integer(a,b)==value(a,b));reenter_before=false;
 CHECK(nested_before_result==value(31,41));CHECK(observed==base+2&&events[base].result==value(a,b)&&events[base+1].result==value(31,41));
 base=observed;reenter_after=true;errno=39;
 CHECK(fixture_entry_integer(a,b)==value(a,b));reenter_after=false;
 CHECK(nested_after_result==value(51,61));CHECK(observed==base+2&&events[base].result==value(a,b)&&events[base+1].result==value(51,61));CHECK(errno==73);
 target(throwing);base=observed;post_base=posted;bool caught=false;
 try{exception_caller();}catch(const Error&e){caught=e.a==0&&e.b==0xfedcba9876543210ULL;}
 CHECK(caught);CHECK(destroyed==4);CHECK(observed==base+3&&posted==post_base);CHECK(errno==74);
 for(unsigned i=0;i<3;++i)CHECK(!events[base+i].returned);
 target(integer);CHECK(fixture_entry_integer(a,b)==value(a,b)); // usable again after unwinding
 page_size=static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
 void *page=mmap(nullptr,page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 CHECK(page!=MAP_FAILED);if(page==MAP_FAILED)return 2;
 auto *task=static_cast<TaskValues*>(page);*task={0,0xcafebabefedcba98ULL,0xffffffffu,0};
 target(destroy_task);destroy_mode=true;base=observed;
 CHECK(fixture_entry_integer(0x1234,reinterpret_cast<U>(page))==0x8000000000000001ULL);destroy_mode=false;
 CHECK(unmap_result==0&&errno==75);CHECK(events[base].after.values.taskno==0&&events[base].after.values.id==0xcafebabefedcba98ULL);
 CHECK(events[base].after.values.delta_bits==0xffffffffu&&events[base].after.values.cumulative_bits==0);
 target(integer);base=observed;post_base=posted;std::atomic<unsigned> thread_errors{0};
 std::thread threads[4];for(unsigned t=0;t<4;++t)threads[t]=std::thread([t,&thread_errors]{
  for(unsigned j=0;j<16;++j){const U x=0x8000000000000000ULL+t*100+j;errno=40+int(t);
   if(fixture_entry_integer(x,17)!=value(x,17)||errno!=73)++thread_errors;}
 });
 for(auto&t:threads)t.join();CHECK(thread_errors==0);CHECK(observed==base+64&&posted==post_base+64);
 for(unsigned i=base;i<base+64;++i){CHECK(events[i].returned);CHECK(!std::memcmp(&events[i].before,&events[i].after,sizeof(TaskCallContext)));CHECK(events[i].result==value(events[i].box,events[i].task));}
 CHECK(zero_errors==0);CHECK(context_errors==0);CHECK(observed==native_calls);CHECK(posted+3==observed);
 std::printf("AArch64 call/return assertions: %u passed, %u failed; before=%u; after=%u; native=%u\n",checks-failures,failures,observed.load(),posted.load(),native_calls.load());
 return failures?1:0;
}
