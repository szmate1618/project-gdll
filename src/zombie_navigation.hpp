#pragma once

#include "zombie_navmesh_search.hpp"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace viewer {
class CollisionWorld;

struct ZombieNavigationStats {
    std::uint64_t requests = 0, foundPaths = 0, failedPaths = 0;
    std::uint64_t steeringCalls = 0, obstacleTests = 0, velocitySamples = 0;
    std::uint64_t trunkOverflows = 0, meshRejectedCandidates = 0, blockedUpdates = 0;
    std::uint64_t movementValidationReuses = 0;
};

// Scenario route navigation. Construction allocates the mesh,
// one independent-search workspace, exactly10MB of path points and per-ID state.
// Configure only in standalone/server owners; replicated clients never enable it.
// All IDs are original ZombieLayer instance indices. The layer/population must
// outlive ordinary calls; reconfigure after replacing/resizing that population.
class ZombieNavigation {
public:
    explicit ZombieNavigation(navmesh_path::Mesh mesh, std::size_t population,
                              std::size_t trunkCapacity = 256);
    ZombieNavigation(const std::filesystem::path& meshPath, std::size_t population);
    ~ZombieNavigation();
    ZombieNavigation(const ZombieNavigation&) = delete;
    ZombieNavigation& operator=(const ZombieNavigation&) = delete;

    // Cold scenario setup only: calculate and retain the initial route now.
    // Returns false for invalid endpoints or a failed search.
    bool plan(std::size_t id, glm::vec3 feet, glm::vec3 goal);
    // Follow only the stored route and goal; never start or retry a search.
    // Invalidated routes and overflowing local trunk queries stop safely.
    glm::vec2 follow(std::size_t id, glm::vec3 feet,
                     float speed, float seconds, const CollisionWorld& world);
    // Supply actual guarded motion, including crowd/terrain constraints.
    void recordMovement(std::size_t id, glm::vec3 before, glm::vec3 after, float seconds);
    void reset(std::size_t id);
    // Cold population replacement only: reset all routes and size fixed ID/path
    // storage. Allocations occur here, never in ordinary follow calls.
    void resetPopulation(std::size_t population);
    bool segmentClear(glm::vec3 a, glm::vec3 b) const;
    // Reuse this ID's most recent follow validation only for an exact X/Z match.
    // Changed collision/crowd steps receive the full mesh coverage check.
    bool movementClear(std::size_t id, glm::vec3 a, glm::vec3 b) const;
    const ZombieNavigationStats& stats() const;
    navmesh_path::Status lastStatus() const;
    const navmesh_path::Mesh& mesh() const;
    std::uint64_t pathFor(std::size_t id) const;
    std::size_t pathArenaBytes() const;
    std::size_t pathUsedBytes() const;
    std::size_t capacity() const;
    const std::string& meshSha256() const;
    const std::string& sourceSha256() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
