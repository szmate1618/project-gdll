#pragma once
#include "visibility.hpp"
#include <array>
#include <optional>

namespace viewer {
// Convex point cloud at the ends of one short angular step, in world metres.
// Vertex chords approximate the arc (under 0.1 mm for the axe's 1/360 s steps).
struct MeleeSweep {
    std::array<glm::vec3, 8> from{}, to{};
    int count = 0;
    VisibilityBounds bounds;
};
MeleeSweep meleeSweep(const glm::vec3* points, int count, const glm::mat4& from, const glm::mat4& to);

// Fixed convex target in its own metric frame: triangle, capsule, or sphere.
// Moving the weapon into this frame preserves affine bone/instance transforms.
struct MeleeCollider {
    std::array<glm::vec3, 3> points{};
    int count = 0;
    float radius = 0;
    glm::dmat4 frame{1}, inverse{1};
    VisibilityBounds bounds;
};
MeleeCollider meleeCollider(const glm::vec3* points, int count, float radius, const glm::dmat4& frame);
struct MeleeSweepHit { float fraction; glm::vec3 point; };
std::optional<MeleeSweepHit> castMeleeShape(const MeleeSweep& sweep, const MeleeCollider& target);
} // namespace viewer
