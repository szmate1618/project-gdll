#include "client/ragdoll_interpolation.hpp"
#include "animated_model.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using multiplayer::CorpsePose;
using multiplayer::interpolateRagdollPose;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void near(glm::vec4 actual, glm::vec4 expected, const char* message) {
    require(glm::length(actual - expected) < .002f, message);
}

glm::mat4 rigid(glm::vec3 origin, float degrees, glm::vec3 axis) {
    return glm::translate(glm::mat4(1), origin) *
        glm::rotate(glm::mat4(1), glm::radians(degrees), axis);
}

glm::mat4 shape(glm::vec3 scale) {
    auto result = glm::scale(glm::mat4(1), scale);
    result[1].x = .7f;
    result[2].x = -.3f;
    result[2].y = .2f;
    return result;
}

bool same(const CorpsePose& a, const CorpsePose& b) {
    return a.id == b.id && a.asset == b.asset && a.version == b.version &&
        a.severedJoint == b.severedJoint && a.transform == b.transform &&
        a.root == b.root && a.bones == b.bones;
}

void sameShape(const glm::mat4& actual, const glm::mat4& original) {
    // Rigid motion preserves signed volume and every pair of basis-vector
    // lengths/angles, including reflected, nonuniformly scaled, sheared assets.
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r)
        require(std::abs(glm::dot(glm::vec3(actual[c]), glm::vec3(actual[r])) -
            glm::dot(glm::vec3(original[c]), glm::vec3(original[r]))) < .002f,
            "Interpolated ragdoll changed size or shear");
    require(std::abs(glm::determinant(glm::mat3(actual)) -
        glm::determinant(glm::mat3(original))) < .002f,
        "Interpolated ragdoll lost its reflection or volume");
}

struct Pair {
    CorpsePose before, after;
    viewer::AnimatedModel asset;
    glm::mat4 normalization = shape({-2, 3, 4});
    std::array<glm::mat4, 2> boneShapes{{shape({-.8f, 1.6f, 1.2f}), shape({1.2f, .6f, 2})}};

    Pair() {
        asset.skeleton.resize(2);
        asset.skeleton[0].inverseBind = glm::translate(glm::mat4(1), {-3, -2, 1});
        asset.skeleton[1].inverseBind = glm::translate(glm::mat4(1), {2, -4, -1});
        before.id = after.id = 7;
        before.asset = after.asset = 2;
        before.version = 10; after.version = 20;
        before.severedJoint = after.severedJoint = 3;
        before.root = after.root = rigid({1, 2, 3}, 47, {0, 1, 0});
        before.transform = rigid({1, 2, 3}, 20, {0, 1, 0}) * normalization;
        after.transform = rigid({11, 10, -3}, 160, {0, 1, 0}) * normalization;
        const std::array<glm::mat4, 2> oldWorld{{
            rigid({0, 2, 0}, 0, {0, 0, 1}) * boneShapes[0],
            rigid({5, 6, 7}, -20, {1, 0, 0}) * boneShapes[1]}};
        const std::array<glm::mat4, 2> newWorld{{
            rigid({10, 6, 4}, 160, {0, 0, 1}) * boneShapes[0],
            rigid({-5, 2, 9}, 120, {1, 0, 0}) * boneShapes[1]}};
        for (std::size_t bone = 0; bone < oldWorld.size(); ++bone) {
            before.bones.push_back(glm::inverse(before.transform) * oldWorld[bone] * asset.skeleton[bone].inverseBind);
            after.bones.push_back(glm::inverse(after.transform) * newWorld[bone] * asset.skeleton[bone].inverseBind);
        }
    }
};

void exactEndpoints() {
    const Pair pair;
    require(same(interpolateRagdollPose(pair.before, pair.after, pair.asset, 0), pair.before) &&
            same(interpolateRagdollPose(pair.before, pair.after, pair.asset, -1), pair.before),
        "Interpolation changed the exact initial pose");
    require(same(interpolateRagdollPose(pair.before, pair.after, pair.asset, 1), pair.after) &&
            same(interpolateRagdollPose(pair.before, pair.after, pair.asset, 2), pair.after),
        "Interpolation changed the exact final pose");
}

void independentWorldMotion() {
    const Pair pair;
    const auto middle = interpolateRagdollPose(pair.before, pair.after, pair.asset, .5f);
    const auto expectedTransform = rigid({6, 6, 0}, 90, {0, 1, 0}) * pair.normalization;
    const std::array<glm::mat4, 2> expectedWorld{{
        rigid({5, 4, 2}, 80, {0, 0, 1}) * pair.boneShapes[0],
        rigid({0, 4, 8}, 50, {1, 0, 0}) * pair.boneShapes[1]}};
    const std::array<glm::vec4, 4> vertices{{{0, 0, 0, 1}, {1, 0, 0, 1},
        {0, 2, 0, 1}, {-.5f, .7f, 1.4f, 1}}};
    for (const auto vertex : vertices) {
        near(middle.transform * vertex, expectedTransform * vertex,
            "Interpolated character transform did not follow its rigid turn");
        near(middle.root * vertex, pair.before.root * vertex,
            "Interpolation moved the frozen ragdoll handoff root");
    }
    for (std::size_t bone = 0; bone < expectedWorld.size(); ++bone) {
        const auto world = middle.transform * middle.bones[bone];
        sameShape(world, pair.before.transform * pair.before.bones[bone]);
        for (const auto vertex : vertices)
            near(world * vertex, expectedWorld[bone] * pair.asset.skeleton[bone].inverseBind * vertex,
                "Root motion distorted an independently moving ragdoll limb");
    }
    sameShape(middle.transform, pair.before.transform);
    require(middle.severedJoint == 3, "Interpolation lost the detached mesh selection");
}

