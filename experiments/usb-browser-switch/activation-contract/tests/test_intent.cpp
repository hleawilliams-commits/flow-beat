// SPDX-License-Identifier: GPL-3.0-only
#include "../intent.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace usb_intent;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"failed at %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
int main() {
    Mailbox m;
    CHECK(m.request(1,0).result == Result::Disabled);
    CHECK(m.request(3,0).result == Result::BadPort);
    m.enable(); auto a=m.request(1,10);
    CHECK(a.result == Result::Queued && Mailbox::port(a.ticket)==1);
    CHECK(m.request(2,10).result == Result::Busy);
    CHECK(m.pending(2009).ticket.value == a.ticket.value);
    CHECK(m.pending(2010).result == Result::Expired);
    CHECK(m.enter(a.ticket,2010,Resolution::Ready).result == Result::Stale);
    auto b=m.request(2,0xfffffff0u);
    CHECK(b.result == Result::Queued && Mailbox::port(b.ticket)==2);
    CHECK(m.pending(20).ticket.value == b.ticket.value); // Monotonic wrap.
    CHECK(m.enter(b.ticket,30,Resolution::Ready).result == Result::Entered);
    CHECK(m.request(1,30).result == Result::Busy); // Native reentrancy.
    CHECK(m.cancel(b.ticket).result == Result::Stale); // Cannot undo entered work.
    m.disable(); m.enable();
    CHECK(m.request(1,31).result == Result::Busy);
    CHECK(m.finish(b.ticket).result == Result::Finished);
    CHECK(m.finish(b.ticket).result == Result::Stale);
    for (auto resolution : {Resolution::Missing,Resolution::Ambiguous,Resolution::Unavailable}) {
        auto t=m.request(1,100);
        CHECK(t.result==Result::Queued);
        auto e=m.enter(t.ticket,100,resolution);
        CHECK(e.result!=Result::Entered);
        CHECK(m.enter(t.ticket,100,Resolution::Ready).result==Result::Stale);
    }
    auto old=m.request(1,100); m.disable(); m.enable(); auto fresh=m.request(2,100);
    CHECK(old.ticket.value!=fresh.ticket.value);
    CHECK(m.enter(old.ticket,101,Resolution::Ready).result==Result::Stale);
    CHECK(m.cancel(old.ticket).result==Result::Stale);
    CHECK(m.cancel(fresh.ticket).result==Result::Cancelled);
    auto back=m.request(1,500);
    CHECK(m.enter(back.ticket,499,Resolution::Ready).result==Result::Expired);
    auto thrown=m.request(1,600);
    CHECK(m.enter(thrown.ticket,600,Resolution::Ready).result==Result::Entered);
    try { Finish f{m,thrown.ticket}; throw 1; } catch(int) {}
    CHECK(m.request(2,601).result==Result::Queued);
    m.disable();m.enable();
    // Real competing dispatchers: exactly one can cross the native entry gate.
    for (unsigned i=0;i<100;++i) {
        auto t=m.request(i%2+1,1000); CHECK(t.result==Result::Queued);
        std::atomic<bool> go(false); std::atomic<unsigned> entered(0);
        auto race=[&] { while(!go.load(std::memory_order_acquire)) {};
            if(m.enter(t.ticket,1000,Resolution::Ready).result==Result::Entered) entered.fetch_add(1); };
        std::thread first(race),second(race);go.store(true,std::memory_order_release);
        first.join();second.join();CHECK(entered==1);
        CHECK(m.finish(t.ticket).result==Result::Finished);
    }
    // Disable and native entry race at one atomic word. Either winner is allowed;
    // after disable returns no still-pending action may start.
    for (unsigned i=0;i<100;++i) {
        m.enable();auto t=m.request(1,1000);CHECK(t.result==Result::Queued);
        std::atomic<bool> go(false);Reply entry{Result::Stale,{}};
        std::thread actor([&]{while(!go.load(std::memory_order_acquire)){};
            entry=m.enter(t.ticket,1000,Resolution::Ready);});
        std::thread disabler([&]{while(!go.load(std::memory_order_acquire)){};m.disable();});
        go.store(true,std::memory_order_release);actor.join();disabler.join();
        CHECK(m.request(2,1000).result==Result::Disabled);
        CHECK(m.enter(t.ticket,1001,Resolution::Ready).result!=Result::Entered);
        if(entry.result==Result::Entered) CHECK(m.finish(t.ticket).result==Result::Finished);
        else CHECK(m.finish(t.ticket).result==Result::Stale);
    }
    std::printf("%u checks passed\n",checks);
}
