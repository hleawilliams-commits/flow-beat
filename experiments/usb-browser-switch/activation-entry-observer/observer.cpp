// SPDX-License-Identifier: GPL-3.0-only
#include "observer.h"
#include <cerrno>

static_assert(sizeof(std::uintptr_t) == 8, "64-bit original slots");
// Assembly LDAR reads the slot object bytes directly. These checks bound size
// and alignment; actual representation remains a target compiler/ABI property.
static_assert(sizeof(std::atomic<std::uintptr_t>) == 8, "slot object size");
static_assert(alignof(std::atomic<std::uintptr_t>) >= 8, "slot object alignment");
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free,
              "no lock-backed target slot");
static_assert(std::atomic<bool>::is_always_lock_free,
              "no lock-backed capture gate");

extern "C" {
alignas(8) std::atomic<std::uintptr_t> az_usb_action_original{0};
alignas(8) std::atomic<std::uintptr_t> az_usb_service_original{0};
}

namespace {
std::atomic<bool> capture_ready{false};
std::atomic_flag sink_busy = ATOMIC_FLAG_INIT;
std::size_t record_count = 0;
az_usb_entry::Record records[az_usb_entry::Capacity]{};

struct FlagRelease {
    ~FlagRelease() noexcept { sink_busy.clear(std::memory_order_release); }
};
struct ErrnoRestore {
    int saved;
    ErrnoRestore() noexcept : saved(errno) {}
    ~ErrnoRestore() noexcept { errno = saved; }
};

az_usb_entry::CaptureResult capture(
    const az_usb_entry::Frame* frame, std::uint32_t boundary) noexcept {
    using az_usb_entry::Boundary;
    using az_usb_entry::CaptureResult;
    if (boundary != static_cast<std::uint32_t>(Boundary::Action) &&
        boundary != static_cast<std::uint32_t>(Boundary::Service)) {
        return CaptureResult::InvalidBoundary;
    }
    if (!capture_ready.load(std::memory_order_acquire)) {
        return CaptureResult::Disabled;
    }
    // One atomic attempt, no spin/retry or waiting mutex. This finite source
    // path is not a real-time or hardware wait-free latency guarantee.
    if (sink_busy.test_and_set(std::memory_order_acquire)) {
        return CaptureResult::Contended;
    }
    FlagRelease release;
    if (record_count == az_usb_entry::Capacity) {
        return CaptureResult::Full;
    }

    az_usb_entry::Record next{};
    next.id = record_count + 1;
    next.boundary = boundary;
    next.original_sp = reinterpret_cast<std::uintptr_t>(frame) + sizeof(*frame);
    next.original_lr = frame->x[30];
    next.x0 = frame->x[0];
    next.x1 = frame->x[1];
    next.x2 = frame->x[2];
    if (boundary == static_cast<std::uint32_t>(Boundary::Service)) {
        for (std::size_t i = 0; i != 8; ++i) {
            next.source[i] = static_cast<std::uint8_t>(frame->x[1] >> (8 * i));
        }
        for (std::size_t i = 0; i != 4; ++i) {
            next.source[8 + i] = static_cast<std::uint8_t>(frame->x[2] >> (8 * i));
        }
    }
    // Only the supplied private frame is read. In particular frame->x[2] is
    // never treated as an address. No allocation, native call, TLS correlation,
    // I/O, user callback or post-target state is involved.
    records[record_count] = next;
    ++record_count;
    return CaptureResult::Recorded;
}
} // namespace

namespace az_usb_entry {
void set_ready(bool ready) noexcept {
    capture_ready.store(ready, std::memory_order_release);
}
bool try_snapshot(SinkSnapshot& out) noexcept {
    if (sink_busy.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    FlagRelease release;
    SinkSnapshot copy{};
    copy.ready = capture_ready.load(std::memory_order_acquire);
    copy.count = record_count;
    for (std::size_t i = 0; i != Capacity; ++i) {
        copy.records[i] = records[i];
    }
    out = copy;
    return true;
}

#ifdef AZ_USB_ENTRY_FIXTURE
bool fixture_hold_sink() noexcept {
    return !sink_busy.test_and_set(std::memory_order_acquire);
}
void fixture_release_sink() noexcept {
    sink_busy.clear(std::memory_order_release);
}
#endif
} // namespace az_usb_entry

extern "C" __attribute__((noinline))
std::uint32_t az_usb_entry_observe(
    const az_usb_entry::Frame* frame, std::uint32_t boundary) noexcept {
    // errno's normal thread-local access is intentional; no custom TLS or
    // native pending-action state exists. All returns restore the entry value.
    ErrnoRestore restore;
    return static_cast<std::uint32_t>(capture(frame, boundary));
}
