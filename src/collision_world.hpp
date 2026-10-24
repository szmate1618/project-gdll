#pragma once

#include "model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace viewer {
class DoorLayer;
class BoxLayer;
struct StaticCollisionFace {
    std::size_t index;
    std::array<glm::vec3, 3> vertices;
};

struct Contact {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 point{0.0f};
    float depth = 0.0f;
    bool terrain = false; // Rendered terrain/roads; not a prop or building floor.
    bool terrainSupport = false; // Terrain support; elevated road edges remain obstacles.
};

// Borrowed callback for synchronous contact enumeration. Return false to stop.
// The context must remain valid for the duration of visitContacts().
struct ContactVisitor {
    void* context;
    bool (*receive)(void*, const Contact&);
    bool operator()(const Contact& contact) const { return receive(context, contact); }
};

enum class ContactPolicy { complete, walking };

struct GroundHit {
    float height = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool terrain = false;
};

// Caller-owned walking query cache. Candidate indices cover a small X/Z patch
// at every static height, preserving overlapping surfaces and changing step
// ceilings. Dense patches fall back to the BVH; live boxes are never cached.
class WalkingGroundCache {
    friend class CollisionWorld;
    std::uint64_t worldId_ = 0;
    glm::vec2 minimum_{0}, maximum_{0};
    std::array<std::uint32_t, 16> triangles_{};
    std::size_t count_ = 0;
    bool complete_ = false;
};

// Caller-owned static contact candidates. Reuse only while the complete query
// AABB stays in the recorded patch; overflowing categories fall back to the BVH.
// Live door/box contacts are never cached.
class ContactCandidates {
    friend class CollisionWorld;
    std::uint64_t worldId_ = 0;
    glm::vec3 minimum_{0}, maximum_{0};
    std::array<std::uint32_t,64> triangles_{};
    std::array<std::uint32_t,32> trunks_{};
    std::size_t triangleCount_ = 0, trunkCount_ = 0;
    bool trianglesComplete_ = false, trunksComplete_ = false, doorFree_ = false;
    float terrainCeiling_ = 0, otherCeiling_ = 0;
    bool walkingBounds_ = false;
};

// Y-up vertical capsule. Base and height include the rounded end caps; the
// center segment runs from base.y + radius to base.y + height - radius.
// These simple solid trunks are independent of rendered foliage/impostors.
struct TrunkCollider {
    glm::vec3 base{0.0f};
    float radius = 0.15f;
    float height = 5.0f;
};

// Bounded static trunk candidates for steering. The entire query AABB must
// stay inside the cached patch; world replacement/exit refresh it. Overflow
// falls back to the original BVH traversal, preserving output order and limits.
class TrunkCandidates {
    friend class CollisionWorld;
    std::uint64_t worldId_ = 0;
    glm::vec3 minimum_{0}, maximum_{0};
    std::array<std::uint32_t,32> trunks_{};
    std::size_t count_ = 0;
    bool complete_ = false;
};

// Stable original construction index and a copy of the physical trunk capsule.
struct NearbyTrunk {
    std::uint32_t id = 0;
    TrunkCollider collider;
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

    // Optional extra clearance (meters) against steep building wall faces only.
    // Does not inflate floors, props, door panels, trunks or rendered geometry.
    std::vector<Contact> contacts(glm::vec3 feet, float radius, float height,
                                  float skin = 0.0f, float wallClearance = 0.0f) const;
    // Same contact generation, without allocating/sorting a full static list.
    // Return as soon as the visitor declines another contact. The walking policy allows
    // certified nonblocking supports to be omitted (zombie walking rules only).
    void visitContacts(glm::vec3 feet, float radius, float height, float skin,
                       float wallClearance, ContactVisitor visitor, ContactCandidates* candidates = nullptr,
                       ContactPolicy policy = ContactPolicy::complete) const;
    // Conservative simplified swept test for distant scenario travel. Terrain
    // height/slope must be sampled separately. Nearby live props/doors and
    // unsupported geometry return false so callers use full capsule movement.
    bool simpleTravelerClear(glm::vec3 start, glm::vec3 end, float radius, float height,
                             ContactCandidates& candidates) const;
    // Support comes from scene meshes and live props, never vegetation. With
    // terrainOnly, ignore props and buildings for terrain-based placement.
    std::optional<GroundHit> groundAt(float x, float z, float minY, float maxY,
                                     bool terrainOnly = false) const;
    // Static floor/terrain only, including exposed foundation caps. Decorative
    // effects must not attach their cached floor plane to a movable box.
    std::optional<GroundHit> staticGroundAt(float x, float z, float minY, float maxY) const;
    // Walking also supports the exposed upper side of generated foundation
    // bottom caps. Other downward faces (ceilings/roofs) remain excluded.
    // Callers bound maxY by their step height and check slope/body clearance.
    std::optional<GroundHit> walkingGroundAt(float x, float z, float minY, float maxY,
                                             WalkingGroundCache* cache = nullptr) const;
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
    std::uint64_t identity() const; // Immutable geometry lifetime; survives moves.
    bool empty() const; // No static geometry, trunks, doors or boxes.
    // Borrowed dynamic props must outlive this world. Queries remain read-only.
    void setBoxes(BoxLayer* boxes);
    void pushBoxes(glm::vec3 feet, float radius, float height, glm::vec3 velocity, float dt) const;
    // BVH-selected static triangles, including the actual clipped doorways.
    // Stable indices let physics upload neighboring regions without duplicates.
    std::vector<StaticCollisionFace> staticFaces(glm::vec3 minimum, glm::vec3 maximum) const;
    // Deepest sphere/trunk overlap through the trunk BVH, ignoring scene meshes
    // and foliage. Center and radius are world meters; no velocity sweep.
    std::optional<Contact> trunkContact(glm::vec3 center, float radius) const;
    // Allocation-free trunk BVH query. range is horizontal distance from feet
    // to the trunk's cylindrical surface, in meters; the body's vertical span
    // must overlap the trunk's full vertical extent. Both boundaries are closed.
    // Copies up to capacity entries in deterministic BVH traversal order; these
    // are not necessarily the nearest entries. overflow reports omitted matches.
    // A null output is equivalent to zero capacity. Invalid input returns zero.
    std::size_t nearbyTrunks(glm::vec3 feet, float agentHeight, float range,
                             NearbyTrunk* output, std::size_t capacity,
                             bool& overflow, TrunkCandidates* candidates = nullptr) const;

private:
    void prepareContactCandidates(glm::vec3 minimum, glm::vec3 maximum, ContactCandidates& cache) const;
    void prepareWalkingBounds(ContactCandidates& cache) const;
    bool walkingRegionClear(float feetY, float radius, float skin, float wallClearance, ContactCandidates& cache) const;
    std::optional<GroundHit> queryGround(float x, float z, float minY, float maxY,
                                        bool terrainOnly, bool foundationCaps, WalkingGroundCache* cache = nullptr,
                                        bool includeBoxes = true) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace viewer
