// SPDX-License-Identifier: GPL-3.0-only
// New synthetic machine-ABI fixture only. No firmware or endpoint access.
#include "observer.h"
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <unwind.h>
#include <sys/mman.h>
#include <unistd.h>

using az_usb_entry::Boundary;
using az_usb_entry::CaptureResult;
using az_usb_entry::Frame;
using az_usb_entry::Record;
using az_usb_entry::SinkSnapshot;

struct Pair { std::uint64_t first, second; };
struct Hfa { float a, b, c, d; };
using Vector = std::uint64_t __attribute__((vector_size(16)));
struct Aggregate { std::uint64_t a, b, c; };

// Each declaration is an explicit assembly-name alias, not a C++ function
// pointer cast to an incompatible type. The callsite contains no typed return
// adapter, and the stable original dispatcher tail-branches to a matching target.
extern "C" std::uint64_t fixture_call_action(std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t fixture_call_service(std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" Pair fixture_call_pair(std::uint64_t, std::uint64_t, std::uint64_t)
    asm("fixture_call_action");
extern "C" Hfa fixture_call_hfa(std::uint64_t, std::uint64_t, std::uint64_t)
    asm("fixture_call_action");
extern "C" Vector fixture_call_vector(std::uint64_t, std::uint64_t, std::uint64_t)
    asm("fixture_call_action");
extern "C" Aggregate fixture_call_aggregate(std::uint64_t, std::uint64_t, std::uint64_t)
    asm("fixture_call_action");
extern "C" std::uint64_t fixture_call_stack(std::uint64_t, std::uint64_t,
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" void fixture_register_action();
extern "C" void fixture_register_service();
extern "C" void fixture_register_target();
extern "C" void fixture_return_dispatch();
extern "C" char fixture_register_action_lr, fixture_register_service_lr;
extern "C" char fixture_call_action_lr, fixture_call_service_lr;
extern "C" char az_usb_action_entry, az_usb_action_entry_end;
extern "C" char az_usb_service_entry, az_usb_service_entry_end;
extern "C" char fixture_interceptor_end;

extern "C" {
alignas(16) std::uint64_t fixture_expected_gpr[31]{};
alignas(16) std::uint8_t fixture_expected_vectors[32][16]{};
std::uint64_t fixture_expected_sp[2]{};
std::uint64_t fixture_return_sp[2]{};
std::uint64_t fixture_register_results[2]{};
std::uint64_t fixture_stack_values[4]{
    UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210),
    UINT64_C(0xa5a55a5af0f00f0f), UINT64_C(0x5a5aa5a50f0ff0f0)};
std::uint64_t fixture_return_stack[2][4]{};
}

namespace {
constexpr std::size_t MaxCalls = 64;
constexpr int EntryErrno = 71;
constexpr std::uint64_t RegisterResult = UINT64_C(0xfedcba9876543210);
constexpr std::uint64_t Nzcv = UINT64_C(0xa0000000);
constexpr std::uint64_t Fpcr = UINT64_C(0x400000);
constexpr std::uint64_t Fpsr = 1;

struct ObservedFrame {
    Frame state{};
    std::uintptr_t original_sp{};
    std::uint32_t boundary{};
    std::uint32_t result{};
    int helper_errno{};
};
struct NativeEntry {
    std::uint64_t args[3]{};
    int incoming_errno{};
};
std::atomic<unsigned> helper_calls{0}, native_calls{0};
std::atomic<bool> fixture_overflow{false};
ObservedFrame observed[MaxCalls]{};
NativeEntry natives[MaxCalls]{};
Frame native_machine[2]{};
std::uint64_t native_stack[2][4]{};
std::uintptr_t native_sp[2]{};
std::uintptr_t trace_ips[64]{};
std::size_t trace_count = 0;
bool trace_enabled = false; // Set before the single helper-unwind invocation.
unsigned guards_constructed = 0, guards_destroyed = 0, normal_target_returns = 0;
unsigned checks = 0, failures = 0;

void ck(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}

std::uintptr_t address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
template<class T> std::uintptr_t code(T p) {
    return reinterpret_cast<std::uintptr_t>(p);
}
template<class A, class S>
void publish(A action, S service) {
    // These stores occur once per fresh fixture process, before any entry.
    az_usb_action_original.store(code(action), std::memory_order_release);
    az_usb_service_original.store(code(service), std::memory_order_release);
}
bool zero_bytes(const void* ptr, std::size_t n) {
    const auto* p = static_cast<const unsigned char*>(ptr);
    for (std::size_t i = 0; i < n; ++i) if (p[i]) return false;
    return true;
}
bool same_record(const Record& a, const Record& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
bool reserved_zero(const Record& r) {
    return r.reserved_12 == 0 && zero_bytes(r.reserved_68, sizeof(r.reserved_68));
}
bool result_is(unsigned at, CaptureResult result) {
    return at < helper_calls.load() && at < MaxCalls &&
        observed[at].result == static_cast<std::uint32_t>(result);
}
bool all_results(unsigned first, unsigned end, CaptureResult result) {
    for (unsigned i = first; i < end; ++i) if (!result_is(i, result)) return false;
    return true;
}
bool exact_counts(unsigned n) {
    return helper_calls.load() == n && native_calls.load() == n &&
        !fixture_overflow.load();
}
std::uint64_t opaque_result(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    return a ^ (b << 1) ^ (c >> 1);
}
unsigned record_native(std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept {
    const int returned_errno = errno;
    const unsigned i = native_calls.fetch_add(1, std::memory_order_relaxed);
    if (i < MaxCalls) {
        natives[i].args[0] = a; natives[i].args[1] = b; natives[i].args[2] = c;
        natives[i].incoming_errno = returned_errno;
    } else fixture_overflow.store(true);
    errno = returned_errno;
    return i;
}
_Unwind_Reason_Code trace_callback(_Unwind_Context* context, void*) {
    if (trace_count == 64) return _URC_END_OF_STACK;
    trace_ips[trace_count++] = static_cast<std::uintptr_t>(_Unwind_GetIP(context));
    return _URC_NO_REASON;
}
bool trace_contains(std::uintptr_t begin, std::uintptr_t end) {
    for (std::size_t i = 0; i < trace_count; ++i)
        if (trace_ips[i] >= begin && trace_ips[i] < end) return true;
    return false;
}
bool trace_has_ip(std::uintptr_t ip) {
    for (std::size_t i = 0; i < trace_count; ++i) if (trace_ips[i] == ip) return true;
    return false;
}
struct Guard {
    Guard() { ++guards_constructed; }
    ~Guard() { ++guards_destroyed; }
};
struct NativeFault { std::uint64_t token; };
} // namespace

// The real production helper has ALREADY returned. Preserve that exact errno,
// including a deliberately corrupted result from a negative control. This
// collector does not repair it to the value that existed before the helper.
extern "C" void fixture_inspect(
    const Frame* frame, std::uint32_t boundary, std::uint32_t result) noexcept {
    const int post_helper_errno = errno;
    const unsigned i = helper_calls.fetch_add(1, std::memory_order_relaxed);
    if (i < MaxCalls) {
        // Copy only initialized fields, not stack padding or a frame pointer.
        for (unsigned j = 0; j < 31; ++j) observed[i].state.x[j] = frame->x[j];
        std::memcpy(observed[i].state.q, frame->q, sizeof(frame->q));
        observed[i].state.nzcv = frame->nzcv;
        observed[i].state.fpcr = frame->fpcr;
        observed[i].state.fpsr = frame->fpsr;
        observed[i].original_sp = address(frame) + 800;
        observed[i].boundary = boundary;
        observed[i].result = result;
        observed[i].helper_errno = post_helper_errno;
        if (trace_enabled) _Unwind_Backtrace(trace_callback, nullptr);
    } else fixture_overflow.store(true);
    errno = post_helper_errno;
}

extern "C" void fixture_target_capture(const Frame* frame) noexcept {
    const unsigned i = record_native(frame->x[0], frame->x[1], frame->x[2]);
    if (i < 2) {
        native_machine[i] = *frame;
        native_sp[i] = address(frame) + 800;
        std::memcpy(native_stack[i], reinterpret_cast<const void*>(native_sp[i]),
                    sizeof(native_stack[i]));
    } else fixture_overflow.store(true);
}

extern "C" std::uint64_t fixture_opaque_target(
    std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    record_native(a, b, c);
    return opaque_result(a, b, c);
}
extern "C" std::uint64_t fixture_scalar_target(
    std::uint64_t mode, std::uint64_t b, std::uint64_t c) {
    record_native(mode, b, c);
    return UINT64_C(0xfedc0123456789ab);
}
extern "C" Pair fixture_pair_target(
    std::uint64_t mode, std::uint64_t b, std::uint64_t c) {
    record_native(mode, b, c);
    return {UINT64_C(0xfedcba9876543210), UINT64_C(0x8123456789abcdef)};
}
extern "C" Hfa fixture_hfa_target(
    std::uint64_t mode, std::uint64_t b, std::uint64_t c) {
    record_native(mode, b, c);
    return {1.25f, -2.5f, 17.0f, -64.0f};
}
extern "C" Vector fixture_vector_target(
    std::uint64_t mode, std::uint64_t b, std::uint64_t c) {
    record_native(mode, b, c);
    return Vector{UINT64_C(0xffeeddccbbaa0099), UINT64_C(0x123456789abcdef0)};
}
extern "C" Aggregate fixture_sret_target(
    std::uint64_t mode, std::uint64_t b, std::uint64_t c) {
    record_native(mode, b, c);
    return {UINT64_C(0x9988776655443322), b ^ UINT64_C(0xf0f0f0f00f0f0f0f), c + 17};
}
extern "C" std::uint64_t fixture_stack_target(
    std::uint64_t a0, std::uint64_t a1, std::uint64_t a2,
    std::uint64_t a3, std::uint64_t a4, std::uint64_t a5,
    std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9) {
    record_native(a0, a1, a2);
    return a0 + 3*a1 + 5*a2 + 7*a3 + 11*a4 + 13*a5 + 17*a6 + 19*a7 + 23*a8 + 29*a9;
}
extern "C" std::uint64_t fixture_recursive_target(
    std::uint64_t depth, std::uint64_t throwing, std::uint64_t token) {
    record_native(depth, throwing, token);
    Guard target_guard;
    if (depth != 0) {
        const auto value = fixture_call_action(depth - 1, throwing, token);
        ++normal_target_returns;
        return value + 1;
    }
    if (throwing) throw NativeFault{token};
    ++normal_target_returns;
    return token;
}

namespace {
bool snapshot(SinkSnapshot& out) {
    return az_usb_entry::try_snapshot(out);
}
void register_state() {
    publish(&fixture_register_target, &fixture_register_target);
    az_usb_entry::set_ready(true);
    for (unsigned i = 0; i < 31; ++i)
        fixture_expected_gpr[i] = UINT64_C(0x8102030405060708) + UINT64_C(0x0101010101010101) * i;
    for (unsigned q = 0; q < 32; ++q)
        for (unsigned b = 0; b < 16; ++b)
            fixture_expected_vectors[q][b] = static_cast<unsigned char>(q * 19 + b * 7 + 3);
    errno = EntryErrno;
    fixture_register_action();
    errno = EntryErrno;
    fixture_register_service();
    SinkSnapshot sink{};
    const bool got = snapshot(sink);
    const std::uintptr_t lrs[2]{
        address(&fixture_register_action_lr), address(&fixture_register_service_lr)};
    bool entry = helper_calls.load() == 2;
    bool forwarded = native_calls.load() == 2;
    bool stack = true, identity = got && sink.count == 2, native_errno = true;
    for (unsigned k = 0; k < 2; ++k) {
        for (unsigned i = 0; i < 31; ++i) {
            const auto expected = i == 30 ? lrs[k] : fixture_expected_gpr[i];
            entry = entry && observed[k].state.x[i] == expected;
            const auto native_expected = i == 16 ? code(&fixture_register_target) : expected;
            forwarded = forwarded && native_machine[k].x[i] == native_expected;
        }
        entry = entry && std::memcmp(observed[k].state.q, fixture_expected_vectors, 512) == 0 &&
            observed[k].state.nzcv == Nzcv && observed[k].state.fpcr == Fpcr &&
            observed[k].state.fpsr == Fpsr;
        forwarded = forwarded && std::memcmp(native_machine[k].q, fixture_expected_vectors, 512) == 0 &&
            native_machine[k].nzcv == Nzcv && native_machine[k].fpcr == Fpcr &&
            native_machine[k].fpsr == Fpsr;
        stack = stack && fixture_expected_sp[k] % 16 == 0 &&
            observed[k].original_sp == fixture_expected_sp[k] &&
            native_sp[k] == fixture_expected_sp[k] &&
            fixture_return_sp[k] == fixture_expected_sp[k] &&
            native_machine[k].x[30] == lrs[k] &&
            std::memcmp(native_stack[k], fixture_stack_values, 32) == 0 &&
            std::memcmp(fixture_return_stack[k], fixture_stack_values, 32) == 0 &&
            fixture_register_results[k] == RegisterResult;
        identity = identity && sink.records[k].original_sp == fixture_expected_sp[k] &&
            sink.records[k].original_lr == lrs[k] && sink.records[k].boundary == k + 1 &&
            sink.records[k].id == k + 1;
        native_errno = native_errno && observed[k].helper_errno == EntryErrno &&
            natives[k].incoming_errno == EntryErrno;
    }
    ck(entry, "entry-register-snapshot");
    ck(forwarded, "native-register-forwarding");
    ck(stack, "original-stack-lr");
    ck(identity, "record-caller-identity");
    ck(native_errno, "native-errno");
    ck(exact_counts(2) && got && sink.ready && sink.count == 2 &&
       all_results(0, 2, CaptureResult::Recorded), "register-exact-forwarding");
}

void value_records() {
    publish(&fixture_opaque_target, &fixture_opaque_target);
    az_usb_entry::set_ready(true);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) throw std::runtime_error("fixture page size");
    void* page = mmap(nullptr, static_cast<std::size_t>(page_size), PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) throw std::runtime_error("fixture mmap");
    const auto pointer_bits = address(page);
    const std::uint64_t ax0 = UINT64_C(0x8123456789abcdef);
    const std::uint64_t ax1 = UINT64_C(0xaabbccdd11223344);
    const std::uint64_t sx0 = UINT64_C(0xfedc987654321001);
    const std::uint64_t sx1 = UINT64_C(0xa1b2c3d4e5f60718);
    const std::uint64_t sx2 = UINT64_C(0x7654321090abcdef);
    const unsigned char expected_source[12]{
        0x18, 0x07, 0xf6, 0xe5, 0xd4, 0xc3, 0xb2, 0xa1, 0xef, 0xcd, 0xab, 0x90};
    errno = EntryErrno;
    const auto action_result = fixture_call_action(ax0, ax1, pointer_bits);
    errno = EntryErrno;
    const auto service_result = fixture_call_service(sx0, sx1, sx2);
    const int unmapped = munmap(page, static_cast<std::size_t>(page_size));
    SinkSnapshot sink{};
    const bool got = snapshot(sink);
    const auto& a = sink.records[0];
    const auto& s = sink.records[1];
    ck(unmapped == 0 && got && sink.count == 2 &&
       a.boundary == static_cast<unsigned>(Boundary::Action) &&
       a.x0 == ax0 && a.x1 == ax1 && a.x2 == pointer_bits &&
       zero_bytes(a.source, sizeof(a.source)) &&
       action_result == opaque_result(ax0, ax1, pointer_bits),
       "opaque-action-pointer");
    ck(got && sink.count == 2 && s.boundary == static_cast<unsigned>(Boundary::Service) &&
       s.x0 == sx0 && s.x1 == sx1 &&
       std::memcmp(s.source, expected_source, sizeof(expected_source)) == 0,
       "service-exact-twelve-bytes");
    ck(s.x2 == sx2 && natives[1].args[2] == sx2 &&
       service_result == opaque_result(sx0, sx1, sx2),
       "service-high-x2-forwarded");
    ck(exact_counts(2) && all_results(0, 2, CaptureResult::Recorded) &&
       a.id == 1 && s.id == 2 && a.original_lr == address(&fixture_call_action_lr) &&
       s.original_lr == address(&fixture_call_service_lr) &&
       reserved_zero(a) && reserved_zero(s), "value-independent-entries");
}

void return_channels() {
    publish(&fixture_return_dispatch, &fixture_return_dispatch);
    az_usb_entry::set_ready(true);
    const auto scalar = fixture_call_action(1, 7, 9);
    const Pair pair = fixture_call_pair(2, 7, 9);
    const Hfa hfa = fixture_call_hfa(3, 7, 9);
    const Vector vector = fixture_call_vector(4, 7, 9);
    const Aggregate aggregate = fixture_call_aggregate(5, UINT64_C(0x8765432101234567), 99);
    const std::uint64_t args[10]{6, 11, 13, 17, 19, 23, 29, 31,
        UINT64_C(0xfedcba9876543210), UINT64_C(0x8123456789abcdef)};
    const auto stack = fixture_call_stack(args[0], args[1], args[2], args[3], args[4],
        args[5], args[6], args[7], args[8], args[9]);
    const unsigned weights[10]{1,3,5,7,11,13,17,19,23,29};
    std::uint64_t expected_stack = 0;
    for (unsigned i = 0; i < 10; ++i) expected_stack += args[i] * weights[i];
    SinkSnapshot sink{};
    const bool got = snapshot(sink);
    ck(scalar == UINT64_C(0xfedc0123456789ab), "scalar-return-high-bits");
    ck(pair.first == UINT64_C(0xfedcba9876543210) &&
       pair.second == UINT64_C(0x8123456789abcdef), "pair-return");
    ck(hfa.a == 1.25f && hfa.b == -2.5f && hfa.c == 17.0f && hfa.d == -64.0f,
       "hfa-return");
    ck(vector[0] == UINT64_C(0xffeeddccbbaa0099) &&
       vector[1] == UINT64_C(0x123456789abcdef0), "vector-return");
    ck(aggregate.a == UINT64_C(0x9988776655443322) &&
       aggregate.b == (UINT64_C(0x8765432101234567) ^ UINT64_C(0xf0f0f0f00f0f0f0f)) &&
       aggregate.c == 116 && observed[4].state.x[8] != 0, "x8-aggregate-return");
    ck(stack == expected_stack, "typed-stack-arguments");
    ck(exact_counts(6) && got && sink.count == 6 &&
       all_results(0, 6, CaptureResult::Recorded), "return-exact-forwarding");
}

std::uint64_t throwing_caller(std::uint64_t token) {
    Guard caller_guard;
    return fixture_call_action(2, 1, token);
}
void native_exception() {
    publish(&fixture_recursive_target, &fixture_recursive_target);
    az_usb_entry::set_ready(true);
    const std::uint64_t token = UINT64_C(0xfedc01239876abcd);
    bool caught = false, payload = false;
    try {
        (void)throwing_caller(token);
    } catch (const NativeFault& e) {
        caught = true;
        payload = e.token == token;
    }
    const unsigned destroyed_on_throw = guards_destroyed;
    const auto first = fixture_call_action(0, 0, token);
    const auto second = fixture_call_action(0, 0, token);
    SinkSnapshot sink{};
    const bool got = snapshot(sink);
    bool entries = got && sink.count == 5;
    for (unsigned i = 0; i < 5; ++i) entries = entries && sink.records[i].id == i + 1 &&
        sink.records[i].boundary == static_cast<unsigned>(Boundary::Action) &&
        zero_bytes(sink.records[i].source, 12);
    ck(caught && payload && destroyed_on_throw == 4 &&
       guards_constructed == 6 && guards_destroyed == 6, "native-throw-cleanup");
    ck(entries && sink.records[3].x0 == sink.records[4].x0 &&
       sink.records[3].x1 == sink.records[4].x1 &&
       sink.records[3].x2 == sink.records[4].x2 &&
       sink.records[3].id != sink.records[4].id, "throw-independent-entry-records");
    ck(first == token && second == token && normal_target_returns == 2 &&
       exact_counts(5) && all_results(0, 5, CaptureResult::Recorded),
       "throw-later-normal-forwarding");
}

void helper_unwind() {
    publish(&fixture_opaque_target, &fixture_opaque_target);
    az_usb_entry::set_ready(true);
    trace_enabled = true;
    const auto result = fixture_call_action(11, 22, 33);
    trace_enabled = false;
    SinkSnapshot sink{};
    const bool got = snapshot(sink);
    const bool entry_frame = trace_contains(address(&az_usb_action_entry),
                                            address(&az_usb_action_entry_end));
    const bool interceptor = trace_contains(code(&az_usb_entry_observe),
                                            address(&fixture_interceptor_end));
    ck(trace_count > 3 && trace_count < 64 && entry_frame && interceptor,
       "entry-interceptor-live-backtrace");
    ck(trace_has_ip(address(&fixture_call_action_lr)), "backtrace-reaches-real-caller");
    ck(result == opaque_result(11, 22, 33) && exact_counts(1) && got &&
       sink.count == 1 && all_results(0, 1, CaptureResult::Recorded),
       "backtrace-exact-forwarding");
}

void disabled() {
    publish(&fixture_opaque_target, &fixture_opaque_target);
    const auto a = fixture_call_action(1, 2, 3);
    const auto b = fixture_call_service(4, 5, 6);
    const auto c = fixture_call_action(1, 2, 3);
    SinkSnapshot sink{};
    const bool got = snapshot(sink);
    ck(got && !sink.ready && sink.count == 0 &&
       zero_bytes(sink.records, sizeof(sink.records)) &&
       all_results(0, 3, CaptureResult::Disabled), "disabled-empty-sink");
    ck(exact_counts(3) && a == opaque_result(1, 2, 3) &&
       b == opaque_result(4, 5, 6) && c == a, "disabled-exact-forwarding");
}

void capacity() {
    publish(&fixture_opaque_target, &fixture_opaque_target);
    az_usb_entry::set_ready(true);
    bool returns = true;
    for (unsigned i = 0; i < az_usb_entry::Capacity; ++i)
        returns = fixture_call_action(i, 2, 3) == opaque_result(i, 2, 3) && returns;
    SinkSnapshot before{};
    const bool first_snapshot = snapshot(before);
    for (unsigned i = az_usb_entry::Capacity; i < az_usb_entry::Capacity + 2; ++i)
        returns = fixture_call_action(i, 2, 3) == opaque_result(i, 2, 3) && returns;
    SinkSnapshot after{};
    const bool second_snapshot = snapshot(after);
    bool unchanged = first_snapshot && second_snapshot &&
        before.count == az_usb_entry::Capacity && after.count == before.count;
    for (unsigned i = 0; i < az_usb_entry::Capacity; ++i)
        unchanged = unchanged && same_record(before.records[i], after.records[i]) &&
            after.records[i].id == i + 1 && after.records[i].x0 == i;
    ck(unchanged, "capacity-keeps-original-records");
    ck(all_results(0, az_usb_entry::Capacity, CaptureResult::Recorded) &&
       all_results(az_usb_entry::Capacity, az_usb_entry::Capacity + 2, CaptureResult::Full),
       "capacity-exact-refusals");
    ck(returns && exact_counts(az_usb_entry::Capacity + 2), "capacity-exact-forwarding");
}

void contention() {
    publish(&fixture_opaque_target, &fixture_opaque_target);
    az_usb_entry::set_ready(true);
    const bool held = az_usb_entry::fixture_hold_sink();
    if (!held) throw std::runtime_error("fixture exclusive sink");
    SinkSnapshot blocked{};
    blocked.ready = true;
    blocked.count = 123;
    const bool refused_snapshot = !snapshot(blocked);
    std::uint64_t returned[2]{};
    std::thread first, second;
    try {
        first = std::thread([&] { errno = EntryErrno; returned[0] = fixture_call_action(1, 2, 3); });
        second = std::thread([&] { errno = EntryErrno; returned[1] = fixture_call_action(4, 5, 6); });
        first.join();
        second.join();
    } catch (...) {
        az_usb_entry::fixture_release_sink();
        if (first.joinable()) first.join();
        if (second.joinable()) second.join();
        throw;
    }
    az_usb_entry::fixture_release_sink();
    SinkSnapshot empty{};
    const bool empty_snapshot = snapshot(empty);
    const auto after = fixture_call_action(7, 8, 9);
    SinkSnapshot complete{};
    const bool complete_snapshot = snapshot(complete);
    ck(refused_snapshot && blocked.ready && blocked.count == 123 &&
       empty_snapshot && empty.count == 0 && all_results(0, 2, CaptureResult::Contended),
       "contention-is-try-only");
    ck(complete_snapshot && complete.count == 1 && complete.records[0].id == 1 &&
       complete.records[0].x0 == 7 && result_is(2, CaptureResult::Recorded),
       "contention-does-not-invent-records");
    ck(exact_counts(3) && returned[0] == opaque_result(1, 2, 3) &&
       returned[1] == opaque_result(4, 5, 6) && after == opaque_result(7, 8, 9),
       "contention-exact-forwarding");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "FIXTURE_ABORT expected one selector\n");
        return 2;
    }
    const std::string which(argv[1]);
    try {
        if (which == "register-state") register_state();
        else if (which == "value-records") value_records();
        else if (which == "return-channels") return_channels();
        else if (which == "native-exception") native_exception();
        else if (which == "helper-unwind") helper_unwind();
        else if (which == "disabled") disabled();
        else if (which == "capacity") capacity();
        else if (which == "contention") contention();
        else throw std::runtime_error("unknown selector");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FIXTURE_ABORT %s\n", e.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "FIXTURE_ABORT unexpected exception\n");
        return 2;
    }
    std::printf("RESULT %s checks=%u failures=%u\n", which.c_str(), checks, failures);
    return failures ? 1 : 0;
}

