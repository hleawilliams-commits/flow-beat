// SPDX-License-Identifier: GPL-3.0-only
// Four fresh-process checks of authored text patched by actual pinned SDK code.
// No firmware, community loader, helper interception, reset, unpatch or teardown.
#include "bridge.h"
#include "binding.h"
#include "observer.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <stdexcept>
#include <string>
#include <type_traits>

static_assert(std::is_standard_layout<transfer_observation>::value, "assembly value layout");
static_assert(sizeof(transfer_observation) == 104, "assembly value size");
static_assert(offsetof(transfer_observation, caller_sp) == 0, "caller sp");
static_assert(offsetof(transfer_observation, caller_lr) == 8, "caller lr");
static_assert(offsetof(transfer_observation, caller_x16) == 16, "caller scratch");
static_assert(offsetof(transfer_observation, body_x16) == 32, "body x16");
static_assert(offsetof(transfer_observation, body_x17) == 40, "body x17");
static_assert(offsetof(transfer_observation, body_entry_sp) == 48, "body sp");
static_assert(offsetof(transfer_observation, body_lr) == 56, "body lr");
static_assert(offsetof(transfer_observation, body_x0) == 64, "body x0");
static_assert(offsetof(transfer_observation, body_x1) == 72, "body x1");
static_assert(offsetof(transfer_observation, body_x2) == 80, "body x2");
static_assert(offsetof(transfer_observation, body_frame_sp) == 88, "frame sp");
static_assert(offsetof(transfer_observation, body_frame_fp) == 96, "frame fp");
static_assert(std::is_same<az_usb_binding::ClaimHook, decltype(&transfer_claim)>::value,
              "exact SDK provider signature");

