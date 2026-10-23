#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "zombie_hit.hpp"
#include "zombie_shooting_fixture.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
glm::mat4 translation(glm::vec3 p) { return glm::translate(glm::mat4(1), p); }

viewer::AnimatedModel asset(bool animated = true) {
    viewer::AnimatedModel result;
    result.frameCount = 2;
    result.duration = 1;
    result.skeleton.resize(3);
    result.skeleton[1].parent = 0; // Hair follows the head.
    result.ragdollBones[static_cast<std::size_t>(viewer::RagdollPart::head)] = 0;
    viewer::Primitive primitive;
    viewer::AnimatedPrimitive animation;
    for (const auto point : {glm::vec3(-0.15f, 1.6f, -0.12f), glm::vec3(0.15f, 1.9f, 0.12f),
                             glm::vec3(0.1f, 1.75f, 0), glm::vec3(-0.1f, 1.7f, 0), glm::vec3(0, 1.8f, 0)}) {
        viewer::Vertex vertex;
        vertex.position = point;
        primitive.vertices.push_back(vertex);
        viewer::SkinningVertex skin;
        skin.joints[0] = primitive.vertices.size() % 2;
        skin.weights[0] = 1;
        animation.skinning.push_back(skin);
    }
    // Exactly 50% head weighting, with an influence in JOINTS_1/WEIGHTS_1.
    animation.skinning[3].joints = {2, 0, 0, 0, 1, 0, 0, 0};
    animation.skinning[3].weights = {0.5f, 0, 0, 0, 0.5f, 0, 0, 0};
    animation.skinning[4].joints = {2, 0, 0, 0, 1, 0, 0, 0};
    animation.skinning[4].weights = {0.51f, 0, 0, 0, 0.49f, 0, 0, 0};
    // Many unrelated body vertices must be skipped by detailed head queries.
    for (int i = 0; i < 1000; ++i) {
        viewer::Vertex vertex;
        vertex.position = {0, 0.5f, 0};
        primitive.vertices.push_back(vertex);
        viewer::SkinningVertex skin;
        skin.joints[0] = 2;
        skin.weights[0] = 1;
        animation.skinning.push_back(skin);
    }
    result.model.primitives.push_back(primitive);
    result.animation.push_back(animation);
    for (int frame = 0; frame < 2; ++frame)
        for (int bone = 0; bone < 3; ++bone)
            result.boneFrames.push_back(translation({animated && bone < 2 ? 3.0f * frame : 0.0f, 0, 0}));
    result.walkClip = viewer::BakedAnimationClip{"walk", 2, 2, 2, result.boneFrames};
    result.runClip = viewer::BakedAnimationClip{"run", 1, 2, 4, result.boneFrames};
    if (animated) {
        for (std::size_t i = 0; i < result.walkClip->boneFrames.size(); ++i)
            if (i % 3 < 2) result.walkClip->boneFrames[i] = translation({8 + float(i / 3), 0, 0});
        for (std::size_t i = 0; i < result.runClip->boneFrames.size(); ++i)
            if (i % 3 < 2) result.runClip->boneFrames[i] = translation({-6 - float(i / 3), 0, 0});
    }
    return result;
}

// Full mesh/ancestry scan provides a reference independently of prepared
// vertex lists, motion bounds, spatial membership, and coarse ray rejection.
viewer::ZombieHitSphere referenceHead(const viewer::AnimatedModel& model, const viewer::ZombieInstance& instance,
                                       const std::vector<glm::mat4>& pose) {
    const int head = model.ragdollBones[static_cast<std::size_t>(viewer::RagdollPart::head)];
    if (head < 0 || std::size_t(head) >= pose.size())
        return {glm::vec3(instance.restRoot * glm::vec4(0, 1.65f, 0, 1)), 0.23f};
    auto low = glm::vec3(std::numeric_limits<float>::max()), high = -low;
    bool found = false;
    for (std::size_t p = 0; p < model.animation.size(); ++p) {
        for (std::size_t v = 0; v < model.animation[p].skinning.size(); ++v) {
            const auto& skin = model.animation[p].skinning[v];
            float weight = 0;
            for (std::size_t i = 0; i < 8; ++i) {
                int ancestor = int(skin.joints[i]);
                for (std::size_t depth = 0; ancestor >= 0 && depth < model.skeleton.size(); ++depth) {
                    if (ancestor == head) { weight += skin.weights[i]; break; }
                    ancestor = model.skeleton[std::size_t(ancestor)].parent;
                }
            }
            if (weight < 0.5f) continue;
            glm::vec4 point(0);
            for (std::size_t i = 0; i < 8; ++i)
                if (skin.weights[i] != 0)
                    point += pose[skin.joints[i]] * model.skeleton[skin.joints[i]].inverseBind *
                        glm::vec4(model.model.primitives[p].vertices[v].position, 1) * skin.weights[i];
            const auto world = glm::vec3(instance.transform * point);
            low = glm::min(low, world);
            high = glm::max(high, world);
            found = true;
        }
    }
    if (!found) return {glm::vec3(instance.transform * pose[std::size_t(head)][3]) + glm::vec3(0, 0.1f, 0), 0.23f};
    return {(low + high) * 0.5f, glm::length(high - low) * 0.5f + 0.02f};
}

