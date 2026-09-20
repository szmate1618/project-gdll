#include "zombie_ragdoll.hpp"

#include <glm/gtc/matrix_transform.hpp>

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
}

int main() {
    try {
        activationUsesLivePoseAndPreservesRoot();
        lowFeetDoNotLaunchThePelvis();
        repeatedActivationsRemainFiniteAndMoving();
        degenerateReferenceCannotCreateAnInvalidHinge();
        std::cout << "Zombie ragdoll tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie ragdoll test failed: " << error.what() << '\n';
        return 1;
    }
}
