#include "zombie_update_profile.hpp"
#include "zombie_layer.hpp"
#include "zombie_ragdoll.hpp"
#include "zombie_ragdoll_pose.hpp"
#include "zombie_placement.hpp"
#include "zombie_animation.hpp"
#include "zombie_lod.hpp"
#include "zombie_navigation.hpp"
#include "zombie_cavity_physics.hpp"
#include "physics_world.hpp"
#include "simulation_clock.hpp"

#include "zombie_movement.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
bool modelFile(const std::filesystem::path& path) {
    const auto extension = path.extension();
    return std::filesystem::is_regular_file(path) && (extension == ".glb" || extension == ".gltf");
}

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

struct ZombieLayer::Physics {
    Physics() : Physics(std::make_shared<PhysicsWorld>()) {}
    explicit Physics(std::shared_ptr<PhysicsWorld> shared) : simulation(std::move(shared)), world(simulation), cavities(simulation) {}
    std::shared_ptr<PhysicsWorld> simulation;
    ZombieRagdollWorld world;
    ZombieCavityPhysics cavities;
};

ZombieLayer::ZombieLayer() = default;
ZombieLayer::~ZombieLayer() = default;
ZombieLayer::ZombieLayer(ZombieLayer&&) noexcept = default;
ZombieLayer& ZombieLayer::operator=(ZombieLayer&&) noexcept = default;

void ZombieLayer::enableNavigation(const std::filesystem::path& navmeshPath) {
    auto navigation = std::make_unique<ZombieNavigation>(navmeshPath, instances.size());
    enableNavigation(std::move(navigation));
}

void ZombieLayer::enableNavigation(std::unique_ptr<ZombieNavigation> navigation) {
    if (!navigation || navigation->capacity() != instances.size())
        throw std::invalid_argument("Zombie navigation must match the layer population");
    navigation_ = std::move(navigation);
}

void ZombieLayer::setScenarioDestinations(const std::vector<glm::vec3>& destinations) {
    if (!navigation_ || destinations.size() != instances.size())
        throw std::invalid_argument("Scenario destinations require navigation and one destination per zombie");
    setScenarioRoutes(std::vector<std::optional<glm::vec3>>(destinations.begin(), destinations.end()));
}

void ZombieLayer::setScenarioRoutes(const std::vector<std::optional<glm::vec3>>& destinations) {
    if (destinations.size() != instances.size() || (!navigation_ &&
            std::any_of(destinations.begin(), destinations.end(), [](const auto& goal) { return goal.has_value(); })))
        throw std::invalid_argument("Scenario routes require one optional destination per zombie and navigation for travel");
    ensureSpatialIndex();
    std::map<std::size_t, ScenarioRoute> routes;
    for (std::size_t id = 0; id < destinations.size(); ++id) {
        if (!destinations[id]) continue;
        auto& instance = instances[id];
        if (instance.removed || instance.ragdoll || instance.asset >= assets.size() ||
            !navigation_->plan(id, glm::vec3(instance.restRoot[3]), *destinations[id])) {
            for (const auto& [planned, route] : routes) {
                (void)route;
                navigation_->reset(planned);
            }
            navigation_->reset(id);
            throw std::runtime_error("Cannot preplan scenario route for zombie " + std::to_string(id));
        }
        routes.emplace(id, ScenarioRoute{*destinations[id], false, false, {}, {}, {}});
    }
    for (std::size_t id = 0; id < instances.size(); ++id) {
        if (!destinations[id]) {
            if (navigation_) navigation_->reset(id);
            continue;
        }
        auto& instance = instances[id];
        instance.chaseNormalization = glm::inverse(instance.restRoot) * instance.restTransform;
        behaviors.remove(id);
    }
    scenarioRoutes_ = std::move(routes);
    scenarioPopulation_ = instances.size();
}

void ZombieLayer::setDistantScenarioTravelEnabled(bool enabled) {
    distantScenarioTravel_=enabled;
    for (auto& [id, route] : scenarioRoutes_) {
        (void)id;
        route.travel = {};
    }
}

