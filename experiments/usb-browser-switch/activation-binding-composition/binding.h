// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "azmod-mod.h"
#include <cstdint>
#include <type_traits>

// Inactive offline composition only. No mod descriptor, loader entry, firmware
// preflight, live claim ownership check or native activation is supplied.
namespace az_usb_binding {

using ClaimHook = decltype(az_core_v1::claim_hook);
static_assert(std::is_same<ClaimHook,
              int (*)(const char*, std::uintptr_t, void*, void**)>::value,
              "exact pinned SDK claim_hook type");

enum class State : std::uint32_t {
    Dormant = 0, Installing = 1, Ready = 2, Closed = 3, Failed = 4
};
enum class InstallResult : std::uint32_t {
    Ready = 0, AlreadyAttempted = 1, Closed = 2, FirstRefused = 3,
    SecondRefused = 4, InvalidClaim = 5, MissingOriginal = 6
};

constexpr std::uintptr_t ActionAddress = 0x1be7e38;
constexpr std::uintptr_t ServiceAddress = 0x20a1550;
extern const char ModuleId[];

// Exactly one caller may consume Dormant, including a null-claim attempt. The
// supplied function must be externally validated and used in the SDK's permitted
// init context; it must honor its nonthrowing C contract. All other fallible
// preparation belongs before this call. The actual SDK may block or make an
// entry reachable before returning: this function does not bound that latency.
//
// Nonzero SDK results refuse that stage; the stage result is retained as this
// return value even if concurrent close won. Success with a null slot is a
// malformed claim contract: MissingOriginal refuses readiness but cannot undo
// or protect an entry already made reachable by that claim. No slot is reset.
//
// Ready describes a successful Installing->Ready transition, not a promise that
// a concurrent close has not subsequently completed. No retry or unpatch exists.
InstallResult install(ClaimHook claim) noexcept;

// Terminal capture close only. Closed and Failed never reopen or overwrite one
// another. A claim whose pre-call check already passed may still run; close
// cannot cancel it, unpatch, drain admitted helpers, or authorize reclamation.
void close() noexcept;
State state() noexcept;
bool capture_ready() noexcept;

} // namespace az_usb_binding

#ifdef AZ_USB_BINDING_FIXTURE
// Explicit scheduling seams compiled only into the synthetic fixture. They are
// neither production callbacks nor an alternate installation/capture path.
extern "C" void az_usb_binding_fixture_before_ready() noexcept;
extern "C" void az_usb_binding_fixture_after_admission() noexcept;
#endif
