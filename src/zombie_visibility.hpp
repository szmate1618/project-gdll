#pragma once

#include "visibility.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace viewer {

struct ZombieVisibilityResult {
    std::size_t instance = 0;
    bool camera = false, shadow = false, flashlight = false;
};
struct ZombieVisibilityStats {
    std::size_t groups = 0, refittedGroups = 0;
    std::size_t blocks = 0, visitedGroups = 0, refittedBlocks = 0, refittedMembers = 0;
    std::size_t cameraBlockTests = 0, shadowBlockTests = 0, flashlightBlockTests = 0;
    std::size_t cameraGroupTests = 0, shadowGroupTests = 0;
    std::size_t flashlightGroupTests = 0, flashlightInstanceTests = 0;
    std::size_t cameraInstanceTests = 0, shadowInstanceTests = 0;
    // Fine groups rejected by the camera region at either level. Blocks rejected
    // by frusta are not descended merely to count additional region rejections.
    std::size_t cameraRegionRejectedGroups = 0;
};

// Render-thread grid in X/Z world meters. Membership uses full bound centers;
// each group encloses its members' entire boxes, including geometry outside the
// cell. A second level groups 8 x 8 cells into blocks enclosing full member
// bounds. Queries refit dirty groups/blocks before independently testing the
// camera, sunlight, and optional flashlight volumes.
class ZombieVisibility {
public:
    void reset(std::size_t population);
    bool contains(std::size_t instance) const;
    // Borrows stable, renderer-owned bounds until erase/reset. Call after any
    // change to that box, even if the instance remains in the same grid cell.
    void update(std::size_t instance, const VisibilityBounds& bounds);
    void erase(std::size_t instance);
    // The optional camera region rejects full bounds before frustum tests.
    // It never restricts either light, even when camera culling is disabled.
    // Results contain unique stable IDs and independent pass flags in ascending
    // ID order, regardless of spatial membership or traversal history.
    // Preserve this initial instance-array order for memory locality in the
    // renderer's source-state, staging and bounds reads, including within batches.
    ZombieVisibilityStats query(const Frustum& camera, const Frustum* light,
                                bool cameraCulling, std::vector<ZombieVisibilityResult>& visible,
                                const Frustum* flashlight = nullptr,
                                const VisibilityBounds* cameraRegion = nullptr);

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
        std::size_t block = noGroup, blockSlot = 0;
        bool dirty = false;
    };
    struct Block {
        Cell cell;
        VisibilityBounds bounds;
        std::vector<std::size_t> groups;
        bool dirty = false;
    };
    void markDirty(std::size_t group);
    void attachGroup(std::size_t group);
    void detachGroup(std::size_t group);
    void refit(ZombieVisibilityStats& stats);
    void orderVisible(std::vector<ZombieVisibilityResult>& visible);
    std::unordered_map<Cell, std::size_t, Hash> cells_;
    std::unordered_map<Cell, std::size_t, Hash> blocks_;
    std::vector<Location> locations_;
    std::vector<Group> groups_;
    std::vector<std::size_t> freeGroups_, dirtyGroups_;
    std::vector<Block> blockGroups_;
    std::vector<std::size_t> freeBlocks_, dirtyBlocks_;
    // Owned by this render-thread index and reused for radix ordering. Reset
    // releases its capacity when replacing the population.
    std::vector<ZombieVisibilityResult> sortScratch_;
};

} // namespace viewer