const glm::vec3* ZombieLayer::scenarioDestination(std::size_t id) const {
    if (scenarioPopulation_ != instances.size()) return nullptr;
    const auto found = scenarioRoutes_.find(id);
    return found != scenarioRoutes_.end() && !found->second.overridden ? &found->second.destination : nullptr;
}

bool ZombieLayer::scenarioFinished(std::size_t id) const {
    const auto found = scenarioRoutes_.find(id);
    return scenarioPopulation_ == instances.size() && found != scenarioRoutes_.end() &&
        !found->second.overridden && found->second.finished;
}

void ZombieLayer::queryLivingNear(glm::vec3 center, float radius, std::vector<std::size_t>& ids) {
    ids.clear();
    if (!finite(center) || !std::isfinite(radius) || radius < 0) return;
    ensureSpatialIndex();
    spatialIndex_.query(center, radius, ids);
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](std::size_t id) {
        const auto& instance = instances[id];
        const auto delta = glm::dvec3(instance.restRoot[3]) - glm::dvec3(center);
        return instance.ragdoll || instance.removed || glm::dot(delta, delta) > double(radius) * radius;
    }), ids.end());
}

void ZombieLayer::queryMeleeNear(glm::vec3 center, float radius, std::vector<std::size_t>& ids) {
    queryLivingNear(center, radius, ids);
    if (!finite(center) || !std::isfinite(radius) || radius < 0) return;
    std::vector<std::size_t> corpses;
    // Feet stay planted during spring simulation. This membership only needs
    // refreshing on activation, explicit owner translation, or removal.
    springRagdollIndex_.query(center, radius, corpses);
    ids.insert(ids.end(), corpses.begin(), corpses.end());
    std::sort(ids.begin(), ids.end());
}

void ZombieLayer::markRenderChanged(std::size_t id, bool bounds) {
    zombie_profile::Timer timer(zombie_profile::Stage::renderNotifications);
    if (id >= instances.size()) throw std::out_of_range("Invalid zombie render ID");
    if (bounds) updateReplicationMembership(id);
    renderUpdates_.mark(id, bounds);
}

void ZombieLayer::registerRagdoll(std::size_t zombie) {
    const auto inserted = ragdollSlots_.emplace(zombie, ragdollIds_.size());
    if (!inserted.second) return;
    try {
        ragdollIds_.push_back(zombie);
    } catch (...) {
        ragdollSlots_.erase(inserted.first);
        throw;
    }
}

void ZombieLayer::unregisterRagdoll(std::size_t zombie) {
    const auto found = ragdollSlots_.find(zombie);
    if (found == ragdollSlots_.end()) return;
    const auto slot = found->second;
    const auto moved = ragdollIds_.back();
    ragdollIds_[slot] = moved;
    ragdollSlots_.at(moved) = slot;
    ragdollIds_.pop_back();
    ragdollSlots_.erase(found);
}

void ZombieLayer::rebuildRagdollIndex() {
    // Bulk construction/replacement may edit public instance fields directly.
    // Publish a complete replacement only after all allocations succeed.
    std::vector<std::size_t> ids;
    std::unordered_map<std::size_t, std::size_t> slots;
    for (std::size_t id = 0; id < instances.size(); ++id) {
        if (!instances[id].ragdoll || instances[id].removed) continue;
        slots.emplace(id, ids.size());
        ids.push_back(id);
    }
    ragdollIds_.swap(ids);
    ragdollSlots_.swap(slots);
}

bool ZombieLayer::shoot(glm::vec3 origin, glm::vec3 direction, double animationSeconds, ShotCaliber caliber) {
    return shootImpl(origin, direction, animationSeconds, nullptr, caliber);
}
bool ZombieLayer::shootWithOcclusion(glm::vec3 origin, glm::vec3 direction, double animationSeconds,
                                    const CollisionWorld& world, ShotCaliber caliber) {
    return shootImpl(origin, direction, animationSeconds, &world, caliber);
}
bool ZombieLayer::shootImpl(glm::vec3 origin, glm::vec3 direction, double animationSeconds,
                           const CollisionWorld* occlusion, ShotCaliber caliber) {
    shootingStats_ = {};
    if (!finite(origin) || !finite(direction)) return false;
    const float length = glm::length(direction);
    if (length < 1e-6f) return false;
    direction /= length;
    std::vector<glm::mat4> selectedPose;
    const auto hit = findShotTarget(origin, direction, animationSeconds, selectedPose);
    if (!hit) return false;
    if (occlusion && !occlusion->lineOfSight(origin, origin + direction * hit->distance)) return false;
    return applyShotHit(*hit, std::move(selectedPose), origin + direction * hit->distance,
                        direction, animationSeconds, occlusion, caliber);
}

