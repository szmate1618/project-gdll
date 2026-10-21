#include "zombie_navmesh_paths.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace viewer::navmesh_path {
namespace {
constexpr std::uint32_t missing = std::numeric_limits<std::uint32_t>::max();
std::uint64_t hashId(std::uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
}

struct PathManager::Impl {
    struct Slot { PathId id = 0; ZombieId zombie = 0; std::uint32_t offset = 0, count = 0; };
    struct Mapping { ZombieId zombie = 0; std::uint32_t slot = missing; std::uint8_t state = 0; };
    struct Span { std::uint32_t offset = 0, count = 0; };
    static_assert(sizeof(Point) == 16, "The arena must contain exactly 10 MB of waypoints");
    static constexpr std::uint32_t pointCapacity = bufferBytes / sizeof(Point);

    Search search;
    std::uint32_t maxPaths, hashCapacity = 1, freeSlotCount = 0, spanCount = 0;
    std::uint64_t sequence = 0;
    std::size_t used = 0;
    Status status = Status::noPath;
    Stats stats{};
    std::unique_ptr<Point[]> arena;
    std::unique_ptr<Slot[]> slots;
    std::unique_ptr<Mapping[]> mapping;
    std::unique_ptr<std::uint32_t[]> freeSlots;
    std::unique_ptr<Span[]> spans;

    Impl(const Mesh& mesh, std::uint32_t capacity, std::uint32_t nodeLimit, double heuristicWeight,
         bool reopenClosed, bool greedy)
        : search(mesh, nodeLimit, heuristicWeight, reopenClosed, greedy), maxPaths(capacity) {
        if (!capacity || capacity > 1'000'000)
            throw std::invalid_argument("Path capacity must be in [1, 1000000]");
        while (hashCapacity < capacity * 2) hashCapacity *= 2;
        arena = std::make_unique<Point[]>(pointCapacity);
        slots = std::make_unique<Slot[]>(capacity);
        mapping = std::make_unique<Mapping[]>(hashCapacity);
        freeSlots = std::make_unique<std::uint32_t[]>(capacity);
        // A replacement is allocated before the old path is freed. Inserting
        // that old span can temporarily need one extra slot before coalescing.
        spans = std::make_unique<Span[]>(capacity + 2);
        reset();
    }

    void reset() {
        std::fill_n(slots.get(), maxPaths, Slot{});
        std::fill_n(mapping.get(), hashCapacity, Mapping{});
        freeSlotCount = maxPaths;
        for (std::uint32_t i = 0; i < maxPaths; ++i) freeSlots[i] = maxPaths - i - 1;
        spanCount = 1;
        spans[0] = {0, pointCapacity};
        used = 0;
        status = Status::noPath;
        stats = {};
        // Sequence is deliberately retained: a released handle never aliases a
        // later path, including after clear().
    }

    std::uint32_t lookup(ZombieId zombie) const {
        auto position = static_cast<std::uint32_t>(hashId(zombie)) & (hashCapacity - 1);
        for (std::uint32_t probes = 0; probes < hashCapacity; ++probes) {
            const auto& entry = mapping[position];
            if (!entry.state) return missing;
            if (entry.state == 1 && entry.zombie == zombie) return position;
            position = (position + 1) & (hashCapacity - 1);
        }
        return missing;
    }

    void insert(ZombieId zombie, std::uint32_t slot) {
        auto position = static_cast<std::uint32_t>(hashId(zombie)) & (hashCapacity - 1);
        std::uint32_t tombstone = missing;
        for (std::uint32_t probes = 0; probes < hashCapacity; ++probes) {
            auto& entry = mapping[position];
            if (entry.state == 2 && tombstone == missing) tombstone = position;
            if (!entry.state) {
                mapping[tombstone == missing ? position : tombstone] = {zombie, slot, 1};
                return;
            }
            position = (position + 1) & (hashCapacity - 1);
        }
        if (tombstone == missing) throw std::logic_error("Preallocated path map is full");
        mapping[tombstone] = {zombie, slot, 1};
    }

    std::uint32_t suitableSpan(std::uint32_t count) const {
        for (std::uint32_t i = 0; i < spanCount; ++i)
            if (spans[i].count >= count) return i;
        return missing;
    }

    bool fitsAfterRelease(const Slot& slot, std::uint32_t count) const {
        std::uint32_t available = slot.count;
        for (std::uint32_t i = 0; i < spanCount; ++i) {
            if (spans[i].offset + spans[i].count == slot.offset ||
                slot.offset + slot.count == spans[i].offset)
                available += spans[i].count;
        }
        return available >= count;
    }

    std::uint32_t allocate(std::uint32_t index, std::uint32_t count) {
        auto& span = spans[index];
        const auto offset = span.offset;
        span.offset += count;
        span.count -= count;
        if (!span.count) {
            for (auto i = index; i + 1 < spanCount; ++i) spans[i] = spans[i + 1];
            --spanCount;
        }
        used += static_cast<std::size_t>(count) * sizeof(Point);
        return offset;
    }

