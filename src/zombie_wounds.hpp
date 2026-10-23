#pragma once

#include "animated_model.hpp"
#include <unordered_map>

namespace viewer {
struct ZombieInstance;
enum class ZombieWoundRegion { torso, head };

// Stable original triangle IDs, independent of animation, health and LOD.
struct ZombieWoundGeometry {
    ZombieWoundRegion region = ZombieWoundRegion::torso;
    struct Vertex { std::size_t primitive, vertex, bit; };
    int bone = -1;
    glm::mat4 referenceFrame{1}; // Rigid region frame in asset meters.
    glm::vec3 center{0};
    std::vector<std::vector<glm::vec3>> referenceVertices;
    std::vector<std::vector<bool>> regionVertices;
    std::vector<std::size_t> triangleOffsets;
    std::vector<bool> regionTriangles;
    std::size_t triangleCount = 0;
    // Anatomical closures of the intact surface, before any bullet damage.
    std::vector<std::array<Vertex, 3>> closures;
};
struct ZombieWound {
    std::uint64_t revision = 0;
    std::vector<std::uint8_t> removedTriangles;
    std::vector<std::uint8_t> vertexMask; // Packed original vertex IDs, for replication.
    std::vector<glm::vec3> removedVertices; // Reference asset meters, for severed mesh correspondence.
};
using ZombieWoundRegions = std::array<ZombieWoundGeometry,2>;
ZombieWoundGeometry prepareZombieWoundRegion(const AnimatedModel& asset, ZombieWoundRegion region);
ZombieWoundGeometry prepareZombieWoundSurface(const AnimatedModel& asset); // Both regions, for rendering.
bool zombieWoundBone(const AnimatedModel& asset, int bone, ZombieWoundRegion region);
// Tests remaining posed region triangles; returns the nearest surface in world meters.
bool zombieWoundRay(const AnimatedModel& asset, const ZombieWoundGeometry& geometry,
                    const ZombieWound* wound, const ZombieInstance& instance,
                    const std::vector<glm::mat4>& pose, glm::vec3 origin, glm::vec3 direction, float& distance);

class ZombieWounds {
public:
    const ZombieWound* find(std::size_t id) const;
    bool hit(std::size_t id, const AnimatedModel& asset, const ZombieWoundGeometry& geometry,
             const ZombieInstance& instance, const std::vector<glm::mat4>& pose, glm::vec3 point);
    void remove(std::size_t id) { entries_.erase(id); }
    bool applyMask(std::size_t id, const AnimatedModel& asset, const ZombieWoundGeometry& geometry,
                   const std::vector<std::uint8_t>& mask);
    // Reconstruct a mask for prepared severed geometry without changing shared assets.
    static std::vector<std::uint8_t> mask(const AnimatedModel& asset, const ZombieWound& wound);
    // GPU flags: bit 0 removes a triangle, bit 1 identifies a cavity interior.
    // Closure triangles follow the original triangles in the mask.
    static std::vector<std::uint8_t> surfaceMask(const AnimatedModel& asset,
        const ZombieWoundGeometry& geometry, const ZombieWound& wound, bool original);
private:
    std::unordered_map<std::size_t, ZombieWound> entries_;
};
} // namespace viewer
