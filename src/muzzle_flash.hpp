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
    // Retrigger on an actual shot, once per shell rather than per pellet.
    // Store the fired weapon's eye-local muzzle so a slot change cannot move it.
    void fire(double seconds, glm::vec3 eyeMuzzle);
    MuzzleFlashLight light(double seconds) const;
private:
    double firedAt_ = -std::numeric_limits<double>::infinity();
    glm::vec3 eyeMuzzle_{0};
};
} // namespace viewer
