#include "camera.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(glm::vec3 first, glm::vec3 second, float tolerance = 0.002F) {
    return glm::length(first - second) < tolerance;
}

void checkFraming(glm::vec3 minimum, glm::vec3 maximum, float aspect) {
    viewer::Camera camera;
    camera.frame(minimum, maximum, aspect);
    const glm::mat4 transform = camera.projection(aspect) * camera.view();
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{(corner & 1) ? maximum.x : minimum.x,
                              (corner & 2) ? maximum.y : minimum.y,
                              (corner & 4) ? maximum.z : minimum.z};
        const glm::vec4 clip = transform * glm::vec4(point, 1.0F);
        require(clip.w > 0.0F, "A framed corner is behind the camera");
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        require(std::abs(ndc.x) < 1.0F && std::abs(ndc.y) < 1.0F &&
                    ndc.z >= -1.0F && ndc.z <= 1.0F,
                "A framed corner is outside the view frustum");
    }
}

void testMovement() {
    viewer::Camera fullStep;
    viewer::Camera splitStep;
    const glm::vec3 start = fullStep.position();
    fullStep.move(1.0F, 0.0F, 0.0F, 1.0F, false);
    for (int i = 0; i < 100; ++i) {
        splitStep.move(1.0F, 0.0F, 0.0F, 0.01F, false);
    }
    require(near(fullStep.position(), splitStep.position()), "Movement depends on frame rate");
    require(std::abs(glm::length(fullStep.position() - start) - fullStep.speed()) < 0.001F,
            "Forward speed is incorrect");

    viewer::Camera diagonal;
    diagonal.move(1.0F, 1.0F, 0.0F, 1.0F, false);
    require(std::abs(glm::length(diagonal.position() - start) - diagonal.speed()) < 0.001F,
            "Diagonal movement increases speed");

    viewer::Camera fast;
    fast.move(1.0F, 0.0F, 0.0F, 1.0F, true);
    require(near(fast.position() - start, (fullStep.position() - start) * 4.0F),
            "Shift does not multiply movement speed by four");

    viewer::Camera vertical;
    vertical.look(670.0F, 850.0F);
    vertical.move(0.0F, 0.0F, 1.0F, 0.5F, false);
    require(near(vertical.position() - start, {0.0F, vertical.speed() * 0.5F, 0.0F}),
            "Vertical movement does not use world Y");
    vertical.move(0.0F, 0.0F, -1.0F, 0.5F, false);
    require(near(vertical.position(), start), "Opposite movement does not cancel");
    vertical.move(1.0F, 1.0F, 1.0F, -1.0F, false);
    require(near(vertical.position(), start), "Negative delta time causes movement");
}

void testLookAndSpeed() {
    viewer::Camera camera;
    const glm::mat4 before = camera.view();
    camera.look(3600.0F, 0.0F);
    for (int column = 0; column < 4; ++column) {
        require(glm::length(camera.view()[column] - before[column]) < 0.001F,
                "A full yaw rotation does not return to the starting view");
    }
    camera.look(0.0F, 100000.0F);
    glm::vec3 start = camera.position();
    camera.move(1.0F, 0.0F, 0.0F, 1.0F, false);
    const glm::vec3 direction = glm::normalize(camera.position() - start);
    require(direction.y > 0.99F && direction.y < 1.0F, "Pitch is not clamped below 90 degrees");
    camera.look(0.0F, -200000.0F);
    start = camera.position();
    camera.move(1.0F, 0.0F, 0.0F, 1.0F, false);
    require(glm::normalize(camera.position() - start).y < -0.99F,
            "Negative pitch does not look down");

    const float speed = camera.speed();
    camera.adjustSpeed(2.0F);
    require(camera.speed() == speed * 2.0F, "Speed adjustment was not applied");
    camera.adjustSpeed(-1.0F);
    camera.adjustSpeed(std::numeric_limits<float>::quiet_NaN());
    require(camera.speed() == speed * 2.0F, "Invalid speed changes were accepted");
}

void testZoomToggle() {
    for (const float aspect : {0.5F, 1.0F, 16.0F / 9.0F}) {
        viewer::Camera camera;
        camera.frame({-20.0F, -5.0F, -10.0F}, {40.0F, 30.0F, 60.0F}, aspect);
        camera.look(123.0F, -45.0F);
        const glm::vec3 position = camera.position();
        const glm::vec3 direction = camera.direction();
        const glm::mat4 originalProjection = camera.projection(aspect);

        for (int toggle = 0; toggle < 8; ++toggle) {
            camera.toggleZoom();
            require(glm::length(camera.position() - position) == 0.0F &&
                        glm::length(camera.direction() - direction) == 0.0F,
                    "Zoom changes the camera position or direction");
            const glm::mat4 projection = camera.projection(aspect);
            if (toggle % 2 == 0) {
                // A ray half a degree above the optical axis reaches the top edge.
                const float halfAngle = glm::radians(0.5F);
                const glm::vec4 clip = projection * glm::vec4(
                    std::sin(halfAngle) * aspect, std::sin(halfAngle),
                    -std::cos(halfAngle), 1.0F);
                require(std::abs(clip.x / clip.w - 1.0F) < 0.00001F &&
                            std::abs(clip.y / clip.w - 1.0F) < 0.00001F,
                        "Zoom does not produce a one-degree vertical field of view");
            } else {
                for (int column = 0; column < 4; ++column) {
                    for (int row = 0; row < 4; ++row) {
                        require(projection[column][row] == originalProjection[column][row],
                                "Zoom does not restore the original projection exactly");
                    }
                }
            }
        }
    }
}

void testZoomLookSensitivity() {
    for (const float sign : {-1.0F, 1.0F}) {
        viewer::Camera camera;
        const auto angles = [&camera] {
            return glm::vec2(camera.yawDegrees(), camera.pitchDegrees());
        };
        const auto lookDelta = [&] {
            const glm::vec2 before = angles();
            camera.look(sign * 100.0F, sign * 50.0F);
            return angles() - before;
        };

        const glm::vec2 normalDelta = lookDelta();
        require(std::abs(normalDelta.x) > 0.0F && std::abs(normalDelta.y) > 0.0F,
                "Mouse look does not change both yaw and pitch");
        camera.toggleZoom();
        const glm::vec2 zoomDelta = lookDelta();
        require(glm::length(zoomDelta * 10.0F - normalDelta) < 0.0001F,
                "Zoom does not reduce yaw and pitch sensitivity by ten");
        camera.toggleZoom();
        require(glm::length(lookDelta() - normalDelta) < 0.0001F,
                "Leaving zoom does not restore normal mouse sensitivity");
    }
}

}  // namespace

int main() {
    try {
        checkFraming({110.0F, -2.0F, -90.0F}, {140.0F, 12.0F, -60.0F}, 16.0F / 9.0F);
        checkFraming({-5000.0F, 200.0F, 10000.0F}, {12000.0F, 2000.0F, 13000.0F}, 0.5F);
        checkFraming({20.0F, 3.0F, -10.0F}, {20.0F, 3.0F, -10.0F}, 1.0F);
        testMovement();
        testLookAndSpeed();
        testZoomToggle();
        testZoomLookSensitivity();
        std::cout << "Camera framing, movement, mouse look, speed, and zoom checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Camera test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
