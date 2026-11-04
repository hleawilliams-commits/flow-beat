// SPDX-License-Identifier: GPL-3.0-only
// New-only offline binding composition. Every selector gets a fresh process;
// no sink/slot reset, native SDK, firmware call, loader, or old register suite.
#include "binding.h"
#include "observer.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

// Machine-ABI aliases call the assembly entries. The header's void declarations
// denote code addresses only; no incompatible C++ function-pointer call occurs.
extern "C" std::uint64_t call_action(std::uint64_t, std::uint64_t, std::uint64_t)
    asm("az_usb_action_entry");
extern "C" std::uint64_t call_service(std::uint64_t, std::uint64_t, std::uint64_t)
    asm("az_usb_service_entry");
extern "C" std::uint64_t native_action(std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t native_service(std::uint64_t, std::uint64_t, std::uint64_t);

namespace {
using az_usb_binding::State;
using az_usb_binding::InstallResult;
using az_usb_entry::SinkSnapshot;
enum class Mode {
    Success, FirstRefusal, SecondRefusal, NullClaim, MissingAction, MissingService,
    Reentrant, CloseBefore, CloseFirst, CloseSecond, CloseBeforeReady, CloseAdmitted,
    CloseFirstRefusal, CloseSecondRefusal
};
Mode mode = Mode::Success;
unsigned checks = 0, failures = 0, claim_count = 0, before_ready_count = 0;
bool protocol_overflow = false, nested_active = false;
State before_ready_state = State::Dormant;
constexpr std::uint64_t A0 = UINT64_C(0x8192a3b4c5d6e7f8);
constexpr std::uint64_t A1 = UINT64_C(0x1029384756abcdef);
constexpr std::uint64_t A2 = UINT64_C(0xfedcba9876543210);
constexpr std::uint64_t W0 = UINT64_C(0x9999888877776666);

struct Sink { bool obtained; SinkSnapshot value; };
Sink sink() {
    Sink result{};
    result.obtained = az_usb_entry::try_snapshot(result.value);
    return result;
}
bool empty_closed(const Sink& s) {
    return s.obtained && !s.value.ready && s.value.count == 0;
}
void CHECK(bool okay, const char* label) {
    ++checks;
    if (!okay) { ++failures; std::printf("FAIL %s\n", label); }
}
void* original(unsigned index) {
    return index == 0 ? __atomic_load_n(&az_usb_action_original, __ATOMIC_ACQUIRE)
                      : __atomic_load_n(&az_usb_service_original, __ATOMIC_ACQUIRE);
}
void** original_slot(unsigned index) {
    return index == 0 ? &az_usb_action_original : &az_usb_service_original;
}
void* target(unsigned index) {
    return index == 0 ? reinterpret_cast<void*>(&native_action)
                      : reinterpret_cast<void*>(&native_service);
}
void* replacement(unsigned index) {
    return index == 0 ? reinterpret_cast<void*>(&az_usb_action_entry)
                      : reinterpret_cast<void*>(&az_usb_service_entry);
}
bool slots(unsigned retained) {
    return original(0) == (retained >= 1 ? target(0) : nullptr) &&
           original(1) == (retained >= 2 ? target(1) : nullptr);
}
std::uint64_t expected_value(unsigned index, std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    return (a ^ (b * (index == 0 ? 3u : 7u))) + c +
           (index == 0 ? UINT64_C(0x8765432101234567) : UINT64_C(0xabcdef0123456789));
}
struct NativeCall { unsigned boundary; std::uint64_t a, b, c; State state; bool ready; };
NativeCall native_log[16]{};
std::atomic<unsigned> native_count{0};
std::atomic<bool> native_overflow{false};
std::uint64_t record_native(unsigned index, std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    const unsigned at = native_count.fetch_add(1, std::memory_order_relaxed);
    if (at < 16) native_log[at] = {index, a, b, c, az_usb_binding::state(), az_usb_binding::capture_ready()};
    else native_overflow.store(true, std::memory_order_relaxed);
    return expected_value(index, a, b, c);
}
// Safe even under slot-identity/reset controls: never branch through a missing
// or different original. Skipped calls remain a failing value oracle.
bool forward(unsigned index, std::uint64_t a = A0, std::uint64_t b = A1, std::uint64_t c = A2) {
    if (original(index) != target(index)) return false;
    const auto got = index == 0 ? call_action(a, b, c) : call_service(a, b, c);
    return got == expected_value(index, a, b, c);
}
struct ClaimSeen {
    bool exact = false, before_null = false, published = false, forwarded = false;
    bool nested = false, nested_preserved = false;
    InstallResult nested_result = InstallResult::Ready;
    State entry_state = State::Dormant;
    Sink before{}, after{};
};
ClaimSeen claims[2]{};
int mock_claim(const char*, std::uintptr_t, void*, void**);
int mock_claim(const char* id, std::uintptr_t address, void* hook, void** output) {
    const unsigned index = claim_count++;
    if (index >= 2) { protocol_overflow = true; return 97; }
    auto& seen = claims[index];
    seen.entry_state = az_usb_binding::state();
    seen.before = sink();
    seen.exact = id == az_usb_binding::ModuleId &&
        std::strcmp(id, "usb-entry-binding") == 0 &&
        address == (index == 0 ? UINT64_C(0x1be7e38) : UINT64_C(0x20a1550)) &&
        hook == replacement(index) && output == original_slot(index) &&
        reinterpret_cast<std::uintptr_t>(output) % 8 == 0;
    seen.before_null = original(index) == nullptr;
    const bool refusing = ((mode == Mode::FirstRefusal || mode == Mode::CloseFirstRefusal) && index == 0) ||
                          ((mode == Mode::SecondRefusal || mode == Mode::CloseSecondRefusal) && index == 1);
    if (refusing) {
        // The selected SDK refusal occurs before publishing this slot or making
        // its replacement reachable. An earlier successful claim remains live.
        if (mode == Mode::CloseFirstRefusal || mode == Mode::CloseSecondRefusal)
            az_usb_binding::close();
        seen.after = sink();
        return index == 0 ? 41 : 42;
    }
    const bool missing = (mode == Mode::MissingAction && index == 0) ||
                         (mode == Mode::MissingService && index == 1);
    if (seen.exact && !missing) {
        // Model the SDK's synchronous publication before replacement reachability.
        // This relaxed store is not a cross-thread installation/publication proof.
        __atomic_store_n(output, target(index), __ATOMIC_RELAXED);
        seen.published = original(index) == target(index);
    }
    if (mode == Mode::Reentrant && !nested_active) {
        nested_active = true;
        const auto old0 = original(0), old1 = original(1);
        const unsigned old_calls = claim_count;
        seen.nested = true;
        seen.nested_result = az_usb_binding::install(&mock_claim);
        seen.nested_preserved = old_calls == claim_count && old0 == original(0) &&
            old1 == original(1) && az_usb_binding::state() == State::Installing;
        nested_active = false;
    }
    if ((mode == Mode::CloseFirst && index == 0) ||
        (mode == Mode::CloseSecond && index == 1)) az_usb_binding::close();
    // The pointer contract is checked BEFORE writing/calling. A temporary-slot
    // control therefore fails cleanly without exposing an entry with null orig.
    if (seen.exact && seen.published) seen.forwarded = forward(index);
    seen.after = sink();
    return 0;
}
bool claim_contract(unsigned count) {
    bool okay = claim_count == count && !protocol_overflow;
    for (unsigned i = 0; i < count; ++i)
        okay = okay && claims[i].exact && claims[i].before_null &&
            claims[i].entry_state == State::Installing;
    return okay;
}
bool claim_forwarding(unsigned count) {
    bool okay = true;
    for (unsigned i = 0; i < count; ++i)
        okay = okay && claims[i].published && claims[i].forwarded &&
            empty_closed(claims[i].before) && empty_closed(claims[i].after) &&
            native_log[i].boundary == i && native_log[i].a == A0 &&
            native_log[i].b == A1 && native_log[i].c == A2 && !native_log[i].ready;
    return okay;
}
bool repeated(InstallResult expected, State state, unsigned retained, unsigned calls) {
    const auto a = original(0), b = original(1);
    const auto first = az_usb_binding::install(&mock_claim);
    const auto second = az_usb_binding::install(nullptr);
    az_usb_binding::close();
    az_usb_binding::close();
    return first == expected && second == expected && claim_count == calls &&
        original(0) == a && original(1) == b && slots(retained) &&
        az_usb_binding::state() == state && !az_usb_binding::capture_ready();
}

std::mutex admitted_mutex;
std::condition_variable admitted_cv;
bool admitted = false, release_admitted = false, admission_timed_out = false;
std::atomic<bool> hold_admission{false};
std::atomic<unsigned> admission_calls{0};

void success_case() {
    const auto result = az_usb_binding::install(&mock_claim);
    const bool ready = az_usb_binding::state() == State::Ready && az_usb_binding::capture_ready();
    const bool a = forward(0), b = forward(1);
    const auto recorded = sink();
    const bool repeat = repeated(InstallResult::AlreadyAttempted, State::Closed, 2, 2);
    const auto closed = sink();
    CHECK(claim_contract(2) && slots(2), "success-sdk-contract");
    CHECK(claim_forwarding(2) && before_ready_count == 1 && before_ready_state == State::Installing,
          "success-claims-forward-closed");
    CHECK(result == InstallResult::Ready && ready && a && b && native_count == 4 &&
          recorded.obtained && recorded.value.ready && recorded.value.count == 2 &&
          recorded.value.records[0].boundary == 1 && recorded.value.records[1].boundary == 2 &&
          recorded.value.records[0].x0 == A0 && recorded.value.records[1].x0 == A0,
          "success-ready-forwarding");
    CHECK(repeat && closed.obtained && !closed.value.ready && closed.value.count == 2 &&
          !native_overflow, "success-repeat-retains");
}
void refusal_case(bool second, bool close_race = false) {
    const unsigned count = second ? 2 : 1;
    const unsigned retained = second ? 1 : 0;
    const auto result = az_usb_binding::install(&mock_claim);
    const auto finished = az_usb_binding::state();
    const bool forwarded = !second || forward(0);
    const auto observed = sink();
    const bool repeat = repeated(close_race ? InstallResult::Closed : InstallResult::AlreadyAttempted,
                                 close_race ? State::Closed : State::Failed, retained, count);
    const char* result_label = close_race
        ? (second ? "close-second-refusal-keeps-closed" : "close-first-refusal-keeps-closed")
        : (second ? "second-refusal-result" : "first-refusal-result");
    const char* preserve_label = close_race
        ? (second ? "close-second-refusal-preserves-slots" : "close-first-refusal-preserves-slots")
        : (second ? "second-refusal-preserves-forwarding" : "first-refusal-preserves-forwarding");
    const char* repeat_label = close_race
        ? (second ? "close-second-refusal-terminal" : "close-first-refusal-terminal")
        : (second ? "second-refusal-repeat" : "first-refusal-repeat");
    CHECK(result == (second ? InstallResult::SecondRefused : InstallResult::FirstRefused) &&
          finished == (close_race ? State::Closed : State::Failed) && claim_contract(count), result_label);
    CHECK(slots(retained) && claim_forwarding(retained) && forwarded &&
          !claims[count - 1].published && !claims[count - 1].forwarded &&
          empty_closed(claims[count - 1].before) && empty_closed(claims[count - 1].after) &&
          native_count == retained * 2 && empty_closed(observed), preserve_label);
    CHECK(repeat && before_ready_count == 0 && !native_overflow, repeat_label);
}
void null_claim_case() {
    const auto result = az_usb_binding::install(nullptr);
    const bool failed = az_usb_binding::state() == State::Failed;
    const bool repeat = repeated(InstallResult::AlreadyAttempted, State::Failed, 0, 0);
    CHECK(result == InstallResult::InvalidClaim && failed && claim_count == 0 &&
          slots(0) && empty_closed(sink()), "null-claim-fails-closed");
    CHECK(repeat && native_count == 0 && before_ready_count == 0, "null-claim-one-attempt");
}
void missing_case(bool second) {
    const auto result = az_usb_binding::install(&mock_claim);
    const bool failed = az_usb_binding::state() == State::Failed;
    const bool forwarded = !second || forward(0);
    const auto observed = sink();
    const bool repeat = repeated(InstallResult::AlreadyAttempted, State::Failed, second ? 1 : 0, second ? 2 : 1);
    CHECK(result == InstallResult::MissingOriginal && failed &&
          claim_contract(second ? 2 : 1) && !claims[second ? 1 : 0].published &&
          !claims[second ? 1 : 0].forwarded && empty_closed(observed),
          second ? "missing-service-fails-closed" : "missing-action-fails-closed");
    CHECK(repeat && before_ready_count == 0 && slots(second ? 1 : 0) && forwarded &&
          native_count == (second ? 2u : 0u) && (!second || claim_forwarding(1)),
          second ? "missing-service-retains-first" : "missing-action-no-unsafe-entry");
}
void reentrant_case() {
    const auto result = az_usb_binding::install(&mock_claim);
    const bool ready = az_usb_binding::capture_ready();
    const auto observed = sink();
    const bool repeat = repeated(InstallResult::AlreadyAttempted, State::Closed, 2, 2);
    CHECK(result == InstallResult::Ready && ready && claim_contract(2) &&
          claims[0].nested && claims[1].nested && claims[0].nested_preserved &&
          claims[1].nested_preserved && claims[0].nested_result == InstallResult::AlreadyAttempted &&
          claims[1].nested_result == InstallResult::AlreadyAttempted,
          "reentrant-does-not-reclaim-or-reset");
    CHECK(claim_forwarding(2) && observed.obtained && observed.value.ready &&
          observed.value.count == 0 && native_count == 2, "reentrant-capture-stays-closed-during-claims");
    CHECK(repeat && slots(2) && before_ready_count == 1, "reentrant-later-repeat-refused");
}
void close_before_case() {
    const bool dormant = az_usb_binding::state() == State::Dormant;
    az_usb_binding::close();
    const auto result = az_usb_binding::install(&mock_claim);
    const bool repeat = repeated(InstallResult::Closed, State::Closed, 0, 0);
    CHECK(dormant && result == InstallResult::Closed && repeat && slots(0), "close-before-init-is-terminal");
    CHECK(claim_count == 0 && native_count == 0 && before_ready_count == 0 &&
          empty_closed(sink()), "close-before-init-does-not-claim");
}
void close_during_case(bool second) {
    const unsigned count = second ? 2 : 1;
    const auto result = az_usb_binding::install(&mock_claim);
    const bool closed = az_usb_binding::state() == State::Closed;
    const bool a = forward(0), b = !second || forward(1);
    const auto observed = sink();
    const bool repeat = repeated(InstallResult::Closed, State::Closed, count, count);
    CHECK(result == InstallResult::Closed && closed && claim_contract(count),
          second ? "close-during-second-blocks-ready" : "close-during-first-blocks-next-claim");
    CHECK(claim_forwarding(count) && a && b && native_count == 2 * count &&
          slots(count) && empty_closed(observed),
          second ? "close-during-second-preserves-forwarding" : "close-during-first-preserves-forwarding");
    CHECK(repeat && before_ready_count == (second ? 1u : 0u),
          second ? "close-during-second-terminal" : "close-during-first-terminal");
}
void close_before_ready_case() {
    const auto result = az_usb_binding::install(&mock_claim);
    const bool closed = az_usb_binding::state() == State::Closed;
    const bool a = forward(0), b = forward(1);
    const auto observed = sink();
    const bool repeat = repeated(InstallResult::Closed, State::Closed, 2, 2);
    CHECK(result == InstallResult::Closed && closed && before_ready_count == 1 &&
          before_ready_state == State::Installing, "close-before-ready-cas-wins");
    CHECK(claim_contract(2) && claim_forwarding(2) && a && b && slots(2) &&
          native_count == 4 && empty_closed(observed), "close-before-ready-preserves-forwarding");
    CHECK(repeat && !az_usb_binding::capture_ready(), "close-before-ready-terminal");
}
void close_admitted_case() {
    const auto result = az_usb_binding::install(&mock_claim);
    const bool ready = az_usb_binding::state() == State::Ready;
    const bool safe = slots(2);
    bool worker_forwarded = false, got_admission = false;
    hold_admission.store(true, std::memory_order_release);
    std::thread worker;
    if (safe) worker = std::thread([&] { worker_forwarded = forward(0, W0, A1, A2); });
    {
        std::unique_lock<std::mutex> lock(admitted_mutex);
        if (safe) got_admission = admitted_cv.wait_for(lock, std::chrono::seconds(5), [] { return admitted; });
    }
    az_usb_binding::close();
    const bool closed_before_release = az_usb_binding::state() == State::Closed;
    const auto before_release = sink();
    {
        std::lock_guard<std::mutex> lock(admitted_mutex);
        release_admitted = true;
    }
    admitted_cv.notify_all();
    if (worker.joinable()) worker.join();
    hold_admission.store(false, std::memory_order_release);
    const auto late = sink();
    const bool a = forward(0), b = forward(1);
    const auto after = sink();
    const bool repeat = repeated(InstallResult::Closed, State::Closed, 2, 2);
    // All worker accesses and waits are complete before semantic checks.
    CHECK(result == InstallResult::Ready && ready && safe && got_admission &&
          !admission_timed_out && closed_before_release && empty_closed(before_release),
          "close-admitted-closes-without-drain");
    CHECK(worker_forwarded && late.obtained && !late.value.ready && late.value.count == 1 &&
          late.value.records[0].boundary == 1 && late.value.records[0].x0 == W0 &&
          late.value.records[0].x1 == A1 && late.value.records[0].x2 == A2 &&
          admission_calls == 1, "close-admitted-late-record-retained");
    CHECK(a && b && slots(2) && native_count == 5 && after.obtained &&
          !after.value.ready && after.value.count == 1 && repeat && !native_overflow,
          "close-admitted-later-calls-forward-without-capture");
}
} // namespace

extern "C" std::uint64_t native_action(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    return record_native(0, a, b, c);
}
extern "C" std::uint64_t native_service(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    return record_native(1, a, b, c);
}
extern "C" void az_usb_binding_fixture_before_ready() noexcept {
    ++before_ready_count;
    before_ready_state = az_usb_binding::state();
    if (mode == Mode::CloseBeforeReady) az_usb_binding::close();
}
extern "C" void az_usb_binding_fixture_after_admission() noexcept {
    admission_calls.fetch_add(1, std::memory_order_relaxed);
    if (!hold_admission.load(std::memory_order_acquire)) return;
    try {
        std::unique_lock<std::mutex> lock(admitted_mutex);
        admitted = true;
        admitted_cv.notify_all();
        if (!admitted_cv.wait_for(lock, std::chrono::seconds(15), [] { return release_admitted; }))
            admission_timed_out = true;
    } catch (...) {
        admission_timed_out = true;
    }
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "FIXTURE_ABORT expected selector\n"); return 2; }
    const std::string name = argv[1];
    try {
        if (name == "success") { mode = Mode::Success; success_case(); }
        else if (name == "first-refusal") { mode = Mode::FirstRefusal; refusal_case(false); }
        else if (name == "second-refusal") { mode = Mode::SecondRefusal; refusal_case(true); }
        else if (name == "null-claim") { mode = Mode::NullClaim; null_claim_case(); }
        else if (name == "missing-action-original") { mode = Mode::MissingAction; missing_case(false); }
        else if (name == "missing-service-original") { mode = Mode::MissingService; missing_case(true); }
        else if (name == "reentrant-repeat") { mode = Mode::Reentrant; reentrant_case(); }
        else if (name == "close-before-init") { mode = Mode::CloseBefore; close_before_case(); }
        else if (name == "close-during-first") { mode = Mode::CloseFirst; close_during_case(false); }
        else if (name == "close-during-second") { mode = Mode::CloseSecond; close_during_case(true); }
        else if (name == "close-before-ready") { mode = Mode::CloseBeforeReady; close_before_ready_case(); }
        else if (name == "close-admitted") { mode = Mode::CloseAdmitted; close_admitted_case(); }
        else if (name == "close-during-first-refusal") { mode = Mode::CloseFirstRefusal; refusal_case(false, true); }
        else if (name == "close-during-second-refusal") { mode = Mode::CloseSecondRefusal; refusal_case(true, true); }
        else throw std::runtime_error("unknown selector");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FIXTURE_ABORT %s\n", e.what());
        return 2;
    }
    std::printf("RESULT %s checks=%u failures=%u\n", name.c_str(), checks, failures);
    return failures == 0 ? 0 : 1;
}