bool referenceSphere(glm::vec3 origin, glm::vec3 direction, viewer::ZombieHitSphere sphere, float& distance) {
    const auto offset = glm::dvec3(origin) - glm::dvec3(sphere.center);
    const auto ray = glm::dvec3(direction);
    const double a = glm::dot(ray, ray), b = glm::dot(offset, ray);
    const double c = glm::dot(offset, offset) - double(sphere.radius) * sphere.radius;
    const double discriminant = b * b - a * c;
    if (discriminant < 0) return false;
    const double root = std::sqrt(discriminant);
    const double near = (-b - root) / a, far = (-b + root) / a;
    distance = static_cast<float>(near >= 0 ? near : far);
    return distance >= 0 && std::isfinite(distance);
}
std::size_t referenceTarget(const viewer::ZombieLayer& layer, glm::vec3 origin, glm::vec3 direction, double seconds) {
    direction = glm::normalize(direction);
    std::size_t target = layer.instances.size();
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < layer.instances.size(); ++i) {
        const auto& instance = layer.instances[i];
        if (instance.ragdoll || instance.removed || instance.asset >= layer.assets.size()) continue;
        const auto pose = viewer::sampleZombiePose(layer.assets[instance.asset], instance, seconds);
        float bodyDistance = 0, headDistance = 0;
        const bool body = referenceSphere(origin, direction,
            {glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1)), 0.62f}, bodyDistance);
        const bool head = referenceSphere(origin, direction, referenceHead(layer.assets[instance.asset], instance, pose), headDistance);
        if (!body && !head) continue;
        const auto distance = body && head ? std::min(bodyDistance, headDistance) : (body ? bodyDistance : headDistance);
        if (distance < nearest) { nearest = distance; target = i; }
    }
    return target;
}

viewer::ZombieLayer singleZombie(glm::vec3 feet) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    layer.instances.emplace_back();
    auto& instance = layer.instances[0];
    instance.restRoot = instance.restTransform = instance.transform = translation(feet);
    return layer;
}

void longDistanceGeometry() {
    const viewer::CollisionWorld clear(viewer::Model{});
    for (const bool occlusion : {false, true}) {
        const auto shoot = [&](viewer::ZombieLayer& layer, glm::vec3 origin, glm::vec3 direction) {
            return occlusion ? layer.shootWithOcclusion(origin, direction, 0, clear) : layer.shoot(origin, direction);
        };
        for (const float range : {10.0f, 1000.0f, 4000.0f, 10000.0f})
            for (const float angle : {0.0f, .03f, .7f})
                for (const float height : {.9f, 1.65f}) {
                    const glm::vec3 feet{range * std::sin(angle), 0, -range * std::cos(angle)};
                    auto layer = singleZombie(feet);
                    require(shoot(layer, {0, height, 0}, feet), "Centered head/body ray must hit at every range and angle");
                }
        struct OffsetShot { float x, height; bool hit; };
        for (const auto shot : {OffsetShot{.7f, .9f, false}, {.5f, 1.4f, false},
                                {.61f, .9f, true}, {.63f, .9f, false},
                                {.62f, .9f, true}, {std::nextafter(.62f, 1.0f), .9f, false},
                                {.22f, 1.65f, true}, {.24f, 1.65f, false},
                                {.23f, 1.65f, true}, {std::nextafter(.23f, 1.0f), 1.65f, false}}) {
            auto layer = singleZombie({0, 0, -4000});
            require(shoot(layer, {shot.x, shot.height, 0}, {0, 0, -1}) == shot.hit,
                    "Long-range offsets must respect body/head radii, including misses inside the coarse bounds");
        }
        auto posed = singleZombie({30, 0, -1000});
        posed.assets[0] = asset(false);
        const auto pose = viewer::sampleZombiePose(posed.assets[0], posed.instances[0], 0);
        const auto head = referenceHead(posed.assets[0], posed.instances[0], pose);
        require(shoot(posed, {0, 1.75f, 0}, head.center - glm::vec3(0, 1.75f, 0)),
                "Long-range headshots must also hit the posed mesh's head sphere");
        auto behind = singleZombie({0, 0, 4000});
        require(!shoot(behind, {0, .9f, 0}, {0, 0, -1}), "A target behind the shot must miss");
        auto inside = singleZombie({0, 0, -4000});
        require(shoot(inside, {0, .9f, -4000}, {0, 0, -1}), "A ray starting inside the body must hit");
        auto nearest = singleZombie({0, 0, -4001});
        nearest.instances.push_back(singleZombie({0, 0, -4000}).instances[0]);
        require(shoot(nearest, {0, .9f, 0}, {0, 0, -1}) && nearest.damage().injury(1) && !nearest.damage().injury(0),
                "A long-range shot must select the nearer target even when listed second");
    }
}

