#pragma once

#include "model.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace viewer {

struct Contact {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 point{0.0f};
    float depth = 0.0f;
};

struct GroundHit {
    float height = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

struct CollisionStats {
    std::size_t triangles = 0;
    std::size_t terrainTriangles = 0;
    std::size_t buildingTriangles = 0;
    std::size_t buildings = 0;
    std::size_t bvhNodes = 0;
    std::size_t skippedDegenerateTriangles = 0;
};

// Static collision data uses the same transformed, Y-up meter coordinates as
// rendering. All query shapes are capsules whose position denotes their feet.
class CollisionWorld {
public:
    explicit CollisionWorld(const Model& model);
    ~CollisionWorld();
    CollisionWorld(CollisionWorld&&) noexcept;
    CollisionWorld& operator=(CollisionWorld&&) noexcept;
    CollisionWorld(const CollisionWorld&) = delete;
    CollisionWorld& operator=(const CollisionWorld&) = delete;

    std::vector<Contact> contacts(glm::vec3 feet, float radius, float height,
                                  float skin = 0.0f) const;
    std::optional<GroundHit> groundAt(float x, float z, float minY, float maxY,
                                     bool terrainOnly = false) const;
    bool insideBuilding(glm::vec3 feet, float radius, float height) const;
    // Returns valid feet, preferring terrain below/near the requested eye.
    std::optional<glm::vec3> findSpawn(glm::vec3 preferredEye, float eyeHeight,
                                      float radius, float height,
                                      float maxSlopeDegrees) const;
    // Consecutive pairs are line segments, suitable for GL_LINES.
    std::vector<glm::vec3> debugLines(glm::vec3 near, float distance = 15.0f) const;
    const CollisionStats& stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace viewer
