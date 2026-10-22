/* SPDX-License-Identifier: GPL-3.0-only */
#define AZDISPATCH_TEST
#include "../observer.cpp"
#include <stdio.h>
#include <stdarg.h>
#include <thread>
using namespace edo;
static std::atomic<unsigned> checks(0);
static unsigned claims, starts, logs, records;
static unsigned firmware_reads;
static int claim_failure, thread_failure;
static bool empty_original;
static const char *tag=firmware;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
extern "C" void az_dispatch_entry(void) {} // Assembly is exercised separately in abi/.
static void native_target(void) {}
static const char *fw() { ++firmware_reads; return tag; }
static void logger(const char *,const char *format,...) {
    ++logs; if (!strncmp(format,"EDO entry ",10)) ++records;
}
static int start_thread(const char *name,const char *,void *(*fn)(void *),void *) {
    CHECK(!strcmp(name,id)); CHECK(fn==worker); ++starts; return thread_failure;
}
static int claim(const char *name,uintptr_t address,void *hook,void **original) {
    CHECK(!strcmp(name,id)); CHECK(address==ENTRY); CHECK(hook==(void *)az_dispatch_entry);
    CHECK(original==&az_dispatch_original); ++claims;
    if (claim_failure) return -1;
    if (!empty_original) __atomic_store_n(original,(void *)native_target,__ATOMIC_RELEASE);
    // Native redirection may be visible before init publishes observation readiness.
    // Original forwarding is assembly-owned; the copy helper must stay inactive here.
    test_tid=pid;
    errno=EDOM;
    az_dispatch_observe_entry(1,2,UINTPTR_MAX,3,4,5);
    CHECK(errno==EDOM && sequence==0 && write_index.load()==0);
    return 0;
}
static az_core_v1 mock_core() {
    az_core_v1 c={}; c.abi=AZ_MOD_ABI; c.size=sizeof c; c.fw_tag=fw;
    c.thread_start=start_thread; c.claim_hook=claim; c.log=logger; return c;
}
static void entry(unsigned i=0) {
    az_dispatch_observe_entry(1+i,UINTPTR_MAX-i,UINT64_C(0xfedcba9876540000)+i,
                             0xffffffff-i,0x80000000+i,UINT64_C(0xdeadbeef1adef580)+i);
}
int main(int argc,char **argv) {
    CHECK(argc==2); const char *scenario=argv[1]; auto c=mock_core();
    setenv("AZ_ENCODER_DISPATCH_OBSERVER_EMULATOR","1",1);
    if (!strcmp(scenario,"env")) {
        unsetenv("AZ_ENCODER_DISPATCH_OBSERVER_EMULATOR"); CHECK(init(&c)==-9);
        setenv("AZ_ENCODER_DISPATCH_OBSERVER_EMULATOR","true",1); CHECK(init(&c)==-9);
        CHECK(!claims && !starts);
    } else if (!strcmp(scenario,"sdk")) {
        CHECK(init(NULL)==-10); c.size=offsetof(az_core_v1,fw_tag); CHECK(init(&c)==-10);
        c=mock_core(); c.size=-1; CHECK(init(&c)==-10);
        c=mock_core(); c.abi=AZ_MOD_ABI+1; CHECK(init(&c)==-10);
        c=mock_core(); c.fw_tag=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.claim_hook=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.thread_start=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.log=NULL; CHECK(init(&c)==-10); CHECK(!claims && !starts);
        CHECK(!firmware_reads);
    } else if (!strcmp(scenario,"firmware")) {
        tag=NULL; CHECK(init(&c)==-10); tag="other"; CHECK(init(&c)==-10); CHECK(!claims && !starts);
    } else if (!strcmp(scenario,"prologue")) {
        test_prologues=false; CHECK(init(&c)==-12); CHECK(!claims && !starts);
    } else if (!strcmp(scenario,"worker")) {
        thread_failure=1; CHECK(init(&c)==-13); CHECK(starts==1 && !claims);
    } else if (!strcmp(scenario,"claim") || !strcmp(scenario,"empty")) {
        claim_failure=!strcmp(scenario,"claim"); empty_original=!strcmp(scenario,"empty");
        CHECK(init(&c)==-20); CHECK(state.load()==-1); CHECK(claims==1);
        WorkerState w; CHECK(!worker_cycle(w)); test_tid=(unsigned)getpid(); entry();
        Record r; CHECK(!pop(r));
    } else {
        CHECK(init(&c)==0); CHECK(claims==1 && starts==1); test_tid=pid;
        CHECK(firmware_reads==1);
        if (!strcmp(scenario,"normal")) {
            unsigned before=logs; entry(); CHECK(logs==before); Record r;
            CHECK(pop(r)); CHECK(r.x0==1 && r.x1==UINTPTR_MAX);
            CHECK(r.x2==UINT64_C(0xfedcba9876540000)); CHECK(r.w3==UINT32_MAX && r.w4==0x80000000);
            CHECK(r.caller==UINT64_C(0xdeadbeef1adef580) && r.sequence==1 && r.tid==pid); CHECK(!pop(r));
            live.store(0); entry(); CHECK(!pop(r)); live.store(1);
            state.store(-1); entry(); CHECK(!pop(r)); state.store(1);
            for (unsigned i=0;i<40;++i) entry(i);
            CHECK(dropped.load()==9); for (unsigned i=0;i<31;++i) { CHECK(pop(r)); CHECK(r.sequence==i+2 && r.x0==i+1); }
            CHECK(!pop(r)); entry(91); CHECK(pop(r)); CHECK(r.sequence==42 && r.x0==92);
        } else if (!strcmp(scenario,"thread") || !strcmp(scenario,"thread-zero") || !strcmp(scenario,"thread-negative")) {
            bool zero=strcmp(scenario,"thread")!=0;
            long raw=!strcmp(scenario,"thread-negative")?-1L:(zero?0L:(long)pid+1);
            std::thread other([&]{test_tid=raw; test_tid_errno=ERANGE;
                test_helper_errno=ENOMEM; errno=EDOM; entry(); CHECK(errno==EDOM);}); other.join();
            Record r;
            if (zero) {
                CHECK(tid_failures.load()==1 && off_state.load()==OFF_EMPTY);
                CHECK(!pop_off(r));
            } else {
                CHECK(pop_off(r)); CHECK(r.tid==pid+1 && r.sequence==0);
                CHECK(r.x2==UINT64_C(0xfedcba9876540000));
                CHECK(off_state.load()==OFF_CONSUMED && !pop_off(r));
            }
            entry(); CHECK(pop(r) && r.tid==pid && r.sequence==1);
            WorkerState w; CHECK(worker_cycle(w)); CHECK(live.load());
        } else if (!strcmp(scenario,"tid-normalization")) {
            CHECK(normalize_tid(-1)==0 && normalize_tid(LONG_MIN)==0);
            CHECK(normalize_tid(0)==0 && normalize_tid(1)==1);
            CHECK(normalize_tid((long)pid)==pid);
            CHECK(normalize_tid((long)UINT_MAX)==UINT_MAX);
            if (sizeof(long)>sizeof(unsigned)) {
                CHECK(normalize_tid((long)((unsigned long)UINT_MAX+1))==0);
                CHECK(normalize_tid(LONG_MAX)==0);
            }
            test_tid=-1; test_tid_errno=ERANGE; errno=EDOM; entry(); Record r;
            CHECK(errno==EDOM && tid_failures.load()==1 && !pop(r) && !pop_off(r));
            test_tid=pid; errno=EIO; entry();
            CHECK(errno==EIO && pop(r) && r.sequence==1 && r.tid==pid);
        } else if (!strcmp(scenario,"errno")) {
            CHECK(noexcept(az_dispatch_observe_entry(0,0,0,0,0,0)));
            test_tid_errno=ERANGE; test_helper_errno=ENOMEM;
            errno=EDOM; unsigned before=test_tid_calls; entry();
            CHECK(errno==EDOM && test_tid_calls==before+1); Record r; CHECK(pop(r));
            // Suppressed, state-not-ready and permanently disabled paths also preserve errno.
            live.store(0); before=test_tid_calls; errno=E2BIG; entry();
            CHECK(errno==E2BIG && test_tid_calls==before && !pop(r)); live.store(1);
            state.store(0); errno=EBUSY; entry(); CHECK(errno==EBUSY && !pop(r)); state.store(1);
            state.store(-1); errno=ENOENT; entry(); CHECK(errno==ENOENT && !pop(r)); state.store(1);
            // Full queue still records no log/allocation and preserves errno through overflow.
            unsigned before_logs=logs;
            for (unsigned i=0;i<32;++i) { errno=EIO; entry(i); CHECK(errno==EIO); }
            CHECK(dropped.load()==1 && logs==before_logs);
            while (pop(r)) {}
        } else if (!strcmp(scenario,"mailbox")) {
            std::atomic<bool> go(false);
            std::thread writers[8];
            for (unsigned i=0;i<8;++i) writers[i]=std::thread([&,i]{
                test_tid=pid+10+i; test_tid_errno=ERANGE; test_helper_errno=ENOMEM;
                while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
                errno=EIO; entry(700+i); CHECK(errno==EIO);
            });
            go.store(true,std::memory_order_release);
            for (auto &writer:writers) writer.join();
            CHECK(off_state.load()==OFF_READY && off_skipped.load()==7 && sequence==0);
            Record r; CHECK(pop_off(r)); unsigned i=(unsigned)r.x0-1;
            CHECK(i>=700 && i<708); CHECK(r.tid==pid+10+i-700 && r.sequence==0);
            CHECK(r.x1==UINTPTR_MAX-i && r.x2==UINT64_C(0xfedcba9876540000)+i);
            CHECK(r.w3==UINT32_MAX-i && r.w4==0x80000000+i);
            CHECK(r.caller==UINT64_C(0xdeadbeef1adef580)+i);
            CHECK(off_state.load()==OFF_CONSUMED && !pop_off(r));
            std::thread later([]{test_tid=pid+30; test_helper_errno=ENOMEM; errno=E2BIG;
                entry(900); CHECK(errno==E2BIG);}); later.join();
            CHECK(off_skipped.load()==8 && off_state.load()==OFF_CONSUMED && !pop_off(r));
            entry(); CHECK(pop(r) && r.sequence==1 && r.tid==pid);
        } else if (!strcmp(scenario,"partial")) {
            static std::atomic<bool> paused(false), release(false);
            test_before_off_copy=[]{
                paused.store(true,std::memory_order_release);
                while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
            };
            std::thread writer([]{test_tid=pid+1; test_tid_errno=ERANGE; errno=EDOM;
                entry(42); CHECK(errno==EDOM);});
            while (!paused.load(std::memory_order_acquire)) std::this_thread::yield();
            CHECK(off_state.load()==OFF_WRITING);
            Record r={123,1,2,3,4,5,6,7}, sentinel=r;
            CHECK(!pop_off(r)); CHECK(!memcmp(&r,&sentinel,sizeof r));
            WorkerState w; CHECK(worker_cycle(w)); CHECK(records==0);
            std::thread loser([]{test_tid=pid+2; test_helper_errno=ENOMEM; errno=EIO;
                entry(99); CHECK(errno==EIO);}); loser.join();
            CHECK(off_skipped.load()==1 && off_state.load()==OFF_WRITING);
            entry(7); CHECK(pop(r) && r.sequence==1 && r.x0==8);
            release.store(true,std::memory_order_release); writer.join(); test_before_off_copy=nullptr;
            CHECK(pop_off(r)); CHECK(r.sequence==0 && r.x0==43 && r.tid==pid+1);
            CHECK(r.x1==UINTPTR_MAX-42 && r.x2==UINT64_C(0xfedcba9876540000)+42);
            CHECK(r.w3==UINT32_MAX-42 && r.w4==0x80000000+42);
            CHECK(r.caller==UINT64_C(0xdeadbeef1adef580)+42 && !pop_off(r));
        } else if (!strcmp(scenario,"nested")) {
            test_tid_errno=ERANGE; test_helper_errno=ENOMEM;
            test_inside_producer=[] { int saved=errno; errno=ENOTTY; entry(999);
                CHECK(errno==ENOTTY); errno=saved; };
            errno=EDOM; entry(); CHECK(errno==EDOM);
            Record r; CHECK(pop(r) && r.x0==1 && r.sequence==1 && !pop(r));
            CHECK(nested_dropped.load()==1 && producer_busy.load()==0);
            test_inside_producer=nullptr;
            producer_busy.store(1); errno=EBUSY; entry();
            CHECK(errno==EBUSY && nested_dropped.load()==2 && !pop(r));
            producer_busy.store(0); entry(1); CHECK(pop(r) && r.x0==2 && r.sequence==2);
        } else if (!strcmp(scenario,"diagnostics")) {
            unsigned before=logs; WorkerState w; test_tid=pid+1;
            for (unsigned i=0;i<20;++i) { entry(i); dispatch_observer_tick(); CHECK(worker_cycle(w)); }
            CHECK(records==1 && w.diagnostics==DIAGNOSTIC_BUDGET && logs==before+1+DIAGNOSTIC_BUDGET);
            CHECK(off_skipped.load()==19 && off_state.load()==OFF_CONSUMED);
            test_tid=pid; entry(); dispatch_observer_tick(); CHECK(worker_cycle(w)); CHECK(records==2);
        } else if (!strcmp(scenario,"heartbeat")) {
            WorkerState w; for (int i=0;i<5;++i) CHECK(worker_cycle(w)); CHECK(!live.load());
            entry(); Record r; CHECK(!pop(r)); dispatch_observer_tick(); CHECK(worker_cycle(w)); CHECK(live.load());
            entry(); CHECK(pop(r));
        } else if (!strcmp(scenario,"budget")) {
            WorkerState w; bool running=true;
            for (unsigned i=0;i<129;++i) { entry(i); dispatch_observer_tick(); running=worker_cycle(w); CHECK(running==(i<128)); }
            CHECK(records==128 && !live.load()); entry(); Record r; CHECK(!pop(r));
        } else if (!strcmp(scenario,"budget-off")) {
            WorkerState w;
            std::thread other([]{test_tid=pid+1; entry();}); other.join();
            dispatch_observer_tick(); CHECK(worker_cycle(w)); CHECK(records==1 && w.logged==1);
            for (unsigned i=0;i<128;++i) { entry(i); dispatch_observer_tick();
                CHECK(worker_cycle(w)==(i<127)); }
            CHECK(records==128 && w.logged==128 && !live.load());
        } else if (!strcmp(scenario,"concurrent")) {
            std::atomic<bool> done(false); unsigned received=0; uint64_t previous=0;
            std::thread consumer([&]{Record r; for (;;) {
                if (pop(r)) {
                    CHECK(r.sequence>previous); previous=r.sequence; CHECK(r.x0==r.sequence);
                    CHECK(r.x1==UINTPTR_MAX-(r.sequence-1));
                    CHECK(r.x2==UINT64_C(0xfedcba9876540000)+(r.sequence-1));
                    CHECK(r.w3==UINT32_MAX-(r.sequence-1)); CHECK(r.w4==0x80000000+(r.sequence-1));
                    CHECK(r.caller==UINT64_C(0xdeadbeef1adef580)+(r.sequence-1)); CHECK(r.tid==pid); ++received;
                } else if (done.load(std::memory_order_acquire)) {
                    if (read_index.load()==write_index.load()) break;
                } else std::this_thread::yield();
            }});
            for (unsigned i=0;i<2000;++i) {
                while ((write_index.load(std::memory_order_relaxed)+1)%QUEUE_SIZE==
                       read_index.load(std::memory_order_acquire)) std::this_thread::yield();
                entry(i);
            }
            done.store(true,std::memory_order_release); consumer.join();
            CHECK(received==2000 && !dropped.load()); CHECK(sequence==2000);
            CHECK(!nested_dropped.load() && off_state.load()==OFF_EMPTY && !tid_failures.load());
        } else { CHECK(false); }
    }
    printf("%s: %u assertions passed\n",scenario,checks.load()); return 0;
}