void longDistanceOcclusion() {
    for (const float wallZ : {-999.0f, -999.8f}) {
        viewer::Model model;
        viewer::Primitive wall;
        for (const auto p : {glm::vec3(-2, 0, wallZ), {2, 0, wallZ}, {2, 3, wallZ}, {-2, 3, wallZ}}) {
            viewer::Vertex vertex; vertex.position = p; wall.vertices.push_back(vertex);
        }
        wall.indices = {0, 1, 2, 0, 2, 3};
        model.primitives.push_back(wall); model.draws.push_back({});
        const viewer::CollisionWorld world(model);
        auto layer = singleZombie({0, 0, -1000});
        // Body entry is z=-999.38: only the wall before that entry blocks the shot.
        require(layer.shootWithOcclusion({0, .9f, 0}, {0, 0, -1}, 0, world) == (wallZ < -999.38f),
                "Long-range wall checks must end at the sphere entry, not its center");
    }
}

void preparedHeadMatchesFullScan() {
    for (const int variant : {0, 1, 2}) {
        auto model = asset();
        if (variant == 1) model.ragdollBones.fill(-1);
        if (variant == 2) model.animation[0].skinning.clear();
        const auto geometry = viewer::prepareZombieHitGeometry(model);
        if (variant == 0) require(geometry.headVertices.size() == 4, "Precompute must preserve ancestry and the inclusive 50% threshold");
        for (int clip = 0; clip < 3; ++clip)
            for (const double time : {0.0, 0.125, 0.5, 0.875, 1e12 + 0.125}) {
                viewer::ZombieInstance instance;
                instance.restRoot = translation({-20.01f, 4, -40});
                instance.transform = instance.restRoot * glm::scale(glm::mat4(1), {-1.5f, 0.75f, 2});
                instance.transform[2].x += 0.3f; // Shear must remain conservative too.
                instance.phase = 0.125f;
                instance.walking = clip > 0;
                instance.running = clip == 2;
                instance.walkSeconds = time;
                const auto pose = viewer::sampleZombiePose(model, instance, time);
                const auto expected = referenceHead(model, instance, pose);
                const auto actual = viewer::zombieHeadHitSphere(model, instance, pose, geometry);
                require(glm::distance(actual.center, expected.center) < 1e-5f && std::abs(actual.radius - expected.radius) < 1e-5f,
                        "Prepared vertices must reproduce the full posed head sphere");
                const auto bounds = viewer::zombieShotBounds(instance, geometry);
                require(glm::all(glm::lessThanEqual(bounds.minimum, actual.center - glm::vec3(actual.radius))) &&
                        glm::all(glm::greaterThanEqual(bounds.maximum, actual.center + glm::vec3(actual.radius))),
                        "Broad bounds must enclose the entire head sphere through every clip and wrap interpolation");
            }
    }
}

