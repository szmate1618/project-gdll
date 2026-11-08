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
namespace {
std::vector<glm::mat4> shotPose(const AnimatedModel& model, const DogInstance& dog, double seconds) {
    const auto* clip = dogAnimationClip(model,dog);
    if (dog.displayedAnimationFrame < 0)
        return sampleAnimatedPose(model,dog.attacking ? dog.attackSeconds : dog.walking ? dog.walkSeconds : seconds,
                                  dog.attacking ? 0 : dog.phase,clip);
    const auto& frames = clip ? clip->boneFrames : model.boneFrames;
    const auto count = clip ? clip->frameCount : model.frameCount;
    const float frame = dog.displayedAnimationFrame;
    if (frames.empty()) return sampleAnimatedPose(model,0,0,clip);
    const auto first = std::min(static_cast<std::size_t>(frame),count-1);
    const auto second = clip && !clip->loop ? std::min(first+1,count-1) : (first+1)%count;
    const float fraction = frame-std::floor(frame);
    std::vector<glm::mat4> pose;
    for (std::size_t bone = 0; bone < model.skeleton.size(); ++bone)
        pose.push_back(frames[first*model.skeleton.size()+bone]*(1-fraction)+frames[second*model.skeleton.size()+bone]*fraction);
    return pose;
}
}
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
std::optional<DogShotTrace> DogLayer::traceShot(glm::vec3 origin, glm::vec3 direction,
        const CollisionWorld& world, double seconds, const std::vector<DogInstance>* displayed) const {
    if (!std::isfinite(glm::length(direction)) || glm::length(direction) < 1e-6f) return {};
    direction = glm::normalize(direction);
    const auto& population = displayed ? *displayed : instances;
    std::optional<DogShotTrace> nearest;
    for (std::size_t id = 0; id < population.size(); ++id) {
        const auto& dog = population[id];
        if (dog.dead || dog.removed || dog.asset >= assets.size()) continue;
        const auto bounds = dogWorldBounds(assetBounds[dog.asset], dog.transform);
        float entry = 0, exit = nearest ? nearest->distance : 1000;
        bool overlap = true;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(direction[axis]) < 1e-8f) {
                if (origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis]) overlap = false;
            } else {
                const float a = (bounds.minimum[axis] - origin[axis]) / direction[axis];
                const float b = (bounds.maximum[axis] - origin[axis]) / direction[axis];
                entry = std::max(entry, std::min(a, b)); exit = std::min(exit, std::max(a, b));
            }
        }
        if (!overlap || exit < entry) continue;
        const auto& model = assets[dog.asset];
        const auto pose = shotPose(model,dog,seconds);
        for (std::size_t p = 0; p < model.model.primitives.size(); ++p) {
            const auto& primitive = model.model.primitives[p];
            const auto& skin = model.animation[p].skinning;
            std::vector<glm::vec3> vertices;
            vertices.reserve(primitive.vertices.size());
            for (std::size_t v = 0; v < primitive.vertices.size(); ++v) {
                glm::vec4 position(primitive.vertices[v].position, 1);
                if (!skin.empty()) {
                    glm::mat4 palette(0);
                    for (std::size_t influence = 0; influence < 8; ++influence)
                        if (skin[v].weights[influence] > 0) {
                            const auto joint = skin[v].joints[influence];
                            palette += (pose[joint] * model.skeleton[joint].inverseBind) * skin[v].weights[influence];
                        }
                    position = palette * position;
                }
                vertices.push_back(glm::vec3(dog.transform * position));
            }
            for (std::size_t t = 0; t < primitive.indices.size(); t += 3) {
                const auto a = vertices[primitive.indices[t]], b = vertices[primitive.indices[t+1]], c = vertices[primitive.indices[t+2]];
                const auto edge1 = b-a, edge2 = c-a, h = glm::cross(direction, edge2);
                const float det = glm::dot(edge1, h);
                if (std::abs(det) < 1e-8f) continue;
                const auto s = origin-a;
                const float u = glm::dot(s, h)/det;
                const auto q = glm::cross(s, edge1);
                const float v = glm::dot(direction, q)/det, distance = glm::dot(edge2, q)/det;
                if (u < 0 || v < 0 || u+v > 1 || distance < 0 || (nearest && distance >= nearest->distance)) continue;
                if (world.lineOfSight(origin, origin + direction * std::max(0.f, distance-.01f))) nearest = DogShotTrace{id, distance};
            }
        }
    }
    return nearest;
}
} // namespace viewer
