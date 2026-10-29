#include "zombie_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
glm::mat4 translation(glm::vec3 point) { return glm::translate(glm::mat4(1), point); }

viewer::ZombieLayer fixture(bool joints = false) {
    viewer::ZombieLayer layer;
    auto& asset = layer.assets.emplace_back();
    asset.skeleton.resize(2);
    asset.skeleton[0].name = "body";
    asset.skeleton[0].referenceWorld = translation({0, .9f, 0});
    asset.skeleton[0].inverseBind = translation({-4, -3, 2});
    asset.skeleton[1].name = "head";
    asset.skeleton[1].parent = 0;
    asset.skeleton[1].referenceWorld = translation({0, 1.65f, 0});
    asset.ragdollBones[std::size_t(viewer::RagdollPart::head)] = 1;
    asset.frameCount = 2; asset.duration = 1;
    asset.boneFrames = {asset.skeleton[0].referenceWorld, asset.skeleton[1].referenceWorld,
                       asset.skeleton[0].referenceWorld, asset.skeleton[1].referenceWorld};
    viewer::ZombieBoneCapsule body, head;
    body.bone = 0; body.a = {0, -.4f, 0}; body.b = {0, .4f, 0}; body.radius = .12f;
    head.bone = 1; head.a = {0, -.05f, 0}; head.b = {0, .05f, 0}; head.radius = .1f; head.headshot = true;
    layer.capsuleAssets.resize(1);
    layer.capsuleAssets[0].capsules = {body, head};
    if (joints) {
        viewer::ZombieJointCollider joint;
        joint.name = "elbow"; joint.bone = 0; joint.part = viewer::RagdollPart::lowerArmLeft;
        joint.radius = .075f;
        layer.dismembermentAssets.resize(1);
        layer.dismembermentAssets[0].joints.push_back(joint);
    }
    auto& instance = layer.instances.emplace_back();
    instance.restRoot = translation({0, 0, -3});
    instance.transform = instance.restTransform = instance.restRoot;
    return layer;
}

void displayedPoseAndReadOnlyQuery() {
    auto layer = fixture();
    const auto displayed = layer.instances[0];
    layer.instances[0].restRoot = layer.instances[0].restTransform = layer.instances[0].transform = translation({10, 0, -3});
    const auto revision = layer.renderUpdates().since(0).revision;
    layer.renderUpdates().acknowledge(revision);
    const auto trace = layer.traceShot(0, displayed, {0, .9f, 0}, {0, 0, -4}, 0);
    require(trace && trace->claim.zombie == 0 && trace->claim.bone == 0 && !trace->claim.headshot,
        "Tracing must use the displayed instance rather than its newer source position");
    require(std::abs(trace->distance - 2.88f) < .0001f &&
            glm::distance(trace->claim.point, glm::vec3(0, 0, .12f)) < .0001f,
        "Bone-local contact must use anatomical bone frames, independent of inverse bind matrices");
    require(!layer.damage().injury(0) && layer.bloodMist().sample(0).empty() &&
            layer.renderUpdates().since(revision).revision == revision,
        "Read-only tracing must not apply damage, publish render changes or initialize simulation indexes");
    require(!layer.traceShot(0, layer.instances[0], {0, .9f, 0}, {0, 0, -1}, 0),
        "A ray beside the current instance must miss its narrow colliders");
    require(!layer.traceShot(0, displayed, {0, .9f, 0}, {}, 0), "A zero direction must not produce a claim");
}

void trustedIntersectionFollowsMovingActor() {
    auto layer = fixture();
    const auto trace = layer.traceShot(0, layer.instances[0], {0, .9f, 0}, {0, 0, -1}, 0);
    require(trace.has_value(), "Body fixture must yield a claim");
    layer.instances[0].restRoot = translation({10, 0, -3});
    layer.instances[0].transform = layer.instances[0].restTransform = layer.instances[0].restRoot;
    viewer::CollisionWorld world(viewer::Model{});
    require(layer.applyReportedShot(trace->claim, {0, .9f, 0}, {0, 0, -1}, 0, world, viewer::ShotCaliber::low),
        "Trusted hits must remain valid after the actor moves away from the original ray");
    require(layer.damage().injury(0) && layer.damage().injury(0)->health == 9,
        "Server-owned low-caliber damage must apply once");
    const auto bursts = layer.bloodMist().sample(0);
    require(bursts.size() == 1 && std::abs(bursts[0].position.x - 10) < .0001f &&
            std::abs(bursts[0].position.y - .9f) < .0001f,
        "Impact effects must follow the reported anatomical point on the current actor");
    const auto head = layer.traceShot(0, layer.instances[0], {10, 1.65f, 0}, {0, 0, -1}, 0);
    require(head && head->claim.headshot && layer.applyReportedShot(head->claim, {10, 1.65f, 0},
                {0, 0, -1}, 0, world, viewer::ShotCaliber::high) && layer.damage().injury(0)->health == 5,
        "Reported head classification must retain server-owned caliber and head damage");
}