namespace {
constexpr std::uint64_t Arg1 = UINT64_C(0xa1b2c3d4e5f60718);
constexpr std::uint64_t Arg2 = UINT64_C(0x1234567890abcdef);
constexpr std::uint64_t Marker = UINT64_C(0xfedcba0987654321);
transfer_observation observed{};
transfer_bridge_report bridge{};
unsigned checks = 0, failures = 0, body_calls = 0, caller_calls = 0;
unsigned body_cleanup = 0, caller_cleanup = 0, body_kind = 0;
bool want_throw = false, body_arguments = false;
struct BodyError { std::uint64_t boundary, marker; };
struct BodyGuard { ~BodyGuard() noexcept { ++body_cleanup; } };
struct CallerGuard { ~CallerGuard() noexcept { ++caller_cleanup; } };

void check(bool yes, const char* name) {
    ++checks;
    if (!yes) { ++failures; std::printf("FAIL %s\n", name); }
}
std::uint32_t u32(const std::uint8_t* p) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(p[i]) << (8*i);
    return result;
}
std::uint64_t u64(const std::uint8_t* p) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= std::uint64_t(p[i]) << (8*i);
    return result;
}
std::uint64_t expected_result(unsigned index) {
    return index == 0 ? UINT64_C(0xfedcba9801234567) : UINT64_C(0x87654321fedcba98);
}
bool installed() {
    if (bridge.prepared != 1 || bridge.claims != 2 || bridge.contract_errors || bridge.diagnostics) return false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& r = bridge.claim[i];
        if (!r.contract_ok || r.internal_result != 1 || r.path != 1 || r.late != 1 ||
            !r.original || !r.island || !r.bytes_available) return false;
    }
    return true;
}
bool jump_shape(const std::uint8_t* p, bool allow_control) {
    const auto load=u32(p), branch=u32(p+4);
    return (load==UINT32_C(0x58000051) && branch==UINT32_C(0xd61f0220)) ||
        (allow_control && load==UINT32_C(0x58000050) && branch==UINT32_C(0xd61f0200));
}
bool route_shape(bool allow_control) {
    for (unsigned i = 0; i < 2; ++i) {
        const auto& r = bridge.claim[i];
        const auto branch = u32(r.after);
        std::int64_t delta = branch & UINT32_C(0x03ffffff);
        if (delta & (INT64_C(1) << 25)) delta -= INT64_C(1) << 26;
        const auto expected_stp = i == 0 ? UINT32_C(0xa9b77bfd) : UINT32_C(0xa9af7bfd);
        if ((branch & UINT32_C(0xfc000000)) != UINT32_C(0x14000000) ||
            r.target + delta * 4 != r.island ||
            u32(r.before) != expected_stp || std::memcmp(r.before+4, r.after+4, 12) != 0 ||
            !jump_shape(r.island_bytes,allow_control) || u64(r.island_bytes+8) != r.hook ||
            u32(r.trampoline_bytes) != expected_stp ||
            !jump_shape(r.trampoline_bytes+4,allow_control) ||
            u64(r.trampoline_bytes+12) != r.target+4) return false;
    }
    return true;
}
bool layouts() { return route_shape(false); }
bool protections() {
    for (const auto& r : bridge.claim)
        if (r.target_prot_before != 5 || r.target_prot_after != 5 ||
            r.island_prot != 5 || r.trampoline_prot != 5) return false;
    return true;
}
bool safe_route() {
    if (!installed() || !protections() || !route_shape(true)) return false;
    for (unsigned i=0; i<2; ++i) {
        const auto& r=bridge.claim[i];
        const auto target=i==0 ? reinterpret_cast<std::uintptr_t>(&fixture_action_target)
                               : reinterpret_cast<std::uintptr_t>(&fixture_service_target);
        const auto hook=i==0 ? reinterpret_cast<std::uintptr_t>(&az_usb_action_entry)
                             : reinterpret_cast<std::uintptr_t>(&az_usb_service_entry);
        void** const slot=i==0 ? &az_usb_action_original : &az_usb_service_original;
        if (!r.target || !r.hook || !r.original || !r.island ||
            ((r.target|r.hook|r.original)&3) || (r.island&15) ||
            (r.output_slot&7) || r.target>UINT64_MAX-16 || r.original>UINT64_MAX-20 ||
            r.island>UINT64_MAX-16 || r.target!=target || r.hook!=hook ||
            r.output_slot!=reinterpret_cast<std::uintptr_t>(slot) ||
            r.original!=reinterpret_cast<std::uintptr_t>(__atomic_load_n(slot,__ATOMIC_ACQUIRE)))
            return false;
    }
    return true;
}
__attribute__((noinline))
std::uint64_t invoke(unsigned index) {
    ++caller_calls;
    CallerGuard guard;
    return index == 0 ? fixture_call_action(&observed, Arg1, Arg2)
                      : fixture_call_service(&observed, Arg1, Arg2);
}
void hex(FILE* f, const std::uint8_t* p, std::size_t n) {
    std::fputc('"', f);
    for (std::size_t i=0; i<n; ++i) std::fprintf(f, "%02x", unsigned(p[i]));
    std::fputc('"', f);
}
void write_report(const char* path, const std::string& selector, unsigned index,
                  bool caught, std::uint64_t result, bool snapshot_ok,
                  const az_usb_entry::SinkSnapshot& snapshot, bool closed_ok) {
    FILE* f = std::fopen(path, "wx");
    if (!f) throw std::runtime_error("cannot create new report");
    std::fprintf(f, "{\"schema\":1,\"selector\":\"%s\",\"synthetic_only\":true,"
        "\"checks\":%u,\"failures\":%u,\"index\":%u,\"throw_requested\":%s,"
        "\"caught_exact\":%s,\"result\":%" PRIu64 ",\"prepared\":%u,\"claims\":%u,"
        "\"contract_errors\":%u,\"diagnostics\":%u,\"routes\":[",
        selector.c_str(), checks, failures, index, want_throw?"true":"false",
        caught?"true":"false", result, bridge.prepared, bridge.claims, bridge.contract_errors, bridge.diagnostics);
    for (unsigned i=0; i<2; ++i) {
        const auto& r=bridge.claim[i];
        if (i) std::fputc(',', f);
        std::fprintf(f, "{\"requested\":%" PRIu64 ",\"target\":%" PRIu64 ",\"hook\":%" PRIu64
            ",\"output_slot\":%" PRIu64 ",\"original\":%" PRIu64 ",\"island\":%" PRIu64
            ",\"contract_ok\":%d,\"internal_result\":%d,\"path\":%d,\"late\":%d,"
            "\"target_prot_before\":%d,\"target_prot_after\":%d,\"island_prot\":%d,"
            "\"trampoline_prot\":%d,\"bytes_available\":%d,\"before\":",
            r.requested,r.target,r.hook,r.output_slot,r.original,r.island,
            r.contract_ok,r.internal_result,r.path,r.late,r.target_prot_before,r.target_prot_after,
            r.island_prot,r.trampoline_prot,r.bytes_available);
        hex(f,r.before,16); std::fputs(",\"after\":",f); hex(f,r.after,16);
        std::fputs(",\"island_bytes\":",f); hex(f,r.island_bytes,16);
        std::fputs(",\"trampoline_bytes\":",f); hex(f,r.trampoline_bytes,20);
        std::fputc('}',f);
    }
    std::fprintf(f, "],\"observation\":{\"caller_sp\":%" PRIu64 ",\"caller_lr\":%" PRIu64
        ",\"caller_x16\":%" PRIu64 ",\"caller_x17\":%" PRIu64 ",\"body_x16\":%" PRIu64
        ",\"body_x17\":%" PRIu64 ",\"body_entry_sp\":%" PRIu64 ",\"body_lr\":%" PRIu64
        ",\"body_x0\":%" PRIu64 ",\"body_x1\":%" PRIu64 ",\"body_x2\":%" PRIu64
        ",\"body_frame_sp\":%" PRIu64 ",\"body_frame_fp\":%" PRIu64 "},"
        "\"body_calls\":%u,\"caller_calls\":%u,\"body_cleanup\":%u,\"caller_cleanup\":%u,"
        "\"body_kind\":%u,\"body_arguments\":%s,\"snapshot_ok\":%s,"
        "\"capture_ready\":%s,\"capture_count\":%zu,\"closed_retained\":%s,\"record\":{",
        observed.caller_sp,observed.caller_lr,observed.caller_x16,observed.caller_x17,
        observed.body_x16,observed.body_x17,observed.body_entry_sp,observed.body_lr,
        observed.body_x0,observed.body_x1,observed.body_x2,observed.body_frame_sp,observed.body_frame_fp,
        body_calls,caller_calls,body_cleanup,caller_cleanup,body_kind,body_arguments?"true":"false",
        snapshot_ok?"true":"false",snapshot.ready?"true":"false",snapshot.count,closed_ok?"true":"false");
    const auto& r=snapshot.records[0];
    std::fprintf(f, "\"id\":%" PRIu64 ",\"boundary\":%u,\"reserved_12\":%u,"
        "\"original_sp\":%" PRIu64 ",\"original_lr\":%" PRIu64 ",\"x0\":%" PRIu64
        ",\"x1\":%" PRIu64 ",\"x2\":%" PRIu64 ",\"source\":",
        r.id,r.boundary,r.reserved_12,r.original_sp,r.original_lr,r.x0,r.x1,r.x2);
    hex(f,r.source,12); std::fputs(",\"reserved_68\":",f); hex(f,r.reserved_68,4);
    std::fputs("}}\n",f);
    const bool error=std::ferror(f);
    const int close_result=std::fclose(f);
    if (error || close_result) throw std::runtime_error("report write/close failed");
}
} // namespace