bool ZombieLayer::applyReportedShot(const ZombieHitClaim& claim, glm::vec3 origin, glm::vec3 direction,
                                    double seconds, const CollisionWorld& world, ShotCaliber caliber,
                                    std::uint32_t excludedMistObserver) {
    if (claim.zombie >= instances.size() || !finite(origin) || !finite(direction) || !finite(claim.point) ||
        !std::isfinite(seconds) || claim.bone < -1 || claim.joint < -1) return false;
    const float length = glm::length(direction);
    if (!std::isfinite(length) || length < 1e-6f) return false;
    direction /= length;
    const auto& instance = instances[claim.zombie];
    if (instance.removed || instance.ragdoll || instance.asset >= assets.size()) return false;
    const auto& asset = assets[instance.asset];
    if (claim.bone >= 0 && std::size_t(claim.bone) >= asset.skeleton.size()) return false;
    ensureHitGeometry();
    const auto& geometry = hitGeometry_[instance.asset];
    if (claim.joint >= 0 && (std::size_t(claim.joint) >= geometry.joints.size() ||
            claim.bone != geometry.joints[std::size_t(claim.joint)].bone || claim.woundRegion)) return false;
    if (claim.woundRegion) {
        const auto region = std::size_t(*claim.woundRegion);
        if (region >= woundGeometry_[instance.asset].size()) return false;
        const auto& wound = woundGeometry_[instance.asset][region];
        if (wound.bone < 0 || claim.bone != wound.bone ||
            claim.headshot != (*claim.woundRegion == ZombieWoundRegion::head)) return false;
    }
    auto pose = sampleZombiePose(asset, instance, seconds);
    const auto frame = claim.bone < 0 ? glm::dmat4(instance.restRoot) :
        glm::dmat4(instance.transform) * glm::dmat4(pose.at(std::size_t(claim.bone)));
    const auto point = glm::vec3(frame * glm::dvec4(claim.point, 1));
    if (!finite(point)) return false;
    // This checks actor-local plausibility only. The trusted intersection need
    // not still lie on the old firing ray after the zombie has moved or turned.
    const auto bounds = zombieShotBounds(instance, geometry);
    constexpr float margin = .1f;
    if (glm::any(glm::lessThan(point, bounds.minimum - glm::vec3(margin))) ||
        glm::any(glm::greaterThan(point, bounds.maximum + glm::vec3(margin)))) return false;
    const float distance = glm::length(point - origin);
    if (!std::isfinite(distance)) return false;
    const ZombieShotHit hit{claim.zombie, distance, claim.headshot, claim.joint, claim.woundRegion};
    return applyShotHit(hit, std::move(pose), point, direction, seconds, &world, caliber, excludedMistObserver);
}

