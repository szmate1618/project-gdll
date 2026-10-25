#include "camera_rig.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

namespace {
void quad(viewer::Model& model, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const std::string& name) {
    viewer::Primitive primitive;
    for (const auto point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name});
    model.primitives.push_back(std::move(primitive));
}

viewer::Model buildingScene() {
    viewer::Model model;
    quad(model, {-50,0,-50}, {-50,0,50}, {50,0,50}, {50,0,-50}, "terrain");
    quad(model, {-2,4,-2}, {-2,4,2}, {2,4,2}, {2,4,-2}, "building_test_roof");
    quad(model, {-2,0,2}, {-2,0,-2}, {2,0,-2}, {2,0,2}, "building_test_walls");
    quad(model, {-2,0,-2}, {-2,0,2}, {-2,4,2}, {-2,4,-2}, "building_test_walls");
    quad(model, {2,0,2}, {2,0,-2}, {2,4,-2}, {2,4,2}, "building_test_walls");
    quad(model, {2,0,-2}, {-2,0,-2}, {-2,4,-2}, {2,4,-2}, "building_test_walls");
    quad(model, {-2,0,2}, {2,0,2}, {2,4,2}, {-2,4,2}, "building_test_walls");
    model.boundsMin = {-50,0,-50};
    model.boundsMax = {50,4,50};
    return model;
}

void settle(viewer::CameraRig& rig, float seconds, int rate = 120) {
    for (int i = 0; i < static_cast<int>(seconds * rate); ++i)
        rig.update(1.0f / rate, 0, 0, 0, false, false);
}

void fallingTransitions() {
    const auto model = buildingScene();
    const viewer::CollisionWorld world(model, {{{8,0,0}, .4f, 6}});
    viewer::CameraRig rig(world, model.boundsMin, model.boundsMax, 16.0f / 9.0f);
    const glm::vec3 aboveRoof(0, 12, 0);
    rig.camera().setPosition(aboveRoof);
    rig.camera().look(20, 10);
    const auto yaw = rig.camera().yawDegrees();
    const auto pitch = rig.camera().pitchDegrees();
    require(rig.enterWalking(), "F9 above a named building failed");
    require(glm::length(rig.camera().position() - aboveRoof) < 1e-5f && !rig.player().grounded(),
            "F9 teleported the camera instead of starting airborne");
    require(rig.camera().yawDegrees() == yaw && rig.camera().pitchDegrees() == pitch,
            "F9 reset camera orientation");
    settle(rig, .25f);
    require(rig.player().verticalVelocity() < -2 && rig.camera().position().y < aboveRoof.y &&
                !rig.player().grounded(), "F9 did not start a gravity-driven fall");
    const auto fallingEye = rig.camera().position();
    const auto fallingVelocity = rig.player().verticalVelocity();
    require(rig.enterWalking() && rig.camera().position() == fallingEye &&
                rig.player().verticalVelocity() == fallingVelocity, "Repeated F9 reset an active fall");
    settle(rig, 2);
    require(rig.player().grounded() && std::abs(rig.player().feet().y - 4) < .01f &&
                std::abs(rig.player().feet().x) < 1e-5f && std::abs(rig.player().feet().z) < 1e-5f,
            "F9 did not land on the roof directly below the camera");
    require(std::abs(rig.camera().position().y - 5.7f) < .01f, "Roof landing lost the eye height");
    rig.update(1.0f / 120, 0, 0, 0, false, true);
    require(rig.player().verticalVelocity() > 3, "Cannot jump after a rooftop landing");
    settle(rig, 1);
    for (int i = 0; i < 180; ++i) rig.update(1.0f / 120, 0, 1, 0, false, false);
    settle(rig, 2);
    require(std::hypot(rig.player().feet().x, rig.player().feet().z) > 4 && rig.player().grounded() &&
                std::abs(rig.player().feet().y) < .01f, "Walking off the roof did not land on terrain");

    // Terminal-velocity falls must also stop on a thin roof at a low frame rate.
    rig.enterFreeFly();
    rig.camera().setPosition({0,300,0});
    require(rig.enterWalking(), "Cannot start a long fall above a roof");
    settle(rig, 10, 30);
    require(rig.player().grounded() && std::abs(rig.player().feet().y - 4) < .01f,
            "Long F9 fall tunneled through the roof");

    for (const auto eye : {glm::vec3(10,10,0), glm::vec3(0,2,0)}) {
        rig.enterFreeFly();
        rig.camera().setPosition(eye);
        require(rig.enterWalking() && glm::length(rig.camera().position() - eye) < 1e-5f,
                "A clear terrain/indoor position was not preserved");
        settle(rig, 2);
        require(rig.player().grounded() && std::abs(rig.player().feet().y) < .01f,
                "F9 did not land on terrain outdoors/inside the shell");
    }

    for (const auto eye : {glm::vec3(0,4.5f,0), glm::vec3(2,2,0), glm::vec3(8,2,0),
                           glm::vec3(10,-2,0), glm::vec3(100,10,0)}) {
        rig.enterFreeFly();
        rig.camera().setPosition(eye);
        require(rig.enterWalking() && rig.player().grounded(),
                "Roof/wall/trunk overlap or missing support failed to recover to safe ground");
        require(glm::length(rig.camera().position() - eye) > .1f,
                "An invalid F9 capsule retained its unsafe position");
        const auto& config = rig.player().config();
        require(!world.insideBuilding(rig.player().feet(), config.radius, config.height) &&
                    world.contacts(rig.player().feet(), config.radius, config.height).empty(),
                "F9 recovery left the capsule inside collision geometry");
    }

    viewer::Model empty;
    const viewer::CollisionWorld emptyWorld(empty);
    viewer::CameraRig emptyRig(emptyWorld, {-10,-10,-10}, {10,10,10}, 1);
    emptyRig.camera().setPosition(aboveRoof);
    require(!emptyRig.enterWalking() && !emptyRig.walking() && !emptyRig.hasPlayer() &&
                emptyRig.camera().position() == aboveRoof, "Failed F9 changed camera/player state");
}
} // namespace

