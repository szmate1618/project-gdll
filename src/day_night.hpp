#pragma once

#include "day_night_clock.hpp"
#include "atmosphere.hpp"
#include "night_lighting.hpp"
#include "sun_lighting.hpp"

namespace viewer {

// Linear RGB colors in the runtime frame (X east, Y up, Z south).
struct DayNightLighting {
    SunLighting sun;
    glm::vec3 fogColor{Atmosphere{}.color};
    glm::vec3 unlitColor{1.0f};
    float nightBlend = 0.0f; // Also fades the camera flashlight and rain lighting.
};

// Stylized east-to-west sun path: sunrise at 06:00, sunset at 18:00.
// Direct light fades out between 12 and 4 degrees above the horizon, bounding
// the visible long-shadow problem without expanding the local shadow volume.
DayNightLighting dayNightLighting(double hour, const SunLighting& day = {},
                                 const NightLighting& night = {}, const Atmosphere& atmosphere = {});

// Approximate day/night modulation for renderers that already output display
// RGB. Encode the shared linear unlit light once on the CPU, then multiply the
// existing shader color by this tint; white preserves the daylight appearance.
glm::vec3 dayNightDisplayTint(const DayNightLighting& lighting);

} // namespace viewer
