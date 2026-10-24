#include "zombie_ragdoll.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using viewer::RagdollPart;
constexpr std::size_t index(RagdollPart part) { return static_cast<std::size_t>(part); }

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

viewer::RagdollPose referencePose(glm::vec3 feet, float yaw) {
    const auto root = glm::translate(glm::mat4(1), feet) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0));
    const std::array<glm::vec3, viewer::ragdollPartCount> bones{{
        {0, 0.94f, 0}, {0, 1.02f, 0}, {0, 1.61f, 0},
        {0.2f, 1.5f, 0}, {0.48f, 1.5f, 0}, {-0.2f, 1.5f, 0}, {-0.48f, 1.5f, 0},
        {0.1f, 0.94f, 0}, {0.12f, 0.53f, 0}, {-0.1f, 0.94f, 0}, {-0.12f, 0.53f, 0}
    }};
    const std::array<glm::vec3, viewer::ragdollPartCount> tips{{
        {0, 1.02f, 0}, {0, 1.56f, 0}, {0, 1.79f, 0},
        {0.48f, 1.5f, 0}, {0.75f, 1.5f, 0}, {-0.48f, 1.5f, 0}, {-0.75f, 1.5f, 0},
        {0.12f, 0.53f, 0}, {0.14f, 0.015f, 0}, {-0.12f, 0.53f, 0}, {-0.14f, 0.015f, 0}
    }};
    viewer::RagdollPose result;
    for (std::size_t part = 0; part < viewer::ragdollPartCount; ++part) {
        // The real files have a centimeters-to-meters parent scale.
        result.bones[part] = root * glm::translate(glm::mat4(1), bones[part]) * glm::scale(glm::mat4(1), glm::vec3(0.01f));
        result.tips[part] = glm::vec3(root * glm::vec4(tips[part], 1));
    }
    return result;
}

bool close(const glm::mat4& a, const glm::mat4& b, float tolerance = 1e-4f) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > tolerance) return false;
    return true;
}

void activationUsesLivePoseAndPreservesRoot() {
    viewer::ZombieRagdollWorld world;
    const glm::vec3 feet(14, 3, -8);
    constexpr float yaw = 0.63f;
    auto reference = referencePose(feet, yaw);
    auto current = reference;
    const auto upper = index(RagdollPart::upperArmLeft), lower = index(RagdollPart::lowerArmLeft);
    const auto pivot = glm::vec3(reference.bones[upper][3]);
    const auto axis = glm::mat3(glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0))) * glm::vec3(0, 0, 1);
    const auto loweredArm = glm::translate(glm::mat4(1), pivot) *
        glm::rotate(glm::mat4(1), -0.8f, axis) * glm::translate(glm::mat4(1), -pivot);
    for (const auto part : {upper, lower}) {
        current.bones[part] = loweredArm * reference.bones[part];
        current.tips[part] = glm::vec3(loweredArm * glm::vec4(reference.tips[part], 1));
    }
    const auto handle = world.create(feet, yaw, {}, reference, current);
    const auto bodies = world.bodyTransforms(handle);
    const auto expectedArmCenter = (glm::vec3(current.bones[upper][3]) + current.tips[upper]) * 0.5f;
    require(glm::distance(glm::vec3(bodies[upper][3]), expectedArmCenter) < 1e-4f,
            "Physics must start with the animated arm lowered, not in reference T-pose");
    const auto expectedRoot = glm::translate(glm::mat4(1), feet) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0));
    require(close(world.rootTransform(handle), expectedRoot), "Activation must preserve the exact instance root");
    const auto initial = world.restBodyTransforms(handle);
    for (std::size_t part = 0; part < viewer::ragdollPartCount; ++part)
        require(close(initial[part], bodies[part]), "Captured body transforms must describe activation, not reference pose");
}

