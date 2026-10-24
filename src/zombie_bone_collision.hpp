#pragma once

#include <box3d/box3d.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace viewer {
struct AnimatedModel;
struct ZombieInstance;

// Immutable, shared collision geometry in bone-local bind coordinates. Every
// original triangle belongs to the bone with the highest sum of vertex weights.
// Unweighted triangles belong to the instance root (bone == -1).
class ZombieBoneCollisionAsset {
public:
    struct Piece {
        int bone = -1;
        glm::mat4 bindWorld{1}, referenceWorld{1};
        std::size_t triangles = 0; // Original faces, before adding reverse faces.
        std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)> mesh{nullptr, b3DestroyMesh};
    };
    static bool supported(const AnimatedModel& asset);
    explicit ZombieBoneCollisionAsset(const AnimatedModel& asset);
    const std::vector<Piece>& pieces() const { return pieces_; }
private:
    std::vector<Piece> pieces_;
};

// Per-zombie kinematic proxies referencing shared immutable meshes. Only rigid
// body transforms change; skinning, refitting, and shape replacement never run.
// Fixed reference scale is baked into each shape on creation. World outlives us.
class ZombieBoneContacts {
public:
    ZombieBoneContacts(b3WorldId world, std::shared_ptr<const ZombieBoneCollisionAsset> geometry,
                       const glm::mat4& instanceTransform);
    ~ZombieBoneContacts();
    ZombieBoneContacts(const ZombieBoneContacts&) = delete;
    ZombieBoneContacts& operator=(const ZombieBoneContacts&) = delete;
    void update(const AnimatedModel& asset, const ZombieInstance& instance, double seconds);
    bool owns(b3BodyId body) const;
    const std::vector<b3BodyId>& bodies() const { return bodies_; }
private:
    std::shared_ptr<const ZombieBoneCollisionAsset> geometry_;
    std::vector<b3BodyId> bodies_;
    std::vector<glm::mat4> previousTransforms_;
    std::vector<bool> initialized_;
};
} // namespace viewer
