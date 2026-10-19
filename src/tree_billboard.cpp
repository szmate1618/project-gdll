#include "tree_billboard.hpp"
#include "vegetation_blast.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {

std::optional<TreeBillboardLayout> treeBillboardLayout(const Model& model) {
    if (model.draws.size() != 4) return std::nullopt;
    std::optional<std::size_t> front;
    std::size_t upright = 0, horizontal = 0;
    float topHeight = std::numeric_limits<float>::max();
    TreeBillboardLayout result;
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const auto& draw = model.draws[i];
        const auto& mesh = model.primitives.at(draw.primitive);
        if (mesh.vertices.size() != 4 || mesh.indices.size() != 6) return std::nullopt;
        auto lo = mesh.vertices[0].position, hi = lo;
        for (const auto& vertex : mesh.vertices) { lo = glm::min(lo, vertex.position); hi = glm::max(hi, vertex.position); }
        const auto extent = hi - lo;
        const auto a = mesh.vertices.at(mesh.indices[0]).position;
        const auto b = mesh.vertices.at(mesh.indices[1]).position;
        const auto c = mesh.vertices.at(mesh.indices[2]).position;
        const auto normal = glm::cross(b - a, c - a);
        constexpr float tolerance = .0001f;
        if (extent.z < tolerance && extent.x > tolerance && extent.y > tolerance && normal.z > 0) {
            if (front) return std::nullopt;
            front = i; ++upright;
        } else if (extent.x < tolerance && extent.y > tolerance && extent.z > tolerance) {
            ++upright;
        } else if (extent.y < tolerance && extent.x > tolerance && extent.z > tolerance && normal.y > 0) {
            ++horizontal;
            const auto center = (lo + hi) * .5f;
            const float height = (draw.transform * glm::vec4(center, 1)).y;
            if (height < topHeight) { topHeight = height; result.topDraw = i; result.topPivot = center; }
        } else return std::nullopt;
    }
    if (!front || upright != 2 || horizontal != 2) return std::nullopt;
    result.sideDraw = *front;
    return result;
}

TreeBillboardMode treeBillboardMode(glm::vec3 root, glm::vec3 eye, float distance) {
    const auto offset = eye - root;
    if (distance <= 0 || glm::dot(offset, offset) < distance * distance) return TreeBillboardMode::Full;
    return offset.y * offset.y > offset.x * offset.x + offset.z * offset.z
        ? TreeBillboardMode::Top : TreeBillboardMode::Side;
}

VisibilityBounds treeBillboardBounds(const TreeInstance& instance, const Model& model,
                                    const TreeBillboardLayout& layout) {
    auto bounds = vegetationBlastBounds(instance);
    const glm::vec3 root(instance.transform[3]);
    const auto& side = model.draws.at(layout.sideDraw);
    const auto sideMatrix = instance.transform * side.transform;
    float reach = 0;
    for (const auto& vertex : model.primitives.at(side.primitive).vertices) {
        const glm::vec3 p(sideMatrix * glm::vec4(vertex.position, 1));
        reach = std::max(reach, glm::length(glm::vec2(p.x-root.x, p.z-root.z)));
    }
    bounds.minimum.x = std::min(bounds.minimum.x, root.x-reach);
    bounds.minimum.z = std::min(bounds.minimum.z, root.z-reach);
    bounds.maximum.x = std::max(bounds.maximum.x, root.x+reach);
    bounds.maximum.z = std::max(bounds.maximum.z, root.z+reach);
    const auto& top = model.draws.at(layout.topDraw);
    const auto topMatrix = instance.transform * top.transform;
    const glm::vec3 pivot(topMatrix * glm::vec4(layout.topPivot, 1));
    float radius = 0;
    for (const auto& vertex : model.primitives.at(top.primitive).vertices)
        radius = std::max(radius, glm::length(glm::vec3(topMatrix * glm::vec4(vertex.position, 1))-pivot));
    bounds.minimum = glm::min(bounds.minimum, pivot-glm::vec3(radius+.01f));
    bounds.maximum = glm::max(bounds.maximum, pivot+glm::vec3(radius+.01f));
    return bounds;
}

} // namespace viewer
