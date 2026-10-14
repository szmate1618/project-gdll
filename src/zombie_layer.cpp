#include "zombie_layer.hpp"
#include "zombie_ragdoll.hpp"
#include "zombie_ragdoll_pose.hpp"
#include "zombie_placement.hpp"
#include "zombie_animation.hpp"
#include "zombie_lod.hpp"

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
    Physics() = default;
    explicit Physics(std::shared_ptr<PhysicsWorld> shared) : world(std::move(shared)) {}
    ZombieRagdollWorld world;
};

ZombieLayer::ZombieLayer() = default;
ZombieLayer::~ZombieLayer() = default;
ZombieLayer::ZombieLayer(ZombieLayer&&) noexcept = default;
ZombieLayer& ZombieLayer::operator=(ZombieLayer&&) noexcept = default;

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

bool ZombieLayer::shoot(glm::vec3 origin, glm::vec3 direction, double animationSeconds) {
    shootingStats_ = {};
    if (!finite(origin) || !finite(direction)) return false;
    const float length = glm::length(direction);
    if (length < 1e-6f) return false;
    direction /= length;
    std::vector<glm::mat4> selectedPose;
    const auto selected = findShotTarget(origin, direction, animationSeconds, selectedPose);
    if (selected == instances.size()) return false;
    if (!physics_) physics_ = std::make_unique<Physics>();
    auto& instance = instances[selected];
    const auto feet = glm::vec3(instance.restRoot[3]);
    const float yaw = std::atan2(instance.restRoot[2][0], instance.restRoot[0][0]);
    const auto& asset = assets[instance.asset];
    instance.activationPose = std::move(selectedPose);
    RagdollPose reference, current;
    if (makeZombieRagdollPoses(instance, asset, reference, current))
        instance.ragdollHandle = physics_->world.create(feet, yaw, direction * 4.0f, reference, current);
    else
        instance.ragdollHandle = physics_->world.create(feet, yaw, direction * 4.0f);
    try {
        registerRagdoll(selected);
    } catch (...) {
        physics_->world.destroy(instance.ragdollHandle);
        throw;
    }
    instance.ragdoll = true;
    instance.physicsRagdoll = true;
    spatialIndex_.erase(selected);
    behaviors.remove(selected);
    instance.walking = false;
    instance.transform = physics_->world.rootTransform(instance.ragdollHandle) *
        glm::inverse(instance.restRoot) * instance.restTransform;
    updateZombieRagdollSkin(instance, assets[instance.asset], physics_->world);
    instance.physicsPoseRevision = physics_->world.poseRevision(instance.ragdollHandle);
    ++instance.ragdollPoseVersion;
    markRenderChanged(selected);
    return true;
}

void ZombieLayer::attachPhysics(std::shared_ptr<PhysicsWorld> world) {
    if (ragdollCount() != 0) throw std::logic_error("Attach physics before creating ragdolls");
    physics_ = std::make_unique<Physics>(std::move(world));
}

void ZombieLayer::update(float seconds) {
    if (!physics_ || !std::isfinite(seconds) || seconds <= 0.0f) return;
    physics_->world.step(seconds);
    synchronizePhysics();
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
    if (instance.physicsRagdoll && physics_) physics_->world.destroy(instance.ragdollHandle);
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
    ZombieLayer result;
    if (count == 0) return result;
    std::vector<std::filesystem::path> paths;
    if (std::filesystem::is_directory(source)) {
        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            if (modelFile(entry.path())) paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());
    } else if (std::filesystem::is_regular_file(source)) paths.push_back(source);
    if (paths.empty()) throw std::runtime_error("No zombie GLB/glTF files found in " + source.string());

    const auto placements = boundary.empty() ? placeZombies(world, scene, center, count, radius) :
        placeZombiesInRegion(world, scene, boundary, count);
    std::vector<glm::mat4> normalization;
    // There is no reason to upload character variants that receive no instances.
    paths.resize(std::min(paths.size(), count));
    for (const auto& path : paths) {
        auto asset = loadZombieModel(path);
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
