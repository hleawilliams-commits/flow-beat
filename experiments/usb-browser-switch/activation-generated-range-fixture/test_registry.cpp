// Offline synthetic metadata fixture. No SDK installer, native dereference,
// signal delivery, mapping, protection change, or attribution file I/O occurs.
#include "registry.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace az_generated;
constexpr std::uint64_t Process = 0x1122334455667788ULL;
constexpr std::uintptr_t Target = 0x100000, Island = 0x200000, Tramp = 0x300000;
constexpr std::uintptr_t Frame = 0x400000, Native = 0x900000;
unsigned checks = 0;
std::vector<const char*> failed;

void check(bool value, const char* name) {
    ++checks;
    if (!value) failed.push_back(name);
}
OwnerInput community(const char* id = "alpha", std::uint64_t process = Process) {
    return {OwnerKind::Community, process, id, std::strlen(id)};
}
OwnerInput core() { return {OwnerKind::Core, Process, nullptr, 0}; }
EmissionInput emission(std::uintptr_t island = Island, std::uintptr_t tramp = Tramp,
                       unsigned n = 1, unsigned m = 0) {
    return {island, tramp, n, m};
}
bool range_is(const Range& r, std::uintptr_t lo, std::uintptr_t hi) {
    return r.begin == lo && r.end == hi;
}
bool no_match(const LookupResult& r) {
    return r.error == Error::Ok && r.kind == MatchKind::NoMatch &&
           r.source == MatchSource::None;
}
bool owns(const LookupResult& r, const char* id,
          MatchSource source = MatchSource::Generated) {
    const std::size_t n = std::strlen(id);
    return r.error == Error::Ok && r.kind == MatchKind::Community &&
           r.source == source && r.owner.kind == OwnerKind::Community &&
           r.owner.process_start == Process && r.owner.id_length == n &&
           std::memcmp(r.owner.id.data(), id, n) == 0 && r.owner.id[n] == '\0';
}
bool is_core(const LookupResult& r) {
    return r.error == Error::Ok && r.kind == MatchKind::Core &&
           r.source == MatchSource::Generated && r.owner.kind == OwnerKind::Core &&
           r.owner.process_start == Process && r.owner.id_length == 0 && r.owner.id[0] == 0;
}
bool publish(Registry& r, const OwnerInput& owner, std::uintptr_t target,
             const EmissionInput& e, const ModuleSnapshot& modules, Ticket* saved = nullptr) {
    Ticket t;
    if (r.reserve(owner, target, t) != Error::Ok) return false;
    if (saved) *saved = t;
    if (r.prepare(t, e, modules) != Error::Ok) return false;
    PublishPermit permit;
    if (r.validate_to_publish(t, modules, permit) != Error::Ok) return false;
    return r.commit(permit) == Error::Ok;
}
bool module(ModuleSnapshot& m, std::uintptr_t lo, std::uintptr_t hi,
            const char* id = "module", std::uint64_t process = Process) {
    const ModuleInput input{lo, hi, community(id, process)};
    return m.assign(&input, 1) == Error::Ok;
}

void emitted_variants() {
    // Independent table: seven reviewed ncopy/ADRP expansion combinations.
    struct Row { unsigned n, m; std::uintptr_t length; const char* name; };
    const Row rows[] = {{1,0,12,"n1-m0"}, {1,1,24,"n1-m1"},
        {4,0,24,"n4-m0"}, {4,1,36,"n4-m1"}, {4,2,48,"n4-m2"},
        {4,3,60,"n4-m3"}, {4,4,72,"n4-m4"}};
    std::uint64_t instance = 1;
    for (const auto& row : rows) {
        Registry r(Process, instance++);
        ModuleSnapshot modules;
        const auto e = emission(row.n == 1 ? Island : 0, Tramp, row.n, row.m);
        EmissionLayout layout;
        const bool made = compute_layout(e, layout) == Error::Ok &&
                          publish(r, community(), Target, e, modules);
        const auto hit = r.lookup(Tramp, modules);
        const bool island_ok = row.n == 1
            ? range_is(layout.island_instructions, Island, Island+8) &&
              range_is(layout.island_literal, Island+8, Island+16) &&
              owns(r.lookup(Island+7, modules), "alpha") &&
              no_match(r.lookup(Island+8, modules))
            : range_is(layout.island_instructions, 0, 0) &&
              range_is(layout.island_literal, 0, 0) && no_match(r.lookup(Island, modules));
        check(made && island_ok && layout.ncopy == row.n && layout.expanded_adrps == row.m &&
              range_is(layout.trampoline_instructions, Tramp, Tramp+row.length) &&
              range_is(layout.trampoline_literal, Tramp+row.length, Tramp+row.length+8) &&
              owns(hit, "alpha") && hit.target == Target &&
              range_is(hit.layout.trampoline_instructions, Tramp, Tramp+row.length) &&
              owns(r.lookup(Tramp+row.length-1, modules), "alpha") &&
              no_match(r.lookup(Tramp+row.length, modules)), row.name);
    }
}

