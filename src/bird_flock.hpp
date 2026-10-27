#pragma once

#include "bird_flight_space.hpp"

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

struct FlockBird {
    glm::vec3 position{0}, velocity{0};
    float phase = 0; // Stable wing-flap variation, radians.
};

enum class BirdFlockMode { wandering, gathering, swooping, recovering };
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
    // Returns whole HP lost (one per bird impact). Invalid dt does no work;
    // frames longer than .1 seconds are clamped to keep the prototype stable.
    unsigned update(float dt, const BirdFlockPlayer& player);
    const std::vector<FlockBird>& birds() const { return birds_; }
    const std::vector<BirdFlockState>& flocks() const { return flocks_; }
    const BirdFlockStats& stats() const { return stats_; }

private:
    struct BirdData {
        glm::vec3 offset{0};
        std::uint32_t hitPass = ~std::uint32_t(0);
        float nextHit = 0;
        bool passedPlayer = false;
    };
    struct FlockData {
        glm::vec3 aim{0}, exit{0}, attackDirection{0,0,1};
        float nextWaypoint = 0, nextAttack = 0;
        std::uint32_t pass = 0;
        bool routeActive = false;
    };
    float randomUnit();
    glm::vec3 randomOffset(float radius, float height);
    bool waypoint(glm::vec3 point, float radius);
    bool corridor(glm::vec3 from, glm::vec3 to, float radius);
    bool chooseWander(std::size_t flock);
    void recover(std::size_t flock);
    void detectPlayer(const BirdFlockPlayer& player);
    BirdFlightSpace space_;
    std::vector<FlockBird> birds_;
    std::vector<BirdData> birdData_;
    std::vector<BirdFlockState> flocks_;
    std::vector<FlockData> flockData_;
    glm::vec3 minimum_{0}, maximum_{0}, preferred_{0};
    std::uint32_t random_;
    float seconds_ = 0;
    BirdFlockStats stats_;
};
} // namespace viewer