void lowFeetDoNotLaunchThePelvis() {
    viewer::ZombieRagdollWorld world;
    const auto pose = referencePose({}, 0);
    const auto handle = world.create({}, 0, {}, pose, pose);
    const auto initial = world.bodyTransforms(handle);
    const auto pelvis = index(RagdollPart::pelvis);
    for (int frame = 0; frame < 12; ++frame) {
        world.step(1.0f / 60.0f);
        const auto bodies = world.bodyTransforms(handle);
        require(bodies[pelvis][3].y <= initial[pelvis][3].y + 0.001f,
                "Foot capsule penetration must not launch the pelvis upward");
    }
}

void springPoseRecoversWithPlantedFeet() {
    auto simulation = std::make_shared<viewer::PhysicsWorld>();
    b3World_SetGravity(simulation->id(), {0, 0, 0});
    viewer::ZombieRagdollWorld world(simulation);
    auto reference = referencePose({}, 0), current = reference;
    const auto upper = index(RagdollPart::upperArmLeft), lower = index(RagdollPart::lowerArmLeft);
    const auto pivot = glm::vec3(reference.bones[upper][3]);
    const auto bend = glm::translate(glm::mat4(1), pivot) * glm::rotate(glm::mat4(1), -.7f, glm::vec3(0, 0, 1)) *
                      glm::translate(glm::mat4(1), -pivot);
    for (const auto p : {upper, lower}) {
        current.bones[p] = bend * reference.bones[p];
        current.tips[p] = glm::vec3(bend * glm::vec4(reference.tips[p], 1));
    }
    const auto handle = world.create({}, 0, {}, reference, current);
    world.holdPose(handle);
    const auto initial = world.bodyTransforms(handle);
    const auto relative = [&](const auto& bodies) {
        return glm::normalize(glm::quat_cast(glm::mat3(glm::inverse(bodies[index(RagdollPart::torso)]) * bodies[upper])));
    };
    const auto target = relative(initial);
    world.applyImpulse(handle, RagdollPart::lowerArmLeft, {0, 3, 0});
    float peak = 0, last = 0;
    for (int f = 0; f < 600; ++f) {
        world.step(1.f / 60);
        const auto bodies = world.bodyTransforms(handle);
        last = 2 * std::acos(std::clamp(std::abs(glm::dot(relative(bodies), target)), 0.f, 1.f));
        peak = std::max(peak, last);
    }
    require(peak > .02f && last < peak * .5f, "Spring ragdoll must resist displacement and recover its captured bent pose");
    for (auto part : {RagdollPart::lowerLegLeft, RagdollPart::lowerLegRight})
        require(close(world.bodyTransforms(handle)[index(part)], initial[index(part)], .003f),
                "Spring ragdoll feet must stay planted while the arm reacts");
}