void interval_boundaries() {
    Registry r(Process, 20);
    ModuleSnapshot modules;
    const bool a = publish(r, community(), Target, emission(Island, Tramp, 1, 1), modules);
    const bool b = publish(r, community("beta"), Target+16,
                           emission(Island+16, Tramp+4096), modules);
    check(a && b, "two-adjacent-slot-records");
    check(no_match(r.lookup(Island-1, modules)) && owns(r.lookup(Island, modules), "alpha") &&
          owns(r.lookup(Island+7, modules), "alpha") && no_match(r.lookup(Island+8, modules)) &&
          no_match(r.lookup(Island+15, modules)), "island-instruction-literal-boundaries");
    check(owns(r.lookup(Island+16, modules), "beta") && owns(r.lookup(Island+23, modules), "beta") &&
          no_match(r.lookup(Island+24, modules)) && no_match(r.lookup(Island+65535, modules)),
          "neighbor-and-pool-slack");
    check(no_match(r.lookup(Tramp-1, modules)) && owns(r.lookup(Tramp, modules), "alpha") &&
          owns(r.lookup(Tramp+23, modules), "alpha") && no_match(r.lookup(Tramp+24, modules)) &&
          no_match(r.lookup(Tramp+31, modules)) && no_match(r.lookup(Tramp+4095, modules)),
          "trampoline-literal-and-page-slack");
    check(no_match(r.lookup(Target, modules)) && no_match(r.lookup(Native, modules)),
          "native-target-not-added");
}

void invalid_inputs() {
    const EmissionInput bad[] = {emission(Island,Tramp,0), emission(Island,Tramp,2),
        emission(0,Tramp,1), emission(Island,Tramp,4), emission(Island,0),
        emission(Island+1,Tramp), emission(Island,Tramp+2), emission(Island,Tramp,1,2),
        emission(0,Tramp,4,5)};
    bool invalid = true;
    for (const auto& e : bad) {
        EmissionLayout out;
        out.ncopy = 777;
        invalid = (compute_layout(e, out) == Error::InvalidEmission && out.ncopy == 777) && invalid;
    }
    check(invalid, "invalid-emissions-leave-output");
    const auto top = std::numeric_limits<std::uintptr_t>::max() & ~std::uintptr_t{3};
    EmissionLayout out;
    out.ncopy = 777;
    const bool overflow = compute_layout(emission(top,Tramp), out) == Error::EndpointOverflow &&
        compute_layout(emission(Island,top), out) == Error::EndpointOverflow &&
        compute_layout(emission(top-8,Tramp), out) == Error::EndpointOverflow &&
        compute_layout(emission(Island,top-12), out) == Error::EndpointOverflow;
    check(overflow && out.ncopy == 777, "literal-endpoint-overflow");
    Registry r(Process, 21);
    Ticket t;
    check(r.reserve(community(), 0, t) == Error::InvalidTarget &&
          r.reserve(community(), Target+1, t) == Error::InvalidTarget &&
          r.reserve(community(), top, t) == Error::EndpointOverflow && t.slot() == kCapacity,
          "invalid-target-no-ticket");
    const char too_long[] = "abcdefghijklmnopqrstuvwxyz1234567";
    const OwnerInput bad_owners[] = {{OwnerKind::Community,Process,nullptr,1},
        {OwnerKind::Community,Process,"",0}, {OwnerKind::Community,Process,too_long,33},
        {OwnerKind::Community,Process,"bad id",6}, community("alpha",Process+1),
        {OwnerKind::Core,Process,"x",1}};
    bool owners = true;
    for (const auto& owner : bad_owners)
        owners = (r.reserve(owner, Target, t) == Error::InvalidOwner) && owners;
    check(owners && t.slot() == kCapacity && r.reserve(community(), Target, t) == Error::Ok,
          "invalid-owner-no-reservation");
    Registry no_process(0,22), no_instance(Process,0);
    Ticket ignored;
    check(no_process.reserve(community(),Target,ignored) == Error::InvalidRegistry &&
          no_instance.reserve(community(),Target,ignored) == Error::InvalidRegistry,
          "invalid-registry-identities");
    ModuleSnapshot modules;
    const bool made = module(modules,Native,Native+32);
    const ModuleInput bad_range{Native,Native,community()};
    const ModuleInput mixed[] = {{Native,Native+16,community()},
                               {Native+32,Native+48,community("beta",Process+1)}};
    check(made && modules.assign(&bad_range,1) == Error::InvalidModules &&
          modules.assign(nullptr,1) == Error::InvalidModules &&
          modules.assign(mixed,2) == Error::InvalidModules && modules.size() == 1 &&
          owns(r.lookup(Native,modules),"module",MatchSource::Module),
          "invalid-module-assign-preserves-copy");
}

