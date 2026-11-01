#pragma once
#include "house_fixture.hpp"
#include "prefab_floorplan.hpp"

namespace apartment_fixture {
inline viewer::Model block(float width = 20, float depth = 24, int levels = 5,
                           const glm::mat4& transform = glm::mat4(1), const std::vector<glm::vec2>& outline = {}) {
    const std::vector<glm::vec2> rectangle{{-width/2,-depth/2},{-width/2,depth/2},{width/2,depth/2},{width/2,-depth/2}};
    auto model = house_fixture::outlined(outline.empty() ? rectangle : outline, transform, levels);
    model.draws.erase(std::remove_if(model.draws.begin(), model.draws.end(), [](const viewer::DrawInstance& d) { return d.name == "building_house_windows"; }), model.draws.end());
    for (int axis = 0; axis < 2; ++axis) for (const float sign : {-1.f, 1.f}) {
        const float length = axis == 0 ? depth : width, wall = sign * (axis == 0 ? width : depth) * .5f;
        const int columns = std::max(1, int(length / 3));
        const auto point = [&](float along, float y) { return axis == 0 ? glm::vec3(wall, y, along) : glm::vec3(along, y, wall); };
        for (int row = 0; row < levels; ++row) for (int col = 0; col < columns; ++col) {
            const float center = -length * .5f + length * (col + .5f) / columns;
            if (axis == 1 && row == 0 && std::abs(center) < 1.25f) continue;
            const float low = .9f + row * 3, high = low + 1.2f;
            interior_fixture::quad(model, point(center - .6f, low), point(center + .6f, low),
                point(center + .6f, high), point(center - .6f, high), "building_house_windows", transform);
        }
    }
    for (auto& draw : model.draws) if (draw.name.rfind("building_house", 0) == 0) draw.name.replace(0, 14, "building_prefab");
    interior_fixture::bounds(model); return model;
}
inline viewer::HouseSource source(const viewer::Model& model, const std::vector<viewer::DoorPanel>& doors) {
    viewer::HouseSource result; result.name = "building_prefab"; result.firstDoor = 100;
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const auto& draw = model.draws[i];
        if (draw.name == "building_prefab_walls" && draw.lowDetail) result.walls.push_back(i);
        if (draw.name == "building_prefab_roof") result.roofs.push_back(i);
        if (draw.name == "building_prefab_windows") result.windows.push_back(i);
    }
    for (std::size_t i = 0; i < doors.size(); ++i) result.doors.push_back(i);
    return result;
}
} // namespace apartment_fixture
