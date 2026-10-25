#include "minimap_roads.hpp"
#include "model.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace viewer {
MinimapRoadGeometry prepareMinimapRoads(const Model& model) {
    std::map<std::pair<int, int>, std::vector<glm::vec2>> cells;
    for (const auto& draw : model.draws) {
        if (draw.name.rfind("roads_", 0) != 0) continue;
        const auto& mesh = model.primitives.at(draw.primitive);
        std::vector<glm::vec2> horizontal;
        horizontal.reserve(mesh.vertices.size());
        for (const auto& vertex : mesh.vertices) {
            const auto position = draw.transform * glm::vec4(vertex.position, 1);
            if (!std::isfinite(position.x) || !std::isfinite(position.z))
                throw std::runtime_error("Non-finite minimap road position");
            horizontal.emplace_back(position.x, position.z);
        }
        if (mesh.indices.size() % 3) throw std::runtime_error("Minimap roads require triangle geometry");
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const std::array<glm::vec2, 3> triangle{{horizontal.at(mesh.indices[i]),
                horizontal.at(mesh.indices[i+1]), horizontal.at(mesh.indices[i+2])}};
            const auto ab = triangle[1] - triangle[0], ac = triangle[2] - triangle[0];
            if (std::abs(ab.x * ac.y - ab.y * ac.x) < 1e-6f) continue;
            const auto center = (triangle[0] + triangle[1] + triangle[2]) / 3.f;
            auto& cell = cells[{int(std::floor(center.x / 100)), int(std::floor(center.y / 100))}];
            cell.insert(cell.end(), triangle.begin(), triangle.end());
        }
    }
    MinimapRoadGeometry result;
    std::size_t count = 0;
    for (const auto& cell : cells) count += cell.second.size();
    result.vertices.reserve(count);
    result.batches.reserve(cells.size());
    for (const auto& cell : cells) {
        glm::vec2 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
        for (const auto& p : cell.second) { minimum = glm::min(minimum, p); maximum = glm::max(maximum, p); }
        result.batches.push_back({result.vertices.size(), cell.second.size(), minimum, maximum});
        result.vertices.insert(result.vertices.end(), cell.second.begin(), cell.second.end());
    }
    return result;
}
} // namespace viewer
