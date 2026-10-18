/* SPDX-License-Identifier: GPL-3.0-only */
#define AZKNOB_TEST
#include "../observer.cpp"
#include <stdio.h>
#include <stdarg.h>
#include <thread>
using namespace ko;
static unsigned checks, claims, starts, logs, records;
static int claim_failure, thread_failure;
static bool empty_original;
static const char *tag=firmware;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
extern "C" void az_knob_entry(void) {} // Assembly is exercised separately in abi/.
static void native_target(void) {}
static const char *fw() { return tag; }
static void logger(const char *,const char *format,...) {
    ++logs; if (!strncmp(format,"WKO entry ",10)) ++records;
}
static int start_thread(const char *name,const char *,void *(*fn)(void *),void *) {
    CHECK(!strcmp(name,id)); CHECK(fn==worker); ++starts; return thread_failure;
}
static int claim(const char *name,uintptr_t address,void *hook,void **original) {
    CHECK(!strcmp(name,id)); CHECK(address==ENTRY); CHECK(hook==(void *)az_knob_entry);
    CHECK(original==&az_knob_original); ++claims;
    if (claim_failure) return -1;
    if (!empty_original) __atomic_store_n(original,(void *)native_target,__ATOMIC_RELEASE);
    return 0;
}
static az_core_v1 mock_core() {
    az_core_v1 c={}; c.abi=AZ_MOD_ABI; c.size=sizeof c; c.fw_tag=fw;
    c.thread_start=start_thread; c.claim_hook=claim; c.log=logger; return c;
}
static void entry(unsigned i=0) { az_knob_observe_entry(1+i,UINTPTR_MAX-i,0xffffffff-i,0x80000000+i,0x1adeab0); }
int main(int argc,char **argv) {
    CHECK(argc==2); const char *scenario=argv[1]; auto c=mock_core();
    setenv("AZ_WAVEFORM_KNOB_OBSERVER_EMULATOR","1",1);
    if (!strcmp(scenario,"env")) {
        unsetenv("AZ_WAVEFORM_KNOB_OBSERVER_EMULATOR"); CHECK(init(&c)==-9);
        setenv("AZ_WAVEFORM_KNOB_OBSERVER_EMULATOR","true",1); CHECK(init(&c)==-9);
        CHECK(!claims && !starts);
    } else if (!strcmp(scenario,"sdk")) {
        CHECK(init(NULL)==-10); c.size=offsetof(az_core_v1,fw_tag); CHECK(init(&c)==-10);
        c=mock_core(); c.claim_hook=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.thread_start=NULL; CHECK(init(&c)==-10);
        c=mock_core(); c.log=NULL; CHECK(init(&c)==-10); CHECK(!claims && !starts);
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
        if (!strcmp(scenario,"normal")) {
            unsigned before=logs; entry(); CHECK(logs==before); Record r;
            CHECK(pop(r)); CHECK(r.x0==1 && r.x1==UINTPTR_MAX); CHECK(r.w2==UINT32_MAX && r.w3==0x80000000);
            CHECK(r.caller==0x1adeab0 && r.sequence==1 && r.tid==pid); CHECK(!pop(r));
            live.store(0); entry(); CHECK(!pop(r)); live.store(1);
            state.store(-1); entry(); CHECK(!pop(r)); state.store(1);
            for (unsigned i=0;i<40;++i) entry(i);
            CHECK(dropped.load()==9); for (unsigned i=0;i<31;++i) { CHECK(pop(r)); CHECK(r.sequence==i+2 && r.x0==i+1); }
            CHECK(!pop(r)); entry(91); CHECK(pop(r)); CHECK(r.sequence==42 && r.x0==92);
        } else if (!strcmp(scenario,"thread")) {
            std::thread other([]{test_tid=pid+1; entry();}); other.join();
            CHECK(bad_thread.load()==pid+1); entry(); Record r; CHECK(!pop(r));
            WorkerState w; CHECK(!worker_cycle(w)); CHECK(!live.load());
        } else if (!strcmp(scenario,"heartbeat")) {
            WorkerState w; for (int i=0;i<5;++i) CHECK(worker_cycle(w)); CHECK(!live.load());
            entry(); Record r; CHECK(!pop(r)); knob_observer_tick(); CHECK(worker_cycle(w)); CHECK(live.load());
            entry(); CHECK(pop(r));
        } else if (!strcmp(scenario,"budget")) {
            WorkerState w; bool running=true;
            for (unsigned i=0;i<129;++i) { entry(i); knob_observer_tick(); running=worker_cycle(w); CHECK(running==(i<128)); }
            CHECK(records==128 && !live.load()); entry(); Record r; CHECK(!pop(r));
        } else if (!strcmp(scenario,"concurrent")) {
            std::atomic<bool> done(false); unsigned received=0; uint64_t previous=0;
            std::thread consumer([&]{Record r; for (;;) {
                if (pop(r)) {
                    CHECK(r.sequence>previous); previous=r.sequence; CHECK(r.x0==r.sequence);
                    CHECK(r.x1==UINTPTR_MAX-(r.sequence-1)); CHECK(r.w2==UINT32_MAX-(r.sequence-1));
                    CHECK(r.w3==0x80000000+(r.sequence-1)); CHECK(r.tid==pid); ++received;
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
            CHECK(received==2000 && !dropped.load()); CHECK(sequence==2000); CHECK(!bad_thread.load());
        } else { CHECK(false); }
    }
    printf("%s: %u assertions passed\n",scenario,checks); return 0;
}
