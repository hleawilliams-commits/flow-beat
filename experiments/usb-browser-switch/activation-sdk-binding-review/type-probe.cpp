// SPDX-License-Identifier: GPL-3.0-only
// Compile-only SDK type contract. This object has no entry point and is never
// linked or executed. Compatible argument types do not authorize a hook claim,
// establish publication/lifetime, or make a cast from atomic storage valid.
#include "azmod-mod.h"
#include "observer.h"
#include <atomic>
#include <cstdint>
#include <type_traits>

#ifndef TYPE_CASE
#define TYPE_CASE 0
#endif
#if !defined(__aarch64__) || !defined(__linux__)
#error SDK_TYPE_PROBE_REQUIRES_LINUX_AARCH64
#endif
static_assert(TYPE_CASE >= 0 && TYPE_CASE <= 2, "SDK_TYPE_PROBE_CASE");
static_assert(sizeof(void*) == 8 && sizeof(std::uintptr_t) == 8, "SDK_TARGET_POINTER_WIDTH");
static_assert(alignof(void*) == 8, "SDK_TARGET_POINTER_ALIGNMENT");
static_assert(sizeof(std::atomic<std::uintptr_t>) == 8, "SDK_TARGET_ATOMIC_SIZE");
static_assert(alignof(std::atomic<std::uintptr_t>) >= 8, "SDK_TARGET_ATOMIC_ALIGNMENT");
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free, "SDK_TARGET_ATOMIC_LOCK_FREE");
static_assert(std::is_same<decltype(az_usb_action_original), std::atomic<std::uintptr_t>>::value, "OBSERVER_ACTION_ORIGINAL_IS_ATOMIC_UINTPTR");
static_assert(std::is_same<decltype(az_usb_service_original), std::atomic<std::uintptr_t>>::value, "OBSERVER_SERVICE_ORIGINAL_IS_ATOMIC_UINTPTR");

#if TYPE_CASE == 2
// Deliberately incorrect expectation: the frozen SDK does not accept this type.
using ExpectedClaim = int (*)(const char*, std::uintptr_t, void*, std::atomic<std::uintptr_t>*);
#else
using ExpectedClaim = int (*)(const char*, std::uintptr_t, void*, void**);
#endif
static_assert(std::is_same<ExpectedClaim, decltype(az_core_v1::claim_hook)>::value, "SDK_CLAIM_HOOK_EXACT_VOID_DOUBLE_POINTER");

// Static storage persists for the program lifetime. This is only a type probe;
// no claim is executed and this slot is never substituted into the observer.
#if TYPE_CASE != 1
alignas(8) static void* permanent_original = nullptr;
#endif

extern "C" int sdk_type_probe(const az_core_v1* core, const char* id,
                              std::uintptr_t address, void* replacement) {
#if TYPE_CASE == 1
    // No cast, reinterpretation, intermediate void**, or permissive conversion.
    return core->claim_hook(id, address, replacement, &az_usb_action_original);
#else
    return core->claim_hook(id, address, replacement, &permanent_original);
#endif
}