bool ZombieLayer::applyShotHit(const ZombieShotHit& hit, std::vector<glm::mat4> selectedPose,
                               glm::vec3 point, glm::vec3 direction, double animationSeconds,
                               const CollisionWorld* occlusion, ShotCaliber caliber, std::uint32_t excludedMistObserver) {
    bool openedWound = false;
    if (hit.woundRegion && (*hit.woundRegion != ZombieWoundRegion::head || caliber == ShotCaliber::high)) {
        const auto& instance = instances[hit.zombie];
        const auto& asset = assets[instance.asset];
        const auto& geometry = woundGeometry_[instance.asset][std::size_t(*hit.woundRegion)];
        if (wounds_.hit(hit.zombie, asset, geometry, instance, selectedPose, point)) {
            openedWound = true;
            if (!physics_) physics_ = std::make_unique<Physics>();
            for (const auto& region : woundGeometry_[instance.asset]) {
                if (region.region != geometry.region && !physics_->cavities.has(hit.zombie,region.region)) continue;
                const auto frame = instance.transform * selectedPose[region.bone] *
                    glm::inverse(asset.skeleton[region.bone].referenceWorld) * region.referenceFrame;
                physics_->cavities.wound(hit.zombie, asset, region, *wounds_.find(hit.zombie), frame, instance.locomotionVelocity);
            }
            markRenderChanged(hit.zombie, false);
        }
    }
    if (hit.joint >= 0)
        activateRagdoll(hit.zombie, std::move(selectedPose), direction * 4.0f, animationSeconds,
                        hit.joint, caliber);
    else if (damage_.hit(hit.zombie, caliber, hit.headshot) == 0)
        activateRagdoll(hit.zombie, std::move(selectedPose), direction * 4.0f, animationSeconds);
    const auto mistSize = openedWound ? BloodMistSize::large : hit.joint >= 0 ? BloodMistSize::medium : BloodMistSize::small;
    // Offset slightly toward the incoming bullet so the billboard clears the surface.
    bloodMist_.emit(point-direction*std::min(.025f,hit.distance),mistSize,animationSeconds,excludedMistObserver);
    emitBloodParticles(hit,point,direction,animationSeconds,occlusion);
    return true;
}

std::size_t ZombieLayer::explode(glm::vec3 origin, float radius, double animationSeconds, const CollisionWorld& world) {
    if (!finite(origin) || !std::isfinite(radius) || radius <= 0) return 0;
    std::vector<std::size_t> ids;
    queryLivingNear(origin, radius + 2, ids);
    std::size_t killed = 0;
    for (const auto id : ids) {
        const auto& instance = instances[id];
        const auto center = glm::vec3(instance.restRoot * glm::vec4(0, .9f, 0, 1));
        const auto offset = center - origin;
        const float distance = glm::length(offset);
        if (distance >= radius || !world.lineOfSight(origin, center)) continue;
        auto pose = sampleZombiePose(assets.at(instance.asset), instance, animationSeconds);
        const auto outward = offset + glm::vec3(0, .5f, 0);
        const auto direction = glm::length(outward) > .001f ? glm::normalize(outward) : glm::vec3(0, 1, 0);
        activateRagdoll(id, std::move(pose), direction * (8.0f * (1 - distance / radius)), animationSeconds);
        ++killed;
    }
    return killed;
}
bool ZombieLayer::knockDown(std::size_t id, glm::vec3 velocity, double seconds) {
    ensureSpatialIndex();
    if (id>=instances.size() || !finite(velocity)) return false;
    const auto& instance=instances[id];
    if (instance.ragdoll || instance.removed || instance.asset>=assets.size()) return false;
    activateRagdoll(id,sampleZombiePose(assets[instance.asset],instance,seconds),velocity,seconds);
    return true;
}

bool ZombieLayer::meleeHit(std::size_t id, glm::vec3 impulse, glm::vec3 point, double seconds) {
    if (!finite(impulse) || !finite(point)) return false;
    ensureSpatialIndex();
    if (id >= instances.size()) return false;
    auto& instance = instances[id];
    if (instance.removed || instance.asset >= assets.size()) return false;
    if (instance.ragdoll) {
        if (!instance.springRagdoll || !instance.physicsRagdoll || !physics_) return false;
    } else {
        if (!knockDown(id, {}, seconds)) return false;
        physics_->world.holdPose(instance.ragdollHandle);
        instance.springRagdoll = true;
        updateSpatialMembership(id);
    }
    const auto handle = instances[id].ragdollHandle;
    const auto transforms = physics_->world.bodyTransforms(handle);
    std::size_t nearest = 0;
    for (std::size_t i = 1; i < transforms.size(); ++i)
        if (glm::distance(glm::vec3(transforms[i][3]), point) < glm::distance(glm::vec3(transforms[nearest][3]), point)) nearest = i;
    physics_->world.applyImpulse(handle, static_cast<RagdollPart>(nearest), impulse);
    return true;
}

