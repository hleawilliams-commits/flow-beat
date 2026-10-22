// SPDX-License-Identifier: GPL-3.0-only
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <unwind.h>

using U = std::uint64_t;
using V = U __attribute__((vector_size(16)));
struct Pair { U a,b; };
struct Large { U a,b,c,d,e; };
struct Hfa { double a,b,c,d; };
struct alignas(16) Capture { U x[19],lr,sp,nzcv,fpcr,fpsr; U q[8][2]; };
static_assert(offsetof(Capture,lr)==152 && offsetof(Capture,q)==192 && sizeof(Capture)==320,"assembly offsets");
extern "C" {
void *az_dispatch_original = nullptr;
Capture fixture_register_capture{};
U fixture_expected_registers[19]{};
alignas(16) U fixture_expected_vectors[8][2]{};
U fixture_expected_sp=0;
extern char az_dispatch_entry[],az_dispatch_entry_end[],fixture_invoke_registers_end[];
U fixture_invoke_registers();
void fixture_register_target();
U fixture_entry_integer(U,U,U,U,U,U,U,U,U,U,U);
Pair fixture_entry_pair(U,U);
Large fixture_entry_large(U,U,U,U,U,U,U,U,U);
Hfa fixture_entry_hfa(double,double,double,double);
V fixture_entry_vector(V,V);
double fixture_entry_double(double,double);
U fixture_entry_nested(U,U,U,U,U);
void fixture_entry_throw(U);
}
static int checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
struct Event { U a,b,c; std::uint32_t d,e; U lr; };
static Event events[64];
static unsigned observed=0,targets=0;
static std::uintptr_t backtrace_ips[64];
static unsigned backtrace_count=0;
static bool trace_requested=false;
static _Unwind_Reason_Code trace_frame(_Unwind_Context *ctx,void *) noexcept {
    if(backtrace_count==64)return _URC_END_OF_STACK;
    backtrace_ips[backtrace_count++]=_Unwind_GetIP(ctx);
    return _URC_NO_REASON;
}
extern "C" void fixture_record(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uint32_t d,std::uint32_t e,std::uintptr_t lr) noexcept {
    if(observed<64)events[observed]={a,b,c,d,e,lr};
    ++observed;
    if(trace_requested){trace_requested=false;_Unwind_Backtrace(trace_frame,nullptr);}
}
template<class F> static void target(F f){az_dispatch_original=reinterpret_cast<void *>(f);}
static U integer(U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k){
    ++targets;return a+3*b+5*c+7*d+11*e+13*f+17*g+19*h+23*i+29*j+31*k;
}
static Pair pair_value(U a,U b){++targets;return {a^0xfeedbeef12345678ULL,b^0x1122334455667788ULL};}
static Large large(U a,U b,U c,U d,U e,U f,U g,U h,U i){++targets;return {a+b,c+d,e+f,g+h,i};}
static Hfa hfa(double a,double b,double c,double d){++targets;return {a+1,b+2,c+3,d+4};}
static V vector_value(V a,V b){++targets;return a+b;}
static double double_value(double a,double b){++targets;return a+b;}
static U nested(U depth,U a,U b,U c,U d){
    ++targets;
    if(depth)return 1+fixture_entry_nested(depth-1,a,b,c,d);
    return a+b+c+d;
}
struct NativeError { U token; };
static unsigned destroyed=0;
struct Guard { ~Guard(){++destroyed;} };
static void throwing(U token){++targets;Guard g;throw NativeError{token};}
static void exception_caller(){Guard g;fixture_entry_throw(0xfedcba9876543210ULL);}
static bool trace_contains(std::uintptr_t low,std::uintptr_t high){
    for(unsigned i=0;i<backtrace_count;++i)if(backtrace_ips[i]>=low&&backtrace_ips[i]<high)return true;
    return false;
}
int main(){
    CHECK(sizeof(void*)==8);
    for(unsigned i=0;i<19;++i)fixture_expected_registers[i]=0xabcd000000000100ULL+i;
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<2;++j)fixture_expected_vectors[i][j]=0x1234567800000000ULL+i*17+j;
    target(fixture_register_target);trace_requested=true;
    U r=fixture_invoke_registers();
    CHECK(r==0x54329876ULL);CHECK(observed==1);
    for(unsigned i=0;i<19;++i)if(i!=16)CHECK(fixture_register_capture.x[i]==fixture_expected_registers[i]);
    CHECK(fixture_register_capture.x[16]==reinterpret_cast<std::uintptr_t>(fixture_register_target));
    CHECK(fixture_register_capture.sp==fixture_expected_sp);
    CHECK(fixture_register_capture.lr==events[0].lr && events[0].lr!=0);
    CHECK(fixture_register_capture.nzcv==0x60000000);
    CHECK(fixture_register_capture.fpcr==0x400000);CHECK(fixture_register_capture.fpsr==1);
    CHECK(!std::memcmp(fixture_register_capture.q,fixture_expected_vectors,sizeof fixture_expected_vectors));
    CHECK(events[0].a==fixture_expected_registers[0]);CHECK(events[0].b==fixture_expected_registers[1]);
    CHECK(events[0].c==fixture_expected_registers[2]);
    CHECK(events[0].d==static_cast<std::uint32_t>(fixture_expected_registers[3]));
    CHECK(events[0].e==static_cast<std::uint32_t>(fixture_expected_registers[4]));
    CHECK(trace_contains(reinterpret_cast<std::uintptr_t>(az_dispatch_entry),reinterpret_cast<std::uintptr_t>(az_dispatch_entry_end)));
    CHECK(backtrace_count>=4);
    CHECK(trace_contains(reinterpret_cast<std::uintptr_t>(fixture_invoke_registers),reinterpret_cast<std::uintptr_t>(fixture_invoke_registers_end)));
    unsigned before=observed,tc=targets;
    target(integer);const U a=0xf000000000000001ULL,c=0x1234567800000003ULL;
    CHECK(fixture_entry_integer(a,2,c,4,5,6,7,8,9,10,11)==a+3*2+5*c+7*4+11*5+13*6+17*7+19*8+23*9+29*10+31*11);
    CHECK(targets==tc+1&&observed==before+1);CHECK(events[before].a==a&&events[before].b==2&&events[before].c==c&&events[before].d==4&&events[before].e==5);
    target(pair_value);Pair p=fixture_entry_pair(0x123456789abcdef0ULL,0xfedcba9876543210ULL);
    CHECK(p.a==(0x123456789abcdef0ULL^0xfeedbeef12345678ULL));CHECK(p.b==(0xfedcba9876543210ULL^0x1122334455667788ULL));
    target(large);Large l=fixture_entry_large(1,2,3,4,5,6,7,8,0x123456789abcdef0ULL);
    CHECK(l.a==3&&l.b==7&&l.c==11&&l.d==15&&l.e==0x123456789abcdef0ULL);
    target(hfa);Hfa h=fixture_entry_hfa(1.25,2.25,3.25,4.25);
    CHECK(h.a==2.25&&h.b==4.25&&h.c==6.25&&h.d==8.25);
    target(vector_value);V v=fixture_entry_vector(V{1,2},V{3,4});CHECK(v[0]==4&&v[1]==6);
    target(double_value);CHECK(fixture_entry_double(1.5,2.25)==3.75);
    before=observed;tc=targets;target(nested);
    CHECK(fixture_entry_nested(3,10,20,30,40)==103);CHECK(observed==before+4&&targets==tc+4);
    for(unsigned i=0;i<4;++i)CHECK(events[before+i].a==3-i&&events[before+i].b==10&&events[before+i].c==20&&events[before+i].d==30&&events[before+i].e==40);
    before=observed;tc=targets;target(throwing);bool caught=false;
    try{exception_caller();}catch(const NativeError&e){caught=e.token==0xfedcba9876543210ULL;}
    CHECK(caught);CHECK(destroyed==2);CHECK(observed==before+1&&targets==tc+1);
    CHECK(observed==targets+1);
    std::printf("AArch64 ABI assertions: %d passed, %d failed; helper calls=%u; native calls=%u\n",checks-failures,failures,observed,targets);
    return failures?1:0;
}
