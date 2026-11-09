#ifndef AZ_GENERATED_RANGE_FIXTURE_REGISTRY_H
#define AZ_GENERATED_RANGE_FIXTURE_REGISTRY_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace az_generated {

// OFFLINE synthetic metadata only: no SDK admission, mappings, signal handler,
// code publication, resource custody, or authentication is implemented here.
// Exactly one externally serialized writer may call mutation APIs. Readers may
// overlap that writer, but the Registry must outlive all readers and tickets.
// process_start and instance_id are trusted synthetic identities, nonzero and
// unique across reconstruction; this class cannot establish that uniqueness.
constexpr std::size_t kCapacity = 8;  // Fixture bound, deliberately <= 512.
constexpr std::size_t kMaxModules = 16;
constexpr std::size_t kMaxId = 32;

enum class Error {
    Ok, InvalidRegistry, InvalidOwner, InvalidTarget, InvalidEmission,
    EndpointOverflow, InvalidModules, ModuleConflict, RangeConflict,
    TargetConflict, CapacityExhausted, GenerationExhausted, InvalidTicket,
    WrongPhase, Published, PermitInUse, InvalidPermit
};
enum class OwnerKind { Core, Community };
enum class MatchKind { NoMatch, Core, Community };
enum class MatchSource { None, Module, Generated };

struct OwnerInput {
    OwnerKind kind = OwnerKind::Core;
    std::uint64_t process_start = 0;
    const char* id = nullptr;
    std::size_t id_length = 0;
};
struct Owner {
    OwnerKind kind = OwnerKind::Core;
    std::uint64_t process_start = 0;
    std::array<char, kMaxId + 1> id{};
    std::size_t id_length = 0;
};
struct Range {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
};
struct EmissionInput {
    std::uintptr_t island = 0;
    std::uintptr_t trampoline = 0;
    unsigned ncopy = 0;
    unsigned expanded_adrps = 0;
};
struct EmissionLayout {
    Range island_instructions{};
    Range island_literal{};
    Range trampoline_instructions{};
    Range trampoline_literal{};
    unsigned ncopy = 0;
    unsigned expanded_adrps = 0;
};

// Computes source-derived lengths only; does not decode instructions or prove
// that the supplied addresses are allocated, mapped, executable or reachable.
// Both bases are instruction aligned. ncopy=1 requires an island; ncopy=4
// requires island=0. expanded_adrps must be <= ncopy. The two full emitted
// blobs (instructions plus eight-byte literals) must be disjoint. Outputs change
// on success. This checks emitted bytes, not the enclosing pool/page extents.
Error compute_layout(const EmissionInput&, EmissionLayout&) noexcept;

struct ModuleInput {
    std::uintptr_t lo = 0;
    std::uintptr_t hi = 0;
    OwnerInput owner{};
};

class Registry;
class ModuleSnapshot {
public:
    ModuleSnapshot() noexcept = default;
    // Copies only accessible caller-owned inputs. Community IDs are 1..32
    // non-NUL ASCII identifier bytes [A-Za-z0-9_.-]; this is shape validation,
    // not an SDK identity/approval check. All entries have one process identity.
    // Overlapping envelopes retain their input order, including envelope holes.
    // Failed assign leaves the previous snapshot unchanged.
    Error assign(const ModuleInput*, std::size_t count) noexcept;
    std::size_t size() const noexcept { return count_; }
private:
    struct Module { Range range{}; Owner owner{}; };
    std::array<Module, kMaxModules> modules_{};
    std::size_t count_ = 0;
    std::uint64_t process_start_ = 0;
    friend class Registry;
    // Snapshot copying/mutation and its lifetime require external coordination:
    // never assign to or destroy a snapshot while any operation reads it.
};

class Ticket {
public:
    Ticket() noexcept = default;
    std::size_t slot() const noexcept { return slot_; }
    std::uint64_t generation() const noexcept { return generation_; }
private:
    const Registry* registry_ = nullptr;
    std::uint64_t instance_ = 0;
    std::uint64_t generation_ = 0;
    std::size_t slot_ = kCapacity;
    friend class Registry;
};

