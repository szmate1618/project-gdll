#include "zombie_layer.hpp"
#include "zombie_ragdoll.hpp"
#include "zombie_ragdoll_pose.hpp"
#include "zombie_placement.hpp"
#include "zombie_animation.hpp"
#include "zombie_lod.hpp"
#include "zombie_navigation.hpp"
#include "zombie_torso_physics.hpp"
#include "physics_world.hpp"
#include "simulation_clock.hpp"

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
    explicit Physics(std::shared_ptr<PhysicsWorld> shared) : simulation(std::move(shared)), world(simulation), torsos(simulation) {}
    std::shared_ptr<PhysicsWorld> simulation;
    ZombieRagdollWorld world;
    ZombieTorsoPhysics torsos;
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
    ensureSpatialIndex();
    std::vector<ScenarioRoute> routes;
    routes.reserve(destinations.size());
    for (std::size_t id = 0; id < destinations.size(); ++id) {
        auto& instance = instances[id];
        if (instance.removed || instance.ragdoll || instance.asset >= assets.size() ||
            !navigation_->plan(id, glm::vec3(instance.restRoot[3]), destinations[id])) {
            for (std::size_t planned = 0; planned <= id; ++planned) navigation_->reset(planned);
            throw std::runtime_error("Cannot preplan scenario route for zombie " + std::to_string(id));
        }
        routes.push_back({destinations[id], false, false, {}});
    }
    for (std::size_t id = 0; id < instances.size(); ++id) {
        auto& instance = instances[id];
        instance.chaseNormalization = glm::inverse(instance.restRoot) * instance.restTransform;
        behaviors.remove(id);
    }
    scenarioRoutes_ = std::move(routes);
}

const glm::vec3* ZombieLayer::scenarioDestination(std::size_t id) const {
    return scenarioRoutes_.size() == instances.size() && id < scenarioRoutes_.size() && !scenarioRoutes_[id].overridden
        ? &scenarioRoutes_[id].destination : nullptr;
}

bool ZombieLayer::scenarioFinished(std::size_t id) const {
    return scenarioDestination(id) && scenarioRoutes_[id].finished;
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

void ZombieLayer::markRenderChanged(std::size_t id, bool bounds) {
    if (id >= instances.size()) throw std::out_of_range("Invalid zombie render ID");
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
    bool openedTorso = false;
    if (hit->torso) {
        const auto& instance = instances[hit->zombie];
        const auto& asset = assets[instance.asset];
        const auto& geometry = torsoGeometry_[instance.asset];
        if (wounds_.hit(hit->zombie, asset, geometry, instance, selectedPose, origin + direction * hit->distance)) {
            openedTorso = true;
            if (!physics_) physics_ = std::make_unique<Physics>();
            const auto frame = instance.transform * selectedPose[geometry.bone] *
                glm::inverse(asset.skeleton[geometry.bone].referenceWorld) * geometry.referenceFrame;
            physics_->torsos.wound(hit->zombie, asset, geometry, *wounds_.find(hit->zombie), frame, instance.locomotionVelocity);
            markRenderChanged(hit->zombie, false);
        }
    }
    if (hit->joint >= 0)
        activateRagdoll(hit->zombie, std::move(selectedPose), direction * 4.0f, animationSeconds,
                        hit->joint, caliber);
    else if (damage_.hit(hit->zombie, caliber, hit->headshot) == 0)
        activateRagdoll(hit->zombie, std::move(selectedPose), direction * 4.0f, animationSeconds);
    const auto mistSize = openedTorso ? BloodMistSize::large : hit->joint >= 0 ? BloodMistSize::medium : BloodMistSize::small;
    // Offset slightly toward the incoming bullet so the billboard clears the surface.
    bloodMist_.emit(origin+direction*std::max(0.0f,hit->distance-.025f),mistSize,animationSeconds);
    emitBloodParticles(*hit,origin+direction*hit->distance,direction,animationSeconds,occlusion);
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
    physics_ = std::make_unique<Physics>(std::move(world));
}

void ZombieLayer::update(float seconds) {
    if (!physics_ || !std::isfinite(seconds) || seconds <= 0.0f) return;
    prepareTorsoPhysics(seconds, SimulationClock::instance().seconds());
    physics_->world.step(seconds);
    synchronizePhysics();
}

void ZombieLayer::prepareTorsoPhysics(float seconds, double animationSeconds, PhysicsScene* scene) {
    if (!physics_) return;
    for (const auto id : physics_->torsos.ids()) {
        const auto& instance = instances.at(id);
        if (instance.removed) continue;
        const auto& asset = assets.at(instance.asset);
        const auto& geometry = torsoGeometry_.at(instance.asset);
        const auto bone = std::size_t(geometry.bone);
        const auto pose = instance.ragdoll ? instance.ragdollBones[bone] * asset.skeleton[bone].referenceWorld :
            sampleZombiePose(asset, instance, animationSeconds)[bone];
        const auto frame = instance.transform * pose * glm::inverse(asset.skeleton[bone].referenceWorld) * geometry.referenceFrame;
        physics_->torsos.move(id, frame, seconds);
    }
    physics_->torsos.prepare(scene);
}

std::vector<glm::mat4> ZombieLayer::torsoContents() const {
    if (replicatedTorsoContents_) return *replicatedTorsoContents_;
    return physics_ ? physics_->torsos.contents() : std::vector<glm::mat4>{};
}

void ZombieLayer::setTorsoWoundMask(std::size_t id, const std::vector<std::uint8_t>& mask) {
    ensureSpatialIndex();
    const auto asset = instances.at(id).asset;
    if (wounds_.applyMask(id, assets.at(asset), torsoGeometry_.at(asset), mask)) markRenderChanged(id, false);
}
void ZombieLayer::setReplicatedTorsoContents(std::vector<glm::mat4> transforms) {
    replicatedTorsoContents_ = std::move(transforms);
}

void ZombieLayer::synchronizePhysics() {
    if (!physics_) return;
    for (const auto id : ragdollIds_) {
        auto& instance = instances[id];
        if (instance.removed || !instance.ragdoll || !instance.physicsRagdoll) continue;
        const auto revision = physics_->world.poseRevision(instance.ragdollHandle);
        if (revision == instance.physicsPoseRevision) continue;
        instance.transform = physics_->world.rootTransform(instance.ragdollHandle) *
            glm::inverse(instance.restRoot) * instance.restTransform;
        if (instance.asset < assets.size()) updateZombieRagdollSkin(instance, assets[instance.asset], physics_->world);
        instance.physicsPoseRevision = revision;
        ++instance.ragdollPoseVersion;
        markRenderChanged(id);
    }
}

void ZombieLayer::translateRagdoll(std::size_t zombie, glm::vec3 displacement) {
    const auto& instance = instances.at(zombie);
    if (instance.removed || !instance.ragdoll || !instance.physicsRagdoll || !physics_)
        throw std::logic_error("Translation requires a physics-owned ragdoll");
    physics_->world.translate(instance.ragdollHandle, displacement);
    synchronizePhysics();
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
    if (physics_) physics_->torsos.remove(zombie);
    wounds_.remove(zombie);
    unregisterRagdoll(zombie);
    instance.removed = true;
    instance.ragdoll = instance.physicsRagdoll = instance.walking = false;
    instance.ragdollBones.clear();
    instance.activationPose.clear();
    ++instance.ragdollPoseVersion;
    spatialIndex_.erase(zombie);
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
                           const std::vector<glm::mat4>& placements) {
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
    result.rebuildSpatialIndex();
    return result;
}

} // namespace viewer
