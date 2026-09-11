#include "camera_rig.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

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
        std::cout << "Camera mode switching, eye height, pitch independence and preserved free-fly passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Camera rig test: " << error.what() << '\n';
        return 1;
    }
}
