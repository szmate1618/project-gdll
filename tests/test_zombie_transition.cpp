#include "zombie_shooting_fixture.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "zombie_chase.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::size_t finger = 0, hand = 1, elbow = 6;

glm::mat4 translation(glm::vec3 position) { return glm::translate(glm::mat4(1), position); }

glm::mat4 around(glm::vec3 center, float radians) {
    return translation(center) * glm::rotate(glm::mat4(1), radians, {0, 0, 1}) * translation(-center);
}

viewer::AnimatedModel humanoid() {
    viewer::AnimatedModel asset;
    asset.duration = 1;
    asset.frameCount = 2;
    // Deliberately put unmapped children before their parents in glTF node order.
    const std::array<glm::vec3, 16> origins{{
        {-0.95f, 1.45f, 0}, {-0.85f, 1.45f, 0},
        {0, 1, 0}, {0, 1.25f, 0}, {0, 1.65f, 0},
        {-0.25f, 1.45f, 0}, {-0.55f, 1.45f, 0},
        {0.25f, 1.45f, 0}, {0.55f, 1.45f, 0},
        {-0.12f, 1, 0}, {-0.12f, 0.55f, 0},
        {0.12f, 1, 0}, {0.12f, 0.55f, 0},
        {0.85f, 1.45f, 0}, {-0.12f, 0.08f, 0}, {0.12f, 0.08f, 0}
    }};
    const std::array<int, 16> parents{{1, 6, -1, 2, 3, 3, 5, 3, 7, 2, 9, 2, 11, 8, 10, 12}};
    asset.skeleton.resize(origins.size());
    for (std::size_t bone = 0; bone < origins.size(); ++bone) {
        auto& node = asset.skeleton[bone];
        node.name = "bone_" + std::to_string(bone);
        node.parent = parents[bone];
        node.referenceWorld = translation(origins[bone]);
        node.inverseBind = glm::inverse(node.referenceWorld);
    }
    asset.skeleton[hand].name = "left_hand";
    asset.skeleton[13].name = "right_hand";
    asset.skeleton[14].name = "left_foot";
    asset.skeleton[15].name = "right_foot";
    for (std::size_t part = 0; part < viewer::ragdollPartCount; ++part)
        asset.ragdollBones[part] = static_cast<int>(part + 2);

    for (int frame = 0; frame < 2; ++frame) {
        const float amount = static_cast<float>(frame);
        const auto body = translation({0.03f * amount, -0.02f * amount, 0});
        const auto arm = around(origins[5], 0.35f + 0.3f * amount);
        const auto wrist = around(origins[hand], -0.3f - 0.2f * amount);
        const auto curl = around(origins[finger], 0.4f + 0.2f * amount);
        for (std::size_t bone = 0; bone < origins.size(); ++bone) {
            auto pose = asset.skeleton[bone].referenceWorld;
            if (bone == finger) pose = curl * pose;
            if (bone == finger || bone == hand) pose = wrist * pose;
            if (bone == finger || bone == hand || bone == 5 || bone == elbow) pose = arm * pose;
            asset.boneFrames.push_back(body * pose);
        }
    }

    viewer::Primitive primitive;
    viewer::AnimatedPrimitive animation;
    for (std::size_t bone = 0; bone < origins.size(); ++bone) {
        viewer::Vertex vertex;
        vertex.position = origins[bone] + glm::vec3(0.03f, 0.02f, 0.01f);
        primitive.vertices.push_back(vertex);
        viewer::SkinningVertex skin;
        skin.joints[0] = static_cast<std::uint32_t>(bone);
        skin.weights[0] = 1;
        animation.skinning.push_back(skin);
    }
    auto blendedVertex = primitive.vertices[hand];
    blendedVertex.position.x += 0.04f;
    primitive.vertices.push_back(blendedVertex);
    viewer::SkinningVertex blended;
    blended.joints[0] = elbow;
    blended.weights[0] = 0.35f;
    blended.joints[4] = hand;
    blended.weights[4] = 0.45f;
    blended.joints[5] = finger;
    blended.weights[5] = 0.2f;
    animation.skinning.push_back(blended);
    for (std::size_t frame = 0; frame < 2; ++frame) {
        for (std::size_t v = 0; v < primitive.vertices.size(); ++v) {
            glm::vec4 position(0);
            for (std::size_t influence = 0; influence < 8; ++influence) {
                const auto& skin = animation.skinning[v];
                const auto joint = skin.joints[influence];
                position += asset.boneFrames[frame * origins.size() + joint] * asset.skeleton[joint].inverseBind *
                    glm::vec4(primitive.vertices[v].position, 1) * skin.weights[influence];
            }
            animation.frames.push_back({position, {0, 1, 0, 0}});
        }
    }
    asset.model.primitives.push_back(std::move(primitive));
    asset.animation.push_back(std::move(animation));
    return asset;
}

