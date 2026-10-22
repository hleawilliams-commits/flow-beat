/* SPDX-License-Identifier: GPL-3.0-only */
#define AZDISPATCH_TEST
#include "../observer.cpp"
#include <stdio.h>
#include <stdarg.h>
#include <thread>
using namespace ero;
static std::atomic<unsigned> checks(0);
static unsigned claims, starts, logs, records, raw_records, registrations;
static int registration_failure;
static az_frame_fn registered_frame;
static unsigned registered_mask;
static unsigned firmware_reads;
static int claim_failure, thread_failure;
static bool empty_original;
static const char *tag=firmware;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
extern "C" void az_dispatch_entry(void) {} // Assembly is exercised separately in abi/.
static void native_target(void) {}
static const char *fw() { ++firmware_reads; return tag; }
static void logger(const char *,const char *format,...) {
    ++logs; if (!strncmp(format,"ERO entry ",10)) ++records;
    if (!strncmp(format,"ERO raw ",8)) ++raw_records;
}
static int register_frame(const char *name,unsigned mask,az_frame_fn fn,void *user) {
    CHECK(!strcmp(name,id)); CHECK(mask==(AZ_FILTER_RAW|(1u<<AZ_BUS_MIXER_IN)));
    CHECK(fn==raw_frame && user==nullptr); CHECK(!claims && !starts); ++registrations;
    if (registration_failure) return -1;
    registered_frame=fn; registered_mask=mask;
    unsigned char f[40]={}; CHECK(fn(AZ_BUS_MIXER_IN,f,sizeof f,nullptr)==0);
    CHECK(raw_sequence==0 && raw_state_gated.load()==1);
    return 0;
}
static int start_thread(const char *name,const char *,void *(*fn)(void *),void *) {
    CHECK(!strcmp(name,id)); CHECK(fn==worker); CHECK(registrations==1 && !claims); ++starts; return thread_failure;
}
static int claim(const char *name,uintptr_t address,void *hook,void **original) {
    CHECK(!strcmp(name,id)); CHECK(address==ENTRY); CHECK(registrations==1 && starts==1); CHECK(hook==(void *)az_dispatch_entry);
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
    c.frame_filter=register_frame; c.thread_start=start_thread; c.claim_hook=claim; c.log=logger; return c;
}
static void entry(unsigned i=0) {
    az_dispatch_observe_entry(1+i,UINTPTR_MAX-i,UINT64_C(0xfedcba9876540000)+i,
                             0xffffffff-i,0x80000000+i,UINT64_C(0xdeadbeef1adef580)+i);
}
int main(int argc,char **argv) {
    CHECK(argc==2); const char *scenario=argv[1]; auto c=mock_core();
    setenv("AZ_ENCODER_ROUTE_OBSERVER_EMULATOR","1",1);
    if (!strcmp(scenario,"env")) {
        unsetenv("AZ_ENCODER_ROUTE_OBSERVER_EMULATOR"); CHECK(init(&c)==-9);
        setenv("AZ_ENCODER_ROUTE_OBSERVER_EMULATOR","true",1); CHECK(init(&c)==-9);
        CHECK(!claims && !starts);
    } else if (!strcmp(scenario,"sdk")) {
        CHECK(init(NULL)==-10); c.size=offsetof(az_core_v1,fw_tag); CHECK(init(&c)==-10);
        c=mock_core(); c.size=-1; CHECK(init(&c)==-10);
        c=mock_core(); c.abi=AZ_MOD_ABI+1; CHECK(init(&c)==-10);
        c=mock_core(); c.fw_tag=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.claim_hook=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.thread_start=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.log=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.frame_filter=NULL; CHECK(init(&c)==-10); CHECK(!claims && !starts);
        CHECK(!firmware_reads);
    } else if (!strcmp(scenario,"firmware")) {
        tag=NULL; CHECK(init(&c)==-10); tag="other"; CHECK(init(&c)==-10); CHECK(!claims && !starts);
    } else if (!strcmp(scenario,"prologue")) {
        test_prologues=false; CHECK(init(&c)==-12); CHECK(!claims && !starts);
    } else if (!strcmp(scenario,"registration")) {
        registration_failure=1; CHECK(init(&c)==-14); CHECK(registrations==1 && !claims && !starts);
        CHECK(state.load()==-1);
    } else if (!strcmp(scenario,"worker")) {
        thread_failure=1; CHECK(init(&c)==-13); CHECK(starts==1 && !claims);
        CHECK(state.load()==-1 && registered_frame); unsigned char f[36]={};
        CHECK(registered_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0); CHECK(raw_state_gated.load()==2 && !raw_sequence);
    } else if (!strcmp(scenario,"claim") || !strcmp(scenario,"empty")) {
        claim_failure=!strcmp(scenario,"claim"); empty_original=!strcmp(scenario,"empty");
        CHECK(init(&c)==-20); CHECK(state.load()==-1); CHECK(claims==1);
        WorkerState w; CHECK(!worker_cycle(w)); test_tid=(unsigned)getpid(); entry();
        Record r; CHECK(!pop(r));
        unsigned char f[36]={}; CHECK(registered_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
        CHECK(raw_state_gated.load()==2 && raw_sequence==0 && native_state_gated.load()>=1);
    } else {
        CHECK(init(&c)==0); CHECK(claims==1 && starts==1); test_tid=pid;
        CHECK(firmware_reads==1);
        CHECK(registered_frame==raw_frame && registered_mask==(AZ_FILTER_RAW|(1u<<AZ_BUS_MIXER_IN)));
        // Mock registration and native redirect invoked helpers while init was gated.
        CHECK(raw_attempted.load()==1 && native_attempted.load()==1);
        CHECK(native_state_gated.load()==1 && raw_state_gated.load()==1);
        if (!strcmp(scenario,"native-gates")) {
            unsigned calls=test_tid_calls;
            test_helper_errno=ENOMEM; test_tid_errno=ERANGE;
            state.store(0); errno=EIO; entry(); CHECK(errno==EIO);
            state.store(-1); errno=E2BIG; entry(); CHECK(errno==E2BIG);
            CHECK(native_attempted.load()==3 && native_state_gated.load()==3);
            CHECK(test_tid_calls==calls && !sequence);
            state.store(1); live.store(0); errno=EDOM; entry(); CHECK(errno==EDOM);
            CHECK(native_attempted.load()==4 && native_live_gated.load()==1);
            CHECK(test_tid_calls==calls && !sequence);
            live.store(1); entry(); CHECK(native_attempted.load()==5 && test_tid_calls==calls+1);
            Record r; CHECK(pop(r) && r.sequence==1);
        } else if (!strcmp(scenario,"raw-gates")) {
            unsigned char f[36]={}; RawRecord r;
            state.store(0); errno=EIO; CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
            state.store(-1); CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
            CHECK(raw_attempted.load()==3 && raw_state_gated.load()==3 && !raw_heartbeat.load());
            state.store(1); live.store(0); CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
            CHECK(raw_attempted.load()==4 && raw_live_gated.load()==1 && !pop_raw(r));
            live.store(1);
            CHECK(raw_frame(2,f,36,nullptr)==0); CHECK(raw_frame(AZ_BUS_MIXER_IN,nullptr,36,nullptr)==0);
            for(int n=-1;n<36;++n) CHECK(raw_frame(AZ_BUS_MIXER_IN,f,n,nullptr)==0);
            CHECK(raw_invalid.load()==39 && raw_attempted.load()==43 && !raw_heartbeat.load());
            CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
            CHECK(raw_heartbeat.load()==1 && pop_raw(r) && r.sequence==1);
            CHECK(errno==EIO);
        } else if (!strcmp(scenario,"raw-values")) {
            unsigned char f[152], before[152];
            for(unsigned i=0;i<sizeof f;++i) f[i]=(unsigned char)(i*7);
            memcpy(before,f,sizeof f); unsigned calls=test_tid_calls, before_logs=logs;
            test_helper_errno=ENOMEM; test_tid_errno=ERANGE; errno=EIO;
            CHECK(raw_frame(AZ_BUS_MIXER_IN,f,sizeof f,nullptr)==0);
            CHECK(!memcmp(before,f,sizeof f)); CHECK(errno==EIO && test_tid_calls==calls && logs==before_logs);
            RawRecord r; CHECK(pop_raw(r) && r.sequence==1);
            CHECK(r.values[0]==f[6] && r.values[1]==f[31] && r.values[2]==f[32]);
            CHECK(r.values[3]==f[33] && r.values[4]==f[34] && r.values[5]==f[35]);
            CHECK(raw_frame(AZ_BUS_MIXER_IN,f,sizeof f,nullptr)==0 && !pop_raw(r));
            f[0]^=1; f[36]^=1; CHECK(raw_frame(AZ_BUS_MIXER_IN,f,sizeof f,nullptr)==0 && !pop_raw(r));
            const unsigned positions[]={6,31,32,33,34,35};
            for(unsigned i=0;i<6;++i) {
                f[positions[i]]^=1; memcpy(before,f,sizeof f);
                CHECK(raw_frame(AZ_BUS_MIXER_IN,f,sizeof f,nullptr)==0);
                CHECK(!memcmp(before,f,sizeof f)); CHECK(pop_raw(r) && r.sequence==i+2);
                CHECK(r.values[i]==f[positions[i]]);
            }
            CHECK(raw_heartbeat.load()==9 && raw_sequence==7 && !raw_dropped.load());
            f[34]=255; raw_frame(AZ_BUS_MIXER_IN,f,sizeof f,nullptr); CHECK(pop_raw(r) && r.values[4]==255);
            f[34]=0; raw_frame(AZ_BUS_MIXER_IN,f,sizeof f,nullptr); CHECK(pop_raw(r) && r.values[4]==0);
            CHECK(!native_live_gated.load() && sequence==0); entry(); Record n; CHECK(pop(n) && n.sequence==1);
        } else if (!strcmp(scenario,"raw-overflow")) {
            unsigned char f[36]={};
            for(unsigned i=0;i<40;++i) { f[34]=(unsigned char)i; CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0); }
            CHECK(raw_sequence==40 && raw_dropped.load()==9 && raw_heartbeat.load()==40);
            RawRecord r; for(unsigned i=0;i<31;++i) CHECK(pop_raw(r) && r.sequence==i+1 && r.values[4]==i);
            CHECK(!pop_raw(r)); raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr); CHECK(!pop_raw(r));
            f[34]=40; raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr); CHECK(pop_raw(r) && r.sequence==41);
            CHECK(!dropped.load() && sequence==0);
        } else if (!strcmp(scenario,"raw-nested")) {
            test_inside_raw=[] { unsigned char f[36]={}; CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0); };
            unsigned char f[36]={}; errno=EIO; CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
            CHECK(errno==EIO && raw_nested_dropped.load()==1 && raw_producer_busy.load()==0);
            RawRecord r; CHECK(pop_raw(r) && r.sequence==1 && !pop_raw(r));
            test_inside_raw=nullptr; raw_producer_busy.store(1); f[34]=1;
            CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
            CHECK(raw_nested_dropped.load()==2 && !pop_raw(r));
            raw_producer_busy.store(0); CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
            CHECK(pop_raw(r) && r.sequence==2 && r.values[4]==1);
        } else if (!strcmp(scenario,"raw-partial")) {
            static std::atomic<bool> paused(false), release(false);
            test_inside_raw=[] { paused.store(true,std::memory_order_release);
                while(!release.load(std::memory_order_acquire)) std::this_thread::yield(); };
            std::thread first([] { unsigned char f[36]={}; f[6]=17; f[34]=251;
                errno=EIO; CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0); CHECK(errno==EIO); });
            while(!paused.load(std::memory_order_acquire)) std::this_thread::yield();
            RawRecord r={123,{1,2,3,4,5,6}}, saved=r;
            CHECK(raw_producer_busy.load()==1 && !pop_raw(r) && !memcmp(&r,&saved,sizeof r));
            std::thread losers[8];
            for(auto &t:losers) t=std::thread([]{unsigned char f[36]={}; CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);});
            for(auto &t:losers) t.join();
            CHECK(raw_nested_dropped.load()==8 && !pop_raw(r));
            entry(); Record n; CHECK(pop(n) && n.sequence==1);
            release.store(true,std::memory_order_release); first.join(); test_inside_raw=nullptr;
            CHECK(pop_raw(r) && r.sequence==1 && r.values[0]==17 && r.values[4]==251);
            CHECK(raw_producer_busy.load()==0 && raw_heartbeat.load()==9);
        } else if (!strcmp(scenario,"raw-budget")) {
            unsigned char f[36]={}; WorkerState w;
            for(unsigned i=0;i<129;++i) { f[34]=(unsigned char)i; raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr);
                route_observer_tick(); CHECK(worker_cycle(w)==(i<128)); }
            CHECK(raw_records==128 && w.raw_logged==128 && !live.load());
            entry(); Record n; CHECK(!pop(n) && native_live_gated.load()==1);
            raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr); CHECK(raw_live_gated.load()==1);
        } else if (!strcmp(scenario,"raw-diagnostics")) {
            WorkerState w; unsigned char f[36]={}; unsigned before=logs;
            for(unsigned i=0;i<20;++i) {
                raw_frame(2,f,36,nullptr); route_observer_tick(); CHECK(worker_cycle(w));
            }
            CHECK(w.diagnostics==DIAGNOSTIC_BUDGET && logs==before+DIAGNOSTIC_BUDGET);
            CHECK(raw_invalid.load()==20 && !raw_records && !records);
        } else if (!strcmp(scenario,"lanes-concurrent")) {
            std::atomic<bool> done(false); unsigned received=0;
            std::thread reader([&]{RawRecord r; for(;;) {
                if(pop_raw(r)) {
                    CHECK(r.sequence==received+1);
                    CHECK(r.values[0]==(unsigned char)(received>>8));
                    CHECK(r.values[1]==(unsigned char)received && r.values[2]==(unsigned char)(received+1));
                    CHECK(r.values[3]==(unsigned char)(received+2) && r.values[4]==(unsigned char)(received+3));
                    CHECK(r.values[5]==(unsigned char)(received+4)); ++received;
                } else if(done.load(std::memory_order_acquire)) {
                    if(raw_read_index.load()==raw_write_index.load()) break;
                } else std::this_thread::yield();
            }});
            for(unsigned i=0;i<2000;++i) {
                while((raw_write_index.load(std::memory_order_relaxed)+1)%QUEUE_SIZE==raw_read_index.load(std::memory_order_acquire)) std::this_thread::yield();
                unsigned char f[36]={}; f[6]=(unsigned char)(i>>8);
                for(unsigned j=0;j<5;++j) f[31+j]=(unsigned char)(i+j);
                CHECK(raw_frame(AZ_BUS_MIXER_IN,f,36,nullptr)==0);
                entry(i); Record n; CHECK(pop(n) && n.sequence==i+1 && n.x0==i+1);
            }
            done.store(true,std::memory_order_release); reader.join();
            CHECK(received==2000 && raw_sequence==2000 && sequence==2000);
            CHECK(!raw_dropped.load() && !raw_nested_dropped.load() && !dropped.load());
            CHECK(raw_heartbeat.load()==2000 && native_attempted.load()==2001);
        } else if (!strcmp(scenario,"normal")) {
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
            // Full queue still records no log and preserves errno through overflow.
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
            for (unsigned i=0;i<20;++i) { entry(i); route_observer_tick(); CHECK(worker_cycle(w)); }
            CHECK(records==1 && w.diagnostics==DIAGNOSTIC_BUDGET && logs==before+1+DIAGNOSTIC_BUDGET);
            CHECK(off_skipped.load()==19 && off_state.load()==OFF_CONSUMED);
            test_tid=pid; entry(); route_observer_tick(); CHECK(worker_cycle(w)); CHECK(records==2);
        } else if (!strcmp(scenario,"heartbeat")) {
            WorkerState w; for (int i=0;i<5;++i) CHECK(worker_cycle(w)); CHECK(!live.load());
            entry(); Record r; CHECK(!pop(r)); route_observer_tick(); CHECK(worker_cycle(w)); CHECK(live.load());
            entry(); CHECK(pop(r));
        } else if (!strcmp(scenario,"budget")) {
            WorkerState w; bool running=true;
            for (unsigned i=0;i<129;++i) { entry(i); route_observer_tick(); running=worker_cycle(w); CHECK(running==(i<128)); }
            CHECK(records==128 && !live.load()); entry(); Record r; CHECK(!pop(r));
        } else if (!strcmp(scenario,"budget-off")) {
            WorkerState w;
            std::thread other([]{test_tid=pid+1; entry();}); other.join();
            route_observer_tick(); CHECK(worker_cycle(w)); CHECK(records==1 && w.logged==1);
            for (unsigned i=0;i<128;++i) { entry(i); route_observer_tick();
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
