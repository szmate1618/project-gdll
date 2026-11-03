#include "day_night_clock.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {

double DayNightCycle::hourAt(double elapsedSeconds) const {
    if (!std::isfinite(startHour) || startHour < 0.0 || startHour >= 24.0)
        throw std::invalid_argument("Time of day must be in [0, 24) hours");
    if (!std::isfinite(dayLengthSeconds) || dayLengthSeconds < 0.0 ||
        (dayLengthSeconds > 0.0 && dayLengthSeconds < 1.0))
        throw std::invalid_argument("Day length must be zero or at least one second");
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0)
        throw std::invalid_argument("Day/night elapsed time must be finite and nonnegative");
    if (dayLengthSeconds == 0.0) return startHour;
    const double fraction = std::fmod(elapsedSeconds, dayLengthSeconds) / dayLengthSeconds;
    return std::fmod(startHour + fraction * 24.0, 24.0);
}

double DayNightCycle::nextMidnightAtOrAfter(double elapsedSeconds) const {
    const auto hour = hourAt(elapsedSeconds);
    if (hour == 0.0) return elapsedSeconds;
    if (dayLengthSeconds == 0.0) return std::numeric_limits<double>::infinity();
    return elapsedSeconds + (24.0 - hour) / 24.0 * dayLengthSeconds;
}

} // namespace viewer
