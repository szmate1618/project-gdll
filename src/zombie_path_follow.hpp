#pragma once

#include <glm/vec2.hpp>
#include <cstddef>

namespace viewer::navmesh_path { struct Point; class Mesh; }

namespace viewer::steering {

struct FollowState {
    std::size_t segment = 0; // Index of the current segment's starting waypoint.
    bool arrived = false;
};

// Horizontal X/Z velocity in world meters/second. Borrowed points must remain
// valid for this call. Reset state for a replacement path. No allocation or
// physical movement occurs here; the caller applies steering and collisions.
// Stops within 0.15 m of the actual final position and caps each step to its
// target to avoid overshoot. A mesh validates proximity/passed-corner shortcuts.
// Without a mesh, corners advance only once reached to floating-point precision.
// Invalid/empty input stops safely; an empty path is not an arrival.
glm::vec2 followPath(const navmesh_path::Point* points, std::size_t count,
                     glm::vec2 position, float speed, float dt, FollowState& state,
                     const navmesh_path::Mesh* mesh = nullptr);

} // namespace viewer::steering
