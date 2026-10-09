#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace viewer {

enum class ZombieState { idle, investigating, chasing, attacking };

struct ZombieBehavior {
    // ID is the stable index in ZombieLayer::instances (never compact that list).
    std::size_t zombie = 0;
    ZombieState state = ZombieState::idle;
    double createdSeconds = 0;
    double idleSinceSeconds = 0;
    glm::vec3 target{0}; // Last seen player feet in X east / Y up / Z south meters.
};

// Dense, unordered pool with expected O(1) membership, lookup and swap removal.
class ZombieBehaviorPool {
public:
    // Returned references/pointers are invalidated by pool creation/removal.
    ZombieBehavior& create(std::size_t zombie, double now);
    bool remove(std::size_t zombie);
    [[nodiscard]] bool contains(std::size_t zombie) const;
    ZombieBehavior* find(std::size_t zombie);
    const ZombieBehavior* find(std::size_t zombie) const;
    const std::vector<ZombieBehavior>& entries() const { return entries_; }

private:
    std::vector<ZombieBehavior> entries_;
    std::unordered_map<std::size_t, std::size_t> indices_;
};

// Player feet and body dimensions in meters. Free-fly uses a virtual body
// below the camera eye so the same three perception rays apply in both modes.
struct ZombiePlayer {
    glm::vec3 feet{0};
    float eyeHeight = 1.7f;
    float height = 1.8f;
};

// Horizontal +Z is the character's forward direction; ignores vertical offset.
bool zombiePlayerInFov(glm::vec3 feet, glm::vec3 forward, glm::vec3 playerFeet);

} // namespace viewer
