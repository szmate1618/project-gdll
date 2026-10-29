#include "zombie_layer.hpp"
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

void ZombieLayer::prepareHitGeometry() const {
    hitGeometry_.clear();
    hitGeometry_.reserve(assets.size());
    woundGeometry_.clear();
    for (const auto& asset : assets)
        woundGeometry_.push_back({prepareZombieWoundRegion(asset,ZombieWoundRegion::torso),
                                  prepareZombieWoundRegion(asset,ZombieWoundRegion::head)});
    for (std::size_t a = 0; a < assets.size(); ++a)
        hitGeometry_.push_back(prepareZombieHitGeometry(assets[a], a < dismembermentAssets.size() ?
            dismembermentAssets[a].joints : std::vector<ZombieJointCollider>{},
            a < capsuleAssets.size() ? capsuleAssets[a].capsules : std::vector<ZombieBoneCapsule>{}));
}

void ZombieLayer::ensureHitGeometry() const {
    if (hitGeometry_.size() != assets.size() || woundGeometry_.size() != assets.size()) prepareHitGeometry();
}

std::optional<ZombieShotHit> ZombieLayer::traceShotInstance(std::size_t id, const ZombieInstance& instance,
        glm::vec3 origin, glm::vec3 direction, double seconds, std::vector<glm::mat4>& pose,
        int& bone, const ZombieWound* wound, ZombieShootingStats* stats) const {
    if (instance.removed || instance.ragdoll || instance.asset >= assets.size()) return std::nullopt;
    const auto& asset = assets[instance.asset];
    const auto& geometry = hitGeometry_[instance.asset];
    float distance = std::numeric_limits<float>::max();
    pose = sampleZombiePose(asset, instance, seconds);
    if (stats) ++stats->poseSamples;
    bool surfaceHit = false, headshot = false;
    std::optional<ZombieWoundRegion> woundRegion;
    std::array<bool,2> tested{}, surfaces{};
    std::array<float,2> surfaceDistances{};
    if (!geometry.capsules.empty()) {
        for (const auto& capsule : geometry.capsules) {
            float d = 0;
            const auto region = capsule.headshot ? ZombieWoundRegion::head : ZombieWoundRegion::torso;
            const auto r = std::size_t(region);
            const auto& surface = woundGeometry_[instance.asset][r];
            if ((capsule.headshot || zombieWoundBone(asset,capsule.bone,region)) && surface.bone >= 0) {
                if (!tested[r]) {
                    surfaces[r] = zombieWoundRay(asset,surface,wound,instance,pose,
                                                 origin,direction,surfaceDistances[r]);
                    tested[r] = true;
                }
                if (surfaces[r] && surfaceDistances[r] < distance) {
                    surfaceHit = true; distance = surfaceDistances[r];
                    headshot = region == ZombieWoundRegion::head; woundRegion = region; bone = surface.bone;
                }
                continue;
            }
            if (zombieCapsuleRay(origin, direction, instance.transform, capsule, pose, d) && d < distance) {
                surfaceHit = true; distance = d; headshot = capsule.headshot; woundRegion.reset(); bone = capsule.bone;
            }
        }
    } else {
        // Custom rigs retain the body proxy; prepared head surfaces honor openings.
        const auto center = glm::vec3(instance.restRoot * glm::vec4(0, .9f, 0, 1));
        const bool bodyHit = raySphere(origin, direction, center, .62f, distance);
        if (stats) stats->headVertices += geometry.headVertices.size();
        const auto head = zombieHeadHitSphere(asset, instance, pose, geometry);
        float headDistance = 0;
        const auto& headGeometry = woundGeometry_[instance.asset][std::size_t(ZombieWoundRegion::head)];
        const bool headHit = headGeometry.bone >= 0 ?
            zombieWoundRay(asset,headGeometry,wound,instance,pose,origin,direction,headDistance) :
            raySphere(origin,direction,head.center,head.radius,headDistance);
        headshot = headHit && (!bodyHit || headDistance <= distance);
        if (headHit) distance = bodyHit ? std::min(distance, headDistance) : headDistance;
        surfaceHit = bodyHit || headHit;
        if (headshot) {
            bone = geometry.head;
            if (headGeometry.bone >= 0) { woundRegion = ZombieWoundRegion::head; bone = headGeometry.bone; }
        }
    }
    int jointHit = -1;
    float jointDistance = std::numeric_limits<float>::max();
    for (std::size_t j = 0; j < geometry.joints.size(); ++j) {
        float d = 0;
        if (zombieJointRay(origin, direction, instance.transform, geometry.joints[j], pose, d) && d < jointDistance) {
            jointHit = static_cast<int>(j); jointDistance = d;
        }
    }
    // Joint regions retain precedence on this actor. Occlusion must end at
    // the actual joint collider, including when the surface is entered first.
    if (jointHit >= 0) { distance = jointDistance; bone = geometry.joints[std::size_t(jointHit)].bone; }
    if (!surfaceHit && jointHit < 0) return std::nullopt;
    return ZombieShotHit{id, distance, headshot, jointHit, jointHit < 0 ? woundRegion : std::nullopt};
}

std::optional<ZombieShotTrace> ZombieLayer::traceShot(std::size_t id, const ZombieInstance& displayed,
        glm::vec3 origin, glm::vec3 direction, double seconds) const {
    return traceShot(id, displayed, origin, direction, seconds, wounds_.find(id));
}

std::optional<ZombieShotTrace> ZombieLayer::traceShot(std::size_t id, const ZombieInstance& displayed,
        glm::vec3 origin, glm::vec3 direction, double seconds, const ZombieWound* displayedWound) const {
    const auto finite = [](glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (id >= instances.size() || id > UINT32_MAX || displayed.asset != instances[id].asset ||
        !finite(origin) || !finite(direction) || !std::isfinite(seconds)) return std::nullopt;
    const float length = glm::length(direction);
    if (!std::isfinite(length) || length < 1e-6f) return std::nullopt;
    direction /= length;
    ensureHitGeometry();
    if (displayed.asset >= hitGeometry_.size() ||
        !zombieRayIntersectsBounds(origin, direction, zombieShotBounds(displayed, hitGeometry_[displayed.asset])))
        return std::nullopt;
    std::vector<glm::mat4> pose;
    int bone = -1;
    const auto hit = traceShotInstance(id, displayed, origin, direction, seconds, pose, bone, displayedWound, nullptr);
    if (!hit || bone > INT16_MAX || hit->joint > INT16_MAX) return std::nullopt;
    const auto frame = bone < 0 ? glm::dmat4(displayed.restRoot) :
        glm::dmat4(displayed.transform) * glm::dmat4(pose.at(std::size_t(bone)));
    const auto determinant = glm::determinant(frame);
    if (!std::isfinite(determinant) || determinant == 0) return std::nullopt;
    const auto point = glm::vec3(glm::inverse(frame) * glm::dvec4(
        glm::dvec3(origin) + glm::dvec3(direction) * double(hit->distance), 1));
    if (!finite(point)) return std::nullopt;
    ZombieHitClaim claim;
    claim.zombie = static_cast<std::uint32_t>(id);
    claim.bone = static_cast<std::int16_t>(bone); claim.joint = static_cast<std::int16_t>(hit->joint);
    claim.headshot = hit->headshot; claim.woundRegion = hit->woundRegion; claim.point = point;
    return ZombieShotTrace{claim, hit->distance};
}

} // namespace viewer
