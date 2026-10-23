#include "zombie_animation.hpp"
#include "zombie_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::ZombieLayer fixture() {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    auto& asset = layer.assets[0];
    asset.skeleton.resize(1);
    asset.skeleton[0].name = "joint";
    asset.skeleton[0].referenceWorld = glm::translate(glm::mat4(1), {2, 1, 0});
    asset.skeleton[0].inverseBind = glm::inverse(asset.skeleton[0].referenceWorld);
    asset.frameCount = 2; asset.duration = 1;
    asset.boneFrames.assign(2, asset.skeleton[0].referenceWorld);
    // Deliberately far outside both existing damage proxy spheres.
    viewer::ZombieJointCollider joint;
    joint.name = "left_elbow"; joint.bone = 0; joint.part = viewer::RagdollPart::lowerArmLeft;
    joint.radius = .1f;
    layer.dismembermentAssets.resize(1);
    layer.dismembermentAssets[0].joints.push_back(joint);
    layer.instances.emplace_back();
    return layer;
}
void jointHitAndCleanup() {
    auto layer = fixture();
    layer.behaviors.create(0, 0);
    require(layer.shoot({0, .9f, 3}, {0, 0, -1}) && layer.damage().injury(0)->health == 9,
            "Ordinary body hits must still use health");
    require(!layer.shoot({2.11f, 1, 3}, {0, 0, -1}) && !layer.instances[0].ragdoll,
            "A ray just outside the joint must miss");
    require(layer.shoot({2, 1, 3}, {0, 0, -1}), "A joint outside the original body proxy must be hittable");
    require(layer.instances[0].severedJoint == 0 && layer.instances[0].ragdoll &&
            layer.instances[0].physicsRagdoll && !layer.behaviors.contains(0) && !layer.damage().injury(0),
            "Joint hit must kill immediately, release behavior and remove the existing injury");
    require(!layer.shoot({2, 1, 3}, {0, 0, -1}), "Dead joint regions must leave the shooting index");
    auto fresh = fixture();
    require(fresh.shoot({2, 1, 3}, {0, 0, -1}) && !fresh.damage().injury(0),
            "A first-hit dismemberment must not create a health injury");
}
void animatedAffineColliders() {
    auto layer = fixture();
    auto& asset = layer.assets[0];
    asset.boneFrames[1] = glm::translate(glm::mat4(1), {6, 3, 0});
    layer.instances[0].phase = .25f;
    layer.instances[0].transform = glm::translate(glm::mat4(1), {10, 5, -7}) *
        glm::scale(glm::mat4(1), {-2, .7f, 1.5f});
    layer.instances[0].transform[1].x += .3f;
    const auto pose = viewer::sampleZombiePose(asset, layer.instances[0], 0);
    const auto& joint = layer.dismembermentAssets[0].joints[0];
    const auto center = glm::vec3(layer.instances[0].transform * viewer::zombieJointTransform(joint, pose) * glm::vec4(0, 0, 0, 1));
    float distance = 0;
    require(viewer::zombieJointRay(center + glm::vec3(0, 0, 3), {0, 0, -1}, layer.instances[0].transform,
                                 joint, pose, distance) && std::abs(distance - 2.85f) < .001f,
            "Joint rays must preserve world distances through affine transforms");
    require(layer.shoot(center + glm::vec3(0, 0, 3), {0, 0, -1}),
            "Prepared broad bounds must enclose animated joint colliders");
    auto blocked = fixture();
    viewer::Model model; viewer::Primitive wall;
    for (const auto p : {glm::vec3(1, 0, 1), {3, 0, 1}, {3, 3, 1}, {1, 3, 1}}) {
        viewer::Vertex v; v.position = p; wall.vertices.push_back(v);
    }
    wall.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(wall); model.draws.push_back({});
    const viewer::CollisionWorld world(model);
    require(!blocked.shootWithOcclusion({2, 1, 3}, {0, 0, -1}, 0, world) &&
            !blocked.instances[0].ragdoll && !blocked.damage().injury(0), "Walls must block joint hits");
}
void releasedConstraint() {
    viewer::ZombieRagdollWorld constrained, severed;
    const auto a = constrained.create({0, 0, 0}, 0, {0, 0, 0});
    const auto b = severed.create({0, 0, 0}, 0, {0, 0, 0});
    const auto before = severed.bodyTransforms(b);
    severed.sever(b, viewer::RagdollPart::lowerArmLeft);
    require(before == severed.bodyTransforms(b), "Releasing a joint must preserve the activation pose");
    constrained.applyImpulse(a, viewer::RagdollPart::lowerArmLeft, {12, 3, 0});
    severed.applyImpulse(b, viewer::RagdollPart::lowerArmLeft, {12, 3, 0});
    for (int i = 0; i < 90; ++i) { constrained.step(1.0f / 120); severed.step(1.0f / 120); }
    const auto attached = constrained.bodyTransforms(a), detached = severed.bodyTransforms(b);
    const auto gap = [](const auto& pose) { return glm::distance(glm::vec3(pose[3][3]), glm::vec3(pose[4][3])); };
    require(gap(detached) > gap(attached) + 1, "Detached forearm must move independently of its upper arm");

    const auto direction = glm::normalize(glm::vec3(1, .5f, -2));
    for (const auto caliber : {viewer::ShotCaliber::low, viewer::ShotCaliber::high})
        for (int sample = 0; sample < 32; ++sample) {
            const auto velocity = viewer::zombieSeverImpulse(caliber, direction, viewer::RagdollPart::head, {2, 0, 0});
            const float speed = glm::length(velocity);
            require(speed >= (caliber == viewer::ShotCaliber::low ? 4.0f : 10.0f) - .001f &&
                    speed <= (caliber == viewer::ShotCaliber::low ? 8.0f : 16.0f) + .001f &&
                    glm::length(glm::cross(velocity, direction)) < .001f,
                    "Heads must retain the shot direction and use the doubled caliber bounds");
        }
}
void outwardLaunch() {
    for (const auto caliber : {viewer::ShotCaliber::low, viewer::ShotCaliber::high}) {
        const float minimum = caliber == viewer::ShotCaliber::low ? 4.f : 10.f;
        const float maximum = caliber == viewer::ShotCaliber::low ? 8.f : 16.f;
        for (const auto offset : {glm::vec3(3, 0, 0), glm::vec3(-2, 0, 0), glm::vec3(0, -4, 0)}) {
            const auto impulse = viewer::zombieSeverImpulse(caliber, {0, 0, -1}, viewer::RagdollPart::lowerArmLeft, offset);
            const float shot = -impulse.z;
            require(shot >= minimum && shot <= maximum, "Outward launch must preserve the doubled shot impulse");
            require(glm::length(impulse - glm::vec3(0, 0, -shot) - glm::normalize(offset) * shot) < .001f,
                    "Non-head parts must get an equal-strength impulse away from the posed zombie center");
        }
        const auto centered = viewer::zombieSeverImpulse(caliber, {0, 0, -1}, viewer::RagdollPart::torso, {});
        require(centered.x == 0 && centered.y == 0 && -centered.z >= minimum && -centered.z <= maximum,
                "A part at the zombie center must keep a finite shot-only impulse");
    }
}
void preparedAsset(const std::filesystem::path& path) {
    auto layer = viewer::loadZombieLayer(path, std::vector<glm::mat4>{glm::mat4(1)});
    require(layer.dismembermentAssets.size() == 1 && layer.dismembermentAssets[0].joints.size() == 10 &&
            layer.dismembermentAssets[0].meshes.size() == 10, "Prepared original must load ten joint variants");
    const auto& prepared = layer.dismembermentAssets[0];
    for (const auto& mesh : prepared.meshes) {
        require(mesh.model.primitives.size() == 2, "Prepared variant must contain the exterior and cut caps");
        const auto& cap = mesh.model.primitives[1];
        require(!cap.indices.empty() && mesh.model.materials[std::size_t(cap.material)].baseColor.r > .1f,
                "Prepared variant must contain visible dark-red cap triangles");
    }
}
void detachedSpin() {
    for (int sample = 0; sample < 32; ++sample) {
        const float speed = glm::length(viewer::zombieSeverSpin());
        require(std::isfinite(speed) && speed >= .999f && speed <= 4.001f,
                "Random spin must stay within one to four radians per second");
    }
    viewer::ZombieRagdollWorld control, spinning;
    const auto a = control.create({0, 0, 0}, 0, {0, 0, 0});
    const auto b = spinning.create({0, 0, 0}, 0, {0, 0, 0});
    control.sever(a, viewer::RagdollPart::head); spinning.sever(b, viewer::RagdollPart::head);
    const auto original = spinning.bodyTransforms(b);
    spinning.addAngularVelocity(b, viewer::RagdollPart::head, {0, 4, 0});
    require(original == spinning.bodyTransforms(b), "Adding spin must not alter the activation pose");
    for (int i = 0; i < 24; ++i) { control.step(1.0f / 120); spinning.step(1.0f / 120); }
    const auto still = control.bodyTransforms(a), turned = spinning.bodyTransforms(b);
    require(glm::dot(glm::vec3(still[2][0]), glm::vec3(turned[2][0])) < .9f,
            "The detached head must rotate under its added angular velocity");
    require(glm::dot(glm::vec3(still[0][0]), glm::vec3(turned[0][0])) > .999f,
            "Spin on a detached head must not rotate the remaining pelvis");
}
void detachedPartsSettle() {
    for (std::size_t index = 1; index < viewer::ragdollPartCount; ++index) {
        const auto part = static_cast<viewer::RagdollPart>(index);
        viewer::ZombieRagdollWorld world;
        const auto handle = world.create({}, 0, {0, 0, -4});
        world.sever(handle, part);
        world.applyImpulse(handle, part, {8, 0, 0});
        world.addAngularVelocity(handle, part, {0, 0, -4});
        for (int frame = 0; frame < 1800; ++frame) {
            world.step(1.0f / 60);
            const auto bodies = world.bodyTransforms(handle);
            for (const auto& body : bodies)
                if (body[3].y <= -.02f)
                    throw std::runtime_error("Severed part " + std::to_string(int(part)) + " fell below the floor at frame " +
                        std::to_string(frame) + " position " + std::to_string(body[3].x) + ", " +
                        std::to_string(body[3].y) + ", " + std::to_string(body[3].z));
        }
        require(world.sleeping(handle), "Severed parts must stop rolling and let the ragdoll sleep");
        const auto settled = world.bodyTransforms(handle);
        for (int frame = 0; frame < 120; ++frame) world.step(1.0f / 60);
        require(settled == world.bodyTransforms(handle), "Settled parts must stay at rest");
    }
}
}
int main(int argc, char** argv) {
    try {
        jointHitAndCleanup(); animatedAffineColliders(); releasedConstraint(); outwardLaunch(); detachedSpin(); detachedPartsSettle();
        if (argc > 1) preparedAsset(argv[1]);
        std::cout << "Joint dismemberment tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
