#pragma once

#include "physics_attachment.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace viewer {
struct DogInstance;
struct AnimatedModel;
struct DogFetching {
    b3BodyId part{};
    // Independent snapshot: home first, interruption point last. Pop to retrace.
    std::vector<glm::vec3> homeRoute;
    std::unique_ptr<PhysicsAttachment> grip;
    bool returning = false;
    float stalledSeconds = 0;
};
glm::vec3 dogMouthPosition(const DogInstance& dog, const AnimatedModel& asset);
} // namespace viewer
