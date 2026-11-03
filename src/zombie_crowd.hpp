#pragma once

#include "zombie_doorway.hpp"
#include <glm/glm.hpp>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace viewer {
class CollisionWorld;

struct ZombieCrowdStats {
    std::size_t moves = 0, neighborChecks = 0, budgetStops = 0, emptySectorSkips = 0;
    std::size_t steeringChecks = 0;
};

// Simulation-thread upright body cylinders, in world X/Z meters, with feet Y.
// Independent of perception, terrain support, animation and rigid-body physics.
// Call constrain in stable ID order, then update with the committed feet. Idle
// and inactive living bodies remain indexed; corpses do not participate.
class ZombieCrowd {
public:
    static constexpr float bodyRadius = 0.30f;
    static constexpr float bodyHeight = 1.8f;
    static constexpr std::size_t neighborBudget = 64;

    ZombieCrowd() = default;
    ZombieCrowd(const ZombieCrowd& other);
    ZombieCrowd& operator=(const ZombieCrowd& other);
    ZombieCrowd(ZombieCrowd&&) noexcept = default;
    ZombieCrowd& operator=(ZombieCrowd&&) noexcept = default;

    void reset(std::size_t population);
    void update(std::size_t id, glm::vec3 feet);
    void erase(std::size_t id);
    void beginStep(float seconds = 0) { stats_ = {}; doorways_.beginStep(seconds); }
    const ZombieCrowdStats& stats() const { return stats_; }
    // Preferred unit X/Z direction for direct pursuit. targetOffset is in
    // meters from the indexed feet. Uses only local body positions, with a
    // stable ID-based passing side; no visibility or scene queries. Collision
    // must still validate the result. Steering has its own neighborBudget.
    glm::vec2 chaseDirection(std::size_t id, glm::vec2 targetOffset);
    // A transient approach/exit/yield point for an open doorway. The caller
    // retains its original target and validates movement through collisions.
    std::optional<glm::vec3> doorwayDestination(std::size_t id, glm::vec3 target,
                                               const CollisionWorld& world);
    // Sweep toward desired feet, stopping/sliding at other bodies. No mutation:
    // the caller can apply terrain support or future scene collision before
    // publishing the accepted position. Existing overlaps may escape outward.
    glm::vec3 constrain(std::size_t id, glm::vec3 desired);
    // Multiple short segments of one movement share the same neighbor budget.
    // The indexed body stays at its published position until the caller commits.
    glm::vec3 constrainFrom(std::size_t id, glm::vec3 start, glm::vec3 desired,
                            std::size_t& checks);

private:
    void eraseBody(std::size_t id); // Grid relocation must retain passage requests.
    static constexpr double cellSize = 1.0, sectorCells = 8.0;
    struct Cell {
        double x = 0, z = 0;
        bool operator==(const Cell& other) const { return x == other.x && z == other.z; }
    };
    struct Hash { std::size_t operator()(const Cell& cell) const; };
    struct Body {
        glm::vec3 feet{0};
        Cell cell, sector;
        std::size_t* sectorPopulation = nullptr;
        std::size_t slot = 0;
        bool present = false;
    };
    std::unordered_map<Cell, std::vector<std::size_t>, Hash> cells_;
    std::vector<Body> bodies_;
    // Coarse counts certify empty fine-grid neighborhoods. Nodes stay valid
    // through map rehash; a live body owns one count in its referenced node.
    std::unordered_map<Cell, std::size_t, Hash> sectors_;
    ZombieCrowdStats stats_;
    ZombieDoorwayGuidance doorways_;
};

} // namespace viewer