glm::vec3 ZombieLayer::pushLiving(std::size_t id, glm::vec3 displacement, const CollisionWorld& world) {
    ensureSpatialIndex();
    if (id>=instances.size() || !finite(displacement)) return glm::vec3(0);
    auto& instance=instances[id];
    if (instance.ragdoll || instance.removed || instance.asset>=assets.size()) return glm::vec3(0);
    const auto start=glm::vec3(instance.restRoot[3]);
    const auto movement=moveZombieBody(start,start+displacement,world,&crowd_,id);
    const auto delta=movement.feet-start;
    if (delta!=glm::vec3(0)) {
        const auto translation=glm::translate(glm::mat4(1),delta);
        instance.restRoot=translation*instance.restRoot;
        instance.restTransform=translation*instance.restTransform;
        instance.transform=translation*instance.transform;
        updateSpatialMembership(id); markRenderChanged(id);
    }
    return delta;
}
void ZombieLayer::activateRagdoll(std::size_t selected, std::vector<glm::mat4> selectedPose, glm::vec3 velocity,
                                double animationSeconds, int severedJoint, ShotCaliber caliber) {
    if (!physics_) physics_ = std::make_unique<Physics>();
    auto& instance = instances[selected];
    if (navigation_) navigation_->reset(selected);
    const auto feet = glm::vec3(instance.restRoot[3]);
    const float yaw = std::atan2(instance.restRoot[2][0], instance.restRoot[0][0]);
    const auto& asset = assets[instance.asset];
    constexpr float motionSampleSeconds = 1.0f / 30;
    const auto previousPose = sampleZombiePose(asset, instance, animationSeconds, -motionSampleSeconds);
    instance.activationPose = std::move(selectedPose);
    RagdollPose reference, current;
    const bool mappedRig = makeZombieRagdollPoses(instance, asset, reference, current);
    if (mappedRig)
        instance.ragdollHandle = physics_->world.create(feet, yaw, velocity, reference, current);
    else
        instance.ragdollHandle = physics_->world.create(feet, yaw, velocity);
    try {
        registerRagdoll(selected);
    } catch (...) {
        physics_->world.destroy(instance.ragdollHandle);
        throw;
    }
    if (instance.locomotionVelocity != glm::vec3(0))
        for (std::size_t part = 0; part < ragdollPartCount; ++part)
            physics_->world.addLinearVelocity(instance.ragdollHandle, static_cast<RagdollPart>(part), instance.locomotionVelocity);
    if (mappedRig) inheritZombieRagdollMotion(instance, asset, previousPose, motionSampleSeconds, physics_->world);
    instance.ragdoll = true;
    instance.severedJoint = severedJoint;
    if (severedJoint >= 0) {
        const auto part = dismembermentAssets.at(instance.asset).joints.at(std::size_t(severedJoint)).part;
        const auto bodies = physics_->world.bodyTransforms(instance.ragdollHandle);
        const auto outward = glm::vec3(bodies[std::size_t(part)][3] - bodies[std::size_t(RagdollPart::pelvis)][3]);
        const auto severImpulse = zombieSeverImpulse(caliber, glm::normalize(velocity), part, outward);
        physics_->world.sever(instance.ragdollHandle, part);
        physics_->world.applyImpulse(instance.ragdollHandle, part, severImpulse);
        physics_->world.addAngularVelocity(instance.ragdollHandle, part, zombieSeverSpin());
    }
    damage_.remove(selected);
    instance.physicsRagdoll = true;
    spatialIndex_.erase(selected);
    crowd_.erase(selected);
    behaviors.remove(selected);
    instance.walking = false;
    instance.locomotionVelocity = glm::vec3(0);
    instance.transform = physics_->world.rootTransform(instance.ragdollHandle) *
        glm::inverse(instance.restRoot) * instance.restTransform;
    updateZombieRagdollSkin(instance, assets[instance.asset], physics_->world);
    instance.physicsPoseRevision = physics_->world.poseRevision(instance.ragdollHandle);
    ++instance.ragdollPoseVersion;
    markRenderChanged(selected);
}

void ZombieLayer::attachPhysics(std::shared_ptr<PhysicsWorld> world) {
    if (ragdollCount() != 0) throw std::logic_error("Attach physics before creating ragdolls");
    auto replacement = std::make_unique<Physics>(std::move(world));
    physics_ = std::move(replacement);
}