void halfTurnKeepsVolume() {
    CorpsePose before, after;
    before.version = 1; after.version = 2;
    before.transform = shape({-2, 3, 4});
    after.transform = rigid({8, 0, 0}, 180, {0, 1, 0}) * before.transform;
    before.bones.emplace_back(1); after.bones.emplace_back(1);
    viewer::AnimatedModel asset; asset.skeleton.resize(1);
    const auto middle = interpolateRagdollPose(before, after, asset, .5f);
    sameShape(middle.transform, before.transform);
    sameShape(middle.transform * middle.bones[0], before.transform);
    near(middle.transform[3], {4, 0, 0, 1}, "Half-turn translation did not advance smoothly");
    const auto axis = glm::normalize(glm::vec2(middle.transform[2].x, middle.transform[2].z));
    const auto original = glm::normalize(glm::vec2(before.transform[2].x, before.transform[2].z));
    require(std::abs(glm::dot(axis, original)) < .001f,
        "Half-turn midpoint snapped instead of rotating through ninety degrees");
}

void offOriginJointPivot() {
    CorpsePose before, after;
    before.version = 1; after.version = 2;
    viewer::AnimatedModel asset; asset.skeleton.resize(1);
    asset.skeleton[0].inverseBind = glm::translate(glm::mat4(1), {-4, -3, 0});
    before.bones.push_back(rigid({1, 2, 0}, 0, {0, 0, 1}) * asset.skeleton[0].inverseBind);
    after.bones.push_back(rigid({1, 2, 0}, 120, {0, 0, 1}) * asset.skeleton[0].inverseBind);
    const auto middle = interpolateRagdollPose(before, after, asset, .5f);
    const auto world = middle.transform * middle.bones[0];
    near(world * glm::vec4(4, 3, 0, 1), {1, 2, 0, 1},
        "Interpolated arm drifted away from its fixed off-origin joint");
    near(world * glm::vec4(6, 3, 0, 1), {2, 2 + std::sqrt(3.f), 0, 1},
        "Interpolated arm tip did not rotate around the anatomical joint");
}

void incompatiblePosesStayCoherent() {
    const Pair pair;
    const auto snaps = [&](const CorpsePose& before, const CorpsePose& after) {
        require(same(interpolateRagdollPose(before, after, pair.asset, .5f), after),
            "Incompatible ragdoll samples published a partial blended pose");
    };
    auto changed = pair.after; changed.severedJoint = 4;
    snaps(pair.before, changed);
    changed = pair.after; changed.id = 8;
    snaps(pair.before, changed);
    changed = pair.after; changed.asset = 3;
    snaps(pair.before, changed);
    changed = pair.after; changed.bones.pop_back();
    snaps(pair.before, changed);
    auto singular = pair.before; singular.transform[0] = glm::vec4(0);
    snaps(singular, pair.after);
    singular = pair.before; singular.bones[1][0] = glm::vec4(0);
    snaps(singular, pair.after);
    changed = pair.after; changed.bones[1][0] *= 2;
    snaps(pair.before, changed);
}

void predictionKeepsJointPivotAndAffineShape(std::uint64_t period) {
    CorpsePose before, after;
    before.version = 1; before.tick = 60;
    after.version = 2; after.tick = before.tick + period;
    viewer::AnimatedModel asset; asset.skeleton.resize(1);
    asset.skeleton[0].inverseBind = glm::translate(glm::mat4(1), {-4, -3, 0});
    before.transform = after.transform = shape({-2, 3, 4});
    const auto boneShape = shape({-.8f, 1.6f, 1.2f});
    before.bones.push_back(glm::inverse(before.transform) * rigid({1, 2, 0}, 0, {0, 0, 1}) *
        boneShape * asset.skeleton[0].inverseBind);
    after.bones.push_back(glm::inverse(after.transform) * rigid({1, 2, 0}, 30, {0, 0, 1}) *
        boneShape * asset.skeleton[0].inverseBind);
    const double interval = double(period) / 60;
    const auto predicted = multiplayer::predictRagdollPose(before, after, asset, interval);
    const auto world = predicted.transform * predicted.bones[0] * glm::inverse(asset.skeleton[0].inverseBind);
    const auto expected = rigid({1, 2, 0}, 60, {0, 0, 1}) * boneShape;
    near(world[3], {1, 2, 0, 1}, "Prediction rotated a joint around the mesh origin");
    near(world * glm::vec4(1, 2, 3, 1), expected * glm::vec4(1, 2, 3, 1),
        "Prediction failed to advance angular motion from server ticks");
    sameShape(world, boneShape);
    sameShape(predicted.transform, after.transform);
    after.sleeping = true;
    require(same(multiplayer::predictRagdollPose(before, after, asset, interval), after),
        "Sleeping pose continued predicting");
}
}

int main() {
    try {
        exactEndpoints(); independentWorldMotion(); halfTurnKeepsVolume(); offOriginJointPivot(); incompatiblePosesStayCoherent();
        for (const std::uint64_t period : {6, 12}) predictionKeepsJointPivotAndAffineShape(period);
        std::cout << "Affine ragdoll interpolation and coherent pose fallback passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
