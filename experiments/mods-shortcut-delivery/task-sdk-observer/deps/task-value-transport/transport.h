// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <limits>
#include "../task-capture-contract/capture.h"
#ifndef TVT_TEST_POINT
#define TVT_TEST_POINT(point, transport, index) ((void)0)
#endif

namespace tvt {
// Test-only scheduling seams. No calls/arguments are evaluated by default.
enum class HookPoint { GatePassed, BeforeReserve, Reserved, BeforeEntryCopy,
    BeforeEntryPublish, ReturnClaimed, BeforeReturnCopy, BeforeReturnPublish };
struct Counters {
    std::uint64_t attempted, gated, prefilter_rejected, capacity, contention,
        capture_rejected, entries, returns_attempted, return_invalid,
        return_scope_mismatch, return_duplicate, returns;
    std::uint32_t reserved;
};
struct ErrnoGuard { int saved=errno; ~ErrnoGuard() noexcept { errno=saved; } };
inline bool same_context(const tc_context& a,const tc_context& b) noexcept {
    return a.values.task_no==b.values.task_no && a.values.id==b.values.id &&
        a.values.delta_bits==b.values.delta_bits && a.values.cumulative_bits==b.values.cumulative_bits &&
        a.run_id==b.run_id && a.invocation_id==b.invocation_id &&
        a.thread_observation==b.thread_observation && a.phase==b.phase &&
        a.status==b.status && a.reserved==b.reserved;
}

// Construct once at quiescence, choose a fresh nonzero run ID, then start once.
// Never reset/reconstruct/reclaim storage while any caller or reader can use it.
// close() is a logical gate only: a begin which observed Open can finish later,
// and a previously reserved submission may publish its normal return afterwards.
template<std::size_t Capacity=16> class Transport {
    static_assert(Capacity>0 && Capacity<std::numeric_limits<std::uint32_t>::max(),"bounded slots");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free &&
                  std::atomic<std::uint64_t>::is_always_lock_free,"lock-free integer atomics required");
    enum : std::uint32_t { Configured=0, Open=1, Closed=2, ReturnEmpty=0, ReturnWriting=1, ReturnReady=2 };
    struct Slot {
        tc_context entry{};
        tc_return_record returned{};
        std::atomic<std::uint32_t> entry_ready{0}, return_state{ReturnEmpty};
    };
    const std::uint64_t run_id_;
    std::atomic<std::uint32_t> gate_{Configured}, claimed_{0};
    Slot slots_[Capacity];
    std::atomic<std::uint64_t> attempted_{0}, gated_{0}, prefilter_rejected_{0}, capacity_{0}, contention_{0},
        capture_rejected_{0}, entries_{0}, returns_attempted_{0}, return_invalid_{0},
        return_scope_mismatch_{0}, return_duplicate_{0}, returns_{0};
public:
    explicit Transport(std::uint64_t run_id) noexcept : run_id_(run_id) {}
    Transport(const Transport&)=delete;
    Transport& operator=(const Transport&)=delete;
    bool start() noexcept {
        ErrnoGuard guard;
        if (!run_id_) return false;
        std::uint32_t expected=Configured;
        return gate_.compare_exchange_strong(expected,Open,std::memory_order_release,std::memory_order_relaxed);
    }
    void close() noexcept { ErrnoGuard guard; gate_.store(Closed,std::memory_order_release); }
    // out is private, writable, and disjoint from task and this transport.
    // Native task storage must be valid and immutable for tc_begin's entry copy.
    // A rejected capture consumes its immutable reservation, publishes its
    // rejection entry, and leaves out zero/unqualified. No native read on a
    // disabled, wrong-phase/caller, full, or contended attempt.
    bool begin(tc_context* out,std::uint32_t phase,std::uint64_t original_lr,
               const void* task,std::uint64_t thread_observation) noexcept {
        ErrnoGuard guard;
        *out={};
        attempted_.fetch_add(1,std::memory_order_relaxed);
        if (gate_.load(std::memory_order_acquire)!=Open) {
            gated_.fetch_add(1,std::memory_order_relaxed); return false;
        }
        TVT_TEST_POINT(HookPoint::GatePassed,this,0);
        if (!run_id_ || (phase!=TC_SUBMISSION && phase!=TC_EXECUTION) ||
            original_lr!=(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR)) {
            prefilter_rejected_.fetch_add(1,std::memory_order_relaxed); return false;
        }
        std::uint32_t expected=claimed_.load(std::memory_order_relaxed);
        if (expected>=Capacity) { capacity_.fetch_add(1,std::memory_order_relaxed); return false; }
        TVT_TEST_POINT(HookPoint::BeforeReserve,this,expected);
        // Exactly one source-level strong reservation CAS; no retry loop.
        if (!claimed_.compare_exchange_strong(expected,expected+1,std::memory_order_relaxed,std::memory_order_relaxed)) {
            if (expected>=Capacity) capacity_.fetch_add(1,std::memory_order_relaxed);
            else contention_.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        const std::uint32_t index=expected;
        TVT_TEST_POINT(HookPoint::Reserved,this,index);
        tc_context local{};
        tc_begin(&local,phase,original_lr,task,run_id_,std::uint64_t(index)+1,thread_observation);
        Slot& slot=slots_[index];
        TVT_TEST_POINT(HookPoint::BeforeEntryCopy,this,index);
        slot.entry=local;
        TVT_TEST_POINT(HookPoint::BeforeEntryPublish,this,index);
        slot.entry_ready.store(1,std::memory_order_release);
        entries_.fetch_add(1,std::memory_order_relaxed);
        if (local.status!=TC_COPIED) {
            capture_rejected_.fetch_add(1,std::memory_order_relaxed); return false;
        }
        *out=local;
        return true;
    }
    // Call only on normal native return, with its own private pre-call copy.
    // No pointer/native input and no gate dependency. Nothing is held across
    // the original call. Duplicate after attempts can never rewrite a slot.
    bool finish(const tc_context& entry,std::uint64_t native_result) noexcept {
        ErrnoGuard guard;
        returns_attempted_.fetch_add(1,std::memory_order_relaxed);
        if (entry.run_id!=run_id_) { return_scope_mismatch_.fetch_add(1,std::memory_order_relaxed); return false; }
        if (!run_id_ || entry.phase!=TC_SUBMISSION || entry.status!=TC_COPIED ||
            !entry.invocation_id || entry.invocation_id>Capacity) {
            return_invalid_.fetch_add(1,std::memory_order_relaxed); return false;
        }
        const std::uint32_t index=static_cast<std::uint32_t>(entry.invocation_id-1);
        Slot& slot=slots_[index];
        if (!slot.entry_ready.load(std::memory_order_acquire) || !same_context(slot.entry,entry)) {
            return_invalid_.fetch_add(1,std::memory_order_relaxed); return false;
        }
        std::uint32_t expected=ReturnEmpty;
        if (!slot.return_state.compare_exchange_strong(expected,ReturnWriting,std::memory_order_relaxed,std::memory_order_relaxed)) {
            return_duplicate_.fetch_add(1,std::memory_order_relaxed); return false;
        }
        TVT_TEST_POINT(HookPoint::ReturnClaimed,this,index);
        TVT_TEST_POINT(HookPoint::BeforeReturnCopy,this,index);
        slot.returned=tc_return(&entry,native_result);
        TVT_TEST_POINT(HookPoint::BeforeReturnPublish,this,index);
        slot.return_state.store(ReturnReady,std::memory_order_release);
        returns_.fetch_add(1,std::memory_order_relaxed);
        return true;
    }
    // Each record has its own acquire. Two successful reads, counters, and
    // reservation count are not one coherent snapshot or a quiescence test.
    bool read_entry(std::size_t index,tc_context& out) const noexcept {
        ErrnoGuard guard;
        if (index>=Capacity || !slots_[index].entry_ready.load(std::memory_order_acquire)) return false;
        out=slots_[index].entry; return true;
    }
    bool read_return(std::size_t index,tc_return_record& out) const noexcept {
        ErrnoGuard guard;
        if (index>=Capacity || slots_[index].return_state.load(std::memory_order_acquire)!=ReturnReady) return false;
        out=slots_[index].returned; return true;
    }
    Counters counters() const noexcept {
        ErrnoGuard guard;
        return {attempted_.load(std::memory_order_relaxed),gated_.load(std::memory_order_relaxed),
            prefilter_rejected_.load(std::memory_order_relaxed),capacity_.load(std::memory_order_relaxed),
            contention_.load(std::memory_order_relaxed),capture_rejected_.load(std::memory_order_relaxed),
            entries_.load(std::memory_order_relaxed),returns_attempted_.load(std::memory_order_relaxed),
            return_invalid_.load(std::memory_order_relaxed),return_scope_mismatch_.load(std::memory_order_relaxed),
            return_duplicate_.load(std::memory_order_relaxed),returns_.load(std::memory_order_relaxed),
            claimed_.load(std::memory_order_relaxed)};
    }
};
} // namespace tvt