    void freeSpan(const Slot& slot) {
        std::uint32_t index = 0;
        while (index < spanCount && spans[index].offset < slot.offset) ++index;
        for (auto i = spanCount; i > index; --i) spans[i] = spans[i - 1];
        spans[index] = {slot.offset, slot.count};
        ++spanCount;
        if (index && spans[index - 1].offset + spans[index - 1].count == spans[index].offset) {
            spans[index - 1].count += spans[index].count;
            for (auto i = index; i + 1 < spanCount; ++i) spans[i] = spans[i + 1];
            --spanCount;
            --index;
        }
        if (index + 1 < spanCount && spans[index].offset + spans[index].count == spans[index + 1].offset) {
            spans[index].count += spans[index + 1].count;
            for (auto i = index + 1; i + 1 < spanCount; ++i) spans[i] = spans[i + 1];
            --spanCount;
        }
        used -= static_cast<std::size_t>(slot.count) * sizeof(Point);
    }

    std::uint32_t pathSlot(PathId id) const {
        if (!id) return missing;
        auto slot = static_cast<std::uint32_t>((id - 1) % maxPaths);
        return slots[slot].id == id ? slot : missing;
    }
};

PathManager::PathManager(const Mesh& mesh, std::uint32_t maxPaths, std::uint32_t nodeLimit,
                         double heuristicWeight, bool reopenClosed, bool greedy)
    : impl_(std::make_unique<Impl>(mesh, maxPaths, nodeLimit, heuristicWeight, reopenClosed, greedy)) {}
PathManager::~PathManager() = default;

PathId PathManager::request(ZombieId zombie, Point start, Point goal) {
    auto& state = *impl_;
    state.stats = {};
    const auto entry = state.lookup(zombie);
    const auto oldSlot = entry == missing ? missing : state.mapping[entry].slot;
    if (oldSlot == missing && !state.freeSlotCount) {
        state.status = Status::bufferFull;
        return invalidPath;
    }
    const auto result = state.search.find(start, goal);
    state.status = result.status;
    state.stats = result.stats;
    if (result.status != Status::found) return invalidPath;
    if (!result.size || result.size > Impl::pointCapacity ||
        state.sequence >= (std::numeric_limits<PathId>::max() - state.maxPaths) / state.maxPaths) {
        state.status = Status::bufferFull;
        return invalidPath;
    }
    const auto count = static_cast<std::uint32_t>(result.size);
    auto span = state.suitableSpan(count);
    bool oldReleased = false;
    if (span == missing) {
        if (oldSlot == missing || !state.fitsAfterRelease(state.slots[oldSlot], count)) {
            state.status = Status::bufferFull;
            return invalidPath;
        }
        state.freeSpan(state.slots[oldSlot]);
        oldReleased = true;
        span = state.suitableSpan(count);
    }
    const auto offset = state.allocate(span, count);
    std::copy_n(result.points, count, state.arena.get() + offset);
    const auto slot = oldSlot == missing ? state.freeSlots[--state.freeSlotCount] : oldSlot;
    if (oldSlot != missing && !oldReleased) state.freeSpan(state.slots[oldSlot]);
    const PathId id = (++state.sequence) * state.maxPaths + slot + 1;
    state.slots[slot] = {id, zombie, offset, count};
    if (entry == missing) state.insert(zombie, slot);
    return id;
}

PathId PathManager::pathFor(ZombieId zombie) const {
    const auto entry = impl_->lookup(zombie);
    return entry == missing ? invalidPath : impl_->slots[impl_->mapping[entry].slot].id;
}

PathView PathManager::view(PathId id) const {
    const auto slot = impl_->pathSlot(id);
    if (slot == missing) return {};
    const auto& path = impl_->slots[slot];
    return {impl_->arena.get() + path.offset, path.count};
}

bool PathManager::release(PathId id) {
    auto& state = *impl_;
    const auto slot = state.pathSlot(id);
    if (slot == missing) return false;
    const auto entry = state.lookup(state.slots[slot].zombie);
    state.mapping[entry].state = 2;
    state.freeSpan(state.slots[slot]);
    state.slots[slot] = {};
    state.freeSlots[state.freeSlotCount++] = slot;
    return true;
}
bool PathManager::releaseZombie(ZombieId zombie) { return release(pathFor(zombie)); }
void PathManager::clear() { impl_->reset(); }
Status PathManager::lastStatus() const { return impl_->status; }
const Stats& PathManager::lastStats() const { return impl_->stats; }
std::size_t PathManager::usedBytes() const { return impl_->used; }
std::size_t PathManager::workspaceBytes() const { return impl_->search.workspaceBytes(); }
std::size_t PathManager::metadataBytes() const {
    return sizeof(Impl) + impl_->maxPaths * (sizeof(Impl::Slot) + sizeof(std::uint32_t)) +
           impl_->hashCapacity * sizeof(Impl::Mapping) + (impl_->maxPaths + 2) * sizeof(Impl::Span);
}
std::uint32_t PathManager::nodeLimit() const { return impl_->search.nodeLimit(); }
double PathManager::heuristicWeight() const { return impl_->search.heuristicWeight(); }
bool PathManager::reopenClosed() const { return impl_->search.reopenClosed(); }
bool PathManager::greedy() const { return impl_->search.greedy(); }

} // namespace viewer::navmesh_path
