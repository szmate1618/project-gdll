#include "zombie_chase.hpp"
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

bool near(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 2e-4f; }

void quad(viewer::Model& model, const std::string& name,
          glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    viewer::Primitive primitive;
    for (auto position : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = position;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name});
    model.primitives.push_back(std::move(primitive));
}

viewer::ZombieLayer crowd(std::initializer_list<glm::vec3> positions) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    for (auto feet : positions) {
        viewer::ZombieInstance instance;
        instance.restRoot = glm::translate(glm::mat4(1), feet);
        instance.transform = instance.restTransform = instance.restRoot;
        layer.instances.push_back(instance);
    }
    return layer;
}

void triggerAndPursuit() {
    const viewer::CollisionWorld world(viewer::Model{});
    auto layer = crowd({{20, 0, 0}, {20.01f, 0, 0}, {0, 20.01f, 0}, {0, 0, 0}});
    layer.chase(1, {0, 0, 0}, world);
    require(layer.instances[0].alerted && layer.instances[0].walking,
            "Exactly 20 meters must trigger pursuit");
    require(near(glm::vec3(layer.instances[0].restRoot[3]), {18.8f, 0, 0}),
            "Walk toward the target at 1.2 m/s");
    require(near(glm::vec3(layer.instances[0].restRoot[2]), {-1, 0, 0}),
            "The character's forward direction must face the target");
    require(!layer.instances[1].alerted && !layer.instances[2].alerted,
            "Zombies outside the 3D detection radius must stay idle");
    require(layer.instances[3].alerted && !layer.instances[3].walking,
            "Coincident target must not divide by zero or walk in place");

    layer.chase(1, {18.8f, 0, 30}, world);
    require(near(glm::vec3(layer.instances[0].restRoot[3]), {18.8f, 0, 1.2f}),
            "An alerted zombie must follow the current target beyond 20 meters");
    layer.chase(1, {18.8f, 0, 1.3f}, world);
    require(near(glm::vec3(layer.instances[0].restRoot[3]), {18.8f, 0, 1.3f}),
            "The last step must stop at the target without overshooting");
    layer.chase(1, {18.8f, 0, 1.3f}, world);
    require(!layer.instances[0].walking, "Return to idle when the target stops moving");
    layer.chase(1, {18.8f, 0, 5}, world);
    require(layer.instances[0].walking, "Resume walking when the alerted target moves away");
}

void terrainWithoutAvoidance() {
    viewer::Model model;
    // Folded quad: the center belongs to a triangle at y=6, not the bilinear y=3.
    quad(model, "terrain", {-20, 0, -20}, {-20, 0, 20}, {20, 12, 20}, {20, 0, -20});
    quad(model, "building_roof", {-20, 25, -20}, {-20, 25, 20}, {20, 25, 20}, {20, 25, -20});
    quad(model, "building_wall", {0, 0, -20}, {0, 0, 20}, {0, 25, 20}, {0, 25, -20});
    const viewer::CollisionWorld world(model, {{{0, 6, 0}, 2, 10}});
    auto layer = crowd({{-1.2f, 5.64f, 0}});
    layer.chase(1, {5, 7.5f, 0}, world);
    require(near(glm::vec3(layer.instances[0].restRoot[3]), {0, 6, 0}),
            "Follow the rendered ground triangle through walls and trunks, below roofs");
    layer.chase(1, {5, 7.5f, 0}, world);
    require(near(glm::vec3(layer.instances[0].restRoot[3]), {1.2f, 6, 0}),
            "Continue straight through obstacles on the next terrain triangle");
}

