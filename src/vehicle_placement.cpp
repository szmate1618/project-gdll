#include "vehicle_placement.hpp"
#include "file_fingerprint.hpp"
#include <json.hpp>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>
#include <unordered_set>

namespace viewer {
namespace {
float number(const nlohmann::json& value) {
    if (!value.is_number()) throw std::runtime_error("Expected a vehicle coordinate");
    const double n = value.get<double>();
    if (!std::isfinite(n) || std::abs(n) > 1e7) throw std::runtime_error("Invalid vehicle coordinate");
    return static_cast<float>(n);
}
}
std::optional<std::vector<VehiclePlacement>> loadVehiclePlacements(const std::filesystem::path& map) {
    const auto path = map.parent_path() / "abandoned_cars.instances.json";
    if (!std::filesystem::exists(path)) return {};
    try {
        std::ifstream stream(path);
        const auto document = nlohmann::json::parse(stream);
        if (document.at("schema") != "godollo.abandoned-car-placements" || !document.at("version").is_number_integer() || document.at("version") != 1)
            throw std::runtime_error("Unsupported vehicle placement schema/version");
        if (document.at("map").at("map_sha256") != fileSha256(map))
            throw std::runtime_error("Vehicle placement map fingerprint mismatch");
        const auto& frame = document.at("transforms");
        const auto& axes = frame.at("asset_axes");
        if (frame.at("units") != "meters" || axes.at("forward") != "+Z" ||
            axes.at("up") != "+Y" || axes.at("right") != "+X")
            throw std::runtime_error("Unsupported vehicle placement coordinate frame");
        const auto& instances = document.at("instances");
        if (!instances.is_array() || instances.size() > maxVehicleInstances)
            throw std::runtime_error("Vehicle placement population exceeds runtime budget");
        if (document.at("counts").at("instances") != instances.size())
            throw std::runtime_error("Vehicle placement count mismatch");
        std::vector<VehiclePlacement> result;
        std::unordered_set<std::string> ids;
        result.reserve(instances.size());
        // Sidecars use a ground-contact pivot and +Z forward. Runtime chassis
        // uses -Z forward and a shared ground-to-origin offset for every type.
        const auto reverse = glm::angleAxis(glm::radians(180.f), glm::vec3(0,1,0));
        const float pivotHeight = vehiclePivotHeight;
        for (const auto& instance : instances) {
            const auto id = instance.at("id").get<std::string>();
            if (id.empty() || !ids.insert(id).second) throw std::runtime_error("Empty or duplicate vehicle placement ID");
            const auto& p = instance.at("position");
            const auto& q = instance.at("rotation_xyzw");
            if (!p.is_array() || p.size() != 3 || !q.is_array() || q.size() != 4)
                throw std::runtime_error("Vehicle placement needs position[3] and rotation_xyzw[4]");
            const glm::vec3 position(number(p[0]),number(p[1]),number(p[2]));
            const glm::quat rotation(number(q[3]),number(q[0]),number(q[1]),number(q[2]));
            if (std::abs(glm::length(rotation) - 1) > .001f)
                throw std::runtime_error("Vehicle placement quaternion must be unit length");
            const auto normalized = glm::normalize(rotation);
            result.push_back({id, position + normalized * glm::vec3(0,pivotHeight,0), normalized * reverse});
        }
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
void placeVehicles(VehicleLayer& cars, const std::vector<VehiclePlacement>& placements, std::uint32_t seed) {
    if (placements.size() > maxVehicleInstances - cars.count())
        throw std::runtime_error("Vehicle placement population exceeds runtime budget");
    std::mt19937 random(seed);
    std::uniform_int_distribution<std::size_t> type(0,cars.typeCount()-1);
    for (const auto& placement : placements) cars.add(placement.center, placement.rotation, type(random));
}
}
