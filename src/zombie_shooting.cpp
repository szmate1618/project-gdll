#include "zombie_update_profile.hpp"
#include "zombie_layer.hpp"
#include "zombie_navigation.hpp"
#include "zombie_animation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
namespace {
bool raySphere(glm::vec3 origin, glm::vec3 direction, glm::vec3 center, float radius, float& distance) {
    const auto offset = glm::dvec3(center) - glm::dvec3(origin);
    const auto ray = glm::dvec3(direction);
    // The closest-point distance avoids subtracting two large squared ranges.
    // Account for the float-normalized direction not being exactly unit length.
    const double lengthSquared = glm::dot(ray, ray);
    const double projection = glm::dot(offset, ray) / lengthSquared;
    const auto perpendicular = offset - projection * ray;
    const double discriminant = double(radius) * radius - glm::dot(perpendicular, perpendicular);
    if (discriminant < 0.0) return false;
    const double root = std::sqrt(discriminant / lengthSquared);
    const double near = projection - root;
    const double far = projection + root;
    distance = static_cast<float>(near >= 0.0 ? near : far);
    return distance >= 0.0f && std::isfinite(distance);
}
}

void ZombieLayer::rebuildSpatialIndex() {
    if (navigation_) navigation_->resetPopulation(instances.size());
    invalidateRenderData();
    rebuildRagdollIndex();
    spatialIndex_.reset(instances.size());
    crowd_.reset(instances.size());
    hitGeometry_.clear();
    hitGeometry_.reserve(assets.size());
    for (std::size_t a = 0; a < assets.size(); ++a)
        hitGeometry_.push_back(prepareZombieHitGeometry(assets[a], a < dismembermentAssets.size() ?
            dismembermentAssets[a].joints : std::vector<ZombieJointCollider>{}));
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
    if (instance.removed || instance.ragdoll || instance.asset >= assets.size()) {
        spatialIndex_.erase(id);
        crowd_.erase(id);
        return;
    }
    const auto feet = glm::vec3(instance.restRoot[3]);
    spatialIndex_.update(id, feet);
    crowd_.update(id, feet);
    auto& bounds = shotBounds_[id];
    bounds = zombieShotBounds(instance, hitGeometry_[instance.asset]);
    // A high-water mark avoids rescanning the crowd when one actor moves. Normal
    // yaw/locomotion cannot grow it unboundedly; explicit rebuild resets it.
    for (const int axis : {0, 2})
        shotPadding_ = std::max({shotPadding_, std::abs(double(bounds.minimum[axis]) - feet[axis]),
                                              std::abs(double(bounds.maximum[axis]) - feet[axis])});
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
        const auto& asset = assets[instance.asset];
        const auto& geometry = hitGeometry_[instance.asset];
        const auto center = glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1));
        float distance = std::numeric_limits<float>::max();
        const bool bodyHit = raySphere(origin, direction, center, 0.62f, distance);
        auto pose = sampleZombiePose(asset, instance, animationSeconds);
        ++shootingStats_.poseSamples;
        shootingStats_.headVertices += geometry.headVertices.size();
        const auto head = zombieHeadHitSphere(asset, instance, pose, geometry);
        float headDistance = 0;
        const bool headHit = raySphere(origin, direction, head.center, head.radius, headDistance);
        // The first region entered determines damage, including overlapping spheres.
        const bool headshot = headHit && (!bodyHit || headDistance <= distance);
        if (headHit) distance = bodyHit ? std::min(distance, headDistance) : headDistance;
        int jointHit = -1;
        float jointDistance = std::numeric_limits<float>::max();
        for (std::size_t j = 0; j < geometry.joints.size(); ++j) {
            float d = 0;
            if (zombieJointRay(origin, direction, instance.transform, geometry.joints[j], pose, d) && d < jointDistance) {
                jointHit = static_cast<int>(j); jointDistance = d;
            }
        }
        // Joint regions override the coarse body/head damage proxies on this
        // actor. Occlusion must end at the actual joint collider, not a proxy.
        if (jointHit >= 0) distance = jointDistance;
        if ((bodyHit || headHit || jointHit >= 0) && distance < nearest) {
            nearest = distance;
            selected = ZombieShotHit{id, distance, headshot, jointHit};
            selectedPose = std::move(pose);
        }
    }
    return selected;
}
} // namespace viewer
