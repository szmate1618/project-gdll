#include "melee_contact_mesh.hpp"
#include "physics_world.hpp"

#include <stdexcept>

namespace viewer {
namespace {
constexpr float weldTolerance = .0001f;
}

MeleeContactMesh::MeleeContactMesh(b3WorldId world, const std::vector<b3Vec3>& vertices,
                                 const std::vector<int32_t>& indices, b3Pos origin)
    : vertexMap_(vertices.size()) {
    auto definition = b3MeshDef{};
    definition.vertices = const_cast<b3Vec3*>(vertices.data());
    definition.indices = const_cast<int32_t*>(indices.data());
    definition.vertexMap = vertexMap_.data();
    definition.vertexCount = static_cast<int>(vertices.size());
    definition.triangleCount = static_cast<int>(indices.size() / 3);
    definition.weldVertices = true;
    definition.weldTolerance = weldTolerance;
    definition.identifyEdges = true;
    mesh_.reset(b3CreateMesh(&definition, nullptr, 0));
    if (!mesh_) throw std::runtime_error("Cannot build melee zombie contact mesh");
    // A previously dropped degenerate face could reappear after deformation.
    canRefit_ = mesh_->triangleCount == definition.triangleCount;
    representatives_.assign(mesh_->vertexCount, -1);
    compactVertices_.resize(mesh_->vertexCount);
    for (std::size_t i = 0; i < vertexMap_.size(); ++i)
        if (representatives_[vertexMap_[i]] < 0) representatives_[vertexMap_[i]] = static_cast<int32_t>(i);

    auto body = b3DefaultBodyDef();
    body.type = b3_kinematicBody;
    body.position = origin;
    body_ = b3CreateBody(world, &body);
    auto shape = b3DefaultShapeDef();
    shape.filter.categoryBits = meleeZombieCategory;
    shape.filter.maskBits = playerBodyCategory;
    shape.baseMaterial.friction = .4f;
    shape_ = b3CreateMeshShape(body_, &shape, mesh_.get(), {1, 1, 1});
}

MeleeContactMesh::~MeleeContactMesh() {
    if (b3Body_IsValid(body_)) b3DestroyBody(body_);
}

bool MeleeContactMesh::update(const std::vector<b3Vec3>& vertices, b3Pos origin) {
    if (!canRefit_ || vertices.size() != vertexMap_.size()) return false;
    for (std::size_t i = 0; i < representatives_.size(); ++i)
        compactVertices_[i] = vertices[representatives_[i]];
    // UV/normal seams may share a collision vertex. If their positions diverge,
    // rebuild instead of pulling a face onto the wrong side of a bent joint.
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& p = vertices[i];
        const auto& q = compactVertices_[vertexMap_[i]];
        const float x = p.x - q.x, y = p.y - q.y, z = p.z - q.z;
        if (x * x + y * y + z * z > weldTolerance * weldTolerance) return false;
    }
    if (!b3RefitMesh(mesh_.get(), compactVertices_.data(), static_cast<int>(compactVertices_.size()), true)) return false;
    b3Body_SetTransform(body_, origin, b3Quat_identity);
    // This refreshes broadphase bounds and discards contact manifolds whose
    // triangle planes have moved; other unchanged poses retain their contacts.
    b3Shape_SetMesh(shape_, mesh_.get(), {1, 1, 1});
    return true;
}

} // namespace viewer
