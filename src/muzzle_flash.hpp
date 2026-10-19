#pragma once

#include <limits>
#include <glm/glm.hpp>

namespace viewer {
// Position is eye-local for the weapon overlay and world-space for the scene.
// Color is linear light intensity; an inactive light has zero color.
struct MuzzleFlashLight {
    glm::vec3 position{0}, color{0};
    float range = 8.0f; // Meters; no shadow-map pass for this short-lived light.
    MuzzleFlashLight transformed(const glm::mat4& transform) const;
};

class MuzzleFlash {
public:
    static constexpr double duration = .075;
    explicit MuzzleFlash(double seconds = duration, glm::vec3 color = {7, 3, .7f}, float range = 8);
    // Retrigger on an actual shot, once per shell rather than per pellet.
    // Positions stay in the caller's frame: eye-local muzzles or world explosions.
    void fire(double seconds, glm::vec3 position);
    MuzzleFlashLight light(double seconds) const;
private:
    double firedAt_ = -std::numeric_limits<double>::infinity();
    glm::vec3 position_{0}, color_;
    double duration_;
    float range_;
};
} // namespace viewer