void reservation_conflicts() {
    Registry r(Process,23);
    ModuleSnapshot modules;
    Ticket first, second, rejected;
    const bool reserved = r.reserve(community(),Target,first) == Error::Ok;
    const auto overlap = r.reserve(community("beta"),Target+12,rejected);
    const auto adjacent = r.reserve(community("beta"),Target+16,second);
    check(reserved && overlap == Error::TargetConflict && adjacent == Error::Ok &&
          rejected.slot() == kCapacity, "target16-reservation-conflict-and-adjacency");
    const bool prepared = r.prepare(first,emission(),modules) == Error::Ok;
    // New island instructions intersect only the old island literal.
    const auto literal_hit = r.prepare(second,emission(Island+8,Tramp+4096),modules);
    // Mixed overlap: both instruction spans and literal tails intersect.
    const auto literals = r.prepare(second,emission(0,Island-16,4,0),modules);
    check(prepared && literal_hit == Error::RangeConflict && literals == Error::RangeConflict &&
          no_match(r.lookup(Island,modules)), "prepared-overlap-is-invisible");
    const auto cross = r.prepare(second,emission(Tramp+12,Tramp+4096),modules);
    const auto future_target = r.reserve(community("gamma"),Tramp+12,rejected);
    const auto future_adjacent = r.reserve(community("gamma"),Tramp+20,rejected);
    check(cross == Error::RangeConflict && future_target == Error::TargetConflict &&
          future_adjacent == Error::Ok, "cross-kind-range-conflict");
    EmissionLayout layout;
    layout.ncopy = 777;
    const auto self = compute_layout(emission(Island,Island+8),layout);
    Registry forward(Process,231);
    Ticket ft;
    const bool forward_reserved = forward.reserve(community(),Target,ft) == Error::Ok;
    const auto own_target = forward.prepare(ft,emission(Target-8,Tramp),modules);
    const auto exact_adjacent = forward.prepare(ft,emission(Target-16,Tramp),modules);
    check(self == Error::RangeConflict && layout.ncopy == 777 && forward_reserved &&
          own_target == Error::TargetConflict && exact_adjacent == Error::Ok,
          "one-emission-internal-overlap");
    PublishPermit permit;
    const bool committed = r.validate_to_publish(first,modules,permit) == Error::Ok &&
                           r.commit(permit) == Error::Ok;
    check(committed && r.reserve(community(),Target+4,rejected) == Error::TargetConflict &&
          r.cancel(first) == Error::Published && owns(r.lookup(Island,modules),"alpha"),
          "published-conflict-cannot-cancel");
}

void capacity() {
    Registry r(Process,24);
    ModuleSnapshot modules;
    std::array<Ticket,kCapacity> tickets{};
    bool all = true;
    for (std::size_t i=0;i<kCapacity;++i)
        all = (r.reserve(community(),Target+16*i,tickets[i]) == Error::Ok) && all;
    Ticket extra;
    check(all && r.reserve(community(),Target+16*kCapacity,extra) == Error::CapacityExhausted &&
          extra.slot() == kCapacity, "reserved-capacity-refuses-before-exposure");
    check(no_match(r.lookup(Island,modules)) && no_match(r.lookup(Tramp,modules)),
          "full-reservations-invisible");
    const Ticket old = tickets[3];
    const auto canceled = r.cancel(old);
    const auto replaced = r.reserve(community("beta"),Target+16*3,extra);
    check(canceled == Error::Ok && replaced == Error::Ok && extra.slot() == old.slot() &&
          extra.generation() != old.generation() && r.cancel(old) == Error::InvalidTicket,
          "cancel-reuses-capacity-with-new-generation");
    Ticket none;
    check(r.reserve(community(),Target+16*kCapacity,none) == Error::CapacityExhausted,
          "replacement-capacity-stays-bounded");
    Registry published(Process,241);
    bool full = true;
    for (std::size_t i=0;i<kCapacity;++i) {
        Ticket retained;
        const bool made = publish(published,community(),Target+16*i,
            emission(Island+256*i,Tramp+256*i),modules,&retained);
        full = made && published.cancel(retained) == Error::Published &&
               owns(published.lookup(Island+256*i,modules),"alpha") && full;
    }
    check(full && published.reserve(community(),Target+16*kCapacity,none) == Error::CapacityExhausted,
          "published-capacity-cannot-be-reclaimed");
}