glm::mat4 worldBone(const viewer::ZombieInstance& instance, const viewer::AnimatedModel& asset,
                     std::size_t bone) {
    return instance.transform * instance.ragdollBones[bone] * glm::inverse(asset.skeleton[bone].inverseBind);
}

void requireMatrixNear(const glm::mat4& actual, const glm::mat4& expected, const char* message) {
    for (int column = 0; column < 4; ++column)
        require(glm::length(actual[column] - expected[column]) < 2e-4f, message);
}

void checkTransition(double seconds, float phase, std::size_t first, std::size_t second, float fraction) {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    const auto& asset = layer.assets[0];
    viewer::ZombieInstance instance;
    instance.restRoot = translation({13, 4, -19}) * glm::rotate(glm::mat4(1), -0.9f, {0, 1, 0});
    instance.restTransform = instance.restRoot * glm::scale(glm::mat4(1), glm::vec3(1.25f)) *
                             translation({0.2f, -0.05f, -0.15f});
    instance.transform = instance.restTransform;
    instance.phase = phase;
    layer.instances.push_back(instance);
    const auto center = glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1));
    require(viewer::test::shootToKill(layer, center + glm::vec3(0, 0, 3), {0, 0, -1}, seconds), "Synthetic humanoid must be hit");
    const auto& ragdoll = layer.instances[0];
    require(ragdoll.ragdoll && ragdoll.activationPose.size() == asset.skeleton.size(),
            "Activation must capture the complete displayed skeleton");
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone) {
        const auto expected = asset.boneFrames[first * asset.skeleton.size() + bone] * (1 - fraction) +
                              asset.boneFrames[second * asset.skeleton.size() + bone] * fraction;
        requireMatrixNear(ragdoll.activationPose[bone], expected, "Activation must capture the requested time and phase");
    }
    const auto& vertices = asset.model.primitives[0].vertices;
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        glm::vec4 skinned(0);
        const auto& skin = asset.animation[0].skinning[v];
        for (std::size_t influence = 0; influence < 8; ++influence)
            skinned += ragdoll.ragdollBones[skin.joints[influence]] * glm::vec4(vertices[v].position, 1) *
                       skin.weights[influence];
        const auto baked = asset.animation[0].frames[first * vertices.size() + v].position * (1 - fraction) +
                           asset.animation[0].frames[second * vertices.size() + v].position * fraction;
        require(glm::length(ragdoll.transform * skinned - instance.restTransform * baked) < 2e-4f,
                "Activation must not move displayed vertices, including blended and unmapped joints");
    }

    const auto capturedHand = glm::inverse(ragdoll.activationPose[elbow]) * ragdoll.activationPose[hand];
    const auto capturedFinger = glm::inverse(ragdoll.activationPose[hand]) * ragdoll.activationPose[finger];
    const auto initialElbow = worldBone(ragdoll, asset, elbow);
    for (int step = 0; step < 30; ++step) {
        layer.update(1.0f / 120);
        const auto handWorld = worldBone(ragdoll, asset, hand);
        const auto elbowWorld = worldBone(ragdoll, asset, elbow);
        const auto fingerWorld = worldBone(ragdoll, asset, finger);
        requireMatrixNear(glm::inverse(elbowWorld) * handWorld, capturedHand,
                          "Unmapped hand must follow its moving parent in the captured pose");
        requireMatrixNear(glm::inverse(handWorld) * fingerWorld, capturedFinger,
                          "Unmapped finger must preserve its captured curl through multiple ancestors");
    }
    require(glm::length(worldBone(ragdoll, asset, elbow)[3] - initialElbow[3]) > 0.01f,
            "The parent must actually move while checking inherited hand and finger poses");
}

