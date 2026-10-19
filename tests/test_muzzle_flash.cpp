#include "muzzle_flash.hpp"
#include "camera.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        viewer::MuzzleFlash flash;
        require(flash.light(0).color == glm::vec3(0), "No flash before a shot");
        flash.fire(2, {.1f, -.2f, -.7f});
        const auto peak = flash.light(2);
        require(peak.color.r > peak.color.g && peak.color.g > peak.color.b && peak.color.b > 0,
                "An actual shot must create a warm point light immediately");
        const auto faded = flash.light(2.04);
        require(faded.color.r > 0 && faded.color.r < peak.color.r * .3f,
                "Muzzle flash must fade rapidly within a few frames");
        require(flash.light(2.08).color == glm::vec3(0) && flash.light(100).color == glm::vec3(0),
                "Flash must turn off completely, including after a frame stall");
        require(flash.light(1.9).color == glm::vec3(0), "A future shot cannot light an earlier time");
        flash.fire(2.05, {-.1f, -.3f, -1});
        require(flash.light(2.05).color == peak.color && flash.light(2.05).position.z == -1,
                "Automatic shots must restart the pulse at the fired weapon's muzzle");

        viewer::Camera camera;
        camera.setPosition({10, 4, -8}); camera.setOrientation(0, 0);
        const auto world = peak.transformed(glm::inverse(camera.view()));
        require(glm::distance(world.position, glm::vec3(10.7f, 3.8f, -7.9f)) < 1e-5f,
                "Eye-local muzzle axes must follow the camera's world translation and yaw");
        camera.setOrientation(0, 45);
        const auto pitched = peak.transformed(glm::inverse(camera.view()));
        require(pitched.position.y > camera.position().y && glm::distance(pitched.position, world.position) > .3f,
                "Pitching the weapon must rotate its muzzle into world space");
        require(pitched.color == peak.color && pitched.range == peak.range,
                "Coordinate conversion must preserve intensity and range");
        camera.cycleViewMode(); camera.cycleViewMode();
        require(peak.transformed(glm::inverse(camera.view())).position == pitched.position,
                "Scope magnification must not move the muzzle light");
        std::cout << "Muzzle pulse, retrigger, camera pose and scope tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