void owner_custody() {
    Registry r(Process,25);
    ModuleSnapshot modules;
    char name[] = "alpha";
    Ticket t;
    const bool reserved = r.reserve(community(name),Target,t) == Error::Ok;
    std::memcpy(name,"omega",5);
    PublishPermit p;
    const bool made = r.prepare(t,emission(),modules) == Error::Ok &&
                      r.validate_to_publish(t,modules,p) == Error::Ok && r.commit(p) == Error::Ok;
    check(reserved && made && owns(r.lookup(Island,modules),"alpha"),
          "owner-copied-before-source-mutation");
    const int modeled_init_result = -1;
    check(modeled_init_result != 0 && r.cancel(t) == Error::Published &&
          owns(r.lookup(Tramp,modules),"alpha"), "modeled-init-failure-retains-published-owner");
    auto copied = r.lookup(Island,modules);
    copied.owner.id[0] = 'X';
    check(copied.owner.id[0] == 'X' && owns(r.lookup(Island,modules),"alpha"),
          "returned-identity-is-independent-copy");
    char module_name[] = "module";
    const bool assigned = module(modules,Native,Native+32,module_name);
    module_name[0] = 'X';
    check(assigned && owns(r.lookup(Native,modules),"module",MatchSource::Module),
          "module-snapshot-copies-owner");
}

void cancel_stale_ticket() {
    Registry r(Process,26), foreign(Process,27);
    ModuleSnapshot modules;
    Ticket old, replacement;
    const bool made = r.reserve(community(),Target,old) == Error::Ok &&
                      r.prepare(old,emission(),modules) == Error::Ok;
    const auto other = foreign.cancel(old);
    const auto canceled = r.cancel(old);
    const auto replaced = r.reserve(community("beta"),Target,replacement);
    check(made && other == Error::InvalidTicket && canceled == Error::Ok && replaced == Error::Ok &&
          old.slot() == replacement.slot() && old.generation() != replacement.generation(),
          "cancel-and-registry-bound-ticket");
    PublishPermit stale;
    const auto stale_cancel = r.cancel(old);
    const auto stale_prepare = r.prepare(old,emission(),modules);
    const auto stale_publish = r.validate_to_publish(old,modules,stale);
    check(stale_cancel == Error::InvalidTicket && stale_prepare == Error::InvalidTicket &&
          stale_publish == Error::InvalidTicket && !stale.valid(),
          "stale-ticket-cannot-touch-replacement");
    PublishPermit original;
    const bool authorized = r.prepare(replacement,emission(),modules) == Error::Ok &&
                            r.validate_to_publish(replacement,modules,original) == Error::Ok;
    PublishPermit moved(std::move(original));
    const auto old_permit = r.commit(original);
    const auto protected_slot = r.cancel(replacement);
    const auto aborted = r.abort(moved);
    check(authorized && !original.valid() && old_permit == Error::InvalidPermit &&
          protected_slot == Error::WrongPhase && aborted == Error::Ok &&
          no_match(r.lookup(Island,modules)), "permit-move-and-invisible-abort");
    check(!moved.valid() && r.commit(moved) == Error::InvalidPermit &&
          r.abort(moved) == Error::InvalidPermit, "consumed-permit-no-replay");
    Ticket retained;
    bool retained_authorized = false;
    {
        PublishPermit dropped;
        retained_authorized = r.reserve(community(),Target,retained) == Error::Ok &&
            r.prepare(retained,emission(),modules) == Error::Ok &&
            r.validate_to_publish(retained,modules,dropped) == Error::Ok;
    }
    check(retained_authorized && r.cancel(retained) == Error::WrongPhase &&
          no_match(r.lookup(Island,modules)), "dropping-permit-does-not-cancel-or-publish");
}

// These are explicitly authored installer decisions. They do not call the SDK,
// allocate code, change protections or establish that any cleanup really unmaps.
enum class Refusal { Target, Prologue, LateNoIsland, ConstructorCover, Mmap,
                     Seal, IslandUnlock, TargetUnlock };
