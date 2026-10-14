#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace viewer {

// Single-threaded, deduplicated changes between simulation and render consumers.
// A consumer that misses an acknowledged batch receives a full refresh instead.
class ZombieRenderUpdates {
public:
    struct View {
        std::uint64_t revision;
        bool full;
        const std::vector<std::size_t>* ids;
    };
    ZombieRenderUpdates() = default;
    ZombieRenderUpdates(ZombieRenderUpdates&&) noexcept = default;
    ZombieRenderUpdates& operator=(ZombieRenderUpdates&& other) noexcept;
    void mark(std::size_t id, bool bounds);
    void invalidate();
    View since(std::uint64_t revision) const;
    bool boundsChanged(std::size_t id) const { return (flags_.at(id) & 2) != 0; }
    // Call only after successful processing. A newer publication is retained.
    void acknowledge(std::uint64_t revision) const;

private:
    void clear() const;
    std::uint64_t revision_ = 1;
    mutable std::uint64_t acknowledged_ = 0;
    mutable bool full_ = true;
    mutable std::vector<std::size_t> ids_;
    mutable std::vector<std::uint8_t> flags_;
};

} // namespace viewer