void unknownRigFollowsFallbackMotion() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    auto& asset = layer.assets[0];
    asset.ragdollBones.fill(-1);
    viewer::ZombieInstance instance;
    instance.restRoot = translation({5, 2, 3}) * glm::rotate(glm::mat4(1), 0.6f, {0, 1, 0});
    instance.restTransform = instance.restRoot * glm::scale(glm::mat4(1), glm::vec3(1.3f));
    instance.transform = instance.restTransform;
    layer.instances.push_back(instance);
    const auto center = glm::vec3(instance.restRoot * glm::vec4(0, 0.9f, 0, 1));
    require(viewer::test::shootToKill(layer, center + glm::vec3(0, 0, 3), {0, 0, -1}, 0.25),
            "An unmapped rig must still support fallback activation");
    const auto& ragdoll = layer.instances[0];
    constexpr std::size_t root = 2; // The fixture's root has one fully weighted vertex.
    const auto& vertices = asset.model.primitives[0].vertices;
    const glm::vec4 vertex(vertices[root].position, 1);
    const auto baked = (asset.animation[0].frames[root].position +
                        asset.animation[0].frames[vertices.size() + root].position) * 0.5f;
    const auto initial = ragdoll.transform * ragdoll.ragdollBones[root] * vertex;
    require(glm::length(initial - instance.restTransform * baked) < 2e-4f,
            "Fallback activation must preserve the displayed root-bound vertex");
    for (int step = 0; step < 30; ++step) {
        layer.update(1.0f / 120);
        const auto actual = ragdoll.transform * ragdoll.ragdollBones[root] * vertex;
        require(glm::length(actual - ragdoll.transform * baked) < 2e-4f,
                "An unmapped root must follow fallback instance motion instead of freezing in world space");
    }
    require(glm::length(ragdoll.transform * ragdoll.ragdollBones[root] * vertex - initial) > 0.01f,
            "Fallback physics must actually move the visible root-bound vertex");
}

void unmatchedRigFollowsFallbackBody() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    auto& asset = layer.assets[0];
    asset.ragdollBones.fill(-1);
    viewer::ZombieInstance instance;
    instance.restRoot = translation({3, 0, -7});
    instance.transform = instance.restTransform = instance.restRoot;
    instance.phase = 0.125f;
    layer.instances.push_back(instance);
    const auto expectedPose = viewer::sampleAnimatedPose(asset, 0.125, instance.phase);
    require(viewer::test::shootToKill(layer, {3, 0.9f, -4}, {0, 0, -1}, 0.125), "Unknown rig must use the fallback body");
    const auto& ragdoll = layer.instances[0];
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
        requireMatrixNear(worldBone(ragdoll, asset, bone), instance.transform * expectedPose[bone],
                          "Unknown rigs must also preserve their captured pose at activation");
    const auto initialBones = ragdoll.ragdollBones;
    const auto initialHand = worldBone(ragdoll, asset, hand);
    const auto initialTransform = ragdoll.transform;
    for (int step = 0; step < 30; ++step) layer.update(1.0f / 60);
    const auto motion = ragdoll.transform[3] - initialTransform[3];
    require(glm::length(motion) > 0.01f, "Fallback physics must move the character");
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
        requireMatrixNear(ragdoll.ragdollBones[bone], initialBones[bone],
                          "An unmatched skeleton must retain its pose relative to the moving instance");
    require(glm::length(worldBone(ragdoll, asset, hand)[3] - initialHand[3] - motion) < 2e-4f,
            "Unmapped skeleton roots must follow the body rather than freeze in world space");
}

