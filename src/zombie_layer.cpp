#include "zombie_layer.hpp"
#include "zombie_ragdoll.hpp"
#include "zombie_placement.hpp"

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

void updateRagdollBones(ZombieInstance& instance, const AnimatedModel& asset,
                        const ZombieRagdollWorld& world) {
    instance.ragdollBones.assign(asset.skeleton.size(), glm::mat4(1.0f));
    if (asset.skeleton.empty()) return;
    std::vector<glm::mat4> currentWorld(asset.skeleton.size(), glm::mat4(1.0f));
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
        currentWorld[bone] = asset.skeleton[bone].referenceWorld;
    const auto currentBodies = world.bodyTransforms(instance.ragdollHandle);
    const auto restBodies = world.restBodyTransforms(instance.ragdollHandle);
    const auto inverseInstance = glm::inverse(instance.transform);
    for (std::size_t part = 0; part < ragdollPartCount; ++part) {
        const int bone = asset.ragdollBones[part];
        if (bone < 0 || static_cast<std::size_t>(bone) >= asset.skeleton.size()) continue;
        const auto& restBody = restBodies[part];
        currentWorld[static_cast<std::size_t>(bone)] = inverseInstance *
            currentBodies[part] * glm::inverse(restBody) * instance.restTransform *
            asset.skeleton[static_cast<std::size_t>(bone)].referenceWorld;
        instance.ragdollBones[static_cast<std::size_t>(bone)] = currentWorld[static_cast<std::size_t>(bone)] *
            asset.skeleton[static_cast<std::size_t>(bone)].inverseBind;
    }
    // Most character rigs contain extra joints (hands, feet, clavicles, fingers).
    // Keep those joints attached to their nearest mapped parent instead of adding
    // more physics bodies.
    for (std::size_t pass = 0; pass < asset.skeleton.size(); ++pass) {
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone) {
            const int parent = asset.skeleton[bone].parent;
            if (parent < 0 || static_cast<std::size_t>(parent) >= asset.skeleton.size()) continue;
            const bool isMapped = std::any_of(asset.ragdollBones.begin(), asset.ragdollBones.end(),
                                              [bone](int value) { return value == static_cast<int>(bone); });
            if (isMapped) continue;
            const auto& referenceParent = asset.skeleton[static_cast<std::size_t>(parent)].referenceWorld;
            currentWorld[bone] = currentWorld[static_cast<std::size_t>(parent)] * glm::inverse(referenceParent) *
                asset.skeleton[bone].referenceWorld;
            instance.ragdollBones[bone] = currentWorld[bone] * asset.skeleton[bone].inverseBind;
        }
    }
}

bool raySphere(glm::vec3 origin, glm::vec3 direction, glm::vec3 center, float radius, float& distance) {
    const auto offset = origin - center;
    const float projection = glm::dot(offset, direction);
    const float discriminant = projection * projection - (glm::dot(offset, offset) - radius * radius);
    if (discriminant < 0.0f) return false;
    const float root = std::sqrt(discriminant);
    const float near = -projection - root;
    const float far = -projection + root;
    distance = near >= 0.0f ? near : far;
    return distance >= 0.0f && std::isfinite(distance);
}
}

struct ZombieLayer::Physics {
    ZombieRagdollWorld world;
};

ZombieLayer::ZombieLayer() = default;
ZombieLayer::~ZombieLayer() = default;
ZombieLayer::ZombieLayer(ZombieLayer&&) noexcept = default;
ZombieLayer& ZombieLayer::operator=(ZombieLayer&&) noexcept = default;

bool ZombieLayer::shoot(glm::vec3 origin, glm::vec3 direction) {
    if (!finite(origin) || !finite(direction)) return false;
    const float length = glm::length(direction);
    if (length < 1e-6f) return false;
    direction /= length;
    std::size_t selected = instances.size();
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const auto& instance = instances[i];
        if (instance.ragdoll || instance.asset >= assets.size()) continue;
        const auto center = glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1));
        float distance = 0.0f;
        // A conservative sphere is cheap to test and makes the hit forgiving at
        // the crowd distances for which this viewer is intended.
        if (raySphere(origin, direction, center, 0.62f, distance) && distance < nearest) {
            nearest = distance;
            selected = i;
        }
    }
    if (selected == instances.size()) return false;
    if (!physics_) physics_ = std::make_unique<Physics>();
    auto& instance = instances[selected];
    const auto feet = glm::vec3(instance.restRoot[3]);
    const float yaw = std::atan2(instance.restRoot[2][0], instance.restRoot[0][0]);
    instance.ragdollHandle = physics_->world.create(feet, yaw, direction * 4.0f);
    instance.ragdoll = true;
    instance.transform = physics_->world.rootTransform(instance.ragdollHandle) *
        glm::inverse(instance.restRoot) * instance.restTransform;
    updateRagdollBones(instance, assets[instance.asset], physics_->world);
    return true;
}

void ZombieLayer::update(float seconds) {
    if (!physics_ || !std::isfinite(seconds) || seconds <= 0.0f) return;
    physics_->world.step(seconds);
    for (auto& instance : instances) {
        if (!instance.ragdoll) continue;
        instance.transform = physics_->world.rootTransform(instance.ragdollHandle) *
            glm::inverse(instance.restRoot) * instance.restTransform;
        if (instance.asset < assets.size()) updateRagdollBones(instance, assets[instance.asset], physics_->world);
    }
}

std::size_t ZombieLayer::ragdollCount() const {
    std::size_t result = 0;
    for (const auto& instance : instances) result += instance.ragdoll;
    return result;
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
                           glm::vec2 center, std::size_t count, float radius) {
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

    const auto placements = placeZombies(world, scene, center, count, radius);
    std::vector<glm::mat4> normalization;
    // There is no reason to upload character variants that receive no instances.
    paths.resize(std::min(paths.size(), count));
    for (const auto& path : paths) {
        auto asset = loadIdleModel(path);
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
        for (const auto& warning : asset.model.warnings)
            std::cerr << "Zombie asset warning: " << warning << '\n';
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
        instance.ragdollBones.resize(result.assets[asset].skeleton.size(), glm::mat4(1.0f));
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
    return result;
}

} // namespace viewer
