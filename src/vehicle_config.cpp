#include "vehicle_config.hpp"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
template<class T> T required(const toml::table& table, const char* key) {
    auto value = table[key].value<T>();
    if (!value) throw std::runtime_error(std::string("Missing or invalid vehicle field: ") + key);
    return *value;
}
glm::vec3 vector(const toml::table& table, const char* key) {
    const auto* a = table[key].as_array();
    if (!a || a->size() != 3) throw std::runtime_error(std::string("Expected three coordinates: ") + key);
    glm::vec3 result;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto value = (*a)[i].value<double>();
        if (!value || !std::isfinite(*value) || std::abs(*value) > 20)
            throw std::runtime_error(std::string("Invalid vehicle coordinate: ") + key);
        result[static_cast<int>(i)] = static_cast<float>(*value);
    }
    return result;
}
}
void validateVehicleConfig(const VehicleConfig& c) {
    if (c.id.empty() || c.name.empty() || c.name.size() > 100 || !std::isfinite(c.weightKg) ||
        c.weightKg <= 100 || c.weightKg > 20000 || c.seats.empty() || c.seats.size() > maxVehicleSeats)
        throw std::runtime_error("Invalid vehicle name, weight or seat count");
    for (auto v : {c.wheels[0],c.wheels[1],c.wheels[2],c.wheels[3],c.headlights[0],c.headlights[1]})
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(v[axis]) || std::abs(v[axis]) > 20)
                throw std::runtime_error("Invalid wheel or headlight position");
    for (std::size_t i = 0; i < 4; ++i)
        if ((i % 2 == 0 ? c.wheels[i].x >= 0 : c.wheels[i].x <= 0) ||
            (i < 2 ? c.wheels[i].z >= 0 : c.wheels[i].z <= 0))
            throw std::runtime_error("Wheels must be ordered front-left, front-right, rear-left, rear-right");
    for (std::size_t i = 0; i < c.seats.size(); ++i) {
        const auto& s = c.seats[i];
        for (auto v : {s.eyePosition, s.entryPosition, s.exitPosition})
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(v[axis]) || std::abs(v[axis]) > 20)
                    throw std::runtime_error("Invalid vehicle seat coordinate");
        if (!std::isfinite(s.entryRadius) || s.entryRadius <= 0 || s.entryRadius > 5 ||
            (i % 2 == 0 ? s.eyePosition.x >= 0 : s.eyePosition.x <= 0) ||
            (i < 2 ? s.eyePosition.z >= 0 : s.eyePosition.z <= 0))
            throw std::runtime_error("Seats must be ordered front-left, front-right, rear-left, rear-right");
    }
}
VehicleConfig loadVehicleConfig(const std::filesystem::path& path) {
    try {
        const auto table = toml::parse_file(path.string());
        VehicleConfig c;
        c.id = path.stem().string(); c.name = required<std::string>(table, "name");
        c.weightKg = static_cast<float>(required<double>(table, "weight_kg"));
        const auto* wheels = table["wheels"].as_table();
        const auto* lights = table["headlights"].as_table();
        if (!wheels || !lights) throw std::runtime_error("Expected [wheels] and [headlights] tables");
        const char* wheelNames[]{"front_left","front_right","rear_left","rear_right"};
        for (std::size_t i=0;i<4;++i) c.wheels[i]=vector(*wheels,wheelNames[i]);
        c.headlights={vector(*lights,"left"),vector(*lights,"right")};
        const auto count = required<std::int64_t>(table, "seat_count");
        const auto* seats = table["seats"].as_array();
        if (count < 1 || count > static_cast<std::int64_t>(maxVehicleSeats) || !seats ||
            seats->size() != static_cast<std::size_t>(count))
            throw std::runtime_error("seat_count must match one to four [[seats]] tables");
        c.seats.clear();
        for (const auto& entry : *seats) {
            const auto* seat = entry.as_table();
            if (!seat) throw std::runtime_error("Expected [[seats]] table");
            c.seats.push_back({vector(*seat,"eye_position"), vector(*seat,"entry_position"),
                              vector(*seat,"exit_position"), static_cast<float>(required<double>(*seat,"entry_radius"))});
        }
        validateVehicleConfig(c);
        return c;
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
}
std::vector<std::filesystem::path> vehicleConfigFiles(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == ".toml") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if (files.empty() || files.size() > maxVehicleTypes) throw std::runtime_error("Expected 1 to 32 vehicle configs");
    return files;
}
std::vector<VehicleConfig> loadVehicleConfigs(const std::filesystem::path& directory) {
    std::vector<VehicleConfig> result;
    for (const auto& path : vehicleConfigFiles(directory)) result.push_back(loadVehicleConfig(path));
    // Stable replicated type IDs, with the Corolla at index zero.
    const auto first = std::find_if(result.begin(), result.end(), [](const auto& c) { return c.id == "toyota_corolla_1999"; });
    if (first != result.end()) std::rotate(result.begin(), first, first + 1);
    return result;
}
const char* vehicleSeatName(std::size_t seat) {
    static constexpr const char* names[]{"driver", "front-right passenger", "rear-left passenger", "rear-right passenger"};
    if (seat >= maxVehicleSeats) throw std::out_of_range("Invalid vehicle seat");
    return names[seat];
}
} // namespace viewer
