#include "zombie_park.hpp"

#include <json.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace viewer {

std::vector<glm::vec2> zombieParkBoundary(const Model& scene,
                                        const std::filesystem::path& region) {
    if (!scene.geographicFrame) return {};
    std::ifstream stream(region);
    if (!stream) throw std::runtime_error("Cannot read zombie park boundary: " + region.string());
    const auto document = nlohmann::json::parse(stream);
    if (document.at("version") != 1) throw std::runtime_error("Unsupported zombie park boundary version");
    if (document.at("crs") != scene.geographicFrame->crs) return {};
    const auto& points = document.at("boundary_projected_m");
    if (!points.is_array() || points.size() < 3 || points.size() > 10000)
        throw std::runtime_error("Zombie park boundary needs 3 to 10000 vertices");
    glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
    std::vector<glm::vec2> result;
    for (const auto& point : points) {
        if (!point.is_array() || point.size() != 2 || !point[0].is_number() || !point[1].is_number())
            throw std::runtime_error("Zombie park boundary needs projected east/north pairs");
        const auto local = glm::dvec2(point[0].get<double>(), point[1].get<double>()) -
                           scene.geographicFrame->origin;
        const glm::vec2 world(local.x, -local.y);
        if (!std::isfinite(world.x) || !std::isfinite(world.y))
            throw std::runtime_error("Zombie park boundary contains non-finite coordinates");
        low = glm::min(low, world);
        high = glm::max(high, world);
        result.push_back(world);
    }
    if (high.x < scene.boundsMin.x || low.x > scene.boundsMax.x ||
        high.y < scene.boundsMin.z || low.y > scene.boundsMax.z) return {};
    return result;
}

} // namespace viewer
