#include "zombie_update_profile.hpp"
#include "zombie_layer.hpp"
#include "zombie_navigation.hpp"
#include "zombie_animation.hpp"
#include "zombie_animation_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {

void ZombieLayer::rebuildSpatialIndex() {
    if (navigation_) navigation_->resetPopulation(instances.size());

    invalidateRenderData();
    rebuildRagdollIndex();
    spatialIndex_.reset(instances.size());
    springRagdollIndex_.reset(instances.size());
    replicationIndex_.reset(instances.size());
    replicationWorldBounds_.resize(instances.size());
    replicationAssetBounds_.clear();
    replicationAssetBounds_.reserve(assets.size());
    for (std::size_t asset = 0; asset < assets.size(); ++asset) {
        auto animation = emptyZombieBounds(), bind = emptyZombieBounds();
        includeZombieGeometryBounds(assets[asset], animation, bind);
        if (asset < lodAssets.size())
            for (const auto& lod : lodAssets[asset]) includeZombieGeometryBounds(lod, animation, bind);
        if (asset < dismembermentAssets.size())
            for (const auto& mesh : dismembermentAssets[asset].meshes)
                includeZombieGeometryBounds(mesh, animation, bind);
        // Nonanimated synthetic/custom rigs can use their reference bounds.
        if (bind.minimum.x > bind.maximum.x) bind = {assets[asset].model.boundsMin, assets[asset].model.boundsMax};
        if (bind.minimum == bind.maximum) bind = {{-.5f, 0, -.5f}, {.5f, 1.8f, .5f}};
        if (animation.minimum.x > animation.maximum.x) animation = bind;
        replicationAssetBounds_.push_back({zombieAnimationHalfBounds(animation), bind});
    }
    replicationPadding_ = 0;
    behaviors.hearingIndex_.reset(instances.size());
    crowd_.reset(instances.size());
    prepareHitGeometry();
    shotBounds_.resize(instances.size());
    shotPadding_ = 0;
    for (std::size_t id = 0; id < instances.size(); ++id) updateSpatialMembership(id);
}

void ZombieLayer::ensureSpatialIndex() {
    if (spatialIndex_.population() != instances.size() || hitGeometry_.size() != assets.size()) rebuildSpatialIndex();
}

void ZombieLayer::updateSpatialMembership(std::size_t id) {
    zombie_profile::Timer timer(zombie_profile::Stage::spatial);
    const auto& instance = instances[id];
    updateReplicationMembership(id);
    springRagdollIndex_.erase(id);
    if (instance.removed || instance.ragdoll || instance.asset >= assets.size()) {
        spatialIndex_.erase(id);
        behaviors.hearingIndex_.erase(id);
        crowd_.erase(id);
        if (!instance.removed && instance.springRagdoll && instance.physicsRagdoll && instance.asset < assets.size())
            springRagdollIndex_.update(id, glm::vec3(instance.transform[3]));
        return;
    }
    const auto feet = glm::vec3(instance.restRoot[3]);
    if (behaviors.contains(id)) behaviors.hearingIndex_.update(id, feet);
    spatialIndex_.update(id, feet);
    crowd_.update(id, feet);
    auto& bounds = shotBounds_[id];
    bounds = zombieShotBounds(instance, hitGeometry_[instance.asset]);
    if (instance.activeRagdoll) {
        const auto& physical = replicationWorldBounds_[id];
        bounds.minimum = glm::min(bounds.minimum, physical.minimum);
        bounds.maximum = glm::max(bounds.maximum, physical.maximum);
    }
    // A high-water mark avoids rescanning the crowd when one actor moves. Normal
    // yaw/locomotion cannot grow it unboundedly; explicit rebuild resets it.
    for (const int axis : {0, 2})
        shotPadding_ = std::max({shotPadding_, std::abs(double(bounds.minimum[axis]) - feet[axis]),
                                              std::abs(double(bounds.maximum[axis]) - feet[axis])});
}

void ZombieLayer::updateReplicationMembership(std::size_t id) {
    if (replicationIndex_.population() != instances.size() || replicationAssetBounds_.size() != assets.size()) return;
    const auto& instance = instances[id];
    if (instance.removed || instance.asset >= assets.size()) {
        replicationIndex_.erase(id);
        return;
    }
    const auto& assetBounds = replicationAssetBounds_[instance.asset];
    auto local = assetBounds[0];
    if (instance.hasPhysicalPose())
        for (const auto& bone : instance.ragdollBones)
            includeZombieBounds(local, transformedZombieBounds(assetBounds[1], bone));
    auto& bounds = replicationWorldBounds_[id];
    bounds = transformedZombieBounds(local, instance.transform);
    const auto center = (bounds.minimum + bounds.maximum) * .5f;
    const glm::vec2 horizontalHalf{(bounds.maximum.x - bounds.minimum.x) * .5f,
                                   (bounds.maximum.z - bounds.minimum.z) * .5f};
    replicationPadding_ = std::max(replicationPadding_, glm::length(horizontalHalf));
    replicationIndex_.update(id, center);
}

void ZombieLayer::visitReplicationNear(glm::vec3 center, float radius,
        const std::function<bool(std::size_t)>& visitor) {
    ensureSpatialIndex();
    replicationIndex_.visitCellsNear(center, double(radius) + replicationPadding_, visitor);
}

std::optional<ZombieShotHit> ZombieLayer::findShotTarget(glm::vec3 origin, glm::vec3 direction, double animationSeconds,
                                                       std::vector<glm::mat4>& selectedPose) {
    ensureSpatialIndex();
    spatialIndex_.queryRay(origin, direction, shotPadding_, spatialCandidates_);
    shootingStats_.spatialCandidates = spatialCandidates_.size();
    std::optional<ZombieShotHit> selected;
    float nearest = std::numeric_limits<float>::max();
    for (const auto id : spatialCandidates_) {
        const auto& instance = instances[id];
        if (instance.removed || instance.ragdoll || instance.asset >= assets.size() ||
            !zombieRayIntersectsBounds(origin, direction, shotBounds_[id])) continue;
        std::vector<glm::mat4> pose;
        int bone = -1;
        const auto hit = traceShotInstance(id, instance, origin, direction, animationSeconds, pose, bone,
                                          wounds_.find(id), &shootingStats_);
        if (hit && hit->distance < nearest) {
            nearest = hit->distance;
            selected = hit;
            selectedPose = std::move(pose);
        }
    }
    return selected;
}
} // namespace viewer
