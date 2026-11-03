#include "door_config.hpp"

#include <toml++/toml.hpp>
#include <cmath>
#include <stdexcept>

namespace viewer {
void validateDoorConfig(const DoorConfig& config) {
    if (!std::isfinite(config.maxClosedFlexMeters) || config.maxClosedFlexMeters < 0)
        throw std::invalid_argument("max_closed_flex_m must be finite and nonnegative");
    if (!std::isfinite(config.breakFlexMeters) || config.breakFlexMeters < 0)
        throw std::invalid_argument("break_flex_m must be finite and nonnegative");
    if (config.breakFlexMeters > 0 && config.maxClosedFlexMeters >= config.breakFlexMeters)
        throw std::invalid_argument("max_closed_flex_m must start below break_flex_m when breaking is enabled");
}

DoorConfig loadDoorConfig(const std::filesystem::path& path) {
    try {
        const auto table = toml::parse_file(path.string());
        const auto travel = table["max_closed_flex_m"].value<double>();
        if (!travel) throw std::runtime_error("Missing or invalid max_closed_flex_m");
        if (*travel < 0) throw std::runtime_error("max_closed_flex_m must be finite and nonnegative");
        DoorConfig config{static_cast<float>(*travel)};
        const auto optionalDistance = [&](const char* key, float& field) {
            if (!table.contains(key)) return;
            const auto value = table[key].value<double>();
            if (!value || *value < 0) throw std::runtime_error(std::string("Invalid ") + key);
            field = static_cast<float>(*value);
        };
        optionalDistance("break_flex_m", config.breakFlexMeters);
        validateDoorConfig(config);
        return config;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
} // namespace viewer