void indexedSelectionMatchesFullScan() {
    for (int test = 0; test < 80; ++test) {
        viewer::ZombieLayer layer;
        layer.assets.push_back(asset());
        const double seconds = test * 0.037;
        for (int i = 0; i < 12; ++i) {
            viewer::ZombieInstance instance;
            instance.restRoot = translation({float(i % 3) * 3 - 3, float(i % 2), -float(i / 3) * 4 - 3});
            instance.restTransform = instance.transform = instance.restRoot;
            instance.phase = float(i % 4) * 0.25f;
            instance.walking = i % 3 > 0;
            instance.running = i % 3 == 2;
            instance.walkSeconds = seconds;
            instance.removed = i == 9;
            instance.ragdoll = i == 10;
            layer.instances.push_back(instance);
        }
        const auto pose = viewer::sampleZombiePose(layer.assets[0], layer.instances[test % 9], seconds);
        const auto head = referenceHead(layer.assets[0], layer.instances[test % 9], pose);
        const glm::vec3 origin{0, test % 2 ? 3.0f : 1.75f, 4};
        const auto direction = head.center + glm::vec3(float(test % 5 - 2) * 0.12f, 0, 0) - origin;
        const auto expected = referenceTarget(layer, origin, direction, seconds);
        const bool hit = layer.shoot(origin, direction, seconds);
        require(hit == (expected != layer.instances.size()), "Indexed shooting must preserve hit/miss results");
        if (hit) require(layer.damage().injury(expected), "Indexed shooting must preserve the nearest target");
    }
    viewer::ZombieLayer ties;
    ties.assets.emplace_back();
    ties.instances.resize(2);
    require(viewer::test::shootToKill(ties, {0, 0.9f, 3}, {0, 0, -1}) && ties.instances[0].ragdoll,
            "Equal-distance hits must preserve original instance ID tie order");
    require(viewer::test::shootToKill(ties, {0, 1.8f, 3}, {0, 0, -1}) && ties.instances[1].ragdoll,
            "Shot removal must leave the next head-only target hittable");
    viewer::ZombieLayer distant;
    distant.assets.emplace_back();
    distant.instances.emplace_back();
    distant.instances[0].restRoot = distant.instances[0].transform = distant.instances[0].restTransform =
        translation({2, 0, -10000});
    require(!distant.shoot({0, 0.9f, 0}, {0, 0, -1}),
            "A ray two meters beside a distant zombie must miss");
}

void largeCrowdSkipsDetailedPoses() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(asset(false));
    for (int z = 0; z < 100; ++z)
        for (int x = 0; x < 100; ++x) {
            viewer::ZombieInstance instance;
            instance.restRoot = translation({float(x) * 3 - 150, 0, -float(z) * 3 - 3});
            instance.restTransform = instance.transform = instance.restRoot;
            layer.instances.push_back(instance);
        }
    layer.rebuildSpatialIndex();
    const auto started = std::chrono::steady_clock::now();
    require(layer.shoot({0, 1.75f, 2}, {0, 0, -1}), "A head-only ray must hit in a 10,000-zombie crowd");
    const auto stats = layer.shootingStats();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::cout << "10,000 zombies: " << stats.spatialCandidates << " grid candidates, " << stats.poseSamples
              << " poses, " << stats.headVertices << " head vertices; shot " << elapsed << " ms\n";
    require(stats.spatialCandidates < 1500 && stats.poseSamples == 100 && stats.headVertices == 400,
            "A narrow shot must pose only its 100 aligned heads, with four head vertices each");
    require(!layer.shoot({0, 20, 2}, {0, 0, -1}), "Ray above the crowd must miss");
    require(layer.shootingStats().poseSamples == 0, "Cheap 3D bounds must reject elevated misses without sampling poses");
    // Same-size external relocation uses the existing explicit rebuild contract.
    layer.instances[50].removed = true;
    layer.instances[0].restRoot = layer.instances[0].transform = layer.instances[0].restTransform = translation({0, 0, 0});
    layer.rebuildSpatialIndex();
    require(viewer::test::shootToKill(layer, {0, 0.9f, 2}, {0, 0, -1}) && layer.instances[0].ragdoll,
            "Explicit rebuild must update shot bounds and spatial membership");
}
}
int main() {
    try {
        longDistanceGeometry();
        longDistanceOcclusion();
        preparedHeadMatchesFullScan();
        indexedSelectionMatchesFullScan();
        largeCrowdSkipsDetailedPoses();
        std::cout << "Zombie shooting tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie shooting test failed: " << error.what() << '\n';
        return 1;
    }
}