void plantedFeetSupportImpactsAndTranslateWithTheirOwner() {
    auto simulation = std::make_shared<viewer::PhysicsWorld>();
    viewer::ZombieRagdollWorld world(simulation);
    const glm::vec3 feet(12, 3, -8);
    const auto pose = referencePose(feet, .63f);
    const auto handle = world.create(feet, .63f, {}, pose, pose);
    const auto initial = world.bodyTransforms(handle);
    const auto looseCounters = b3World_GetCounters(simulation->id());
    world.holdPose(handle);
    const auto pinnedCounters = b3World_GetCounters(simulation->id());
    require(pinnedCounters.bodyCount == looseCounters.bodyCount &&
            pinnedCounters.jointCount == looseCounters.jointCount,
            "Planting feet must reuse the owned lower-leg bodies");
    world.holdPose(handle);
    require(b3World_GetCounters(simulation->id()).jointCount == pinnedCounters.jointCount,
            "Repeated spring activation must not duplicate foot anchors");
    for (std::size_t p = 0; p < viewer::ragdollPartCount; ++p)
        require(close(world.bodyTransforms(handle)[p], initial[p]), "Planting feet must preserve the exact captured pose");
    world.applyImpulse(handle, RagdollPart::torso, {0, 0, 12});
    float upperBodyMotion = 0, peakTorsoLean = 0, torsoLean = 0;
    const auto torso = index(RagdollPart::torso);
    const auto initialUp = glm::normalize(glm::vec3(initial[torso][1]));
    for (int f = 0; f < 600; ++f) {
        world.step(1.f / 60);
        const auto bodies = world.bodyTransforms(handle);
        for (auto part : {RagdollPart::lowerLegLeft, RagdollPart::lowerLegRight}) {
            require(close(bodies[index(part)], initial[index(part)], .003f),
                    "Feet must not slide or tip under gravity and a punch impulse");
        }
        const auto up = glm::normalize(glm::vec3(bodies[torso][1]));
        torsoLean = std::acos(glm::clamp(glm::dot(initialUp, up), -1.f, 1.f));
        peakTorsoLean = std::max(peakTorsoLean, torsoLean);
        require(torsoLean < glm::radians(30.f),
                "The planted torso must not fold over under gravity after a punch");
        upperBodyMotion = std::max(upperBodyMotion, glm::distance(glm::vec3(bodies[torso][3]), glm::vec3(initial[torso][3])));
        require(bodies[index(RagdollPart::pelvis)][3].y > feet.y + .6f,
                "The planted spring ragdoll must stay standing after a punch");
    }
    std::cout << "Torso lean: peak " << glm::degrees(peakTorsoLean)
              << " degrees, settled " << glm::degrees(torsoLean) << " degrees\n";
    require(torsoLean < glm::radians(5.f), "The torso must recover near its captured posture after a punch");
    require(upperBodyMotion > .01f, "Planted feet must still allow the upper body to react to a punch");
    const glm::vec3 displacement(5, 2, -3);
    world.translate(handle, displacement);
    for (int f = 0; f < 120; ++f) world.step(1.f / 60);
    const auto translated = world.bodyTransforms(handle);
    for (auto part : {RagdollPart::lowerLegLeft, RagdollPart::lowerLegRight}) {
        auto expected = initial[index(part)];
        expected[3] += glm::vec4(displacement, 0);
        require(close(translated[index(part)], expected, .003f), "Translating a ragdoll must carry both foot anchors");
    }
    world.destroy(handle);
    const auto removed = b3World_GetCounters(simulation->id());
    require(removed.bodyCount == 0 && removed.jointCount == 0, "Removing a planted ragdoll must release its foot anchors");
}

void repeatedActivationsRemainFiniteAndMoving() {
    viewer::ZombieRagdollWorld world;
    for (int count = 0; count < 24; ++count) {
        const glm::vec3 feet((count % 6) * 8, count % 3, (count / 6) * 8);
        const auto pose = referencePose(feet, count * 0.17f);
        const auto handle = world.create(feet, count * 0.17f, {1, 0, 0}, pose, pose);
        const auto initial = world.bodyTransforms(handle);
        for (int frame = 0; frame < 60; ++frame) world.step(1.0f / 60.0f);
        const auto bodies = world.bodyTransforms(handle);
        float largestMovement = 0;
        for (std::size_t part = 0; part < viewer::ragdollPartCount; ++part) {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    require(std::isfinite(bodies[part][c][r]), "Repeated ragdolls must keep finite body transforms");
            require(glm::distance(glm::vec3(bodies[part][3]), feet) < 4,
                    "Repeated ragdoll bodies must stay near their spawn point");
            largestMovement = std::max(largestMovement, glm::distance(glm::vec3(bodies[part][3]), glm::vec3(initial[part][3])));
        }
        require(largestMovement > 0.1f, "Every new ragdoll must continue moving after repeated activation");
    }
}

