#include "dog_layer.hpp"
#include "zombie_lod.hpp"
#include "zombie_bounds.hpp"
#include "zombie_animation_buffer.hpp"
#include "zombie_placement.hpp"
#include "zombie_hit.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
DogDetail dogCameraDetail(float distance) {
    return distance < 20 ? DogDetail{} : distance < 45 ? DogDetail{1, 20} :
        distance < 90 ? DogDetail{2, 10} : DogDetail{3, 5};
}
const BakedAnimationClip* dogAnimationClip(const AnimatedModel& model, const DogInstance& dog) {
    if (dog.attacking && model.attackLeftClip) return &*model.attackLeftClip;
    if (dog.walking && dog.running && model.runClip) return &*model.runClip;
    return dog.walking && model.walkClip ? &*model.walkClip : nullptr;
}
float dogAnimationFrame(const AnimatedModel& model, const DogInstance& dog, double seconds, float hz) {
    const auto* clip = dogAnimationClip(model, dog);
    double time = dog.dead ? 0 : dog.attacking ? dog.attackSeconds : dog.walking ? dog.walkSeconds : seconds;
    if (hz > 0) time = std::floor(time * hz) / hz;
    return clip && !clip->loop ? clampedAnimationFrame(clip->duration, clip->frameCount, time) :
        animatedFrame(clip ? clip->duration : model.duration, clip ? clip->frameCount : model.frameCount,
                      time, dog.phase);
}
VisibilityBounds dogWorldBounds(const VisibilityBounds& bounds, const glm::mat4& transform) {
    return transformedZombieBounds(bounds, transform);
}
std::vector<std::filesystem::path> dogAssetPaths(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> paths;
    for (int variant = 1; variant <= 3; ++variant)
        paths.push_back(directory / ("undead_dog_variant_" + std::to_string(variant) + ".glb"));
    return paths;
}
DogLayer loadDogLayer(const std::filesystem::path& directory, const std::vector<glm::mat4>& placements, bool replica) {
    if (placements.size() > maxDogs) throw std::invalid_argument("Dog population exceeds " + std::to_string(maxDogs));
    DogLayer result;
    if (placements.empty()) return result;
    for (const auto& path : dogAssetPaths(directory)) {
        auto model = loadDogModel(path);
        if (!model.walkClip) throw std::runtime_error("Dog model requires a walk clip: " + path.string());
        if (!model.attackLeftClip) throw std::runtime_error("Dog model requires an attack clip: " + path.string());
        result.lodAssets.push_back(loadZombieLods(path, model, loadDogModel));
        VisibilityBounds animated{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
        VisibilityBounds bind = animated;
        includeZombieGeometryBounds(model, animated, bind);
        for (const auto& lod : result.lodAssets.back()) includeZombieGeometryBounds(lod, animated, bind);
        result.assetBounds.push_back(zombieAnimationHalfBounds(animated));
        result.assets.push_back(std::move(model));
    }
    for (std::size_t id = 0; id < placements.size(); ++id) {
        DogInstance dog;
        dog.asset = id % result.assets.size();
        dog.root = placements[id];
        dog.transform = dog.root; // Authored meters and foot origin; no humanoid height scaling.
        dog.phase = static_cast<float>(std::fmod(double(id) * .61803398875, 1.));
        dog.removed = replica;
        result.instances.push_back(dog);
    }
    result.behaviors.resize(placements.size());
    if (!replica) result.rebuildSpatialIndex();
    return result;
}
DogLayer loadDogLayer(const std::filesystem::path& directory, const CollisionWorld& world,
        const Model& scene, glm::vec2 center, std::size_t count, float radius) {
    if (!count) return {};
    return loadDogLayer(directory, placeZombies(world, scene, center, count, radius));
}
void DogLayer::rebuildSpatialIndex() {
    doorways_.reset();
    if (huntingNavigation_ && huntingNavigation_->capacity() != instances.size()) huntingNavigation_->resetPopulation(instances.size());
    spatial_ = ZombieSpatialIndex{};
    spatial_.growPopulation(instances.size());
    behaviors.resize(instances.size());
    for (std::size_t id = 0; id < instances.size(); ++id) {
        if (!instances[id].home && !instances[id].removed) {
            instances[id].home = glm::vec3(instances[id].root[3]);
            instances[id].nextHuntSeconds = double(id % 60)/60;
        }
        if (!instances[id].removed && !instances[id].dead) spatial_.update(id, glm::vec3(instances[id].root[3]));
    }
}
void DogLayer::releaseAnimationVertices() {
    for (auto& asset : assets) releaseZombieAnimationVertices(asset);
    for (auto& levels : lodAssets) for (auto& asset : levels) releaseZombieAnimationVertices(asset);
}
bool DogLayer::damage(std::size_t id, float amount) {
    if (id >= instances.size() || !std::isfinite(amount) || amount <= 0) return false;
    auto& dog = instances[id];
    if (dog.dead || dog.removed) return false;
    dog.health = std::max(0.f, dog.health - amount);
    if (dog.health < 1e-6f) dog.health = 0;
    if (dog.health == 0) {
        dog.dead = true;
        dog.walking = dog.running = dog.attacking = false;
        // Quadrupeds do not enter the humanoid ragdoll/wound system.
        dog.transform = dog.root * glm::translate(glm::mat4(1), {0, .18f, 0}) *
            glm::rotate(glm::mat4(1), 1.57079633f, {0, 0, 1});
        spatial_.erase(id);
        doorways_.erase(id);
        cancelHunt(id);
        behaviors[id] = {};
    }
    return true;
}
} // namespace viewer
