#pragma once

#include "collision_world.hpp"

namespace viewer {
struct CharacterStepConfig {
    float radius = .30f, height = 1.8f;
    float maxRise = .30f, minNormalY = .70710678f, skin = .002f;
};

struct CharacterStep {
    glm::vec3 feet;
    glm::vec3 groundNormal;
};

// Attempt one short horizontal capsule step (at most radius/3 meters). Probe
// within the forward foot radius for walkable support, check the lift and
// forward passage, then lower the capsule onto the first support. No impulses,
// gravity, publication or crowd queries; callers handle those independently.
std::optional<CharacterStep> tryCharacterStep(glm::vec3 start, glm::vec3 desired,
                                             const CollisionWorld& world,
                                             const CharacterStepConfig& config = {});
} // namespace viewer
