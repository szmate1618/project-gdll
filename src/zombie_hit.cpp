#include "zombie_hit.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
ZombieHitBounds emptyBounds() {
    return {glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
}
void include(ZombieHitBounds& bounds, glm::vec3 point) {
    bounds.minimum = glm::min(bounds.minimum, point);
    bounds.maximum = glm::max(bounds.maximum, point);
}
glm::vec4 headVertexPosition(const AnimatedModel& asset, ZombieHeadVertex vertex, const glm::mat4* pose) {
    const auto& skin = asset.animation[vertex.primitive].skinning[vertex.vertex];
    const auto& point = asset.model.primitives[vertex.primitive].vertices[vertex.vertex].position;
    glm::vec4 position(0);
    for (std::size_t i = 0; i < skin.weights.size(); ++i) {
        if (skin.weights[i] == 0) continue;
        const auto joint = skin.joints[i];
        position += pose[joint] * asset.skeleton[joint].inverseBind * glm::vec4(point, 1) * skin.weights[i];
    }
    return position;
}
ZombieHitBounds transformed(const ZombieHitBounds& bounds, const glm::mat4& matrix) {
    const auto center = glm::vec3(matrix * glm::vec4((bounds.minimum + bounds.maximum) * 0.5f, 1));
    const auto half = (bounds.maximum - bounds.minimum) * 0.5f;
    const auto extent = glm::abs(glm::vec3(matrix[0])) * half.x +
                        glm::abs(glm::vec3(matrix[1])) * half.y + glm::abs(glm::vec3(matrix[2])) * half.z;
    return {center - extent, center + extent};
}
}

ZombieHitGeometry prepareZombieHitGeometry(const AnimatedModel& asset, const std::vector<ZombieJointCollider>& joints,
                                          const std::vector<ZombieBoneCapsule>& capsules) {
    ZombieHitGeometry geometry;
    geometry.joints = joints;
    geometry.capsules = capsules;
    geometry.capsulePoints = emptyBounds();
    geometry.jointPoints = emptyBounds();
    geometry.head = asset.ragdollBones[static_cast<std::size_t>(RagdollPart::head)];
    if (geometry.head < 0 || static_cast<std::size_t>(geometry.head) >= asset.skeleton.size()) {
        geometry.head = -1;
    }
    std::vector<bool> headJoint(asset.skeleton.size(), false);
    for (std::size_t joint = 0; joint < asset.skeleton.size(); ++joint) {
        int ancestor = static_cast<int>(joint);
        for (std::size_t depth = 0; ancestor >= 0 && depth < asset.skeleton.size(); ++depth) {
            if (static_cast<std::size_t>(ancestor) >= asset.skeleton.size())
                throw std::invalid_argument("Invalid zombie skeleton ancestry");
            if (ancestor == geometry.head) { headJoint[joint] = true; break; }
            ancestor = asset.skeleton[static_cast<std::size_t>(ancestor)].parent;
        }
    }
    for (std::size_t p = 0; capsules.empty() && p < asset.animation.size(); ++p) {
        const auto& skinning = asset.animation[p].skinning;
        if (p >= asset.model.primitives.size() || skinning.size() > asset.model.primitives[p].vertices.size())
            throw std::invalid_argument("Invalid zombie hit geometry topology");
        for (std::size_t v = 0; v < skinning.size(); ++v) {
            const auto& skin = skinning[v];
            float headWeight = 0;
            for (std::size_t i = 0; i < skin.weights.size(); ++i) {
                if (skin.joints[i] >= headJoint.size()) throw std::invalid_argument("Invalid zombie skin joint");
                if (headJoint[skin.joints[i]]) headWeight += skin.weights[i];
            }
            if (headWeight >= 0.5f) geometry.headVertices.push_back({p, v});
        }
    }
    geometry.headPoints = emptyBounds();
    const auto sample = [&](const glm::mat4* pose) {
        if (capsules.empty() && geometry.head >= 0 && geometry.headVertices.empty())
            include(geometry.headPoints, glm::vec3(pose[geometry.head][3]));
        else for (const auto vertex : geometry.headVertices)
            include(geometry.headPoints, glm::vec3(headVertexPosition(asset, vertex, pose)));
        for (const auto& capsule : capsules) {
            if (capsule.bone < 0 || std::size_t(capsule.bone) >= asset.skeleton.size())
                throw std::invalid_argument("Invalid shot capsule bone");
            const auto matrix = pose[capsule.bone] * capsule.boneFromMeters;
            const auto extent = capsule.radius * (glm::abs(glm::vec3(matrix[0])) +
                glm::abs(glm::vec3(matrix[1])) + glm::abs(glm::vec3(matrix[2])));
            for (const auto endpoint : {capsule.a, capsule.b}) {
                const auto center = glm::vec3(matrix * glm::vec4(endpoint, 1));
                include(geometry.capsulePoints, center - extent);
                include(geometry.capsulePoints, center + extent);
            }
        }
        for (const auto& joint : joints) {
            if (joint.bone < 0 || std::size_t(joint.bone) >= asset.skeleton.size())
                throw std::invalid_argument("Invalid zombie joint collider bone");
            const auto matrix = pose[joint.bone] * joint.boneFromMeters;
            const auto center = glm::vec3(matrix * glm::vec4(joint.center, 1));
            const auto extent = joint.radius * (glm::abs(glm::vec3(matrix[0])) +
                glm::abs(glm::vec3(matrix[1])) + glm::abs(glm::vec3(matrix[2])));
            include(geometry.jointPoints, center - extent);
            include(geometry.jointPoints, center + extent);
        }
    };
    const auto frames = [&](const std::vector<glm::mat4>& poses, std::size_t count) {
        if (poses.empty()) {
            std::vector<glm::mat4> reference;
            for (const auto& bone : asset.skeleton) reference.push_back(bone.referenceWorld);
            sample(reference.data());
        } else {
            if (!count || poses.size() / asset.skeleton.size() != count || poses.size() % asset.skeleton.size())
                throw std::invalid_argument("Invalid zombie bone samples");
            for (std::size_t frame = 0; frame < count; ++frame) sample(poses.data() + frame * asset.skeleton.size());
        }
    };
    frames(asset.boneFrames, asset.frameCount);
    if (asset.walkClip) frames(asset.walkClip->boneFrames, asset.walkClip->frameCount);
    if (asset.runClip) frames(asset.runClip->boneFrames, asset.runClip->frameCount);
    if (asset.attackLeftClip) frames(asset.attackLeftClip->boneFrames, asset.attackLeftClip->frameCount);
    if (asset.attackRightClip) frames(asset.attackRightClip->boneFrames, asset.attackRightClip->frameCount);
    return geometry;
}

