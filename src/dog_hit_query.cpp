#include "dog_hit_query.hpp"
#include <algorithm>
#include <cmath>

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
std::vector<glm::vec3> posedVertices(const AnimatedModel& model, const DogInstance& dog,
        const std::vector<glm::mat4>& pose, std::size_t primitiveId) {
    const auto& primitive = model.model.primitives[primitiveId];
    const auto& skin = model.animation[primitiveId].skinning;
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
    return vertices;
}
bool rayBounds(glm::vec3 origin, glm::vec3 direction, const VisibilityBounds& bounds, float limit) {
    float entry = 0, exit = limit;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-8f) {
            if (origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis]) return false;
        } else {
            const float a = (bounds.minimum[axis] - origin[axis]) / direction[axis];
            const float b = (bounds.maximum[axis] - origin[axis]) / direction[axis];
            entry = std::max(entry, std::min(a, b)); exit = std::min(exit, std::max(a, b));
        }
    }
    return exit >= entry;
}
std::optional<float> triangleRay(glm::vec3 origin, glm::vec3 direction, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const auto edge1 = b - a, edge2 = c - a, h = glm::cross(direction, edge2);
    const float det = glm::dot(edge1, h);
    if (std::abs(det) < 1e-8f) return {};
    const auto s = origin - a;
    const float u = glm::dot(s, h) / det;
    const auto q = glm::cross(s, edge1);
    const float v = glm::dot(direction, q) / det, distance = glm::dot(edge2, q) / det;
    if (u < 0 || v < 0 || u + v > 1 || distance < 0) return {};
    return distance;
}
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
        if (!rayBounds(origin, direction, bounds, nearest ? nearest->distance : 1000)) continue;
        const auto& model = assets[dog.asset];
        const auto pose = shotPose(model,dog,seconds);
        for (std::size_t p = 0; p < model.model.primitives.size(); ++p) {
            const auto& primitive = model.model.primitives[p];
            const auto vertices = posedVertices(model, dog, pose, p);
            for (std::size_t t = 0; t < primitive.indices.size(); t += 3) {
                const auto a = vertices[primitive.indices[t]], b = vertices[primitive.indices[t+1]], c = vertices[primitive.indices[t+2]];
                const auto distance = triangleRay(origin, direction, a, b, c);
                if (!distance || (nearest && *distance >= nearest->distance)) continue;
                if (world.lineOfSight(origin, origin + direction * std::max(0.f, *distance - .01f)))
                    nearest = DogShotTrace{id, *distance};
            }
        }
    }
    return nearest;
}
void DogLayer::queryMeleeNear(glm::vec3 center, float radius, std::vector<std::size_t>& ids) const {
    ids.clear();
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        !std::isfinite(radius) || radius < 0) return;
    // Living dogs use foot translations and yaw rotations. Pad the feet grid
    // by the largest animated X/Z reach, independent of current facing.
    float padding = 0;
    for (const auto& bounds : assetBounds) {
        const auto extent = glm::max(glm::abs(bounds.minimum), glm::abs(bounds.maximum));
        padding = std::max(padding, glm::length(glm::vec2(extent.x, extent.z)));
    }
    spatial_.query(center, double(radius) + padding, ids);
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](std::size_t id) {
        const auto& dog = instances[id];
        if (dog.dead || dog.removed || dog.asset >= assets.size() || dog.asset >= assetBounds.size()) return true;
        const auto bounds = dogWorldBounds(assetBounds[dog.asset], dog.transform);
        const auto delta = glm::clamp(center, bounds.minimum, bounds.maximum) - center;
        return glm::dot(delta, delta) > radius * radius;
    }), ids.end());
}

DogMeleeQuery::DogMeleeQuery(const DogLayer* dogs, glm::vec3 center, float radius, double seconds)
    : dogs_(dogs), seconds_(seconds) {
    if (!dogs || !std::isfinite(seconds)) return;
    std::vector<std::size_t> ids;
    dogs->queryMeleeNear(center, radius, ids);
    targets_.reserve(ids.size());
    for (const auto id : ids) {
        const auto& dog = dogs->instances[id];
        targets_.push_back({id, dogWorldBounds(dogs->assetBounds[dog.asset], dog.transform), false, {}, {}});
    }
}

void DogMeleeQuery::prepare(Target& target) {
    if (target.prepared) return;
    const auto& dog = dogs_->instances[target.id];
    const auto& model = dogs_->assets[dog.asset];
    const auto pose = shotPose(model, dog, seconds_);
    for (std::size_t p = 0; p < model.model.primitives.size(); ++p) {
        const auto vertices = posedVertices(model, dog, pose, p);
        const auto& indices = model.model.primitives[p].indices;
        for (std::size_t t = 0; t < indices.size(); t += 3)
            target.triangles.push_back({vertices[indices[t]], vertices[indices[t + 1]], vertices[indices[t + 2]]});
    }
    target.prepared = true;
}

std::vector<DogMeleeQuery::Contact> DogMeleeQuery::cast(const MeleeSweep& sweep) {
    std::vector<Contact> result;
    for (auto& target : targets_) {
        const auto& dog = dogs_->instances[target.id];
        if (dog.dead || dog.removed ||
            glm::any(glm::greaterThan(sweep.bounds.minimum, target.bounds.maximum)) ||
            glm::any(glm::lessThan(sweep.bounds.maximum, target.bounds.minimum))) continue;
        prepare(target);
        if (target.colliders.empty()) {
            target.colliders.reserve(target.triangles.size());
            for (const auto& triangle : target.triangles) {
                const std::array<glm::vec3, 3> points{triangle.a, triangle.b, triangle.c};
                target.colliders.push_back(meleeCollider(points.data(), 3, 0, glm::dmat4(1)));
            }
        }
        for (const auto& collider : target.colliders)
            if (const auto hit = castMeleeShape(sweep, collider)) result.push_back({target.id, *hit});
    }
    return result;
}

std::optional<DogShotTrace> DogMeleeQuery::trace(glm::vec3 start, glm::vec3 end, const CollisionWorld& world) {
    const float length = glm::distance(start, end);
    if (!std::isfinite(length) || length < 1e-6f) return {};
    const auto direction = (end - start) / length;
    std::optional<DogShotTrace> nearest;
    for (auto& target : targets_) {
        const auto& dog = dogs_->instances[target.id];
        if (dog.dead || dog.removed) continue;
        if (!rayBounds(start, direction, target.bounds, nearest ? nearest->distance : length)) continue;
        prepare(target);
        for (const auto& triangle : target.triangles) {
            const auto distance = triangleRay(start, direction, triangle.a, triangle.b, triangle.c);
            if (!distance || *distance > length || (nearest && *distance >= nearest->distance)) continue;
            if (world.lineOfSight(start, start + direction * std::max(0.f, *distance - .01f)))
                nearest = DogShotTrace{target.id, *distance};
        }
    }
    return nearest;
}
} // namespace viewer