void degenerateReferenceCannotCreateAnInvalidHinge() {
    viewer::ZombieRagdollWorld world;
    auto pose = referencePose({}, 0);
    const auto arm = index(RagdollPart::upperArmLeft);
    pose.tips[arm] = glm::vec3(pose.bones[arm][3]);
    bool rejected = false;
    try {
        world.create({}, 0, {}, pose, pose);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "A zero-length reference limb must be rejected before normalizing its hinge axis");
    require(world.count() == 0, "Rejected anatomy must not publish a partial ragdoll");
    const auto valid = referencePose({}, 0);
    world.create({}, 0, {}, valid, valid);
    world.step(1.0f / 60.0f);
    require(world.count() == 1, "The world must remain usable after rejecting invalid anatomy");
}
void motionRevisionsSleepWakeAndRemoval() {
    auto simulation = std::make_shared<viewer::PhysicsWorld>();
    viewer::ZombieRagdollWorld world(simulation);
    const auto handle = world.create({}, 0, {});
    const auto initialRevision = world.poseRevision(handle);
    world.step(1.0f / 120);
    require(world.poseRevision(handle) == initialRevision, "A substep must not dirty the pose");
    world.step(1.0f / 120);
    require(world.poseRevision(handle) > initialRevision, "Physics movement must dirty the owner");
    for (int frame = 0; frame < 3600 && !world.sleeping(handle); ++frame) world.step(1.0f / 60);
    require(world.sleeping(handle), "The fixture ragdoll must settle naturally");
    const auto settled = world.poseRevision(handle);
    for (int frame = 0; frame < 120; ++frame) world.step(1.0f / 60);
    require(world.poseRevision(handle) == settled, "Sleeping physics must retain its final palette revision");

    // Wake through a collision with an unrelated physics actor. No ragdoll API
    // or explicit invalidation is involved in this normal simulation change.
    const auto pelvis = world.bodyTransforms(handle)[index(RagdollPart::pelvis)][3];
    auto definition = b3DefaultBodyDef();
    definition.type = b3_dynamicBody;
    definition.position = {pelvis.x, pelvis.y + 1, pelvis.z};
    const auto falling = b3CreateBody(simulation->id(), &definition);
    auto shape = b3DefaultShapeDef();
    shape.density = 200;
    const b3Sphere sphere{{0, 0, 0}, 0.25f};
    b3CreateSphereShape(falling, &shape, &sphere);
    for (int frame = 0; frame < 90 && world.poseRevision(handle) == settled; ++frame) world.step(1.0f / 60);
    require(world.poseRevision(handle) > settled, "Collision wake-up must invalidate without manual notifications");
    b3DestroyBody(falling);

    const auto beforeTranslation = world.bodyTransforms(handle);
    const auto beforeRoot = world.rootTransform(handle);
    const auto beforeRevision = world.poseRevision(handle);
    world.translate(handle, {5, 2, -3});
    require(world.poseRevision(handle) > beforeRevision, "External translation must invalidate immediately");
    require(glm::distance(glm::vec3(world.rootTransform(handle)[3] - beforeRoot[3]), {5, 2, -3}) < 1e-4f,
            "Translation must move the root exactly once");
    const auto translated = world.bodyTransforms(handle);
    for (std::size_t part = 0; part < viewer::ragdollPartCount; ++part)
        require(glm::distance(glm::vec3(translated[part][3] - beforeTranslation[part][3]), {5, 2, -3}) < 1e-4f,
                "Owner translation must move every body");
    const auto revision = world.poseRevision(handle);
    world.applyImpulse(handle, RagdollPart::pelvis, {10, 0, 0});
    world.step(1.0f / 60);
    require(world.poseRevision(handle) > revision && !world.sleeping(handle), "Impulse must wake and invalidate");
    world.destroy(handle);
    require(!world.valid(handle) && world.count() == 0, "Removal must destroy and unregister the owner");
    require(b3World_GetCounters(simulation->id()).bodyCount == 0, "Removal must release ground and body resources");
    const auto replacement = world.create({}, 0, {});
    require(replacement != handle, "Destroyed handles must not alias new corpses");
    world.step(1.0f / 60);
    require(world.poseRevision(replacement) > 1, "Reused physics body storage must notify the new owner");
}
}

int main() {
    try {
        activationUsesLivePoseAndPreservesRoot();
        lowFeetDoNotLaunchThePelvis();
        springPoseRecoversWithPlantedFeet();
        plantedFeetSupportImpactsAndTranslateWithTheirOwner();
        repeatedActivationsRemainFiniteAndMoving();
        degenerateReferenceCannotCreateAnInvalidHinge();
        motionRevisionsSleepWakeAndRemoval();
        std::cout << "Zombie ragdoll tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie ragdoll test failed: " << error.what() << '\n';
        return 1;
    }
}
