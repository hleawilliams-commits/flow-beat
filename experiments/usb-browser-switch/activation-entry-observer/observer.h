// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Inactive offline AArch64 entry-observation source. No installer or native
// activation API. The assembly symbols have no C++ result signature: they tail
// forward arbitrary native return conventions without a return callback.
namespace az_usb_entry {

enum class Boundary : std::uint32_t { Action = 1, Service = 2 };
enum class CaptureResult : std::uint32_t {
    Recorded = 0, Disabled = 1, Contended = 2, Full = 3, InvalidBoundary = 4
};

struct alignas(16) Frame {
    std::uint64_t x[31];
    std::uint64_t reserved_248;
    std::uint8_t q[32][16];
    std::uint64_t nzcv;
    std::uint64_t fpcr;
    std::uint64_t fpsr;
    std::uint64_t reserved_792;
};
static_assert(std::is_standard_layout<Frame>::value, "assembly layout");
static_assert(sizeof(Frame) == 800 && alignof(Frame) == 16, "entry frame");
static_assert(offsetof(Frame, x) == 0, "x0");
static_assert(offsetof(Frame, reserved_248) == 248, "padding");
static_assert(offsetof(Frame, q) == 256, "q0");
static_assert(offsetof(Frame, nzcv) == 768, "NZCV");
static_assert(offsetof(Frame, fpcr) == 776, "FPCR");
static_assert(offsetof(Frame, fpsr) == 784, "FPSR");
static_assert(offsetof(Frame, reserved_792) == 792, "padding");

// IDs are local successful-record ordinals, not attempt IDs or proof that
// action/service records belong to one invocation. x0/x2 pointer-looking bits
// are diagnostics only; no pointee or native ownership is acquired.
struct Record {
    std::uint64_t id;
    std::uint32_t boundary;
    std::uint32_t reserved_12;
    std::uint64_t original_sp;
    std::uint64_t original_lr;
    std::uint64_t x0;
    std::uint64_t x1;
    std::uint64_t x2;
    std::uint8_t source[12]; // Service: LE x1 then low w2. Action: all zero.
    std::uint8_t reserved_68[4];
};
static_assert(std::is_trivially_copyable<Record>::value, "fixed record");
static_assert(sizeof(Record) == 72, "record size");

constexpr std::size_t Capacity = 16;
struct SinkSnapshot {
    bool ready;
    std::size_t count;
    Record records[Capacity];
};

// The static sink is never reset or overwritten. Publication does not initialize
// either original slot. Closing capture does not drain an already admitted
// helper or authorize reclaiming code, slots, records or configuration.
void set_ready(bool ready) noexcept;
// One try only. False leaves out unchanged. Success copies fixed owned records,
// including zero unused records. The ready value is diagnostic, not quiescence.
bool try_snapshot(SinkSnapshot& out) noexcept;

#ifdef AZ_USB_ENTRY_FIXTURE
// Synthetic contention seam only. It acquires exactly the production sink flag;
// the fixture must release a successful acquisition before cleanup. No callback
// or alternative capture implementation is injected into production.
bool fixture_hold_sink() noexcept;
void fixture_release_sink() noexcept;
#endif

} // namespace az_usb_entry

extern "C" {
// Caller-provided initialization invariant: both corresponding slots are aligned,
// nonnull, initialized BEFORE entry reachability, and stable until no forwarding
// code/caller can remain. No null-target fallback or runtime setter is supplied.
// The definitions begin at zero solely because this candidate is never installed.
alignas(8) extern std::atomic<std::uintptr_t> az_usb_action_original;
alignas(8) extern std::atomic<std::uintptr_t> az_usb_service_original;

// frame is the valid private 800-byte assembly snapshot, borrowed only for this
// call. It is never retained. Unknown boundary tags refuse before reading it.
std::uint32_t az_usb_entry_observe(
    const az_usb_entry::Frame* frame, std::uint32_t boundary) noexcept;
}