void shotWhileWalking() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    auto& animated = layer.assets[0];
    animated.runClip = viewer::BakedAnimationClip{"running", 1, 2, 2, animated.boneFrames};
    for (auto& bone : animated.runClip->boneFrames) bone = translation({0, 0.1f, 0}) * bone;
    viewer::ZombieInstance instance;
    instance.restRoot = translation({0, 4, 0}) * glm::rotate(glm::mat4(1), glm::radians(90.0f), {0, 1, 0});
    instance.transform = instance.restTransform = instance.restRoot *
        glm::scale(glm::mat4(1), glm::vec3(1.25f));
    instance.phase = 0.125f;
    layer.instances.push_back(instance);
    const viewer::CollisionWorld world(viewer::Model{});
    layer.chase(2, {10, 4, 0}, world, 0.125);
    require(layer.instances[0].walking, "The fixture must be walking when shot");
    require(!layer.shoot({0, 4.9f, 3}, {0, 0, -1}, 0.125),
            "The old spawn position must no longer be hittable");
    const auto displayed = layer.instances[0];
    const auto& asset = layer.assets[0];
    const auto displayedPose = viewer::sampleZombiePose(asset, displayed, 0.125);
    require(viewer::test::shootToKill(layer, {6, 4.9f, 3}, {0, 0, -1}, 0.125),
            "The moving zombie must be hittable at its current position");
    const auto& ragdoll = layer.instances[0];
    require(ragdoll.ragdoll && !ragdoll.walking, "A shot must stop walking and start physics");
    require(!layer.behaviors.contains(0), "A shot must immediately remove the behavior object");
    for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone) {
        requireMatrixNear(ragdoll.activationPose[bone], displayedPose[bone],
                          "Capture the displayed gait instead of reverting to idle");
        requireMatrixNear(worldBone(ragdoll, asset, bone), displayed.transform * displayedPose[bone],
                          "Walking-to-ragdoll activation must preserve the world pose");
    }
    layer.chase(1, {0, 4, 0}, world);
    require(layer.instances[0].walkSeconds == displayed.walkSeconds,
            "A shot zombie must not advance its walk cycle");
    layer.update(1.0f / 60);
}

void headshotFollowsDisplayedPose(bool walking) {
    viewer::ZombieLayer layer;
    auto asset = humanoid();
    // Move the skull outside the old torso sphere and its reference position.
    // Interpolation and phase put it at x=1.53 rather than either keyframe.
    for (std::size_t frame = 0; frame < 2; ++frame) {
        asset.boneFrames[frame * asset.skeleton.size() + 4] =
            translation({static_cast<float>(frame) * 3, 0.45f, 0}) *
            asset.skeleton[4].referenceWorld;
        auto& headVertex = asset.animation[0].frames[frame * asset.model.primitives[0].vertices.size() + 4];
        headVertex.position = asset.boneFrames[frame * asset.skeleton.size() + 4] *
            asset.skeleton[4].inverseBind * glm::vec4(asset.model.primitives[0].vertices[4].position, 1);
    }
    viewer::ZombieInstance instance;
    instance.phase = 0.125f;
    instance.transform = instance.restTransform = instance.restRoot = translation({0, 3, -4});
    instance.walking = walking;
    if (walking) {
        instance.walkSeconds = 0.125;
        asset.walkClip = viewer::BakedAnimationClip{"walk", 1, 2, 2, asset.boneFrames};
        for (std::size_t frame = 0; frame < 2; ++frame)
            asset.walkClip->boneFrames[frame * asset.skeleton.size() + 4] =
                translation({0.7f, 0, 0}) * asset.walkClip->boneFrames[frame * asset.skeleton.size() + 4];
    }
    layer.assets.push_back(std::move(asset));
    layer.instances.push_back(instance);
    require(!layer.shoot({0, 5.12f, 0}, {0, 0, -1}, 0.125),
            "A shot at the old head position must miss the displayed head");
    const float skullX = walking ? 2.23f : 1.53f;
    // A second, farther target verifies selection using the actual head ray.
    auto farther = instance;
    farther.transform = farther.restTransform = farther.restRoot = translation({0, 3, -9});
    layer.instances.insert(layer.instances.begin(), farther);
    require(viewer::test::shootToKill(layer, {skullX, 5.12f, 0}, {0, 0, -2}, 0.125),
            "A headshot must follow the displayed pose, phase, and instance transform");
    require(layer.instances[1].ragdoll && !layer.instances[0].ragdoll,
            "A headshot must activate only the nearest living zombie");
    require(!layer.instances[1].walking, "A headshot must stop pursuit");
}