struct ModelResult {
    bool reserved=false, allocation=false, cleanup_attempted=false, allocation_retained=false;
    bool invisible=false, reservation_released=false, original=false, redirect=false;
};
ModelResult refuse_model(Refusal stop, bool cleanup_succeeds, std::uint64_t instance,
                         bool constructor_fallback = false) {
    Registry r(Process,instance);
    ModuleSnapshot modules;
    Ticket t;
    ModelResult out;
    const auto reserved = r.reserve(community(),stop == Refusal::Target ? 0 : Target,t);
    out.reserved = reserved == Error::Ok;
    if (!out.reserved) {
        out.invisible = no_match(r.lookup(Island,modules));
        return out;
    }
    out.allocation = stop == Refusal::Seal || stop == Refusal::IslandUnlock ||
                     stop == Refusal::TargetUnlock;
    if (stop == Refusal::TargetUnlock) {
        PublishPermit p;
        const auto e = constructor_fallback ? emission(0,Tramp,4,0) : emission();
        const bool authorized = r.prepare(t,e,modules) == Error::Ok &&
            r.validate_to_publish(t,modules,p) == Error::Ok;
        out.reservation_released = authorized && r.abort(p) == Error::Ok;
    } else {
        out.reservation_released = r.cancel(t) == Error::Ok;
    }
    if (out.allocation) {
        out.cleanup_attempted = true;
        out.allocation_retained = !cleanup_succeeds;
    }
    out.invisible = no_match(r.lookup(Island,modules)) && no_match(r.lookup(Tramp,modules));
    return out;
}
void modeled_prepublication_refusals() {
    const std::array<Refusal,8> stops{{Refusal::Target,Refusal::Prologue,Refusal::LateNoIsland,
        Refusal::ConstructorCover,Refusal::Mmap,Refusal::Seal,Refusal::IslandUnlock,Refusal::TargetUnlock}};
    const char* names[] = {"target-refusal","prologue-refusal","late-island-refusal","cover-refusal",
        "trampoline-map-refusal","seal-refusal","island-unlock-refusal","target-unlock-refusal"};
    for (std::size_t i=0;i<stops.size();++i) {
        const auto got = refuse_model(stops[i],true,40+i);
        const bool allocated = i >= 5;
        check(got.reserved == (i != 0) && got.reservation_released == (i != 0) &&
              got.allocation == allocated && got.cleanup_attempted == allocated &&
              !got.allocation_retained && got.invisible && !got.original && !got.redirect, names[i]);
    }
    const auto retained = refuse_model(Refusal::TargetUnlock,false,48);
    check(retained.reserved && retained.reservation_released && retained.cleanup_attempted &&
          retained.allocation_retained && retained.invisible && !retained.original && !retained.redirect,
          "failed-cleanup-not-release-proof");
    const auto fallback = refuse_model(Refusal::TargetUnlock,true,49,true);
    check(fallback.reserved && fallback.reservation_released && fallback.cleanup_attempted &&
          !fallback.allocation_retained && fallback.invisible && !fallback.original && !fallback.redirect,
          "constructor-fallback-target-unlock-refusal");
    const auto fallback_retained = refuse_model(Refusal::TargetUnlock,false,50,true);
    check(fallback_retained.reserved && fallback_retained.reservation_released &&
          fallback_retained.cleanup_attempted && fallback_retained.allocation_retained &&
          fallback_retained.invisible && !fallback_retained.original && !fallback_retained.redirect,
          "constructor-fallback-cleanup-retained");
}

void publication_order() {
    Registry r(Process,60);
    ModuleSnapshot modules;
    Ticket t;
    PublishPermit p;
    const bool reserved = r.reserve(community(),Target,t) == Error::Ok;
    const bool at_reserved = no_match(r.lookup(Island,modules));
    const bool prepared = r.prepare(t,emission(),modules) == Error::Ok;
    const bool at_prepared = no_match(r.lookup(Island,modules));
    const bool authorized = r.validate_to_publish(t,modules,p) == Error::Ok;
    const bool at_authorized = no_match(r.lookup(Island,modules));
    bool target_unlocked = false, original = false, redirect = false;
    check(reserved && prepared && authorized && at_reserved && at_prepared && at_authorized &&
          !target_unlocked && !original && !redirect, "all-invisible-stages-before-unlock");
    target_unlocked = true; // Modeled successful unlock, while the sole writer still owns admission.
    const auto committed = r.commit(p);
    check(target_unlocked && committed == Error::Ok && owns(r.lookup(Island,modules),"alpha") &&
          owns(r.lookup(Tramp,modules),"alpha") && !original && !redirect,
          "metadata-visible-before-original");
    original = true; // Modeled publication checkpoints, not executable pointers.
    const bool original_checkpoint = owns(r.lookup(Tramp,modules),"alpha") && !redirect;
    redirect = true;
    check(original && redirect && original_checkpoint && owns(r.lookup(Island,modules),"alpha"),
          "original-precedes-redirect");
    check(r.commit(p) == Error::InvalidPermit && r.cancel(t) == Error::Published &&
          owns(r.lookup(Island,modules),"alpha"), "visibility-is-irreversible");
}

