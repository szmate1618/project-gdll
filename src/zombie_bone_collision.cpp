#include "zombie_bone_collision.hpp"
#include "animated_model.hpp"
#include "physics_world.hpp"
#include "zombie_animation.hpp"
#include "zombie_layer.hpp"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
using Mesh = std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)>;
struct Partition {
    std::vector<b3Vec3> vertices;
    std::vector<int32_t> indices;
};
// Keep the two windings topologically separate, so edge classification is not
// confused by four faces meeting at each shared edge of a double-sided mesh.
Mesh buildTwoSided(Partition& input) {
    auto definition = b3MeshDef{};
    definition.vertices = input.vertices.data(); definition.indices = input.indices.data();
    definition.vertexCount = static_cast<int>(input.vertices.size());
    definition.triangleCount = static_cast<int>(input.indices.size() / 3);
    definition.weldVertices = true; definition.weldTolerance = .0001f;
    Mesh front(b3CreateMesh(&definition, nullptr, 0), b3DestroyMesh);
    if (!front) throw std::runtime_error("Cannot build rigid zombie bone surface");
    const auto* points = b3GetMeshVertices(front.get());
    std::vector<b3Vec3> vertices(points, points + front->vertexCount);
    vertices.insert(vertices.end(), points, points + front->vertexCount);
    std::vector<int32_t> indices;
    indices.reserve(front->triangleCount * 6);
    const auto* triangles = b3GetMeshTriangles(front.get());
    for (int i = 0; i < front->triangleCount; ++i) {
        const auto& t = triangles[i];
        indices.insert(indices.end(), {t.index1, t.index2, t.index3});
        indices.insert(indices.end(), {t.index1 + front->vertexCount, t.index3 + front->vertexCount,
                                      t.index2 + front->vertexCount});
    }
    definition.vertices = vertices.data(); definition.indices = indices.data();
    definition.vertexCount = static_cast<int>(vertices.size());
    definition.triangleCount = static_cast<int>(indices.size() / 3);
    definition.weldVertices = false; definition.identifyEdges = true;
    Mesh result(b3CreateMesh(&definition, nullptr, 0), b3DestroyMesh);
    if (!result) throw std::runtime_error("Cannot build two-sided zombie bone surface");
    return result;
}
glm::quat rigidRotation(const glm::mat4& transform) {
    auto x = glm::vec3(transform[0]);
    x = glm::length(x) > 1e-8f ? glm::normalize(x) : glm::vec3(1, 0, 0);
    auto y = glm::vec3(transform[1]) - x * glm::dot(x, glm::vec3(transform[1]));
    if (glm::length(y) < 1e-8f) {
        const auto helper = std::abs(x.y) < .8f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        y = helper - x * glm::dot(x, helper);
    }
    y = glm::normalize(y);
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));
}
}

bool ZombieBoneCollisionAsset::supported(const AnimatedModel& asset) {
    if (asset.skeleton.empty()) return false;
    return std::any_of(asset.animation.begin(), asset.animation.end(), [](const auto& p) { return !p.skinning.empty(); });
}