extern "C" __attribute__((noinline))
std::uint64_t transfer_body(transfer_observation* value, std::uint64_t a,
                            std::uint64_t b, std::uint64_t kind) {
    ++body_calls;
    body_kind=unsigned(kind);
    body_arguments=value==&observed && a==Arg1 && b==Arg2;
    BodyGuard guard;
    if (want_throw) throw BodyError{kind,Marker};
    return kind==1 ? UINT64_C(0xfedcba9801234567) : UINT64_C(0x87654321fedcba98);
}

int main(int argc, char** argv) {
    if (argc!=3) { std::fprintf(stderr,"FIXTURE_ABORT selector report.json required\n"); return 2; }
    try {
        const std::string name=argv[1];
        unsigned index=0;
        if (name=="action-return") { index=0; want_throw=false; }
        else if (name=="service-return") { index=1; want_throw=false; }
        else if (name=="action-throw") { index=0; want_throw=true; }
        else if (name=="service-throw") { index=1; want_throw=true; }
        else throw std::runtime_error("unknown selector");
        const int prepared=transfer_prepare();
        const auto install=az_usb_binding::install(&transfer_claim);
        transfer_report(&bridge);
        az_usb_entry::SinkSnapshot captured{};
        bool caught=false, snapshot_ok=false, closed_ok=false;
        std::uint64_t returned=0;
        // An unexpected setup failure retains a report and never invokes an
        // unvalidated entry. The safe scratch control may use paired x16 jumps;
        // malformed code/destinations/protections are refused before invocation.
        if (prepared || install!=az_usb_binding::InstallResult::Ready || !safe_route()) {
            write_report(argv[2],name,index,caught,returned,false,captured,false);
            throw std::runtime_error("actual late island installation failed");
        }
        try { returned=invoke(index); }
        catch (const BodyError& e) { caught=e.boundary==index+1 && e.marker==Marker; }
        snapshot_ok=az_usb_entry::try_snapshot(captured);
        const auto& r=bridge.claim[index];
        const auto& record=captured.records[0];
        const std::uint64_t frame=index==0?144:272;
        const std::uint8_t source[12]={0x18,0x07,0xf6,0xe5,0xd4,0xc3,0xb2,0xa1,0xef,0xcd,0xab,0x90};
        const std::uint8_t zero[12]={};
        const bool forwarded=observed.caller_sp!=0 && observed.caller_lr!=0 &&
            observed.caller_x16==0x1616 && observed.caller_x17==0x1717 &&
            observed.body_entry_sp==observed.caller_sp && observed.body_lr==observed.caller_lr &&
            observed.body_frame_sp+frame==observed.caller_sp &&
            observed.body_frame_fp==observed.body_frame_sp &&
            observed.body_x0==reinterpret_cast<std::uintptr_t>(&observed) &&
            observed.body_x1==Arg1 && observed.body_x2==Arg2 && body_arguments;
        az_usb_binding::close();
        az_usb_entry::SinkSnapshot after{};
        closed_ok=az_usb_binding::state()==az_usb_binding::State::Closed &&
            az_usb_entry::try_snapshot(after) && !after.ready && after.count==captured.count &&
            reinterpret_cast<std::uintptr_t>(__atomic_load_n(&az_usb_action_original,__ATOMIC_ACQUIRE))==bridge.claim[0].original &&
            reinterpret_cast<std::uintptr_t>(__atomic_load_n(&az_usb_service_original,__ATOMIC_ACQUIRE))==bridge.claim[1].original;
        check(installed(),"late-island-installation");
        check(layouts(),"emitted-route");
        check(protections(),"restored-rx-protections");
        check(observed.body_x16==r.original && observed.body_x17==r.target+4,"body-scratch");
        check(forwarded,"caller-stack-registers");
        check(snapshot_ok && captured.ready && captured.count==1 && record.id==1 &&
            record.boundary==index+1 && record.original_sp==observed.caller_sp &&
            record.original_lr==observed.caller_lr && record.x0==observed.body_x0 &&
            record.x1==Arg1 && record.x2==Arg2 &&
            std::memcmp(record.source,index==0?zero:source,12)==0,"entry-capture");
        check(want_throw?(caught && returned==0):(!caught && returned==expected_result(index)),
              "ordinary-return-or-throw");
        check(body_calls==1 && caller_calls==1 && body_kind==index+1 &&
            body_cleanup==1 && caller_cleanup==1 && closed_ok,"cleanup-and-retention");
        write_report(argv[2],name,index,caught,returned,snapshot_ok,captured,closed_ok);
        std::printf("RESULT %s checks=%u failures=%u\n",name.c_str(),checks,failures);
        return failures?1:0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"FIXTURE_ABORT %s\n",e.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr,"FIXTURE_ABORT unexpected exception\n");
        return 2;
    }
}
