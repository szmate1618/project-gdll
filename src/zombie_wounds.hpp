#pragma once

#include "animated_model.hpp"
#include <unordered_map>

namespace viewer {
struct ZombieInstance;

// Stable original triangle IDs, independent of animation, health and LOD.
struct ZombieTorsoGeometry {
    int bone = -1;
    glm::mat4 referenceFrame{1}; // Rigid torso frame in asset meters.
    glm::vec3 center{0};
    std::vector<std::vector<glm::vec3>> referenceVertices;
    std::vector<std::vector<bool>> torsoVertices;
    std::vector<std::size_t> triangleOffsets;
    std::vector<bool> torsoTriangles;
    std::size_t triangleCount = 0;
};
struct ZombieTorsoWound {
    std::uint64_t revision = 0;
    std::vector<std::uint8_t> removedTriangles;
    std::vector<std::uint8_t> vertexMask; // Packed original vertex IDs, for replication.
    std::vector<glm::vec3> removedVertices; // Reference asset meters, for severed mesh correspondence.
};
ZombieTorsoGeometry prepareZombieTorso(const AnimatedModel& asset);
bool zombieTorsoBone(const AnimatedModel& asset, int bone);
// Tests remaining posed torso triangles; returns the nearest surface in world meters.
bool zombieTorsoRay(const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
                    const ZombieTorsoWound* wound, const ZombieInstance& instance,
                    const std::vector<glm::mat4>& pose, glm::vec3 origin, glm::vec3 direction, float& distance);

class ZombieWounds {
public:
    const ZombieTorsoWound* find(std::size_t id) const;
    bool hit(std::size_t id, const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
             const ZombieInstance& instance, const std::vector<glm::mat4>& pose, glm::vec3 point);
    void remove(std::size_t id) { entries_.erase(id); }
    bool applyMask(std::size_t id, const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
                   const std::vector<std::uint8_t>& mask);
    // Reconstruct a mask for prepared severed geometry without changing shared assets.
    static std::vector<std::uint8_t> mask(const AnimatedModel& asset, const ZombieTorsoWound& wound);
private:
    std::unordered_map<std::size_t, ZombieTorsoWound> entries_;
};
} // namespace viewer
