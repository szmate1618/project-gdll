#pragma once

#include "model.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace viewer {
class DoorLayer;

struct Contact {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 point{0.0f};
    float depth = 0.0f;
};

struct GroundHit {
    float height = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

// Y-up vertical capsule. Base and height include the rounded end caps; the
// center segment runs from base.y + radius to base.y + height - radius.
// These simple solid trunks are independent of rendered foliage/impostors.
struct TrunkCollider {
    glm::vec3 base{0.0f};
    float radius = 0.15f;
    float height = 5.0f;
};

struct CollisionStats {
    std::size_t triangles = 0;
    std::size_t terrainTriangles = 0;
    std::size_t buildingTriangles = 0;
    std::size_t buildings = 0;
    std::size_t bvhNodes = 0;
    std::size_t skippedDegenerateTriangles = 0;
    std::size_t trunks = 0;
    std::size_t trunkBvhNodes = 0;
};

// Static scene BVHs and live door contacts use rendering's transformed, Y-up
// meters. All query capsules denote feet. A supplied door layer must outlive
// this world; queries read its current physics pose without changing it.
class CollisionWorld {
public:
    explicit CollisionWorld(const Model& model, const std::vector<TrunkCollider>& trunks = {},
                            const DoorLayer* doors = nullptr);
    ~CollisionWorld();
    CollisionWorld(CollisionWorld&&) noexcept;
    CollisionWorld& operator=(CollisionWorld&&) noexcept;
    CollisionWorld(const CollisionWorld&) = delete;
    CollisionWorld& operator=(const CollisionWorld&) = delete;

    std::vector<Contact> contacts(glm::vec3 feet, float radius, float height,
                                  float skin = 0.0f) const;
    // Support comes only from the scene mesh, never foliage or trunk capsules.
    std::optional<GroundHit> groundAt(float x, float z, float minY, float maxY,
                                     bool terrainOnly = false) const;
    bool insideBuilding(glm::vec3 feet, float radius, float height) const;
    // Finite segment visibility through scene triangles and solid trunk
    // capsules. Double-sided geometry blocks sight; foliage does not. Ignore
    // the first/last millimeter so a target's supporting ground is not a wall.
    bool lineOfSight(glm::vec3 origin, glm::vec3 target) const;
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
