#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "file_fingerprint.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
glm::mat4 translation(glm::vec3 p) { return glm::translate(glm::mat4(1), p); }

void rayGeometry() {
    viewer::ZombieBoneCapsule capsule;
    capsule.bone = 0; capsule.a = {0, 0, 0}; capsule.b = {0, 1, 0}; capsule.radius = .1f;
    const std::vector<glm::mat4> pose{glm::mat4(1)};
    float d = 0;
    const auto ray = [&](glm::vec3 o, glm::vec3 direction) {
        return viewer::zombieCapsuleRay(o, direction, glm::mat4(1), capsule, pose, d);
    };
    require(ray({0, .5f, 3}, {0, 0, -1}) && std::abs(d-2.9f) < 1e-5f, "Ray must enter the cylindrical side");
    require(ray({0, 3, 0}, {0, -1, 0}) && std::abs(d-1.9f) < 1e-5f, "Axial ray must hit the rounded end");
    require(ray({.1f, .5f, 3}, {0, 0, -1}) && std::abs(d-3) < 1e-5f, "Tangent must hit");
    require(!ray({.10001f, .5f, 3}, {0, 0, -1}) && !ray({0, 1.101f, 3}, {0, 0, -1}),
            "Capsules must reject rays beside their side and beyond their ends");
    require(ray({0, .5f, 0}, {1, 0, 0}) && d == 0, "A shot starting inside must hit at its origin");
    require(!ray({0, .5f, 3}, {0, 0, 1}) && !ray({0, .5f, 3}, {}), "Backward and zero rays must miss");
    capsule.b = capsule.a;
    require(ray({0, 0, 3}, {0, 0, -1}) && std::abs(d-2.9f) < 1e-5f, "Zero-length capsules must work as spheres");
    capsule.radius = .2f;
    auto instance = translation({10, 3, -10000}) * glm::scale(glm::mat4(1), {-2, .7f, 1.5f});
    instance[1].x += .3f;
    require(viewer::zombieCapsuleRay({10, 3, -9997}, {0, 0, -1}, instance, capsule, pose, d) && std::abs(d-2.7f) < 1e-5f,
            "Affine transforms must preserve world ray distance at long range");
}

viewer::ZombieLayer fixture() {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    auto& asset = layer.assets[0];
    asset.skeleton.resize(2);
    asset.skeleton[0].name = "body";
    asset.skeleton[1].name = "head"; asset.skeleton[1].parent = 0;
    asset.skeleton[1].referenceWorld = translation({0, 1.65f, 0});
    asset.ragdollBones[std::size_t(viewer::RagdollPart::head)] = 1;
    asset.frameCount = 2; asset.duration = 1;
    asset.boneFrames = {glm::mat4(1), asset.skeleton[1].referenceWorld, glm::mat4(1), asset.skeleton[1].referenceWorld};
    viewer::ZombieBoneCapsule body, head, leg;
    body.bone = 0; body.a = {0, .5f, 0}; body.b = {0, 1.3f, 0}; body.radius = .12f;
    head.bone = 1; head.a = {0, -.05f, 0}; head.b = {0, .05f, 0}; head.radius = .1f; head.headshot = true;
    leg.bone = 0; leg.a = {.1f, .08f, 0}; leg.b = {.1f, .5f, 0}; leg.radius = .05f;
    layer.capsuleAssets.resize(1);
    layer.capsuleAssets[0].capsules = {body, head, leg};
    layer.instances.emplace_back();
    layer.instances[0].restRoot = layer.instances[0].restTransform = layer.instances[0].transform = translation({0, 0, -3});
    return layer;
}

void narrowHitsAndAnimation() {
    auto layer = fixture();
    require(!layer.shoot({.5f, .9f, 0}, {0, 0, -1}), "Empty space inside the old torso sphere must miss");
    require(!layer.shoot({.2f, 1.65f, 0}, {0, 0, -1}), "Empty space inside the old head sphere must miss");
    require(layer.shoot({0, .9f, 0}, {0, 0, -1}) && layer.damage().injury(0)->health == 9,
            "Capsule body hit must retain ordinary damage");
    require(layer.shoot({0, 1.65f, 0}, {0, 0, -1}) && layer.damage().injury(0)->health == 7,
            "Head capsule hit must retain double damage");
    require(layer.shoot({.1f, .1f, 0}, {0, 0, -1}) && layer.damage().injury(0)->health == 6,
            "Broad bounds must include ankle capsules outside the old spheres");
    auto animated = fixture();
    animated.assets[0].boneFrames[2] = translation({4, 0, 0});
    animated.instances[0].phase = .25f;
    require(animated.shoot({2, .9f, 0}, {0, 0, -1}), "Capsules and broad bounds must follow interpolated animation");
    require(animated.shootingStats().headVertices == 0, "Prepared capsules must avoid per-shot head vertex scans");
}

