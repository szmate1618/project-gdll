#pragma once

#include <box3d/box3d.h>
#include <memory>
#include <vector>

namespace viewer {

// Owns a single winding of a posed triangle surface. Positions are world meters
// relative to origin; topology and the vertex welding map survive pose changes.
// Update only between world steps. Destroy this owner before its physics world.
class MeleeContactMesh {
public:
    MeleeContactMesh(b3WorldId world, const std::vector<b3Vec3>& vertices,
                     const std::vector<int32_t>& indices, b3Pos origin);
    ~MeleeContactMesh();
    MeleeContactMesh(const MeleeContactMesh&) = delete;
    MeleeContactMesh& operator=(const MeleeContactMesh&) = delete;
    // False leaves the old surface intact: the caller must build fresh topology.
    bool update(const std::vector<b3Vec3>& vertices, b3Pos origin);
    b3BodyId body() const { return body_; }
private:
    std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)> mesh_{nullptr, b3DestroyMesh};
    std::vector<int32_t> vertexMap_, representatives_;
    std::vector<b3Vec3> compactVertices_;
    b3BodyId body_{};
    b3ShapeId shape_{};
    bool canRefit_ = false;
};

} // namespace viewer