void ZombieLayer::update(float seconds) {
    if (!physics_ || !std::isfinite(seconds) || seconds <= 0.0f) return;
    prepareWoundPhysics(seconds, SimulationClock::instance().seconds());
    physics_->world.step(seconds);
    synchronizePhysics();
}

void ZombieLayer::prepareWoundPhysics(float seconds, double animationSeconds, PhysicsScene* scene) {
    if (!physics_) return;
    for (const auto& [id,region] : physics_->cavities.ids()) {
        const auto& instance = instances.at(id);
        if (instance.removed) continue;
        const auto& asset = assets.at(instance.asset);
        const auto& geometry = woundGeometry_.at(instance.asset)[std::size_t(region)];
        const auto bone = std::size_t(geometry.bone);
        const auto pose = instance.ragdoll ? instance.ragdollBones[bone] * asset.skeleton[bone].referenceWorld :
            sampleZombiePose(asset, instance, animationSeconds)[bone];
        const auto frame = instance.transform * pose * glm::inverse(asset.skeleton[bone].referenceWorld) * geometry.referenceFrame;
        physics_->cavities.move(id, region, frame, seconds);
    }
    physics_->cavities.prepare(scene);
}

std::vector<glm::mat4> ZombieLayer::woundContents() const {
    if (replicatedWoundContents_) return *replicatedWoundContents_;
    return physics_ ? physics_->cavities.contents() : std::vector<glm::mat4>{};
}

std::vector<glm::mat4> ZombieLayer::woundContents(const std::vector<std::size_t>& ids,
        std::size_t maxCount) const {
    if (replicatedWoundContents_) {
        const auto count = std::min(maxCount, replicatedWoundContents_->size());
        return {replicatedWoundContents_->begin(), replicatedWoundContents_->begin() + count};
    }
    return physics_ ? physics_->cavities.contentsForIds(ids, maxCount) : std::vector<glm::mat4>{};
}

void ZombieLayer::setWoundMask(std::size_t id, const std::vector<std::uint8_t>& mask) {
    // Applying replicated appearance must not initialize the authoritative
    // crowd, shooting and navigation indexes for every hidden client slot.
    if (woundGeometry_.size() != assets.size()) {
        std::vector<ZombieWoundRegions> prepared;
        prepared.reserve(assets.size());
        for (const auto& model : assets)
            prepared.push_back({prepareZombieWoundRegion(model, ZombieWoundRegion::torso),
                                prepareZombieWoundRegion(model, ZombieWoundRegion::head)});
        woundGeometry_ = std::move(prepared);
    }
    const auto asset = instances.at(id).asset;
    const auto& regions = woundGeometry_.at(asset);
    const auto& geometry = regions[0].referenceVertices.empty() ? regions[1] : regions[0];
    if (wounds_.applyMask(id,assets.at(asset),geometry,mask)) markRenderChanged(id,false);
}
void ZombieLayer::setReplicatedWoundContents(std::vector<glm::mat4> transforms) {
    replicatedWoundContents_ = std::move(transforms);
}

void ZombieLayer::setRagdollCleanupHeight(float cutoffY) {
    if (!std::isfinite(cutoffY)) throw std::invalid_argument("Ragdoll cleanup height must be finite");
    ragdollCleanupY_ = cutoffY;
    if (!physics_) return;
    physics_->cavities.removeContentsBelow(cutoffY);
    // Configuration can also change after corpses have settled. Check those
    // once without invalidating their unchanged physics/skin revisions.
    for (std::size_t slot = 0; slot < ragdollIds_.size();) {
        const auto id = ragdollIds_[slot];
        const auto& instance = instances[id];
        if (instance.physicsRagdoll && physics_->world.hasBodyBelow(instance.ragdollHandle, cutoffY))
            removeRagdoll(id);
        else ++slot;
    }
}

void ZombieLayer::synchronizePhysics() {
    if (!physics_) return;
    // Loose contents move independently, even when their owning corpse sleeps.
    if (ragdollCleanupY_) physics_->cavities.removeContentsBelow(*ragdollCleanupY_);
    for (std::size_t slot = 0; slot < ragdollIds_.size();) {
        const auto id = ragdollIds_[slot];
        synchronizePhysics(id);
        // Removal swaps the last corpse into this slot; process that owner too.
        if (slot < ragdollIds_.size() && ragdollIds_[slot] == id) ++slot;
    }
}

