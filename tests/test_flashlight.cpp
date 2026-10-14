#include "flashlight.hpp"
#include "visibility.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        viewer::Camera camera;
        camera.setPosition({10, 4, -8});
        const auto light = viewer::flashlightView(camera);
        const auto aim = camera.position() + camera.direction() * 10.0f;
        const auto clip = light.projectionView() * glm::vec4(aim, 1);
        require(std::abs(clip.x / clip.w) < 1e-4f && std::abs(clip.y / clip.w) < 1e-4f,
                "Held flashlight must aim at the camera reticle's ten-meter target");
        require(glm::length(light.position - camera.position()) > .2f && glm::length(light.position - camera.position()) < .4f,
                "Flashlight must have a small visible-shadow-producing offset from the eye");
        const viewer::Frustum frustum(light.projectionView());
        const auto inside = light.position + light.direction * 12.0f;
        const auto far = light.position + light.direction * 28.0f;
        require(frustum.intersects(inside - glm::vec3(.1f), inside + glm::vec3(.1f)), "Near cone caster must remain inside");
        require(!frustum.intersects(far - glm::vec3(.1f), far + glm::vec3(.1f)), "Caster beyond 25 m must be rejected");
        const auto behind = light.position - light.direction * 2.0f;
        require(!frustum.intersects(behind - glm::vec3(.1f), behind + glm::vec3(.1f)), "Caster behind the torch must be rejected");
        camera.cycleViewMode(); camera.cycleViewMode();
        const auto scoped = viewer::flashlightView(camera);
        require(light.projectionView() == scoped.projectionView(), "Flashlight cone must be independent of camera zoom");
        camera.cycleViewMode(); // Restore normal sensitivity before turning.
        camera.look(2000, 600);
        const auto turned = viewer::flashlightView(camera);
        require(glm::dot(light.direction, turned.direction) < .5f, "Turning the camera must turn the flashlight");
        std::cout << "Flashlight pose and bounded cone tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
