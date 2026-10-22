/* SPDX-License-Identifier: GPL-3.0-only
 * Passive one-shot task values. Native forwarding belongs exclusively to the
 * unchanged assembly wrappers and their permanent SDK original-pointer slots.
 */
#include "azmod-mod.h"
#include "deps/task-value-transport/transport.h"
#include "deps/task-call-return-abi/context.h"
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <new>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
extern "C" {
alignas(void*) void *az_task_call_original=nullptr;
alignas(void*) void *az_task_exec_original=nullptr;
void az_task_call_entry(void);
void az_task_exec_entry(void);
}
namespace eto {
static constexpr char ID[]="encoder-task-observer";
static constexpr char FIRMWARE[]="ep147-9e9454e357389ece";
static constexpr uintptr_t SUBMIT_ENTRY=0x237a440, EXECUTE_ENTRY=0x1b029c0;
static constexpr uintptr_t SUBMIT_SLOT=0x321b138, EXECUTE_SLOT=0x2d444e8;
static constexpr uint32_t SUBMIT_WORDS[]={0xa9bd7bfd,0x910003fd,0xf9000bf3,0xaa0003f3};
static constexpr uint32_t EXECUTE_WORDS[]={0xf9400c03,0x90006fc1,0x9102d021,0x91067062};
static constexpr unsigned CAPACITY=16, POLL_BUDGET=120, QUIET_POLLS=5, DIAGNOSTIC_BUDGET=4;
using Transport=tvt::Transport<CAPACITY>;
static_assert(sizeof(TaskCallContext)==sizeof(tc_context),"64-byte wrapper context");
static_assert(offsetof(TaskCallContext,values)==offsetof(tc_context,values)&&
 offsetof(TaskCallContext,run_id)==offsetof(tc_context,run_id)&&
 offsetof(TaskCallContext,invocation_id)==offsetof(tc_context,invocation_id)&&
 offsetof(TaskCallContext,thread_observation)==offsetof(tc_context,thread_observation)&&
 offsetof(TaskCallContext,phase)==offsetof(tc_context,phase)&&
 offsetof(TaskCallContext,status)==offsetof(tc_context,status)&&
 offsetof(TaskCallContext,reserved)==offsetof(tc_context,reserved),"context field offsets");
static_assert(sizeof(TaskValues)==sizeof(tc_values)&&
 offsetof(TaskValues,taskno)==offsetof(tc_values,task_no)&&
 offsetof(TaskValues,id)==offsetof(tc_values,id)&&
 offsetof(TaskValues,delta_bits)==offsetof(tc_values,delta_bits)&&
 offsetof(TaskValues,cumulative_bits)==offsetof(tc_values,cumulative_bits),"payload field offsets");
enum State : unsigned { Never=0, Installing=1, Ready=2, Closed=3, Failed=4 };
static std::atomic<unsigned> state{Never};
alignas(Transport) static unsigned char storage[sizeof(Transport)];
static std::atomic<Transport*> published{nullptr};
static const az_core_v1* core=nullptr;
static std::atomic<uint64_t> heartbeat{0}, attempted{0}, state_gated{0}, caller_gated{0}, tid_failed{0}, worker_cycles{0};
enum WorkerExit : unsigned { WorkerRunning=0, WorkerInitFailed=1, WorkerSleepFailed=2, WorkerMissingStorage=3 };
static std::atomic<unsigned> worker_exit_reason{WorkerRunning};
#ifdef AZTASK_TEST
static void test_static_read(void*,uintptr_t,size_t);
static long test_tid(void);
static int test_sleep(struct timespec*);
#endif
static void static_read(void* dst,uintptr_t address,size_t size) {
#ifdef AZTASK_TEST
    test_static_read(dst,address,size);
#else
    std::memcpy(dst,reinterpret_cast<const void*>(address),size);
#endif
}
static bool check_static() {
    uintptr_t submit=0,execute=0;uint32_t sw[4]{},ew[4]{};
    static_read(&submit,SUBMIT_SLOT,sizeof submit);static_read(&execute,EXECUTE_SLOT,sizeof execute);
    static_read(sw,SUBMIT_ENTRY,sizeof sw);static_read(ew,EXECUTE_ENTRY,sizeof ew);
    return submit==SUBMIT_ENTRY&&execute==EXECUTE_ENTRY&&
        !std::memcmp(sw,SUBMIT_WORDS,sizeof sw)&&!std::memcmp(ew,EXECUTE_WORDS,sizeof ew);
}
static bool parse_run(const char* s,uint64_t& result) noexcept {
    // Decimal only, entire string consumed, 1..20 digits; fresh run uniqueness
    // is an external requirement, not established by a time/PID heuristic.
    if(!s||!*s)return false;
    uint64_t n=0;
    for(unsigned i=0;i<21;++i) {
        const unsigned char ch=static_cast<unsigned char>(s[i]);
        if(!ch){if(!n)return false;result=n;return true;}
        if(i==20||ch<'0'||ch>'9')return false;
        const unsigned digit=ch-'0';
        if(n>(UINT64_MAX-digit)/10)return false;
        n=n*10+digit;
    }
    return false;
}
static unsigned current_tid() noexcept {
#ifdef AZTASK_TEST
    const long raw=test_tid();
#elif defined(__linux__)
    const long raw=syscall(SYS_gettid);
#else
    const long raw=0;
#endif
    return raw>0&&static_cast<unsigned long>(raw)<=UINT_MAX?static_cast<unsigned>(raw):0;
}
static void close_capture() noexcept {
    unsigned expected=Ready;
    if(state.compare_exchange_strong(expected,Closed,std::memory_order_acq_rel,std::memory_order_relaxed)) {
        Transport* t=published.load(std::memory_order_acquire);
        if(t)t->close();
    }
}
struct WorkerState {
    bool entry_copied[CAPACITY]{},return_copied[CAPACITY]{};
    unsigned polls=0,quiet=0,entry_count=0,return_count=0,log_attempts=0,diagnostics=0;
    uint64_t seen_heartbeat=0,seen_capacity=0,seen_contention=0,seen_rejected=0,seen_tid_failed=0;
    bool close_attempted=false;
};
static bool worker_cycle(WorkerState& w) noexcept {
    worker_cycles.fetch_add(1,std::memory_order_relaxed);
    const unsigned s=state.load(std::memory_order_acquire);
    if(s==Failed)return false;
    if(s!=Ready&&s!=Closed)return true; // no timeout may close pending init
    Transport* t=published.load(std::memory_order_acquire);
    if(!t)return false; // unreachable under publication contract
    if(s==Ready) {
        const uint64_t h=heartbeat.load(std::memory_order_relaxed);
        w.quiet=h==w.seen_heartbeat?(w.quiet<QUIET_POLLS?w.quiet+1:QUIET_POLLS):0;
        w.seen_heartbeat=h;if(w.polls<POLL_BUDGET)++w.polls;
        if(w.quiet>=QUIET_POLLS||w.polls>=POLL_BUDGET||t->counters().reserved>=CAPACITY)close_capture();
    }
    if(!w.close_attempted&&state.load(std::memory_order_acquire)==Closed) {
        w.close_attempted=true;++w.log_attempts;
        core->log(ID,"ETO capture closed permanently; late return scanning continues; no quiescence or quarantine claim");
    }
    for(unsigned i=0;i<CAPACITY;++i) {
        tc_context c{};
        if(!w.entry_copied[i]&&t->read_entry(i,c)) {
            w.entry_copied[i]=true;++w.entry_count;++w.log_attempts;
            core->log(ID,"ETO entry run=%lu invocation=%lu phase=%u status=%u thread=%lu taskno=%lu id=%lx delta=%08x cumulative=%08x values-only=1",
                static_cast<unsigned long>(c.run_id),static_cast<unsigned long>(c.invocation_id),c.phase,c.status,
                static_cast<unsigned long>(c.thread_observation),static_cast<unsigned long>(c.values.task_no),
                static_cast<unsigned long>(c.values.id),c.values.delta_bits,c.values.cumulative_bits);
        }
        tc_return_record r{};
        if(!w.return_copied[i]&&t->read_return(i,r)) {
            w.return_copied[i]=true;++w.return_count;++w.log_attempts;
            core->log(ID,"ETO return run=%lu invocation=%lu result=%lu relation=%u normal-return-only=1 no-completion=1",
                static_cast<unsigned long>(r.entry.run_id),static_cast<unsigned long>(r.entry.invocation_id),
                static_cast<unsigned long>(r.native_result),r.relation);
        }
    }
    const auto c=t->counters();const uint64_t tf=tid_failed.load(std::memory_order_relaxed);
    if(c.capacity!=w.seen_capacity||c.contention!=w.seen_contention||c.capture_rejected!=w.seen_rejected||tf!=w.seen_tid_failed) {
        w.seen_capacity=c.capacity;w.seen_contention=c.contention;w.seen_rejected=c.capture_rejected;w.seen_tid_failed=tf;
        if(w.diagnostics<DIAGNOSTIC_BUDGET) {
            ++w.diagnostics;++w.log_attempts;
            core->log(ID,"ETO diagnostic capacity=%lu contention=%lu capture-rejected=%lu tid-failed=%lu noncoherent=1 delivery-unknown=1",
                static_cast<unsigned long>(c.capacity),static_cast<unsigned long>(c.contention),
                static_cast<unsigned long>(c.capture_rejected),static_cast<unsigned long>(tf));
        }
    }
    return true; // retained process-lifetime polling, including absent/late returns
}
static void stop_scanning() noexcept {
    unsigned s=state.load(std::memory_order_acquire);
    while(s==Installing||s==Ready) {
        const unsigned next=s==Installing?Failed:Closed;
        if(state.compare_exchange_strong(s,next,std::memory_order_acq_rel,std::memory_order_acquire))break;
    }
    Transport* t=published.load(std::memory_order_acquire);if(t)t->close();
}
static void* worker(void*) noexcept {
    WorkerState w;
    for(;;) {
        struct timespec remaining={0,500000000};
        for(;;) {
#ifdef AZTASK_TEST
            const int result=test_sleep(&remaining);
#else
            const int result=nanosleep(&remaining,&remaining);
#endif
            if(!result)break;
            if(errno!=EINTR){stop_scanning();worker_exit_reason.store(WorkerSleepFailed,std::memory_order_release);return nullptr;}
        }
        if(!worker_cycle(w)){worker_exit_reason.store(state.load(std::memory_order_acquire)==Failed?WorkerInitFailed:WorkerMissingStorage,std::memory_order_release);return nullptr;}
    }
}
static int fail(int code) noexcept { state.store(Failed,std::memory_order_release);return code; }
static int init(const az_core_v1* c) noexcept {
    tvt::ErrnoGuard saved;
    unsigned expected=Never;
    if(!state.compare_exchange_strong(expected,Installing,std::memory_order_acq_rel,std::memory_order_relaxed))return -8;
    const char* opt=std::getenv("AZ_ENCODER_TASK_OBSERVER_EMULATOR");uint64_t run=0;
    if(!opt||std::strcmp(opt,"1")||!parse_run(std::getenv("AZ_ENCODER_TASK_OBSERVER_RUN_ID"),run))return fail(-9);
    if(!c||c->abi!=AZ_MOD_ABI||c->size<0||!AZ_HAS(c,fw_tag)||!AZ_HAS(c,claim_hook)||!AZ_HAS(c,thread_start)||!AZ_HAS(c,log))return fail(-10);
    const char* tag=c->fw_tag();if(!tag||std::strcmp(tag,FIRMWARE))return fail(-10);
#if !(defined(__linux__) && defined(__aarch64__)) && !defined(AZTASK_TEST)
    return fail(-11);
#else
    if(!check_static())return fail(-12);
    core=c;Transport* t=new(storage) Transport(run);
    published.store(t,std::memory_order_release);
    if(core->thread_start(ID,"encoder-task-observer",worker,nullptr))return fail(-13);
    if(state.load(std::memory_order_acquire)!=Installing)return fail(-23);
    // SDK stores directly into the permanent slot used by its wrapper before
    // redirect. Helpers may run here but recording stays gated until both claims.
    if(core->claim_hook(ID,SUBMIT_ENTRY,reinterpret_cast<void*>(az_task_call_entry),&az_task_call_original)||
       !__atomic_load_n(&az_task_call_original,__ATOMIC_ACQUIRE))return fail(-20);
    if(state.load(std::memory_order_acquire)!=Installing)return fail(-23);
    if(core->claim_hook(ID,EXECUTE_ENTRY,reinterpret_cast<void*>(az_task_exec_entry),&az_task_exec_original)||
       !__atomic_load_n(&az_task_exec_original,__ATOMIC_ACQUIRE))return fail(-21);
    if(state.load(std::memory_order_acquire)!=Installing)return fail(-23);
    if(!t->start())return fail(-22);
    expected=Installing;
    if(!state.compare_exchange_strong(expected,Ready,std::memory_order_release,std::memory_order_relaxed)){t->close();return fail(-23);}
    core->log(ID,"ETO initialization complete; capture status separate; copied values only; no admission or completion claim");
    return 0;
#endif
}
static void capture(tc_context& c,unsigned phase,uintptr_t task,uintptr_t lr) noexcept {
    attempted.fetch_add(1,std::memory_order_relaxed);
    if(state.load(std::memory_order_acquire)!=Ready){state_gated.fetch_add(1,std::memory_order_relaxed);return;}
    if(lr!=(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR)){caller_gated.fetch_add(1,std::memory_order_relaxed);return;}
    Transport* t=published.load(std::memory_order_acquire);if(!t)return;
    const unsigned tid=current_tid();if(!tid){tid_failed.fetch_add(1,std::memory_order_relaxed);return;}
    t->begin(&c,phase,lr,reinterpret_cast<const void*>(task),tid);
}
} // namespace eto
extern "C" void az_task_call_before(TaskCallContext* raw,uintptr_t,uintptr_t task,uintptr_t lr) noexcept {
    tvt::ErrnoGuard saved;tc_context c{};eto::capture(c,TC_SUBMISSION,task,lr);std::memcpy(raw,&c,sizeof c);
}
extern "C" void az_task_call_after(const TaskCallContext* raw,uint64_t result) noexcept {
    tvt::ErrnoGuard saved;tc_context c{};std::memcpy(&c,raw,sizeof c);
    if(c.phase!=TC_SUBMISSION||c.status!=TC_COPIED||!c.run_id||!c.invocation_id)return;
    auto* t=eto::published.load(std::memory_order_acquire);if(t)t->finish(c,result);
}
extern "C" void az_task_exec_observe(uintptr_t task,uintptr_t lr) noexcept {
    tvt::ErrnoGuard saved;tc_context c{};eto::capture(c,TC_EXECUTION,task,lr);
}
extern "C" int task_observer_init(const az_core_v1* core) { return eto::init(core); }
extern "C" void task_observer_tick(void) { eto::heartbeat.fetch_add(1,std::memory_order_relaxed); }
