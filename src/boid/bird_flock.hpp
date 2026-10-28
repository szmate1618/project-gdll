#pragma once

#include "bird_flight_space.hpp"
#include "presentation.hpp"

#include <cstdint>
#include <vector>

namespace viewer {
class CollisionWorld;
struct TreeLayer;

struct BirdFlockSettings {
    std::size_t flockCount = 6;
    std::size_t birdsPerFlock = 192;
    std::uint32_t seed = 0x62697264;
};

// Feet and dimensions describe the current Y-up world-space player capsule.
struct BirdFlockPlayer {
    glm::vec3 feet{0};
    float height = 1.8f;
    float radius = .3f;
    bool targetable = true;
};

using BirdFlockMode = boid::Mode;
struct BirdFlockState {
    std::size_t firstBird = 0, birdCount = 0;
    glm::vec3 center{0}, velocity{0}, target{0};
    BirdFlockMode mode = BirdFlockMode::wandering;
    float secondsRemaining = 0;
};

// Per-update work counters, reset even for a zero/invalid timestep.
struct BirdFlockStats {
    std::size_t sightChecks = 0, waypointChecks = 0, segmentChecks = 0, hits = 0;
};

// Swept sphere against a vertical player capsule; includes tangency and both
// endpoints so a fast bird cannot skip an impact between presentation frames.
bool birdHitsPlayer(glm::vec3 from, glm::vec3 to, const BirdFlockPlayer& player,
                    float birdRadius = .14f);

// Fixed storage and linear work: shared flock steering, four sampled neighbors
// per bird, and at most one sight ray per update. No per-bird scene queries.
class BirdFlocks {
public:
    BirdFlocks(const CollisionWorld& world, const TreeLayer* trees,
               glm::vec3 boundsMin, glm::vec3 boundsMax, glm::vec3 preferredCenter,
               BirdFlockSettings settings = {});
    // Standalone scheduler: accumulate frame time into exact 60 Hz boid steps.
    // Other simulations keep their existing timestep and numeric representation.
    unsigned update(float dt, const BirdFlockPlayer& player);
    // Authority step: one global perception budget, damage indexed by player.
    std::vector<unsigned> step(const std::vector<BirdFlockPlayer>& players);
    const boid::Simulation& simulation() const { return simulation_; }
    const std::vector<boid::Command>& commands() const { return commands_; }
    const std::vector<FlockBird>& birds() const { return birds_; }
    const std::vector<BirdFlockState>& flocks() const { return flocks_; }
    const BirdFlockStats& stats() const { return stats_; }

private:
    struct BirdData {
        std::uint32_t hitPass = ~std::uint32_t(0);
        std::uint64_t nextHit = 0;
    };
    struct FlockData {
        std::uint64_t nextWaypoint = 0, nextAttack = 0;
        bool routeActive = false;
    };
    std::uint32_t randomInteger();
    boid::Vec3 randomOffset(std::int32_t radius, std::int32_t height);
    bool waypoint(boid::Vec3 point, std::int32_t radius);
    bool corridor(boid::Vec3 from, boid::Vec3 to, std::int32_t radius);
    bool chooseWander(std::size_t flock, boid::Control& control);
    void recover(std::size_t flock, boid::Control& control);
    void detectPlayer(const BirdFlockPlayer& player, std::vector<boid::Control>& plans);
    void refreshPresentation();
    BirdFlightSpace space_;
    std::vector<FlockBird> birds_;
    std::vector<BirdData> birdData_;
    std::vector<BirdFlockState> flocks_;
    std::vector<FlockData> flockData_;
    boid::Simulation simulation_;
    std::vector<boid::Command> commands_;
    std::vector<boid::Control> plans_;
    boid::Vec3 minimum_{}, maximum_{}, preferred_{};
    std::uint32_t random_;
    double pendingSeconds_ = 0;
    BirdFlockStats stats_;
};
} // namespace viewer