void timeAndNormalization() {
    const viewer::CollisionWorld world(viewer::Model{});
    auto once = crowd({{0, 4, 0}});
    auto split = crowd({{0, 4, 0}});
    const auto normalization = glm::scale(glm::translate(glm::mat4(1), {0.2f, -0.1f, 0.3f}),
                                          glm::vec3(1.3f));
    once.instances[0].transform = once.instances[0].restTransform *= normalization;
    split.instances[0].transform = split.instances[0].restTransform *= normalization;
    once.chase(1, {6, 4, 8}, world);
    for (int i = 0; i < 10; ++i) split.chase(0.1f, {6, 4, 8}, world);
    require(near(glm::vec3(once.instances[0].restRoot[3]), {0.72f, 4, 0.96f}),
            "Diagonal movement must have the same speed as axis-aligned movement");
    for (int c = 0; c < 4; ++c) {
        require(glm::length(once.instances[0].transform[c] - split.instances[0].transform[c]) < 2e-4f,
                "Movement must be independent of frame rate");
        const auto retained = glm::inverse(split.instances[0].restRoot) * split.instances[0].restTransform;
        require(glm::length(retained[c] - normalization[c]) < 2e-4f,
                "Changing yaw and position must preserve asset scale and origin");
    }
    const auto before = once.instances[0].transform;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    once.chase(0, {0, 0, 0}, world);
    once.chase(-1, {0, 0, 0}, world);
    once.chase(nan, {0, 0, 0}, world);
    once.chase(1, {nan, 0, 0}, world);
    require(once.instances[0].transform == before, "Invalid input must not corrupt transforms");
    once.instances[0].ragdoll = true;
    once.chase(1, {0, 0, 0}, world);
    require(once.instances[0].transform == before, "Ragdolls must not pursue the player");
}

void walkingLegsAndDescendants() {
    viewer::AnimatedModel asset;
    // Feet appear before knees/hips to exercise unordered skeleton traversal.
    const glm::vec3 positions[] = {{-0.1f, 0, 0}, {-0.1f, 0.5f, 0}, {-0.1f, 1, 0},
                                  {0.1f, 0, 0}, {0.1f, 0.5f, 0}, {0.1f, 1, 0}};
    const int parents[] = {1, 2, -1, 4, 5, -1};
    for (int i = 0; i < 6; ++i) {
        viewer::SkeletonBone bone;
        bone.parent = parents[i];
        bone.referenceWorld = glm::translate(glm::mat4(1), positions[i]);
        bone.inverseBind = glm::inverse(bone.referenceWorld);
        asset.skeleton.push_back(bone);
    }
    asset.ragdollBones[static_cast<std::size_t>(viewer::RagdollPart::upperLegLeft)] = 2;
    asset.ragdollBones[static_cast<std::size_t>(viewer::RagdollPart::lowerLegLeft)] = 1;
    asset.ragdollBones[static_cast<std::size_t>(viewer::RagdollPart::upperLegRight)] = 5;
    asset.ragdollBones[static_cast<std::size_t>(viewer::RagdollPart::lowerLegRight)] = 4;
    const auto first = viewer::sampleZombieWalkPose(asset, 0, 0.25, 0);
    const auto second = viewer::sampleZombieWalkPose(asset, 0, 0.75, 0);
    require(first[1][3].z > 0.1f && first[4][3].z < -0.1f &&
            second[1][3].z < -0.1f && second[4][3].z > 0.1f,
            "The legs must step in opposite directions and alternate each half cycle");
    require(near(glm::vec3(first[2][3]), positions[2]), "Swing must pivot around the hip");
    require(std::abs(glm::length(glm::vec3(first[0][3] - first[1][3])) - 0.5f) < 2e-4f,
            "The foot must follow the knee without stretching the leg");
    require(near(glm::vec3((glm::inverse(first[1]) * first[0])[3]), {0, -0.5f, 0}),
            "Unmapped descendants must preserve their local pose");
}
}

int main() {
    try {
        triggerAndPursuit();
        terrainWithoutAvoidance();
        timeAndNormalization();
        walkingLegsAndDescendants();
        std::cout << "Zombie chase tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie chase test failed: " << error.what() << '\n';
        return 1;
    }
}
