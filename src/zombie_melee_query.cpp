#include "zombie_melee_query.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include <algorithm>

namespace viewer {
ZombieMeleeQuery::ZombieMeleeQuery(ZombieLayer* layer, const std::vector<std::size_t>& ids, double seconds)
    : layer_(layer), seconds_(seconds) {
    if (!layer || ids.empty()) return;
    layer->ensureHitGeometry();
    for (const auto id : ids) targets_.push_back({id, {}});
    for (const auto id : ids) refresh(id);
}
void ZombieMeleeQuery::refresh(std::size_t id) {
    auto found = std::find_if(targets_.begin(), targets_.end(), [&](const auto& t) { return t.id == id; });
    if (found == targets_.end()) return;
    auto& parts = found->parts; parts.clear();
    const auto& instance = layer_->instances[id];
    if (instance.removed || instance.asset >= layer_->assets.size()) return;
    const auto& asset = layer_->assets[instance.asset];
    std::vector<glm::mat4> pose;
    if (instance.hasPhysicalPose()) {
        if (instance.ragdollBones.size() != asset.skeleton.size()) return;
        for (std::size_t b = 0; b < asset.skeleton.size(); ++b)
            pose.push_back(instance.ragdollBones[b] * glm::inverse(asset.skeleton[b].inverseBind));
    } else pose = sampleZombiePose(asset, instance, seconds_);
    const auto& geometry = layer_->hitGeometry_[instance.asset];
    const auto boneFrame = [&](int bone) {
        return bone < 0 ? glm::dmat4(instance.restRoot) :
            glm::dmat4(instance.transform) * glm::dmat4(pose.at(std::size_t(bone)));
    };
    const auto body = [&](const glm::vec3* points, int count, float radius, const glm::dmat4& frame,
                          int bone, bool headshot, std::optional<ZombieWoundRegion> region) {
        ZombieHitClaim claim; claim.zombie = static_cast<std::uint32_t>(id);
        claim.bone = static_cast<std::int16_t>(bone); claim.headshot = headshot; claim.woundRegion = region;
        parts.push_back({meleeCollider(points, count, radius, frame), claim, glm::inverse(boneFrame(bone)), -1});
    };
    if (!instance.ragdoll) {
        const auto& surfaces = layer_->woundGeometry_[instance.asset];
        std::array<bool, 2> added{};
        const auto surface = [&](ZombieWoundRegion region) {
            const auto r = std::size_t(region);
            if (added[r]) return;
            added[r] = true;
            for (const auto& triangle : zombieWoundTriangles(asset, surfaces[r], layer_->wounds_.find(id), pose))
                body(triangle.data(), 3, 0, glm::dmat4(instance.transform), surfaces[r].bone,
                     region == ZombieWoundRegion::head, region);
        };
        for (const auto& capsule : geometry.capsules) {
            const auto region = capsule.headshot ? ZombieWoundRegion::head : ZombieWoundRegion::torso;
            if ((capsule.headshot || zombieWoundBone(asset, capsule.bone, region)) && surfaces[std::size_t(region)].bone >= 0) {
                surface(region); continue;
            }
            const std::array<glm::vec3, 2> points{capsule.a, capsule.b};
            body(points.data(), 2, capsule.radius, boneFrame(capsule.bone) * glm::dmat4(capsule.boneFromMeters),
                 capsule.bone, capsule.headshot, {});
        }
        if (geometry.capsules.empty()) {
            const glm::vec3 torso(0, .9f, 0);
            body(&torso, 1, .62f, glm::dmat4(instance.restRoot), -1, false, {});
            if (surfaces[1].bone >= 0) surface(ZombieWoundRegion::head);
            else {
                const auto head = zombieHeadHitSphere(asset, instance, pose, geometry);
                body(&head.center, 1, head.radius, glm::dmat4(1), geometry.head, true, {});
            }
        }
    }
    if (instance.severedJoint < 0) for (std::size_t j = 0; j < geometry.joints.size(); ++j) {
        const auto& joint = geometry.joints[j];
        parts.push_back({meleeCollider(&joint.center, 1, joint.radius,
            boneFrame(joint.bone) * glm::dmat4(joint.boneFromMeters)), {}, glm::dmat4(1), static_cast<int>(j)});
    }
}
std::vector<ZombieMeleeContact> ZombieMeleeQuery::cast(const MeleeSweep& sweep, bool jointsOnly) const {
    std::vector<ZombieMeleeContact> result;
    for (const auto& target : targets_) {
        if (layer_->instances[target.id].removed) continue;
        for (const auto& part : target.parts) {
            if (jointsOnly && part.joint < 0) continue;
            const auto hit = castMeleeShape(sweep, part.collider);
            if (!hit) continue;
            auto claim = part.body;
            if (claim) claim->point = glm::vec3(part.claimInverse * glm::dvec4(hit->point, 1));
            result.push_back({target.id, *hit, claim, part.joint});
        }
    }
    return result;
}
} // namespace viewer