void crowdedJointSelection() {
    auto layer = fixture();
    auto neighbor = layer.instances[0];
    neighbor.restRoot = neighbor.restTransform = neighbor.transform = translation({.6f, 0, -2.9f});
    layer.instances.push_back(neighbor);
    viewer::ZombieJointCollider joint;
    joint.name = "elbow"; joint.bone = 0; joint.part = viewer::RagdollPart::lowerArmLeft;
    joint.center = {0, .9f, 0}; joint.radius = .075f;
    layer.dismembermentAssets.resize(1);
    layer.dismembermentAssets[0].joints.push_back(joint);
    require(layer.shoot({0, .9f, 0}, {0, 0, -1}) && layer.instances[0].severedJoint == 0 && !layer.damage().injury(1),
            "Neighbor's obsolete body sphere must not steal a joint hit");

    auto blocked = fixture();
    blocked.assets.push_back(blocked.assets[0]);
    blocked.capsuleAssets.push_back(blocked.capsuleAssets[0]);
    blocked.dismembermentAssets.resize(2);
    blocked.dismembermentAssets[0].joints.push_back(joint);
    neighbor.asset = 1;
    neighbor.restRoot = neighbor.restTransform = neighbor.transform = translation({0, 0, -2});
    blocked.instances.push_back(neighbor);
    require(blocked.shoot({0, .9f, 0}, {0, 0, -1}) && blocked.damage().injury(1) && !blocked.instances[0].ragdoll,
            "A real foreground body capsule must still block a farther joint");
}

void manifestValidation() {
    auto layer = fixture();
    auto& asset = layer.assets[0];
    asset.skeleton[0].referenceWorld = glm::scale(glm::mat4(1), glm::vec3(.01f));
    const auto directory = std::filesystem::temp_directory_path() /
        ("zombie-capsules-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
    const auto source = directory / "model.glb", manifest = directory / "model.capsules.json";
    { std::ofstream out(source); out << "capsule fixture"; }
    require(viewer::loadZombieHitCapsules(source, asset).capsules.empty(), "Missing optional definitions must keep the fallback");
    const std::string prefix = "{\"schema\":\"godollo.zombie_bone_capsules\",\"version\":1,\"units\":\"meters\","
        "\"space\":\"bone_reference_meters\",\"source_sha256\":\"" + viewer::fileSha256(source) + "\",\"bones\":[";
    const auto write = [&](const std::string& headName, float radius) {
        std::ofstream out(manifest);
        out << prefix << "{\"bone\":\"body\",\"enabled\":true,\"shape\":\"capsule\",\"a\":[0,0,0],\"b\":[0,1,0],\"radius\":"
            << radius << "},{\"bone\":\"" << headName << "\",\"enabled\":true,\"shape\":\"capsule\","
            "\"a\":[0,0,0],\"b\":[0,0.1,0],\"radius\":0.1}]}";
    };
    write("head", .1f);
    const auto prepared = viewer::loadZombieHitCapsules(source, asset);
    require(prepared.capsules.size() == 2 && prepared.capsules[1].headshot && !prepared.capsules[0].headshot &&
            std::abs(prepared.capsules[0].boneFromMeters[0][0]-100) < .001f && prepared.resources.size() == 1,
            "Loader must attach by bone name, classify head ancestry and compensate source scale");
    const auto rejected = [&] {
        try { viewer::loadZombieHitCapsules(source, asset); return false; } catch (const std::runtime_error&) { return true; }
    };
    write("body", .1f); require(rejected(), "Duplicate/missing bone definitions must fail");
    write("head", -1); require(rejected(), "Invalid capsule geometry must fail");
    write("head", .1f); { std::ofstream out(source); out << "changed fixture"; }
    require(rejected(), "Stale source fingerprints must fail");
}
}

int main(int argc, char** argv) {
    try {
        rayGeometry(); narrowHitsAndAnimation(); crowdedJointSelection(); manifestValidation();
        for (int i = 1; i < argc; ++i) {
            const auto asset = viewer::loadZombieModel(argv[i]);
            const auto prepared = viewer::loadZombieHitCapsules(argv[i], asset);
            require(prepared.capsules.size() >= 20, "Prepared characters must load their per-bone capsule definitions");
        }
        std::cout << "Bone capsule shooting tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
