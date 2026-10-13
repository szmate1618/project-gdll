#pragma once

#include "visibility.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace viewer {

struct ZombieVisibilityResult {
    std::size_t instance = 0;
    bool camera = false, shadow = false;
};
struct ZombieVisibilityStats {
    std::size_t groups = 0, refittedGroups = 0;
    std::size_t cameraGroupTests = 0, shadowGroupTests = 0;
    std::size_t cameraInstanceTests = 0, shadowInstanceTests = 0;
};

// Render-thread grid in X/Z world meters. Membership uses full bound centers;
// each group encloses its members' entire boxes, including geometry outside the
// cell. Queries refit dirty groups before independently testing camera/light.
class ZombieVisibility {
public:
    void reset(std::size_t population);
    bool contains(std::size_t instance) const;
    // Borrows stable, renderer-owned bounds until erase/reset. Call after any
    // change to that box, even if the instance remains in the same grid cell.
    void update(std::size_t instance, const VisibilityBounds& bounds);
    void erase(std::size_t instance);
    ZombieVisibilityStats query(const Frustum& camera, const Frustum* light,
                                bool cameraCulling, std::vector<ZombieVisibilityResult>& visible);

private:
    static constexpr std::size_t noGroup = static_cast<std::size_t>(-1);
    struct Cell {
        double x = 0, z = 0; // Integer-valued keys cover all finite float bounds.
        bool operator==(const Cell& other) const { return x == other.x && z == other.z; }
    };
    struct Hash { std::size_t operator()(const Cell& cell) const; };
    struct Location {
        const VisibilityBounds* bounds = nullptr;
        std::size_t group = noGroup, slot = 0;
    };
    struct Group {
        Cell cell;
        VisibilityBounds bounds;
        std::vector<std::size_t> members;
        bool dirty = false;
    };
    void markDirty(std::size_t group);
    std::size_t refit();
    std::unordered_map<Cell, std::size_t, Hash> cells_;
    std::vector<Location> locations_;
    std::vector<Group> groups_;
    std::vector<std::size_t> freeGroups_, dirtyGroups_;
};

} // namespace viewer
