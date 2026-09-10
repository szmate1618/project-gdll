#include "camera.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {

float validAspect(float aspect) {
    return std::isfinite(aspect) && aspect > 0.0F ? aspect : 1.0F;
}

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

}  // namespace

glm::vec3 Camera::forward() const {
    const float yaw = glm::radians(yaw_);
    const float pitch = glm::radians(pitch_);
    return glm::normalize(glm::vec3{std::cos(yaw) * std::cos(pitch),
                                    std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
}

void Camera::frame(glm::vec3 minimum, glm::vec3 maximum, float aspect) {
    if (!finite(minimum) || !finite(maximum) ||
        glm::any(glm::greaterThan(minimum, maximum))) {
        throw std::invalid_argument("Cannot frame invalid model bounds");
    }

    const glm::vec3 halfExtent = (maximum - minimum) * 0.5F;
    const glm::vec3 center = minimum + halfExtent;
    const float radius = std::max(glm::length(halfExtent), 0.5F);
    const float halfVertical = glm::radians(verticalFov_) * 0.5F;
    const float halfHorizontal = std::atan(std::tan(halfVertical) * validAspect(aspect));
    const float distance = 1.2F * radius / std::sin(std::min(halfVertical, halfHorizontal));

    // Fitting the enclosing sphere also fits all corners of an arbitrarily offset
    // model. The margin keeps the model clear of the initial window edges.
    const glm::vec3 direction = glm::normalize(glm::vec3{-1.0F, -0.75F, -1.0F});
    position_ = center - direction * distance;
    yaw_ = glm::degrees(std::atan2(direction.z, direction.x));
    pitch_ = glm::degrees(std::asin(direction.y));
    far_ = std::max(10000.0F, distance + radius * 4.0F);
    speed_ = std::clamp(radius * 0.45F, 1.0F, 5000.0F);
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position_, position_ + forward(), glm::vec3{0.0F, 1.0F, 0.0F});
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(glm::radians(verticalFov_), validAspect(aspect), near_, far_);
}

void Camera::move(float forwardInput, float rightInput, float upInput, float dt, bool fast) {
    if (!std::isfinite(dt) || dt <= 0.0F || !std::isfinite(forwardInput) ||
        !std::isfinite(rightInput) || !std::isfinite(upInput)) {
        return;
    }
    const glm::vec3 forwardDirection = forward();
    const glm::vec3 rightDirection = glm::normalize(
        glm::cross(forwardDirection, glm::vec3{0.0F, 1.0F, 0.0F}));
    glm::vec3 movement = forwardDirection * forwardInput + rightDirection * rightInput +
                         glm::vec3{0.0F, upInput, 0.0F};
    const float length = glm::length(movement);
    if (length > 1.0F) {
        movement /= length;
    }
    position_ += movement * speed_ * dt * (fast ? 4.0F : 1.0F);
}

void Camera::look(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return;
    }
    yaw_ = std::remainder(yaw_ + dx * 0.1F, 360.0F);
    pitch_ = std::clamp(pitch_ + dy * 0.1F, -89.0F, 89.0F);
}

void Camera::adjustSpeed(float multiplier) {
    if (std::isfinite(multiplier) && multiplier > 0.0F) {
        speed_ = std::clamp(speed_ * multiplier, 0.1F, 100000.0F);
    }
}

}  // namespace viewer
