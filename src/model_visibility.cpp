#include "model_visibility.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
VisibilityBounds primitiveBounds(const Primitive& primitive) {
    if (primitive.vertices.empty()) throw std::runtime_error("Model visibility requires non-empty draw primitives");
    VisibilityBounds result{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    for (const auto& vertex : primitive.vertices) {
        for (int axis = 0; axis < 3; ++axis) if (!std::isfinite(vertex.position[axis]))
            throw std::runtime_error("Model visibility requires finite vertex positions");
        result.minimum = glm::min(result.minimum, vertex.position);
        result.maximum = glm::max(result.maximum, vertex.position);
    }
    return result;
}

VisibilityBounds transformBounds(const VisibilityBounds& local, const glm::mat4& transform) {
    VisibilityBounds result{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
        if (!std::isfinite(transform[column][row])) throw std::runtime_error("Model visibility requires finite draw transforms");
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 localCorner{(corner & 1) ? local.maximum.x : local.minimum.x,
                                    (corner & 2) ? local.maximum.y : local.minimum.y,
                                    (corner & 4) ? local.maximum.z : local.minimum.z};
        const glm::vec4 transformed = transform * glm::vec4(localCorner, 1.0f);
        if (transformed.w != 1.0f || !std::isfinite(transformed.x) || !std::isfinite(transformed.y) || !std::isfinite(transformed.z))
            throw std::runtime_error("Model visibility requires finite affine draw transforms");
        const glm::vec3 point(transformed);
        result.minimum = glm::min(result.minimum, point);
        result.maximum = glm::max(result.maximum, point);
    }
    return result;
}
} // namespace

ModelVisibility::ModelVisibility(const Model& model) {
    std::vector<VisibilityBounds> localBounds;
    localBounds.reserve(model.primitives.size());
    for (const auto& primitive : model.primitives) localBounds.push_back(primitiveBounds(primitive));
    std::vector<VisibilityBounds> drawBounds;
    drawBounds.reserve(model.draws.size());
    for (const auto& draw : model.draws) {
        if (draw.primitive >= localBounds.size()) throw std::runtime_error("Model visibility draw references an invalid primitive");
        drawBounds.push_back(transformBounds(localBounds[draw.primitive], draw.transform));
    }
    index_ = VisibilityIndex(drawBounds);
}

VisibilityStats ModelVisibility::query(const glm::mat4& projectionView, std::vector<std::size_t>& visible) const {
    return index_.query(projectionView, visible);
}
} // namespace viewer
