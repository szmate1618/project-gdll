#include "day_night.hpp"

#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
void validateHour(double hour) {
    if (!std::isfinite(hour) || hour < 0.0 || hour >= 24.0)
        throw std::invalid_argument("Time of day must be in [0, 24) hours");
}

float elevationBlend(float lowDegrees, float highDegrees, float sunY) {
    return glm::smoothstep(std::sin(glm::radians(lowDegrees)),
                           std::sin(glm::radians(highDegrees)), sunY);
}
}

double DayNightCycle::hourAt(double elapsedSeconds) const {
    validateHour(startHour);
    if (!std::isfinite(dayLengthSeconds) || dayLengthSeconds < 0.0 ||
        (dayLengthSeconds > 0.0 && dayLengthSeconds < 1.0))
        throw std::invalid_argument("Day length must be zero or at least one second");
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0)
        throw std::invalid_argument("Day/night elapsed time must be finite and nonnegative");
    if (dayLengthSeconds == 0.0) return startHour;
    const double fraction = std::fmod(elapsedSeconds, dayLengthSeconds) / dayLengthSeconds;
    return std::fmod(startHour + fraction * 24.0, 24.0);
}

DayNightLighting dayNightLighting(double hour, const SunLighting& day,
                                 const NightLighting& night, const Atmosphere& atmosphere) {
    validateHour(hour);
    const double angle = (hour - 6.0) * glm::radians(15.0);
    const float height = static_cast<float>(std::sin(angle));
    DayNightLighting result;
    result.sun = day;
    // A tilted orbit avoids the light-view up-axis singularity at noon. This
    // is an artistic cycle, not a geographic/date-based solar calculation.
    result.sun.direction = glm::normalize(glm::vec3(static_cast<float>(std::cos(angle)),
                                                   0.85f * height, 0.30f * height));
    const float sunY = result.sun.direction.y;
    const float daylight = elevationBlend(-6.0f, 15.0f, sunY);
    const float direct = elevationBlend(4.0f, 12.0f, sunY);
    const float highSun = elevationBlend(8.0f, 30.0f, sunY);
    const auto warmSun = day.color * glm::vec3(1.15f, 0.65f, 0.38f);
    result.sun.color = glm::mix(warmSun, day.color, highSun) * direct;
    result.sun.skyAmbient = glm::mix(night.skyAmbient, day.skyAmbient, daylight);
    result.sun.groundAmbient = glm::mix(night.groundAmbient, day.groundAmbient, daylight);
    result.fogColor = glm::mix(night.fogColor, atmosphere.color, daylight);
    const float twilight = elevationBlend(-6.0f, 0.0f, sunY) *
                           (1.0f - elevationBlend(0.0f, 15.0f, sunY));
    result.fogColor = glm::mix(result.fogColor, glm::vec3(0.30f, 0.12f, 0.08f), 0.55f * twilight);
    result.unlitColor = glm::mix(night.skyAmbient, glm::vec3(1.0f), daylight);
    result.nightBlend = 1.0f - daylight;
    return result;
}

} // namespace viewer
