#include "minimap_roads.hpp"
#include "minimap_fixture.hpp"
#include "visibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        auto model = minimapRoadFixture();
        model.draws.back().transform = glm::translate(glm::mat4(1), {400, 90, -500}) *
            glm::rotate(glm::mat4(1), glm::radians(90.f), {0, 1, 0}) * glm::scale(glm::mat4(1), {-1, 1, 1});
        auto building = model.draws.back(); building.name = "building_roof"; model.draws.push_back(building);
        const auto roads = viewer::prepareMinimapRoads(model);
        require(roads.vertices.size() == 6, "Road preparation included other scene geometry or lost triangles");
        require(roads.batches.size() == 2, "Long road triangles were not split into spatial batches");
        for (const auto p : roads.vertices)
            require(p.x >= 49.99f && p.x <= 750.01f && p.y >= -420.01f && p.y <= -399.99f,
                    "Road positions used the wrong frame or applied the node transform twice");
        std::vector<viewer::VisibilityBounds> bounds;
        for (const auto& batch : roads.batches) {
            require(batch.count == 3 && batch.first + batch.count <= roads.vertices.size(), "Invalid cached road range");
            bounds.push_back({{batch.minimum.x, 0, batch.minimum.y}, {batch.maximum.x, 0, batch.maximum.y}});
        }
        viewer::VisibilityIndex index(bounds);
        std::vector<std::size_t> visible;
        index.query(viewer::VisibilityBounds{{55, 0, -415}, {65, 0, -405}}, visible);
        require(visible.size() == 2, "A road crossing a cell edge was culled by its centroid instead of its full bounds");
        index.query(viewer::VisibilityBounds{{1000, 0, 1000}, {1400, 0, 1400}}, visible);
        require(visible.empty(), "Distant roads survived the minimap bounds query");
        require(viewer::prepareMinimapRoads(minimapFixture()).vertices.empty(), "Scene without roads created road geometry");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