ZombieHitBounds zombieShotBounds(const ZombieInstance& instance, const ZombieHitGeometry& geometry) {
    const auto body = glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1));
    ZombieHitBounds result{body - glm::vec3(0.62f), body + glm::vec3(0.62f)};
    ZombieHitBounds head;
    if (!geometry.capsules.empty()) {
        head = transformed(geometry.capsulePoints, instance.transform);
    } else if (geometry.head < 0) {
        const auto center = glm::vec3(instance.restRoot * glm::vec4(0, 1.65f, 0, 1));
        head = {center - glm::vec3(0.23f), center + glm::vec3(0.23f)};
    } else {
        head = transformed(geometry.headPoints, instance.transform);
        // The hit region is a sphere around the posed AABB, not the mesh AABB
        // itself: include its corners' radius and the existing aiming allowance.
        if (geometry.headVertices.empty()) {
            head.minimum.y += 0.1f;
            head.maximum.y += 0.1f;
        }
        const float radius = geometry.headVertices.empty() ? 0.23f :
            glm::length(head.maximum - head.minimum) * 0.5f + 0.02f;
        head.minimum -= glm::vec3(radius);
        head.maximum += glm::vec3(radius);
    }
    include(result, head.minimum);
    include(result, head.maximum);
    if (!geometry.joints.empty()) {
        const auto joints = transformed(geometry.jointPoints, instance.transform);
        include(result, joints.minimum); include(result, joints.maximum);
    }
    // Outward tolerance covers affine/frame interpolation rounding. Broad-phase
    // bounds may overinclude; the detailed colliders make the final decision.
    const auto padding = glm::vec3(0.001f) +
        glm::max(glm::abs(result.minimum), glm::abs(result.maximum)) * (8 * std::numeric_limits<float>::epsilon());
    return {result.minimum - padding, result.maximum + padding};
}

bool zombieRayIntersectsBounds(glm::vec3 origin, glm::vec3 direction, const ZombieHitBounds& bounds) {
    double near = 0, far = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) return false;
        if (direction[axis] == 0) {
            if (origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis]) return false;
        } else {
            const double a = (double(bounds.minimum[axis]) - origin[axis]) / direction[axis];
            const double b = (double(bounds.maximum[axis]) - origin[axis]) / direction[axis];
            near = std::max(near, std::min(a, b));
            far = std::min(far, std::max(a, b));
            if (near > far) return false;
        }
    }
    return true;
}

ZombieHitSphere zombieHeadHitSphere(const AnimatedModel& asset, const ZombieInstance& instance,
                                   const std::vector<glm::mat4>& pose, const ZombieHitGeometry& geometry) {
    const auto fallback = ZombieHitSphere{glm::vec3(instance.restRoot * glm::vec4(0, 1.65f, 0, 1)), 0.23f};
    if (geometry.head < 0 || static_cast<std::size_t>(geometry.head) >= pose.size()) return fallback;
    auto bounds = emptyBounds();
    for (const auto vertex : geometry.headVertices)
        include(bounds, glm::vec3(instance.transform * headVertexPosition(asset, vertex, pose.data())));
    if (geometry.headVertices.empty())
        return {glm::vec3(instance.transform * pose[static_cast<std::size_t>(geometry.head)][3]) +
                    glm::vec3(0, 0.1f, 0), fallback.radius};
    return {(bounds.minimum + bounds.maximum) * 0.5f, glm::length(bounds.maximum - bounds.minimum) * 0.5f + 0.02f};
}

ZombieHitSphere zombieHeadHitSphere(const AnimatedModel& asset, const ZombieInstance& instance,
                                   const std::vector<glm::mat4>& pose) {
    return zombieHeadHitSphere(asset, instance, pose, prepareZombieHitGeometry(asset));
}
} // namespace viewer
