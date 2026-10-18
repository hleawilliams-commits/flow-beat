// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>

namespace usb_intent {
// Value-only request policy. No firmware pointers, native calls or UI controls.
// A native adapter must separately prove its live argument/dispatch contract.
enum class Result { Queued, Disabled, Busy, BadPort, Exhausted, Pending,
                    Expired, Missing, Ambiguous, Unavailable, Cancelled,
                    Entered, Finished, Stale };
enum class Resolution { Ready, Missing, Ambiguous, Unavailable };
struct Ticket { std::uint64_t value = 0; explicit operator bool() const { return value != 0; } };
struct Reply { Result result; Ticket ticket; };
class Mailbox {
    static constexpr std::uint64_t ENABLE = UINT64_C(1) << 63;
    static constexpr unsigned STATE_SHIFT = 61, PORT_SHIFT = 59, SEQ_SHIFT = 32;
    static constexpr std::uint64_t STATE_MASK = UINT64_C(3) << STATE_SHIFT;
    static constexpr std::uint64_t PORT_MASK = UINT64_C(3) << PORT_SHIFT;
    static constexpr std::uint64_t SEQ_MASK = UINT64_C(0x7ffffff) << SEQ_SHIFT;
    static constexpr std::uint64_t PENDING = UINT64_C(1) << STATE_SHIFT;
    static constexpr std::uint64_t ENTERED = UINT64_C(2) << STATE_SHIFT;
    static constexpr std::uint64_t ID_MASK = ~(ENABLE | STATE_MASK);
    std::atomic<std::uint64_t> word_{0};
    static Ticket ticket(std::uint64_t w) { return {w & ID_MASK}; }
    static bool same(std::uint64_t w, Ticket t) { return t && ticket(w).value == t.value; }
    static bool expired(std::uint64_t w, std::uint32_t now) {
        // Deadline is bounded to two seconds. A backwards/ambiguous clock
        // difference is >= 2^31 and also fails closed.
        return std::uint32_t(now - std::uint32_t(w)) >= 2000;
    }
public:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "native callback cannot use a hidden atomic mutex");
    Mailbox() = default;
    Mailbox(const Mailbox &) = delete;
    Mailbox &operator=(const Mailbox &) = delete;
    void enable() { word_.fetch_or(ENABLE, std::memory_order_acq_rel); }
    void disable() {
        auto w = word_.load(std::memory_order_acquire);
        for (;;) {
            // Entered native work has passed the dispatch linearization point.
            // Disabling cancels pending work but never claims to undo it.
            auto next = w & ~ENABLE;
            if ((w & STATE_MASK) == PENDING) next &= ~STATE_MASK;
            if (word_.compare_exchange_weak(w, next, std::memory_order_acq_rel)) return;
        }
    }
    Reply request(unsigned port, std::uint32_t now) {
        if (port != 1 && port != 2) return {Result::BadPort,{}};
        auto w = word_.load(std::memory_order_acquire);
        for (;;) {
            if (!(w & ENABLE)) return {Result::Disabled,{}};
            if (w & STATE_MASK) return {Result::Busy,{}};
            auto seq = ((w & SEQ_MASK) >> SEQ_SHIFT) + 1;
            if (seq > 0x7ffffff) return {Result::Exhausted,{}}; // Never reuse a ticket.
            auto next = ENABLE | PENDING | (std::uint64_t(port) << PORT_SHIFT) |
                        (seq << SEQ_SHIFT) | now;
            if (word_.compare_exchange_weak(w, next, std::memory_order_acq_rel))
                return {Result::Queued,ticket(next)};
        }
    }
    Reply pending(std::uint32_t now) {
        auto w = word_.load(std::memory_order_acquire);
        if (!(w & ENABLE)) return {Result::Disabled,{}};
        if ((w & STATE_MASK) != PENDING) return {Result::Pending,{}};
        if (!expired(w, now)) return {Result::Pending,ticket(w)};
        return terminal(ticket(w), Result::Expired);
    }
    Reply cancel(Ticket t) { return terminal(t, Result::Cancelled); }
    Reply enter(Ticket t, std::uint32_t now, Resolution resolved) {
        auto w = word_.load(std::memory_order_acquire);
        for (;;) {
            if (!same(w,t) || (w & STATE_MASK) != PENDING) return {Result::Stale,t};
            if (!(w & ENABLE)) return {Result::Disabled,t};
            Result failure = Result::Entered;
            if (expired(w,now)) failure = Result::Expired;
            else if (resolved == Resolution::Missing) failure = Result::Missing;
            else if (resolved == Resolution::Ambiguous) failure = Result::Ambiguous;
            else if (resolved == Resolution::Unavailable) failure = Result::Unavailable;
            auto next = (w & ~STATE_MASK) | (failure == Result::Entered ? ENTERED : 0);
            if (word_.compare_exchange_weak(w,next,std::memory_order_acq_rel))
                return {failure,t};
        }
    }
    Reply finish(Ticket t) {
        auto w = word_.load(std::memory_order_acquire);
        for (;;) {
            if (!same(w,t) || (w & STATE_MASK) != ENTERED) return {Result::Stale,t};
            if (word_.compare_exchange_weak(w,w & ~STATE_MASK,std::memory_order_acq_rel))
                return {Result::Finished,t};
        }
    }
    static unsigned port(Ticket t) { return unsigned((t.value & PORT_MASK) >> PORT_SHIFT); }
private:
    Reply terminal(Ticket t, Result result) {
        auto w = word_.load(std::memory_order_acquire);
        for (;;) {
            if (!same(w,t) || (w & STATE_MASK) != PENDING) return {Result::Stale,t};
            if (word_.compare_exchange_weak(w,w & ~STATE_MASK,std::memory_order_acq_rel))
                return {result,t};
        }
    }
};
// Use only on a stack already holding an independently proven native scope.
// This destructor only finishes the copied token; it never accesses firmware.
struct Finish {
    Mailbox &mailbox; Ticket ticket;
    ~Finish() { mailbox.finish(ticket); }
};
}