void invalidReportsAreIgnored() {
    auto layer = fixture();
    viewer::CollisionWorld world(viewer::Model{});
    const auto trace = layer.traceShot(0, layer.instances[0], {0, .9f, 0}, {0, 0, -1}, 0);
    require(trace.has_value(), "Body fixture must yield a claim");
    const auto rejected = [&](const viewer::ZombieHitClaim& claim) {
        require(!layer.applyReportedShot(claim, {0, .9f, 0}, {0, 0, -1}, 0, world, viewer::ShotCaliber::high),
            "Invalid reported geometry must be rejected before applying effects");
    };
    auto claim = trace->claim; claim.zombie = 1; rejected(claim);
    claim = trace->claim; claim.bone = 2; rejected(claim);
    claim = trace->claim; claim.bone = -2; rejected(claim);
    claim = trace->claim; claim.joint = 0; rejected(claim);
    claim = trace->claim; claim.point.x = std::numeric_limits<float>::quiet_NaN(); rejected(claim);
    claim = trace->claim; claim.point = {100, 100, 100}; rejected(claim);
    claim = trace->claim; claim.woundRegion = static_cast<viewer::ZombieWoundRegion>(2); rejected(claim);
    claim = trace->claim; claim.woundRegion = viewer::ZombieWoundRegion::head; rejected(claim);
    layer.instances[0].removed = true; rejected(trace->claim);
    layer.instances[0].removed = false; layer.instances[0].ragdoll = true; rejected(trace->claim);
    require(!layer.damage().injury(0) && layer.bloodMist().sample(0).empty(),
        "Rejected reports must not mutate health or blood effects");
}

void jointClaimsPreserveDismemberment() {
    auto layer = fixture(true);
    const auto trace = layer.traceShot(0, layer.instances[0], {0, .9f, 0}, {0, 0, -1}, 0);
    require(trace && trace->claim.joint == 0 && trace->claim.bone == 0 && !trace->claim.woundRegion,
        "Joint colliders must retain precedence and carry their anatomical bone");
    viewer::CollisionWorld world(viewer::Model{});
    require(layer.applyReportedShot(trace->claim, {0, .9f, 0}, {0, 0, -1}, 0, world, viewer::ShotCaliber::low) &&
            layer.instances[0].ragdoll && layer.instances[0].severedJoint == 0,
        "A trusted joint hit must use the existing authoritative dismemberment path");
}

void displayedWoundMaskStaysIndependent() {
    auto layer = fixture();
    auto& asset = layer.assets[0];
    asset.skeleton[1].inverseBind = translation({0, -1.65f, 0});
    auto& primitive = asset.model.primitives.emplace_back();
    auto& animation = asset.animation.emplace_back();
    for (const auto point : {glm::vec3(-.12f, 1.5f, -.1f), {.12f, 1.5f, -.1f},
            {.12f, 1.8f, -.1f}, {-.12f, 1.8f, -.1f}, {-.12f, 1.5f, .1f},
            {.12f, 1.5f, .1f}, {.12f, 1.8f, .1f}, {-.12f, 1.8f, .1f}}) {
        viewer::Vertex vertex; vertex.position = point; primitive.vertices.push_back(vertex);
        viewer::SkinningVertex skin; skin.joints[0] = 1; skin.weights[0] = 1;
        animation.skinning.push_back(skin);
    }
    primitive.indices = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                         3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    const auto displayed = layer.instances[0];
    const glm::vec3 origin(0, 1.65f, 0), direction(0, 0, -1);
    require(layer.traceShot(0, displayed, origin, direction, 0).has_value(),
        "Intact head surface must yield a displayed claim");
    layer.setWoundMask(0, {255});
    require(!layer.traceShot(0, displayed, origin, direction, 0),
        "Current wound mask must remove the complete head surface");
    require(layer.traceShot(0, displayed, origin, direction, 0, nullptr).has_value(),
        "A newer source wound must not remove a surface that was displayed intact");
    const auto displayedHole = *layer.wounds().find(0);
    layer.setWoundMask(0, {});
    require(layer.traceShot(0, displayed, origin, direction, 0).has_value() &&
            !layer.traceShot(0, displayed, origin, direction, 0, &displayedHole),
        "An explicit displayed hole must remain authoritative after the source mask changes");
}
} // namespace

int main() {
    try {
        displayedPoseAndReadOnlyQuery(); trustedIntersectionFollowsMovingActor(); invalidReportsAreIgnored();
        jointClaimsPreserveDismemberment();
        displayedWoundMaskStaysIndependent();
        std::cout << "Read-only displayed tracing and trusted authoritative hit effects passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