void ZombieLayer::synchronizePhysics(std::size_t id) {
    if (!physics_ || id >= instances.size()) return;
    auto& instance = instances[id];
    if (instance.removed || !instance.ragdoll || !instance.physicsRagdoll) return;
    const auto revision = physics_->world.poseRevision(instance.ragdollHandle);
    const bool sleeping = physics_->world.sleeping(instance.ragdollHandle);
    if (revision == instance.physicsPoseRevision && sleeping == instance.ragdollSleeping) return;
    instance.ragdollSleeping = sleeping;
    if (ragdollCleanupY_ && physics_->world.hasBodyBelow(instance.ragdollHandle, *ragdollCleanupY_)) {
        removeRagdoll(id);
        return;
    }
    instance.transform = physics_->world.rootTransform(instance.ragdollHandle) *
        glm::inverse(instance.restRoot) * instance.restTransform;
    if (instance.asset < assets.size()) updateZombieRagdollSkin(instance, assets[instance.asset], physics_->world);
    instance.physicsPoseRevision = revision;
    ++instance.ragdollPoseVersion;
    markRenderChanged(id);
}

void ZombieLayer::translateRagdoll(std::size_t zombie, glm::vec3 displacement) {
    const auto& instance = instances.at(zombie);
    if (instance.removed || !instance.ragdoll || !instance.physicsRagdoll || !physics_)
        throw std::logic_error("Translation requires a physics-owned ragdoll");
    physics_->world.translate(instance.ragdollHandle, displacement);
    synchronizePhysics();
    updateSpatialMembership(zombie);
}
void ZombieLayer::applyRagdollImpulse(std::size_t zombie, RagdollPart part, glm::vec3 impulse) {
    const auto& instance = instances.at(zombie);
    if (instance.removed || !instance.ragdoll || !instance.physicsRagdoll || !physics_)
        throw std::logic_error("Impulse requires a physics-owned ragdoll");
    physics_->world.applyImpulse(instance.ragdollHandle, part, impulse);
}
void ZombieLayer::removeRagdoll(std::size_t zombie) {
    auto& instance = instances.at(zombie);
    if (!instance.ragdoll || instance.removed) return;
    if (navigation_) navigation_->reset(zombie);
    if (instance.physicsRagdoll && physics_) physics_->world.destroy(instance.ragdollHandle);
    if (physics_) physics_->cavities.remove(zombie);
    wounds_.remove(zombie);
    unregisterRagdoll(zombie);
    instance.removed = true;
    instance.ragdoll = instance.physicsRagdoll = instance.springRagdoll = instance.walking = false;
    instance.ragdollBones.clear();
    instance.activationPose.clear();
    ++instance.ragdollPoseVersion;
    spatialIndex_.erase(zombie);
    springRagdollIndex_.erase(zombie);
    behaviors.remove(zombie);
    markRenderChanged(zombie);
}
void ZombieLayer::setRagdollPalette(std::size_t zombie, std::vector<glm::mat4> palette) {
    auto& instance = instances.at(zombie);
    if (instance.removed || instance.physicsRagdoll) throw std::logic_error("Palette is owned by physics or removed");
    if (instance.asset >= assets.size() || palette.size() != assets[instance.asset].skeleton.size())
        throw std::invalid_argument("Palette does not match the zombie skeleton");
    for (const auto& matrix : palette)
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(matrix[column][row])) throw std::invalid_argument("Palette must be finite");
    registerRagdoll(zombie);
    if (navigation_) navigation_->reset(zombie);
    instance.ragdollBones = std::move(palette);
    instance.ragdoll = true;
    instance.walking = false;
    ++instance.ragdollPoseVersion;
    spatialIndex_.erase(zombie);
    behaviors.remove(zombie);
    markRenderChanged(zombie);
}

std::size_t ZombieLayer::ragdollCount() const {
    return ragdollIds_.size();
}

