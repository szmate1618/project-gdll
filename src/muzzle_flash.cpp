#include "muzzle_flash.hpp"

#include <cmath>

namespace viewer {
MuzzleFlashLight MuzzleFlashLight::transformed(const glm::mat4& transform) const {
    auto result = *this;
    result.position = glm::vec3(transform * glm::vec4(position, 1));
    return result;
}

void MuzzleFlash::fire(double seconds, glm::vec3 eyeMuzzle) {
    if (!std::isfinite(seconds)) return;
    firedAt_ = seconds;
    eyeMuzzle_ = eyeMuzzle;
}

MuzzleFlashLight MuzzleFlash::light(double seconds) const {
    MuzzleFlashLight result;
    result.position = eyeMuzzle_;
    const double elapsed = seconds - firedAt_;
    if (!std::isfinite(elapsed) || elapsed < 0 || elapsed >= duration) return result;
    const float remaining = static_cast<float>(1 - elapsed / duration);
    result.color = glm::vec3(7.0f, 3.0f, .7f) * (remaining * remaining);
    return result;
}
} // namespace viewer