void postpublication_warnings() {
    const char* names[] = {"island-relock-warning","target-relock-warning","both-relock-warnings"};
    for (unsigned i=0;i<3;++i) {
        Registry r(Process,70+i);
        ModuleSnapshot modules;
        Ticket t;
        const bool made = publish(r,community(),Target,emission(),modules,&t);
        const bool modeled_island_warning = i != 1, modeled_target_warning = i != 0;
        check(made && (modeled_island_warning || modeled_target_warning) &&
              r.cancel(t) == Error::Published && owns(r.lookup(Island,modules),"alpha") &&
              owns(r.lookup(Tramp,modules),"alpha"), names[i]);
    }
    Registry r(Process,74);
    ModuleSnapshot modules;
    Ticket t;
    PublishPermit p;
    const bool made = r.reserve(community(),Target,t) == Error::Ok &&
        r.prepare(t,emission(),modules) == Error::Ok &&
        r.validate_to_publish(t,modules,p) == Error::Ok && r.commit(p) == Error::Ok;
    // An authored abort before modeled original publication cannot retract visible metadata.
    check(made && r.abort(p) == Error::InvalidPermit && r.cancel(t) == Error::Published &&
          owns(r.lookup(Island,modules),"alpha"), "postvisibility-abort-retains-record");
}

LookupResult nested_inner(const Registry& r, const ModuleSnapshot& m, bool& ok) {
    auto inner = r.lookup(Island+0x100,m);
    ok = owns(inner,"beta") && owns(r.lookup(Island,m),"alpha");
    return inner;
}
void nested_read() {
    Registry r(Process,80);
    ModuleSnapshot modules;
    const bool made = publish(r,community(),Target,emission(),modules) &&
        publish(r,community("beta"),Target+16,emission(Island+0x100,Tramp+0x100),modules);
    const auto outer = r.lookup(Island,modules);
    bool inner_ok = false;
    auto inner = nested_inner(r,modules,inner_ok);
    // Nested harness calls with an outer result live; not interruption inside lookup.
    check(made && inner_ok && owns(outer,"alpha") && owns(inner,"beta"),
          "nested-fixture-calls-preserve-outer-copy");
    inner.owner.id[0] = 'X';
    check(owns(outer,"alpha") && owns(r.lookup(Island,modules),"alpha") &&
          owns(r.lookup(Island+0x100,modules),"beta"), "lookup-results-do-not-share-mutable-identity");
}

void held_writer_reader() {
    Registry r(Process,81);
    const ModuleSnapshot modules;
    std::mutex writer_mutex, gate_mutex;
    std::condition_variable cv;
    bool prepared=false, release=false, read_done=false, reader_missed=false;
    bool writer_ok=false, reader_before_release=false, infrastructure_ok=true;
    std::thread writer, reader;
    try {
        writer = std::thread([&] {
            try {
                std::unique_lock<std::mutex> sole_writer(writer_mutex);
                Ticket t;
                PublishPermit p;
                const bool ready = r.reserve(community(),Target,t) == Error::Ok &&
                    r.prepare(t,emission(),modules) == Error::Ok &&
                    r.validate_to_publish(t,modules,p) == Error::Ok;
                std::unique_lock<std::mutex> gate(gate_mutex);
                prepared = true;
                cv.notify_all();
                const bool released = cv.wait_for(gate,std::chrono::seconds(15),[&]{return release;});
                gate.unlock();
                writer_ok = ready && released && r.commit(p) == Error::Ok;
            } catch (...) {
                std::lock_guard<std::mutex> gate(gate_mutex);
                infrastructure_ok = false;
                prepared = true;
                cv.notify_all();
            }
        });
        std::unique_lock<std::mutex> gate(gate_mutex);
        const bool started = cv.wait_for(gate,std::chrono::seconds(5),[&]{return prepared;});
        gate.unlock();
        if (started) {
            reader = std::thread([&] {
                const bool misses = no_match(r.lookup(Island,modules)) && no_match(r.lookup(Tramp,modules));
                std::lock_guard<std::mutex> lock(gate_mutex);
                reader_missed = misses;
                read_done = true;
                cv.notify_all();
            });
            gate.lock();
            reader_before_release = cv.wait_for(gate,std::chrono::seconds(5),[&]{return read_done;});
            gate.unlock();
        }
    } catch (...) {
        std::lock_guard<std::mutex> gate(gate_mutex);
        infrastructure_ok = false;
    }
    // Always release and join both ordinary threads before any semantic oracle.
    { std::lock_guard<std::mutex> gate(gate_mutex); release = true; cv.notify_all(); }
    if (writer.joinable()) writer.join();
    if (reader.joinable()) reader.join();
    check(infrastructure_ok && reader_before_release && reader_missed,
          "reader-completes-while-writer-held");
    check(infrastructure_ok && writer_ok && owns(r.lookup(Island,modules),"alpha") &&
          owns(r.lookup(Tramp,modules),"alpha"), "release-then-commit-visible-after-joins");
}

