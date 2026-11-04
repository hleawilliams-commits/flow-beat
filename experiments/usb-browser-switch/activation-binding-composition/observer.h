// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Inactive offline AArch64 entry-observation composition. No mod descriptor,
// loader integration or native activation API. The assembly symbols have no C++ result signature: they tail
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
// The binding state is the sole readiness authority; no reusable setter exists.
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
// Permanent plain pointer objects are passed directly to the SDK as void**;
// the unchanged assembly LDAR reads these exact objects. The selected GNU/ARM64
// contract requires atomic publication by the SDK and atomic diagnostic loads.
// Never cast these objects to std::atomic, reset them, or copy from a temporary.
// The SDK must publish a nonnull original BEFORE making the entry reachable.
// A check after claim returns cannot protect a callback inside a malformed claim.
// Slots, code and sink must remain for process lifetime after any successful
// claim, including partial failure or close. No null-target fallback exists.
alignas(8) extern void* az_usb_action_original;
alignas(8) extern void* az_usb_service_original;

// Address declarations only: these assembly entries have no asserted native
// argument/result signature. They preserve the frozen tail-forward ABI.
void az_usb_action_entry();
void az_usb_service_entry();

// frame is the valid private 800-byte assembly snapshot, borrowed only for this
// call. It is never retained. Unknown boundary tags refuse before reading it.
std::uint32_t az_usb_entry_observe(
    const az_usb_entry::Frame* frame, std::uint32_t boundary) noexcept;
}
