#include "registry.h"

#include <limits>

namespace az_generated {
namespace {

bool present(const Range& range) noexcept {
    return range.begin < range.end;
}

bool contains(const Range& range, std::uintptr_t address) noexcept {
    return address >= range.begin && address < range.end;
}

bool overlaps(const Range& a, const Range& b) noexcept {
    return present(a) && present(b) && a.begin < b.end && b.begin < a.end;
}

bool endpoint(std::uintptr_t begin, std::uintptr_t length,
              std::uintptr_t& end) noexcept {
    if (length > std::numeric_limits<std::uintptr_t>::max() - begin)
        return false;
    end = begin + length;
    return true;
}

bool identifier_byte(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

Error copy_owner(const OwnerInput& input, Owner& output) noexcept {
    if (input.process_start == 0)
        return Error::InvalidOwner;
    Owner copied;
    copied.kind = input.kind;
    copied.process_start = input.process_start;
    switch (input.kind) {
    case OwnerKind::Core:
        if (input.id_length != 0)
            return Error::InvalidOwner;
        break;
    case OwnerKind::Community:
        if (input.id == nullptr || input.id_length == 0 ||
            input.id_length > kMaxId)
            return Error::InvalidOwner;
        for (std::size_t i = 0; i < input.id_length; ++i) {
            const auto c = static_cast<unsigned char>(input.id[i]);
            if (!identifier_byte(c))
                return Error::InvalidOwner;
            copied.id[i] = static_cast<char>(c);
        }
        copied.id_length = input.id_length;
        break;
    default:
        return Error::InvalidOwner;
    }
    output = copied;
    return Error::Ok;
}

Range island_blob(const EmissionLayout& layout) noexcept {
    return {layout.island_instructions.begin, layout.island_literal.end};
}

Range trampoline_blob(const EmissionLayout& layout) noexcept {
    return {layout.trampoline_instructions.begin, layout.trampoline_literal.end};
}

// Storage admission includes literals. Lookup below deliberately does not.
bool layout_overlap(const EmissionLayout& a, const EmissionLayout& b) noexcept {
    return overlaps(island_blob(a), island_blob(b)) ||
           overlaps(island_blob(a), trampoline_blob(b)) ||
           overlaps(trampoline_blob(a), island_blob(b)) ||
           overlaps(trampoline_blob(a), trampoline_blob(b));
}

bool blob_overlap_range(const EmissionLayout& layout, const Range& range) noexcept {
    return overlaps(island_blob(layout), range) ||
           overlaps(trampoline_blob(layout), range);
}

} // namespace

Error compute_layout(const EmissionInput& input, EmissionLayout& output) noexcept {
    if ((input.ncopy != 1 && input.ncopy != 4) ||
        input.expanded_adrps > input.ncopy ||
        input.trampoline == 0 || (input.trampoline & 3U) != 0 ||
        (input.ncopy == 1 && (input.island == 0 || (input.island & 3U) != 0)) ||
        (input.ncopy == 4 && input.island != 0))
        return Error::InvalidEmission;

    EmissionLayout layout;
    layout.ncopy = input.ncopy;
    layout.expanded_adrps = input.expanded_adrps;
    // Validated small operands: at most 4 + 3*4 = 16 relocated instructions.
    const std::uintptr_t relocated = input.ncopy + 3U * input.expanded_adrps;
    layout.trampoline_instructions.begin = input.trampoline;
    if (!endpoint(input.trampoline, 4U * (relocated + 2U),
                  layout.trampoline_instructions.end))
        return Error::EndpointOverflow;
    layout.trampoline_literal.begin = layout.trampoline_instructions.end;
    if (!endpoint(layout.trampoline_literal.begin, 8U, layout.trampoline_literal.end))
        return Error::EndpointOverflow;

    if (input.ncopy == 1) {
        layout.island_instructions.begin = input.island;
        if (!endpoint(input.island, 8U, layout.island_instructions.end))
            return Error::EndpointOverflow;
        layout.island_literal.begin = layout.island_instructions.end;
        if (!endpoint(layout.island_literal.begin, 8U, layout.island_literal.end))
            return Error::EndpointOverflow;
        if (overlaps(island_blob(layout), trampoline_blob(layout)))
            return Error::RangeConflict;
    }
    output = layout;
    return Error::Ok;
}

Error ModuleSnapshot::assign(const ModuleInput* inputs, std::size_t count) noexcept {
    if (count > kMaxModules || (count != 0 && inputs == nullptr))
        return Error::InvalidModules;
    ModuleSnapshot copied;
    for (std::size_t i = 0; i < count; ++i) {
        const ModuleInput& input = inputs[i];
        if (input.lo >= input.hi || input.owner.kind != OwnerKind::Community)
            return Error::InvalidModules;
        Owner owner;
        if (copy_owner(input.owner, owner) != Error::Ok)
            return Error::InvalidModules;
        if (i == 0)
            copied.process_start_ = owner.process_start;
        else if (owner.process_start != copied.process_start_)
            return Error::InvalidModules;
        copied.modules_[i].range = {input.lo, input.hi};
        copied.modules_[i].owner = owner;
    }
    copied.count_ = count;
    *this = copied;
    return Error::Ok;
}

PublishPermit::PublishPermit(PublishPermit&& other) noexcept
    : ticket_(other.ticket_), live_(other.live_) {
    other.ticket_ = Ticket{};
    other.live_ = false;
}

Registry::Registry(std::uint64_t process_start, std::uint64_t instance_id) noexcept
    : process_start_(process_start), instance_id_(instance_id) {}

bool Registry::configured() const noexcept {
    return process_start_ != 0 && instance_id_ != 0;
}

Error Registry::ticket_slot(const Ticket& ticket, Slot*& output) noexcept {
    if (!configured())
        return Error::InvalidRegistry;
    if (ticket.registry_ != this || ticket.instance_ != instance_id_ ||
        ticket.slot_ >= kCapacity || ticket.generation_ == 0)
        return Error::InvalidTicket;
    Slot& slot = slots_[ticket.slot_];
    if (slot.generation != ticket.generation_)
        return Error::InvalidTicket;
    output = &slot;
    return Error::Ok;
}

Error Registry::reserve(const OwnerInput& input, std::uintptr_t target,
                        Ticket& output) noexcept {
    if (!configured())
        return Error::InvalidRegistry;
    Owner owner;
    Error error = copy_owner(input, owner);
    if (error != Error::Ok || owner.process_start != process_start_)
        return Error::InvalidOwner;
    if (target == 0 || (target & 3U) != 0)
        return Error::InvalidTarget;
    Range target_range{target, 0};
    if (!endpoint(target, 16U, target_range.end))
        return Error::EndpointOverflow;

    std::size_t available = kCapacity;
    bool exhausted_generation = false;
    for (std::size_t i = 0; i < kCapacity; ++i) {
        const Slot& slot = slots_[i];
        if (slot.phase != Phase::Empty) {
            if (overlaps(target_range, slot.payload.target))
                return Error::TargetConflict;
            if ((slot.phase == Phase::Prepared || slot.phase == Phase::Authorized ||
                 slot.phase == Phase::Published) &&
                blob_overlap_range(slot.payload.layout, target_range))
                return Error::TargetConflict;
        } else if (slot.generation == std::numeric_limits<std::uint64_t>::max()) {
            exhausted_generation = true;
        } else if (available == kCapacity) {
            available = i;
        }
    }
    if (available == kCapacity)
        return exhausted_generation ? Error::GenerationExhausted : Error::CapacityExhausted;

    Slot& slot = slots_[available];
    // Empty slots have never committed. Cancel/abort never clear a committed
    // flag, and their payload can be reused because readers skipped that flag.
    ++slot.generation;
    slot.payload = Payload{};
    slot.payload.owner = owner;
    slot.payload.target = target_range;
    slot.phase = Phase::Reserved;
    Ticket ticket;
    ticket.registry_ = this;
    ticket.instance_ = instance_id_;
    ticket.generation_ = slot.generation;
    ticket.slot_ = available;
    output = ticket;
    return Error::Ok;
}

Error Registry::validate_ranges(std::size_t index, const EmissionLayout& layout,
                                const ModuleSnapshot& modules) const noexcept {
    if (modules.count_ != 0 && modules.process_start_ != process_start_)
        return Error::InvalidModules;
    for (std::size_t i = 0; i < modules.count_; ++i)
        if (blob_overlap_range(layout, modules.modules_[i].range))
            return Error::ModuleConflict;
    for (std::size_t i = 0; i < kCapacity; ++i) {
        const Slot& other = slots_[i];
        // Include the current emission's target and invisible reservations.
        // This is symmetric with reserve's check against already known blobs.
        if (other.phase != Phase::Empty &&
            blob_overlap_range(layout, other.payload.target))
            return Error::TargetConflict;
        if (i == index)
            continue;
        if ((other.phase == Phase::Prepared || other.phase == Phase::Authorized ||
             other.phase == Phase::Published) &&
            layout_overlap(layout, other.payload.layout))
            return Error::RangeConflict;
    }
    return Error::Ok;
}

Error Registry::prepare(const Ticket& ticket, const EmissionInput& input,
                        const ModuleSnapshot& modules) noexcept {
    Slot* slot = nullptr;
    Error error = ticket_slot(ticket, slot);
    if (error != Error::Ok)
        return error;
    if (slot->phase == Phase::Published)
        return Error::Published;
    if (slot->phase != Phase::Reserved)
        return Error::WrongPhase;
    EmissionLayout layout;
    error = compute_layout(input, layout);
    if (error != Error::Ok)
        return error;
    error = validate_ranges(ticket.slot_, layout, modules);
    if (error != Error::Ok)
        return error;
    slot->payload.layout = layout;
    slot->phase = Phase::Prepared;
    return Error::Ok;
}

Error Registry::validate_to_publish(const Ticket& ticket, const ModuleSnapshot& modules,
                                    PublishPermit& output) noexcept {
    if (output.live_)
        return Error::PermitInUse;
    Slot* slot = nullptr;
    Error error = ticket_slot(ticket, slot);
    if (error != Error::Ok)
        return error;
    if (slot->phase == Phase::Published)
        return Error::Published;
    if (slot->phase != Phase::Prepared)
        return Error::WrongPhase;
    error = validate_ranges(ticket.slot_, slot->payload.layout, modules);
    if (error != Error::Ok)
        return error;
    slot->phase = Phase::Authorized;
    output.ticket_ = ticket;
    output.live_ = true;
    return Error::Ok;
}

Error Registry::permit_slot(const PublishPermit& permit, Slot*& output) noexcept {
    if (!permit.live_)
        return Error::InvalidPermit;
    Error error = ticket_slot(permit.ticket_, output);
    if (error != Error::Ok)
        return error;
    if (output->phase != Phase::Authorized)
        return Error::InvalidPermit;
    return Error::Ok;
}

Error Registry::commit(PublishPermit& permit) noexcept {
    Slot* slot = nullptr;
    Error error = permit_slot(permit, slot);
    if (error != Error::Ok)
        return error;
    // All payload writes and validation precede this final publication. The
    // administrative phase is never read by lookup and changes only here.
    slot->phase = Phase::Published;
    slot->committed.store(1U, std::memory_order_release);
    permit.ticket_ = Ticket{};
    permit.live_ = false;
    return Error::Ok;
}

Error Registry::abort(PublishPermit& permit) noexcept {
    Slot* slot = nullptr;
    Error error = permit_slot(permit, slot);
    if (error != Error::Ok)
        return error;
    slot->payload = Payload{};
    slot->phase = Phase::Empty;
    permit.ticket_ = Ticket{};
    permit.live_ = false;
    return Error::Ok;
}

Error Registry::cancel(const Ticket& ticket) noexcept {
    Slot* slot = nullptr;
    Error error = ticket_slot(ticket, slot);
    if (error != Error::Ok)
        return error;
    if (slot->phase == Phase::Published)
        return Error::Published;
    if (slot->phase != Phase::Reserved && slot->phase != Phase::Prepared)
        return Error::WrongPhase;
    slot->payload = Payload{};
    slot->phase = Phase::Empty;
    return Error::Ok;
}

LookupResult Registry::lookup(std::uintptr_t address,
                              const ModuleSnapshot& modules) const noexcept {
    LookupResult result;
    if (!configured()) {
        result.error = Error::InvalidRegistry;
        return result;
    }
    if (modules.count_ != 0 && modules.process_start_ != process_start_) {
        result.error = Error::InvalidModules;
        return result;
    }
    // Preserve the legacy first-index module-envelope winner at this address.
    for (std::size_t i = 0; i < modules.count_; ++i) {
        if (contains(modules.modules_[i].range, address)) {
            result.kind = MatchKind::Community;
            result.source = MatchSource::Module;
            result.owner = modules.modules_[i].owner;
            return result;
        }
    }
    for (std::size_t i = 0; i < kCapacity; ++i) {
        const Slot& slot = slots_[i];
        if (slot.committed.load(std::memory_order_acquire) == 0U)
            continue;
        // An acquire that sees committed permits these immutable payload reads.
        // No phase, generation, or uncommitted payload is read by the reader.
        const Payload& payload = slot.payload;
        if (!contains(payload.layout.island_instructions, address) &&
            !contains(payload.layout.trampoline_instructions, address))
            continue;
        result.kind = payload.owner.kind == OwnerKind::Core
                          ? MatchKind::Core : MatchKind::Community;
        result.source = MatchSource::Generated;
        result.owner = payload.owner;
        result.target = payload.target.begin;
        result.layout = payload.layout;
        return result;
    }
    return result;
}

} // namespace az_generated
