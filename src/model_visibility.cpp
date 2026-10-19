#include "model_visibility.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
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
    drawBounds_.reserve(model.draws.size());
    detailDistances_.reserve(model.draws.size());
    lowDetails_.reserve(model.draws.size());
    for (const auto& draw : model.draws) {
        if (draw.primitive >= localBounds.size()) throw std::runtime_error("Model visibility draw references an invalid primitive");
        drawBounds_.push_back(transformBounds(localBounds[draw.primitive], draw.transform));
        if (!std::isfinite(draw.horizontalMotionRadius) || draw.horizontalMotionRadius < 0)
            throw std::runtime_error("Model visibility requires finite nonnegative motion padding");
        // A small vertical allowance covers hinge solver slop under contact.
        const glm::vec3 padding(draw.horizontalMotionRadius, draw.horizontalMotionRadius > 0 ? .02f : 0,
                                draw.horizontalMotionRadius);
        drawBounds_.back().minimum -= padding;
        drawBounds_.back().maximum += padding;
        if (!std::isfinite(draw.detailDistance) || draw.detailDistance < 0.0f)
            throw std::runtime_error("Model visibility requires a nonnegative finite detail distance");
        detailDistances_.push_back(draw.detailDistance);
        lowDetails_.push_back(draw.lowDetail);
    }
    index_ = VisibilityIndex(drawBounds_);
    detailBounds_ = drawBounds_;
    struct Group { VisibilityBounds bounds; float distance; };
    // A mesh group can be instanced under several scene transforms. Alternatives
    // under the same transform share one decision; distinct instances stay apart.
    using GroupKey = std::pair<std::string, std::array<float, 16>>;
    std::map<GroupKey, Group> groups;
    const auto key = [](const DrawInstance& draw) {
        std::array<float, 16> transform{};
        for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
            transform[static_cast<std::size_t>(column * 4 + row)] = draw.transform[column][row];
        return GroupKey{draw.detailGroup, transform};
    };
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const auto& draw = model.draws[i];
        if (draw.detailGroup.empty()) continue;
        auto inserted = groups.emplace(key(draw), Group{drawBounds_[i], draw.detailDistance});
        auto& group = inserted.first->second;
        if (group.distance != draw.detailDistance)
            throw std::runtime_error("Building detail group requires matching distances");
        group.bounds.minimum = glm::min(group.bounds.minimum, drawBounds_[i].minimum);
        group.bounds.maximum = glm::max(group.bounds.maximum, drawBounds_[i].maximum);
    }
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        if (!model.draws[i].detailGroup.empty()) detailBounds_[i] = groups.at(key(model.draws[i])).bounds;
    }
}

VisibilityStats ModelVisibility::query(const glm::mat4& projectionView, std::vector<std::size_t>& visible,
    const HorizontalVisibilityRange* range, bool frustumCulling) const {
    return index_.query(projectionView, visible, range, frustumCulling);
}

bool buildingDetailsNear(const VisibilityBounds& bounds, float range,
                         glm::vec3 eye, const glm::mat4& projection) {
    if (range == 0) return false;
    // Perspective vertical scale is cot(FOV/2). Normal view uses 60 degrees;
    // 10° and 1° scopes therefore extend the range by about 6.6x and 66x.
    const float magnification = std::max(1.0f, projection[1][1] / std::sqrt(3.0f));
    const auto nearest = glm::clamp(eye, bounds.minimum, bounds.maximum);
    const auto delta = nearest - eye;
    const float effectiveRange = range * magnification;
    return glm::dot(delta, delta) <= effectiveRange * effectiveRange;
}

void ModelVisibility::filterDetails(const glm::vec3& eye, const glm::mat4& projection,
                                    std::vector<std::size_t>& visible) const {
    visible.erase(std::remove_if(visible.begin(), visible.end(), [&](std::size_t index) {
        const float range = detailDistances_.at(index);
        if (range == 0.0f) return false;
        const bool near = buildingDetailsNear(detailBounds_[index], range, eye, projection);
        return lowDetails_[index] ? near : !near;
    }), visible.end());
}
} // namespace viewer
