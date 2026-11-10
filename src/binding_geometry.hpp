#pragma once

#include <box3d/id.h>
#include <glm/vec3.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace viewer {
class PhysicsWorld;

// World-space vertical rectangle, in metres. Its other axis is world +Y and
// its horizontal normal is cross(right, +Y).
struct BindingPlane {
    glm::vec3 center{0}, right{1, 0, 0};
    static constexpr float halfSize = .15f;
};

struct BindingSegment {
    glm::vec3 a{0}, b{0};
    b3BodyId body{};
    std::uint8_t plane = 0;
};

struct BindingGeometry {
    static constexpr std::size_t maxSegments = 1024;
    std::vector<BindingSegment> segments;
    bool truncated = false;
};

// Project the normal onto X/Z; vertical viewing directions retain a stable -Z
// fallback. The five plane centers are one centimetre apart along that normal.
std::array<BindingPlane, 5> bindingPlanes(glm::vec3 center, glm::vec3 horizontalNormal);

// Read-only broadphase plus clipped surface sections. Static geometry is kept
// in the result; the caller decides which dynamic bodies receive binding forces.
BindingGeometry queryBindingGeometry(std::shared_ptr<PhysicsWorld> world,
    const std::array<BindingPlane, 5>& planes, b3BodyId excluded = b3_nullBodyId);
}