// Authored scalar frame table, never native addresses. This is a NEW modeled
// attribution composition, not execution of frozen cm_blame_fault or a signal.
struct Cell { std::uintptr_t address, value; };
struct Frames {
    std::vector<Cell> cells;
    std::vector<std::uintptr_t> reads;
    bool read(std::uintptr_t address, std::uintptr_t& out) {
        reads.push_back(address);
        for (const auto& c : cells) if (c.address == address) { out=c.value; return true; }
        return false;
    }
};
LookupResult choose(const Registry& r, const ModuleSnapshot& modules, std::uintptr_t pc,
                    std::uintptr_t lr, std::uintptr_t fp, Frames& frames) {
    auto result = r.lookup(pc,modules);
    if (result.error != Error::Ok || result.kind == MatchKind::Community) return result;
    result = r.lookup(lr,modules);
    if (result.error != Error::Ok || result.kind == MatchKind::Community) return result;
    for (unsigned i=0;i<16 && fp>=0x10000 && (fp&7U)==0;++i) {
        std::uintptr_t next=0, ra=0;
        if (!frames.read(fp,next) || !frames.read(fp+8,ra)) break;
        result = r.lookup(ra,modules);
        if (result.error != Error::Ok || result.kind == MatchKind::Community) return result;
        if (next<=fp) break;
        fp=next;
    }
    return {};
}
void lookup_precedence() {
    Registry r(Process,90);
    ModuleSnapshot empty, modules;
    const bool made = publish(r,community(),Target,emission(),empty) &&
                      module(modules,0x700000,0x701000,"module");
    Frames frames{{{Frame,0},{Frame+8,0x700000}}, {}};
    const auto chosen = choose(r,modules,Island,0x700000,Frame,frames);
    check(made && owns(chosen,"alpha") && frames.reads.empty(), "generated-pc-precedes-module-lr");
    const ModuleInput overlaps[] = {{Island-16,Island+64,community("first")},
                                    {Island,Island+128,community("second")}};
    const bool copied = modules.assign(overlaps,2) == Error::Ok;
    check(copied && owns(r.lookup(Island,modules),"first",MatchSource::Module),
          "first-module-envelope-beats-generated");
    check(owns(r.lookup(Island+32,modules),"first",MatchSource::Module),
          "module-envelope-hole-policy-unchanged");
}
void core_fallback() {
    Registry r(Process,91);
    ModuleSnapshot empty, modules;
    const bool made = publish(r,core(),Target,emission(),empty) &&
        publish(r,community("beta"),Target+16,emission(Island+0x100,Tramp+0x100),empty) &&
        module(modules,0x700000,0x701000);
    Frames unused;
    check(made && is_core(r.lookup(Island,modules)) &&
          owns(choose(r,modules,Island,0x700000,0,unused),"module",MatchSource::Module),
          "core-pc-continues-to-community-lr");
    Frames hit{{{Frame,0},{Frame+8,Island+0x100}}, {}};
    check(owns(choose(r,modules,Island,Tramp,Frame,hit),"beta") &&
          hit.reads == std::vector<std::uintptr_t>({Frame,Frame+8}),
          "core-pc-and-lr-continue-to-frame");
    Frames only_core{{{Frame,0},{Frame+8,Tramp}}, {}};
    check(no_match(choose(r,empty,Island,Tramp,Frame,only_core)), "core-never-becomes-community-blame");
}
void later_module_masking() {
    Registry r(Process,92);
    ModuleSnapshot empty, conflict, literal;
    Ticket t;
    PublishPermit p;
    const bool reserved = r.reserve(community(),Target,t) == Error::Ok;
    const bool copied = module(conflict,Island,Island+128) && module(literal,Tramp+12,Tramp+20,"literal");
    const auto first = r.prepare(t,emission(),conflict);
    const auto literal_conflict = r.prepare(t,emission(),literal);
    const auto prepared = r.prepare(t,emission(),empty);
    const auto changed = r.validate_to_publish(t,conflict,p);
    check(reserved && copied && first == Error::ModuleConflict && literal_conflict == Error::ModuleConflict &&
          prepared == Error::Ok && changed == Error::ModuleConflict && !p.valid() &&
          no_match(r.lookup(Island,empty)), "current-and-final-admission-module-conflict");
    const bool made = r.validate_to_publish(t,empty,p) == Error::Ok && r.commit(p) == Error::Ok;
    check(made && owns(r.lookup(Island,empty),"alpha") &&
          owns(r.lookup(Island,conflict),"module",MatchSource::Module) &&
          owns(r.lookup(Tramp+12,literal),"literal",MatchSource::Module),
          "later-envelope-masks-without-clearing-record");
    check(owns(r.lookup(Tramp,conflict),"alpha") && r.cancel(t) == Error::Published &&
          no_match(r.lookup(Tramp+12,empty)), "masking-does-not-reclaim-generated-owner");
}
Frames chain(unsigned hit) {
    Frames frames;
    for (unsigned i=0;i<17;++i) {
        const auto fp = Frame+16*i;
        frames.cells.push_back({fp,i==16?0:fp+16});
        frames.cells.push_back({fp+8,i==hit?Island:Native});
    }
    return frames;
}
void frame_composition() {
    Registry r(Process,93);
    ModuleSnapshot modules;
    const bool made = publish(r,community(),Target,emission(),modules);
    auto sixteenth = chain(15);
    const auto at16 = choose(r,modules,Native,Native,Frame,sixteenth);
    std::vector<std::uintptr_t> expected;
    for (unsigned i=0;i<16;++i) { expected.push_back(Frame+16*i); expected.push_back(Frame+16*i+8); }
    check(made && owns(at16,"alpha") && sixteenth.reads == expected,
          "generated-sixteenth-frame-is-eligible");
    auto seventeenth = chain(16);
    check(no_match(choose(r,modules,Native,Native,Frame,seventeenth)) && seventeenth.reads == expected,
          "generated-seventeenth-frame-is-not-read");
    Frames truncated{{{Frame,Frame+16},{Frame+16,0},{Frame+24,Island}}, {}};
    check(no_match(choose(r,modules,Native,Native,Frame,truncated)) &&
          truncated.reads == std::vector<std::uintptr_t>({Frame,Frame+8}),
          "failed-read-truncates-before-generated-later-frame");
    Frames backward{{{Frame,Frame},{Frame+8,Island}}, {}};
    check(owns(choose(r,modules,Native,Native,Frame,backward),"alpha") &&
          backward.reads == std::vector<std::uintptr_t>({Frame,Frame+8}),
          "generated-ra-checked-before-next-frame-order");
}