void layerOwnsPoseInvalidation() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    layer.instances.emplace_back();
    require(viewer::test::shootToKill(layer, {0, 0.9f, 3}, {0, 0, -1}), "Fixture must activate physics");
    require(layer.ragdollIds() == std::vector<std::size_t>{0} && layer.ragdollCount() == 1,
            "Shooting must publish the corpse ID immediately");
    const auto initial = layer.instances[0].ragdollPoseVersion;
    auto renderRevision = layer.renderUpdates().since(0).revision;
    layer.renderUpdates().acknowledge(renderRevision);
    layer.synchronizePhysics();
    require(layer.instances[0].ragdollPoseVersion == initial, "Unchanged physics must skip palette reconstruction");
    require(!layer.renderUpdates().since(renderRevision).ids, "Unchanged physics must not dirty renderer data");
    layer.update(1.0f / 60);
    require(layer.instances[0].ragdollPoseVersion > initial, "Moving physics must publish a new palette version");
    auto changes = layer.renderUpdates().since(renderRevision);
    require(changes.ids && *changes.ids == std::vector<std::size_t>{0} && layer.renderUpdates().boundsChanged(0),
            "Physics motion must publish exactly the changed corpse's render bounds");
    const auto before = layer.instances[0].transform;
    const auto revision = layer.instances[0].ragdollPoseVersion;
    layer.translateRagdoll(0, {7, 3, -2});
    require(glm::distance(glm::vec3(layer.instances[0].transform[3] - before[3]), {7, 3, -2}) < 1e-4f,
            "Owner translation must publish matching rendering placement");
    require(layer.instances[0].ragdollPoseVersion > revision, "Owner translation must invalidate the GPU pose");
    bool rejected = false;
    try { layer.setRagdollPalette(0, layer.instances[0].ragdollBones); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "External palette replacement must not bypass a physics owner");
    layer.removeRagdoll(0);
    require(layer.ragdollIds().empty() && layer.ragdollCount() == 0,
            "Removing a physics corpse must immediately remove its membership");
    layer.update(1.0f / 60);
    layer.rebuildSpatialIndex();
    require(layer.instances[0].removed && layer.ragdollCount() == 0, "Owner removal must leave a stable tombstone");
    require(!layer.shoot({7, 3.9f, 1}, {0, 0, -1}), "Removed corpses must not become living targets again");
}

void renderChangesFollowBehavior() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    layer.instances.emplace_back();
    layer.rebuildSpatialIndex();
    auto revision = layer.renderUpdates().since(0).revision;
    layer.renderUpdates().acknowledge(revision);
    viewer::CollisionWorld world(viewer::Model{});
    layer.chase(.1f, {0, 0, 10}, world);
    auto changes = layer.renderUpdates().since(revision);
    require(changes.ids && *changes.ids == std::vector<std::size_t>{0} && layer.renderUpdates().boundsChanged(0),
            "Locomotion must notify the renderer automatically");
    layer.chase(5, {0, 0, 10}, world);
    changes = layer.renderUpdates().since(revision);
    revision = changes.revision;
    layer.renderUpdates().acknowledge(revision);
    require(!layer.instances[0].walking, "Fixture must reach attack range and stop");
    layer.chase(.1f, {0, 0, 10}, world);
    changes = layer.renderUpdates().since(revision);
    require(changes.ids && *changes.ids == std::vector<std::size_t>{0} && layer.renderUpdates().boundsChanged(0),
            "Physical attack wind-up must publish its changed root and bounds");
    layer.chase(1.f, {0, 0, 10}, world); // Complete this cycle; the cooldown retains a stationary idle pose.
    revision = layer.renderUpdates().since(revision).revision;
    layer.renderUpdates().acknowledge(revision);
    layer.chase(.01f, {0, 0, 10}, world);
    require(!layer.renderUpdates().since(revision).ids,
            "A stationary attack cooldown must not dirty renderer data every behavior step");
}

