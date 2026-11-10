#include "item_model_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
ItemModelBounds itemModelBounds(const ItemModelConfig& config) {
    if (config.parts.empty()) return {};
    ItemModelBounds bounds{glm::vec3(std::numeric_limits<float>::infinity()),
        glm::vec3(-std::numeric_limits<float>::infinity())};
    for (const auto& part : config.parts) {
        const auto top = part.shape == ItemModelShape::Frustum ? part.topSize : glm::vec2(part.size.x, part.size.z);
        const glm::vec3 extent(std::max(part.size.x, top.x), part.size.y, std::max(part.size.z, top.y));
        bounds.minimum = glm::min(bounds.minimum, part.center - extent * .5f);
        bounds.maximum = glm::max(bounds.maximum, part.center + extent * .5f);
    }
    return bounds;
}

Model buildItemModel(const ItemModelConfig& config) {
    Model model;
    const auto bounds = itemModelBounds(config);
    model.boundsMin = bounds.minimum; model.boundsMax = bounds.maximum;
    for (const auto& part : config.parts) {
        const auto half = part.size * .5f;
        const auto top = part.shape == ItemModelShape::Frustum ? part.topSize * .5f : glm::vec2(half.x, half.z);
        const std::array<glm::vec3, 8> corners{{
            {-half.x, -half.y, -half.z}, {half.x, -half.y, -half.z},
            {half.x, -half.y, half.z}, {-half.x, -half.y, half.z},
            {-top.x, half.y, -top.y}, {top.x, half.y, -top.y},
            {top.x, half.y, top.y}, {-top.x, half.y, top.y}}};
        // Outward CCW quads: bottom, top, -Z, +X, +Z, -X.
        constexpr std::array<std::array<int, 4>, 6> faces{{
            {{0, 1, 2, 3}}, {{4, 7, 6, 5}}, {{0, 4, 5, 1}},
            {{1, 5, 6, 2}}, {{2, 6, 7, 3}}, {{3, 7, 4, 0}}}};
        Primitive primitive;
        primitive.material = static_cast<int>(model.materials.size());
        primitive.vertices.reserve(24); primitive.indices.reserve(36);
        for (const auto& face : faces) {
            const auto a = corners[face[0]], b = corners[face[1]], c = corners[face[2]];
            const auto cross = glm::cross(b - a, c - a);
            const auto length = glm::length(cross);
            if (!std::isfinite(length) || length <= 0) throw std::invalid_argument("Degenerate item model part: " + part.name);
            const auto normal = cross / length;
            constexpr std::array<glm::vec2, 4> uv{{{0,0}, {1,0}, {1,1}, {0,1}}};
            const auto first = static_cast<std::uint32_t>(primitive.vertices.size());
            for (std::size_t index = 0; index < face.size(); ++index)
                primitive.vertices.push_back({part.center + corners[face[index]], normal, uv[index], glm::vec4(1)});
            for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) primitive.indices.push_back(first + index);
        }
        Material material; material.baseColor = glm::vec4(part.color, 1);
        model.materials.push_back(material);
        model.draws.push_back({model.primitives.size(), glm::mat4(1), part.name});
        model.primitives.push_back(std::move(primitive));
    }
    return model;
}

std::string itemModelGeometryKey(const ItemModelConfig& config) {
    std::string key;
    const auto append = [&](const auto& value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    for (const auto& part : config.parts) {
        append(part.shape);
        for (int axis = 0; axis < 3; ++axis) { append(part.center[axis]); append(part.size[axis]); append(part.color[axis]); }
        if (part.shape == ItemModelShape::Frustum) { append(part.topSize.x); append(part.topSize.y); }
        key += part.name; key.push_back('\0'); key += part.material; key.push_back('\0');
    }
    return key;
}
} // namespace viewer