bool hasZombieModels(const std::filesystem::path& source) {
    if (std::filesystem::is_directory(source)) {
        for (const auto& entry : std::filesystem::directory_iterator(source))
            if (modelFile(entry.path())) return true;
        return false;
    }
    return modelFile(source);
}

ZombieLayer loadZombieLayer(const std::filesystem::path& source,
                           const CollisionWorld& world, const Model& scene,
                           glm::vec2 center, std::size_t count, float radius,
                           const std::vector<glm::vec2>& boundary) {
    if (count == 0) return {};
    const auto placements = boundary.empty() ? placeZombies(world, scene, center, count, radius) :
        placeZombiesInRegion(world, scene, boundary, count);
    return loadZombieLayer(source, placements);
}

ZombieLayer loadZombieLayer(const std::filesystem::path& source,
                           const std::vector<glm::mat4>& placements, bool replica) {
    ZombieLayer result;
    const auto count = placements.size();
    if (count == 0) return result;
    std::vector<std::filesystem::path> paths;
    if (std::filesystem::is_directory(source)) {
        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            if (modelFile(entry.path())) paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());
    } else if (std::filesystem::is_regular_file(source)) paths.push_back(source);
    if (paths.empty()) throw std::runtime_error("No zombie GLB/glTF files found in " + source.string());

    std::vector<glm::mat4> normalization;
    // There is no reason to upload character variants that receive no instances.
    paths.resize(std::min(paths.size(), count));
    for (const auto& path : paths) {
        auto asset = loadZombieModel(path);
        result.dismembermentAssets.push_back(loadZombieDismemberment(path, asset));
        result.capsuleAssets.push_back(loadZombieHitCapsules(path, asset));
        const auto low = asset.model.boundsMin, high = asset.model.boundsMax;
        const float height = high.y - low.y;
        if (!std::isfinite(height) || height <= 0)
            throw std::runtime_error("Zombie asset has no positive height: " + path.string());
        const float scale = 1.8f / height;
        const auto origin = glm::vec3((low.x + high.x) * .5f, low.y, (low.z + high.z) * .5f);
        normalization.push_back(glm::scale(glm::mat4(1), glm::vec3(scale)) *
                                glm::translate(glm::mat4(1), -origin));
        std::cout << "Zombie asset: " << path.filename() << "; idle clip " << asset.clipName
                  << ", " << asset.duration << " s, " << asset.frameCount << " samples\n";
        for (const auto* clip : {asset.walkClip ? &*asset.walkClip : nullptr, asset.runClip ? &*asset.runClip : nullptr})
            if (clip) std::cout << "  Locomotion: " << clip->name << ", " << clip->duration
                               << " s, " << clip->frameCount << " samples\n";
        for (const auto& warning : asset.model.warnings)
            std::cerr << "Zombie asset warning: " << warning << '\n';
        result.lodAssets.push_back(loadZombieLods(path, asset));
        std::cout << "  Reduced mesh levels: " << result.lodAssets.back().size() << '\n';
        result.assets.push_back(std::move(asset));
    }
    result.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    result.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    result.instances.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto asset = i % result.assets.size();
        const auto restRoot = placements[i];
        const auto transform = restRoot * normalization[asset];
        const float phase = static_cast<float>(std::fmod(static_cast<double>(i) * 0.61803398875, 1.0));
        ZombieInstance instance;
        instance.asset = asset;
        instance.transform = transform;
        instance.restTransform = transform;
        instance.restRoot = restRoot;
        instance.phase = phase;
        instance.removed = replica;
        result.instances.push_back(std::move(instance));
        const auto& model = result.assets[asset].model;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p(corner & 1 ? model.boundsMax.x : model.boundsMin.x,
                              corner & 2 ? model.boundsMax.y : model.boundsMin.y,
                              corner & 4 ? model.boundsMax.z : model.boundsMin.z);
            const glm::vec3 worldPoint(transform * glm::vec4(p, 1));
            result.boundsMin = glm::min(result.boundsMin, worldPoint);
            result.boundsMax = glm::max(result.boundsMax, worldPoint);
        }
    }
    if (replica) result.invalidateRenderData();
    else result.rebuildSpatialIndex();
    return result;
}

} // namespace viewer