// No destructor action. Dropping a live permit retains an invisible reservation.
// Move construction transfers the permit; copying and assignment are forbidden.
class PublishPermit {
public:
    PublishPermit() noexcept = default;
    PublishPermit(PublishPermit&& other) noexcept;
    PublishPermit(const PublishPermit&) = delete;
    PublishPermit& operator=(const PublishPermit&) = delete;
    PublishPermit& operator=(PublishPermit&&) = delete;
    bool valid() const noexcept { return live_; }
private:
    Ticket ticket_{};
    bool live_ = false;
    friend class Registry;
};

struct LookupResult {
    Error error = Error::Ok;
    MatchKind kind = MatchKind::NoMatch;
    MatchSource source = MatchSource::None;
    Owner owner{};
    std::uintptr_t target = 0;  // Zero for module matches.
    EmissionLayout layout{};   // Zero for module matches.
};

class Registry {
public:
    Registry(std::uint64_t process_start, std::uint64_t instance_id) noexcept;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    // Reserve fixed metadata capacity AND [target,target+16) ownership together.
    // Refuse overlap with retained targets or any already prepared/authorized/
    // published emitted blob, including its literal. Failed calls leave output
    // tickets and all retained reservations unchanged.
    Error reserve(const OwnerInput&, std::uintptr_t target, Ticket&) noexcept;
    // Storage admission rejects overlap of the FULL emitted blobs (instructions
    // plus literals) with other prepared/authorized/published blobs, known module
    // envelopes, or any retained target16 reservation, including its own. A
    // successful prepare is one-way until cancel; failed prepare retains the
    // original reservation, allowing an explicit cancellation before visibility.
    // This is synthetic byte-range separation, not physical allocation custody.
    Error prepare(const Ticket&, const EmissionInput&, const ModuleSnapshot&) noexcept;
    // This is the final fallible range/module validation, BEFORE modeled target
    // unlock. It creates a permit and freezes its exact prepared payload.
    Error validate_to_publish(const Ticket&, const ModuleSnapshot&, PublishPermit&) noexcept;
    // For a valid permit, under uninterrupted exclusive writer ownership and
    // unchanged admission inputs, commit is bounded and cannot fail. Defensive
    // identity/phase errors are misuse detection, not a post-unlock failure
    // branch. No module/pointer/provider validation, allocation or lock occurs.
    // The release store is irreversible; no subsequent API clears it.
    Error commit(PublishPermit&) noexcept;
    // Modeled target-unlock failure may abort before any visibility. Neither
    // abort nor cancel performs actual cleanup/unmap. No published cancellation.
    Error abort(PublishPermit&) noexcept;
    Error cancel(const Ticket&) noexcept;

    // Per-address legacy first-module precedence, then generated instruction
    // ranges only. Core is typed, not a community ID: a composing PC/LR/frame
    // walk must continue on Core. Such a walk belongs to the separate harness.
    // Exactly one acquire load per examined slot; no waits, callbacks, reader
    // writes or retry loops. A late immutable module snapshot may mask a record
    // or match its literal bytes: literal exclusion applies to generated-record
    // lookup, not to a global NoMatch promise for that address.
    LookupResult lookup(std::uintptr_t address, const ModuleSnapshot&) const noexcept;

private:
    enum class Phase { Empty, Reserved, Prepared, Authorized, Published };
    struct Payload {
        Owner owner{};
        Range target{};
        EmissionLayout layout{};
    };
    struct Slot {
        std::atomic<std::uint32_t> committed{0};
        // The following administrative fields are writer-only. Readers must
        // not touch them, including during invisible cancellation/reuse.
        Phase phase = Phase::Empty;
        std::uint64_t generation = 0;
        Payload payload{};
    };
    std::array<Slot, kCapacity> slots_{};
    const std::uint64_t process_start_;
    const std::uint64_t instance_id_;
    bool configured() const noexcept;
    Error ticket_slot(const Ticket&, Slot*&) noexcept;
    Error validate_ranges(std::size_t slot, const EmissionLayout&,
                          const ModuleSnapshot&) const noexcept;
    Error permit_slot(const PublishPermit&, Slot*&) noexcept;
};

static_assert(kCapacity <= 512, "fixture capacity exceeds reviewed inline ceiling");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "publication must not use a hidden lock");

} // namespace az_generated
#endif