void corpseMembership() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(humanoid());
    layer.instances.resize(100000);
    layer.attachPhysics(std::make_shared<viewer::PhysicsWorld>());
    const auto palette = std::vector<glm::mat4>(layer.assets[0].skeleton.size(), glm::mat4(1));
    const auto check = [](const viewer::ZombieLayer& crowd, std::vector<std::size_t> expected) {
        auto actual = crowd.ragdollIds();
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        require(actual == expected && crowd.ragdollCount() == expected.size(),
                "Corpse membership must contain precisely the live corpse IDs without duplicates");
        for (const auto id : actual)
            require(crowd.instances[id].ragdoll && !crowd.instances[id].removed, "Corpse IDs must reference live corpses");
    };
    check(layer, {});
    layer.setRagdollPalette(70000, palette);
    layer.setRagdollPalette(7, palette);
    layer.setRagdollPalette(99999, palette);
    layer.setRagdollPalette(7, palette); // Pose updates must not duplicate an ID.
    check(layer, {7, 70000, 99999});
    bool rejected = false;
    try { layer.setRagdollPalette(123, {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Malformed palettes must not create corpse membership");
    check(layer, {7, 70000, 99999});
    const auto version = layer.instances[7].ragdollPoseVersion;
    layer.synchronizePhysics(); // Externally driven corpses have no physics body.
    require(layer.instances[7].ragdollPoseVersion == version, "Physics synchronization must preserve external palettes");
    layer.removeRagdoll(7); // Swap-removes the middle member.
    layer.removeRagdoll(99999); // Removes the member moved into its slot.
    layer.removeRagdoll(7); // Repeated removal is harmless.
    layer.removeRagdoll(123); // Living zombies stay living.
    check(layer, {70000});
    require(layer.instances.size() == 100000 && layer.instances[7].removed && layer.instances[99999].removed &&
            !layer.instances[123].removed && !layer.instances[123].ragdoll,
            "Corpse list compaction must preserve population IDs and living instances");
    layer.setRagdollPalette(60000, palette);
    auto moved = std::move(layer);
    check(moved, {60000, 70000});
    viewer::ZombieLayer assigned;
    assigned = std::move(moved);
    assigned.removeRagdoll(70000);
    check(assigned, {60000});
    assigned.rebuildSpatialIndex();
    check(assigned, {60000});
    // Initial/bulk fixtures can edit flags, then explicitly rebuild all indices.
    assigned.instances[123].ragdoll = true;
    assigned.instances[123].ragdollBones = palette;
    assigned.instances[60000].removed = true;
    assigned.rebuildSpatialIndex();
    check(assigned, {123});
    assigned.removeRagdoll(123);
    check(assigned, {});
    rejected = false;
    try { assigned.setRagdollPalette(7, palette); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected && assigned.ragdollCount() == 0, "Removed corpse IDs must remain tombstones");
}

void animationMotionOnDeath(int clip, bool published, int death, bool loopStart = false, bool stationary = false) {
    viewer::ZombieLayer layer;
    auto asset = humanoid();
    const auto frames = [&](float factor) {
        std::vector<glm::mat4> result;
        for (int frame = 0; frame < 2; ++frame)
            for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
                result.push_back(translation({0, 0, stationary ? 0 : frame * factor * .05f * (bone + 1)}) *
                                 asset.skeleton[bone].referenceWorld);
        return result;
    };
    asset.boneFrames = frames(1);
    asset.walkClip = viewer::BakedAnimationClip{"walk", 1, 2, 2, frames(2)};
    asset.runClip = viewer::BakedAnimationClip{"run", 1, 2, 4, frames(3)};
    layer.assets.push_back(std::move(asset));
    layer.instances.emplace_back();
    auto& instance = layer.instances[0];
    glm::vec3 feet(700, 3, -2600);
    instance.restRoot = translation(feet) * glm::rotate(glm::mat4(1), glm::radians(90.f), {0, 1, 0});
    instance.restTransform = instance.transform = instance.restRoot * glm::scale(glm::mat4(1), glm::vec3(1.25f));
    instance.chaseNormalization = glm::inverse(instance.restRoot) * instance.restTransform;
    const viewer::CollisionWorld collision(viewer::Model{});
    const float travelSpeed = clip == 0 ? 0 : clip == 1 ? 1.2f : 3.f;
    viewer::moveZombieToward(instance, layer.assets[0], feet + glm::vec3(10, 0, 0), travelSpeed, .1f, collision, 0);
    feet = glm::vec3(instance.restRoot[3]);
    instance.walking = clip != 0;
    instance.running = clip == 2;
    instance.walkSeconds = loopStart ? 0 : .25;
    if (published) {
        instance.displayedAnimationFrame = loopStart ? 0 : .5f;
        instance.displayedAnimationClip = clip;
        // A displayed locomotion clip must win over a newer simulation state.
        instance.walking = false;
        instance.walkSeconds = .8;
    }
    auto simulation = std::make_shared<viewer::PhysicsWorld>();
    layer.attachPhysics(simulation);
    if (death == 0)
        require(viewer::test::shootToKill(layer, feet + glm::vec3(0, .9f, 3), {0, 0, -1}, loopStart ? 0 : .25),
                "Motion fixture must die from ordinary health damage");
    else if (death == 1)
        require(layer.explode(feet + glm::vec3(0, .9f, 2), 8, loopStart ? 0 : .25, collision) == 1,
                "Motion fixture must die from a blast");
    else {
        viewer::ZombieJointCollider joint;
        joint.bone = layer.assets[0].ragdollBones[std::size_t(viewer::RagdollPart::head)];
        joint.part = viewer::RagdollPart::head;
        joint.radius = .1f;
        layer.dismembermentAssets.resize(1);
        layer.dismembermentAssets[0].joints.push_back(joint);
        const auto pose = viewer::sampleZombiePose(layer.assets[0], instance, loopStart ? 0 : .25);
        const auto center = glm::vec3(instance.restTransform * pose[std::size_t(joint.bone)][3]);
        require(layer.shoot(center + glm::vec3(0, 0, 3), {0, 0, -1}, loopStart ? 0 : .25) && instance.severedJoint == 0,
                "Motion fixture must die from dismemberment");
    }
    std::vector<float> velocities;
    b3World_OverlapAABB(simulation->id(), {{690, -10, -2610}, {710, 20, -2590}}, b3DefaultQueryFilter(),
        [](b3ShapeId shape, void* context) {
            const auto body = b3Shape_GetBody(shape);
            if (b3Body_GetType(body) == b3_dynamicBody)
                static_cast<std::vector<float>*>(context)->push_back(b3Body_GetLinearVelocity(body).x);
            return true;
        }, &velocities);
    std::vector<float> expected;
    for (const auto bone : layer.assets[0].ragdollBones)
        expected.push_back(travelSpeed + (stationary ? 0 : (loopStart ? -1 : 1) * 1.25f * (clip + 1) * .1f * (bone + 1)));
    std::sort(expected.begin(), expected.end());
    std::sort(velocities.begin(), velocities.end());
    require(velocities.size() == expected.size(), "Every body must receive its own animation motion estimate");
    for (std::size_t part = 0; part < expected.size(); ++part)
        require(std::abs(velocities[part] - expected[part]) < .02f,
                "Death must inherit per-body animation velocity in world meters, independently of the shot/blast");
}

} // namespace

int main() {
    try {
        checkTransition(0.125, 0.125f, 0, 1, 0.5f);
        checkTransition(0.625, 0.25f, 1, 0, 0.75f);
        checkTransition(1e12 + 0.125, 0.125f, 0, 1, 0.5f);
        unmatchedRigFollowsFallbackBody();
        unknownRigFollowsFallbackMotion();
        shotWhileWalking();
        layerOwnsPoseInvalidation();
        renderChangesFollowBehavior();
        corpseMembership();
        headshotFollowsDisplayedPose(false);
        headshotFollowsDisplayedPose(true);
        for (int clip = 0; clip < 3; ++clip)
            for (const bool published : {false, true})
                for (int death = 0; death < 3; ++death)
                    animationMotionOnDeath(clip, published, death);
        animationMotionOnDeath(1, true, 0, true);
        animationMotionOnDeath(0, false, 0, false, true);
        std::cout << "Zombie activation transition tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie transition test failed: " << error.what() << '\n';
        return 1;
    }
}