int main() {
    try {
        viewer::Model model;
        viewer::Primitive ground;
        for (glm::vec3 point : {glm::vec3(-50, 0, -50), glm::vec3(50, 0, -50),
                               glm::vec3(50, 0, 50), glm::vec3(-50, 0, 50)}) {
            viewer::Vertex vertex;
            vertex.position = point;
            ground.vertices.push_back(vertex);
        }
        ground.indices = {0, 2, 1, 0, 3, 2};
        model.primitives.push_back(ground);
        model.draws.push_back({0, glm::mat4(1), "terrain"});
        model.boundsMin = {-50, 0, -50}; model.boundsMax = {50, 0, 50};
        viewer::CollisionWorld world(model);
        viewer::CameraRig rig(world, model.boundsMin, model.boundsMax, 16.0f / 9.0f);
        rig.camera().setPosition({0, 10, 0});
        const auto before = rig.camera().position();
        rig.update(1, 0, 0, 1, false, false);
        require(rig.camera().position().y > before.y, "Free-fly vertical movement changed");
        require(rig.enterWalking(glm::vec3(0, 10, 0)), "Cannot switch to FPS above flat ground");
        require(rig.walking(), "FPS mode not selected");
        require(rig.player().grounded(), "Explicit startup spawn stopped placing the player on ground");
        rig.camera().look(0, 1000); // clamp looking straight up, movement still horizontal
        for (int i = 0; i < 120; ++i) rig.update(1.0f / 120, 1, 0, 1, false, false);
        require(std::abs(rig.player().feet().y) < 0.02f, "Pitch or Q/E caused walking to fly");
        require(std::abs(rig.camera().position().y - 1.7f) < 0.02f, "FPS eye height is incorrect");
        const auto walkingEye = rig.camera().position();
        const auto yaw = rig.camera().yawDegrees();
        rig.enterFreeFly();
        require(!rig.walking(), "Cannot switch back to free-fly");
        require(glm::length(rig.camera().position() - walkingEye) < 1e-5f, "Mode change teleported camera");
        require(rig.camera().yawDegrees() == yaw, "Mode change reset mouse look");
        rig.update(1, 0, 0, 1, false, false);
        require(rig.camera().position().y > walkingEye.y, "Gravity leaked into free-fly");
        fallingTransitions();
        std::cout << "Camera mode switching, falling onto roofs/terrain, safe recovery, eye height, "
                     "pitch independence and preserved free-fly passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Camera rig test: " << error.what() << '\n';
        return 1;
    }
}
