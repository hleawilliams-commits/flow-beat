// SPDX-License-Identifier: GPL-3.0-only
#include "binding.h"
#include "observer.h"
#include <atomic>

static_assert(std::atomic<az_usb_binding::State>::is_always_lock_free,
              "no lock-backed capture state");

namespace {
std::atomic<az_usb_binding::State> capture_state{
    az_usb_binding::State::Dormant};

az_usb_binding::InstallResult fail(
    az_usb_binding::InstallResult result) noexcept {
    // Failure may race close. It must never overwrite an earlier Closed state.
    auto expected = az_usb_binding::State::Installing;
    capture_state.compare_exchange_strong(
        expected, az_usb_binding::State::Failed,
        std::memory_order_acq_rel, std::memory_order_acquire);
    return result;
}
} // namespace

namespace az_usb_binding {
const char ModuleId[] = "usb-entry-binding";

State state() noexcept {
    return capture_state.load(std::memory_order_acquire);
}
bool capture_ready() noexcept {
    return state() == State::Ready;
}

InstallResult install(ClaimHook claim) noexcept {
    auto expected = State::Dormant;
    if (!capture_state.compare_exchange_strong(
            expected, State::Installing,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return expected == State::Closed ? InstallResult::Closed
                                         : InstallResult::AlreadyAttempted;
    }
    if (claim == nullptr) {
        return fail(InstallResult::InvalidClaim);
    }

    // Each check admits at most this following claim. Close immediately after
    // the check cannot cancel the admitted SDK call; it still blocks readiness.
    if (state() != State::Installing) {
        return InstallResult::Closed;
    }
    const int action_result = claim(
        ModuleId, ActionAddress, reinterpret_cast<void*>(&az_usb_action_entry),
        &az_usb_action_original);
    if (action_result != 0) {
        return fail(InstallResult::FirstRefused);
    }
    if (__atomic_load_n(&az_usb_action_original, __ATOMIC_ACQUIRE) == nullptr) {
        return fail(InstallResult::MissingOriginal);
    }

    if (state() != State::Installing) {
        return InstallResult::Closed;
    }
    const int service_result = claim(
        ModuleId, ServiceAddress, reinterpret_cast<void*>(&az_usb_service_entry),
        &az_usb_service_original);
    if (service_result != 0) {
        return fail(InstallResult::SecondRefused);
    }
    if (__atomic_load_n(&az_usb_service_original, __ATOMIC_ACQUIRE) == nullptr) {
        return fail(InstallResult::MissingOriginal);
    }

#ifdef AZ_USB_BINDING_FIXTURE
    az_usb_binding_fixture_before_ready();
#endif
    // This single authoritative CAS is also the helper's readiness source.
    // A late initializer cannot replace Closed with Ready or a separate bool.
    expected = State::Installing;
    if (!capture_state.compare_exchange_strong(
            expected, State::Ready,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return InstallResult::Closed;
    }
    return InstallResult::Ready;
}

void close() noexcept {
    auto current = state();
    while (current != State::Closed && current != State::Failed) {
        if (capture_state.compare_exchange_strong(
                current, State::Closed,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return;
        }
        // No reset exists: competing transitions are finite. This nevertheless
        // supplies no real-time, native cancellation or quiescence guarantee.
    }
}
} // namespace az_usb_binding