ZombieBoneCollisionAsset::ZombieBoneCollisionAsset(const AnimatedModel& asset) {
    std::vector<Partition> partitions(asset.skeleton.size() + 1);
    std::vector<float> weights(asset.skeleton.size());
    for (const auto& draw : asset.model.draws) {
        const auto& primitive = asset.model.primitives.at(draw.primitive);
        const auto* skin = draw.primitive < asset.animation.size() ? &asset.animation[draw.primitive].skinning : nullptr;
        for (std::size_t face = 0; face < primitive.indices.size(); face += 3) {
            std::fill(weights.begin(), weights.end(), 0);
            for (int corner = 0; corner < 3; ++corner) {
                const auto v = primitive.indices.at(face + corner);
                if (!skin || skin->empty()) continue;
                const auto& influences = skin->at(v);
                for (std::size_t j = 0; j < influences.weights.size(); ++j)
                    if (influences.weights[j] > 0) weights.at(influences.joints[j]) += influences.weights[j];
            }
            const auto best = std::max_element(weights.begin(), weights.end());
            const auto bone = best != weights.end() && *best > 0 ?
                static_cast<std::size_t>(best - weights.begin()) : asset.skeleton.size();
            auto& part = partitions[bone];
            const auto inverseBind = bone < asset.skeleton.size() ? asset.skeleton[bone].inverseBind : glm::mat4(1);
            for (int corner = 0; corner < 3; ++corner) {
                const auto& vertex = primitive.vertices.at(primitive.indices.at(face + corner));
                const auto p = glm::vec3(inverseBind * draw.transform * glm::vec4(vertex.position, 1));
                part.indices.push_back(static_cast<int32_t>(part.vertices.size()));
                part.vertices.push_back({p.x, p.y, p.z});
            }
        }
    }
    for (std::size_t bone = 0; bone < partitions.size(); ++bone) {
        auto& partition = partitions[bone];
        if (partition.indices.empty()) continue;
        Piece piece;
        if (bone < asset.skeleton.size()) {
            piece.bone = static_cast<int>(bone);
            piece.bindWorld = glm::inverse(asset.skeleton[bone].inverseBind);
            piece.referenceWorld = asset.skeleton[bone].referenceWorld;
        }
        piece.triangles = partition.indices.size() / 3;
        piece.mesh = buildTwoSided(partition);
        pieces_.push_back(std::move(piece));
    }
}

ZombieBoneContacts::ZombieBoneContacts(b3WorldId world, std::shared_ptr<const ZombieBoneCollisionAsset> geometry,
                                     const glm::mat4& instanceTransform) : geometry_(std::move(geometry)) {
    const auto size = geometry_->pieces().size();
    bodies_.reserve(size); previousTransforms_.resize(size); initialized_.resize(size, false);
    for (const auto& part : geometry_->pieces()) {
        const auto reference = instanceTransform * part.referenceWorld;
        glm::vec3 scale(glm::length(glm::vec3(reference[0])), glm::length(glm::vec3(reference[1])),
                        glm::length(glm::vec3(reference[2])));
        if (glm::determinant(glm::mat3(reference)) < 0) scale.z = -scale.z;
        auto body = b3DefaultBodyDef(); body.type = b3_kinematicBody;
        bodies_.push_back(b3CreateBody(world, &body));
        auto shape = b3DefaultShapeDef();
        shape.filter.categoryBits = meleeZombieCategory; shape.filter.maskBits = playerBodyCategory;
        shape.baseMaterial.friction = .4f;
        b3CreateMeshShape(bodies_.back(), &shape, part.mesh.get(), {scale.x, scale.y, scale.z});
    }
}
ZombieBoneContacts::~ZombieBoneContacts() {
    for (const auto body : bodies_) if (b3Body_IsValid(body)) b3DestroyBody(body);
}
void ZombieBoneContacts::update(const AnimatedModel& asset, const ZombieInstance& instance, double seconds) {
    const auto pose = instance.ragdoll ? std::vector<glm::mat4>{} : sampleZombiePose(asset, instance, seconds);
    const auto& root = instance.ragdoll ? instance.transform : instance.restTransform;
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        const auto& part = geometry_->pieces()[i];
        auto transform = root;
        if (part.bone >= 0)
            transform *= instance.ragdoll ? instance.ragdollBones.at(part.bone) * part.bindWorld : pose.at(part.bone);
        if (initialized_[i] && transform == previousTransforms_[i]) continue;
        const auto q = rigidRotation(transform);
        const auto p = glm::vec3(transform[3]);
        b3Body_SetTransform(bodies_[i], {p.x, p.y, p.z}, {{q.x, q.y, q.z}, q.w});
        previousTransforms_[i] = transform; initialized_[i] = true;
    }
}
bool ZombieBoneContacts::owns(b3BodyId body) const {
    return std::any_of(bodies_.begin(), bodies_.end(), [&](auto b) { return B3_ID_EQUALS(b, body); });
}
} // namespace viewer
