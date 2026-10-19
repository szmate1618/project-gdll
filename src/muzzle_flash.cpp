#include "muzzle_flash.hpp"

#include <cmath>
#include <stdexcept>

namespace viewer {
MuzzleFlashLight MuzzleFlashLight::transformed(const glm::mat4& transform) const {
    auto result = *this;
    result.position = glm::vec3(transform * glm::vec4(position, 1));
    return result;
}

MuzzleFlash::MuzzleFlash(double seconds, glm::vec3 color, float range)
    : color_(color), duration_(seconds), range_(range) {
    if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(range) || range <= 0 ||
        !std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
        glm::any(glm::lessThan(color, glm::vec3(0)))) throw std::invalid_argument("Invalid flash light profile");
}

void MuzzleFlash::fire(double seconds, glm::vec3 position) {
    if (!std::isfinite(seconds)) return;
    firedAt_ = seconds;
    position_ = position;
}

MuzzleFlashLight MuzzleFlash::light(double seconds) const {
    MuzzleFlashLight result;
    result.position = position_;
    result.range = range_;
    const double elapsed = seconds - firedAt_;
    if (!std::isfinite(elapsed) || elapsed < 0 || elapsed >= duration_) return result;
    const float remaining = static_cast<float>(1 - elapsed / duration_);
    result.color = color_ * (remaining * remaining);
    return result;
}
} // namespace viewer