struct Selector { const char* name; void (*run)(); };
const Selector selectors[] = {
    {"emitted-variants",emitted_variants}, {"interval-boundaries",interval_boundaries},
    {"invalid-inputs",invalid_inputs}, {"reservation-conflicts",reservation_conflicts},
    {"capacity",capacity}, {"owner-custody",owner_custody},
    {"cancel-stale-ticket",cancel_stale_ticket}, {"modeled-prepublication-refusals",modeled_prepublication_refusals},
    {"publication-order",publication_order}, {"postpublication-warnings",postpublication_warnings},
    {"nested-read",nested_read}, {"held-writer-reader",held_writer_reader},
    {"lookup-precedence",lookup_precedence}, {"core-fallback",core_fallback},
    {"later-module-masking",later_module_masking}, {"frame-composition",frame_composition}
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fputs("one selector required\n",stderr); return 2; }
    const Selector* selected = nullptr;
    for (const auto& selector : selectors)
        if (std::strcmp(selector.name,argv[1]) == 0) selected = &selector;
    if (!selected) { std::fputs("unknown selector\n",stderr); return 2; }
    try { selected->run(); } catch (...) { check(false,"fixture-exception"); }
    std::printf("{\"selector\":\"%s\",\"checks\":%u,\"failures\":%zu,\"failed_checks\":[",
                selected->name,checks,failed.size());
    for (std::size_t i=0;i<failed.size();++i) std::printf("%s\"%s\"",i?",":"",failed[i]);
    std::puts("]}");
    return failed.empty()?0:1;
}
