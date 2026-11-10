#include "item_footprint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
bool finite(glm::vec3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
double cross(glm::dvec2 a, glm::dvec2 b) { return a.x * b.y - a.y * b.x; }

std::vector<glm::dvec2> hull(std::vector<glm::dvec2> points) {
    std::sort(points.begin(), points.end(), [](auto a, auto b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3) return points;
    std::vector<glm::dvec2> result;
    for (const auto point : points) {
        while (result.size() >= 2 && cross(result.back() - result[result.size() - 2], point - result.back()) <= 0)
            result.pop_back();
        result.push_back(point);
    }
    const auto lower = result.size();
    for (std::size_t index = points.size() - 1; index-- > 0;) {
        const auto point = points[index];
        while (result.size() > lower && cross(result.back() - result[result.size() - 2], point - result.back()) <= 0)
            result.pop_back();
        result.push_back(point);
    }
    result.pop_back();
    return result;
}

struct Box {
    glm::dmat3 axes{1};
    glm::dvec3 dimensions{0};
    double volume = std::numeric_limits<double>::infinity();
    double area = std::numeric_limits<double>::infinity();
    double perimeter = std::numeric_limits<double>::infinity();
};

Box canonicalBox(glm::dmat3 axes, glm::dvec3 dimensions) {
    std::array<int, 3> order{0, 1, 2};
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return dimensions[a] > dimensions[b]; });
    Box box;
    for (int axis = 0; axis < 3; ++axis) {
        box.axes[axis] = axes[order[axis]];
        box.dimensions[axis] = dimensions[order[axis]];
    }
    if (glm::dot(glm::cross(box.axes[0], box.axes[1]), box.axes[2]) < 0) box.axes[2] = -box.axes[2];
    box.area = box.dimensions.x * box.dimensions.y;
    box.volume = box.area * box.dimensions.z;
    box.perimeter = box.dimensions.x + box.dimensions.y + box.dimensions.z;
    return box;
}

bool better(const Box& candidate, const Box& previous, double scale) {
    if (!std::isfinite(previous.volume)) return true;
    const double volumeTolerance = scale * scale * scale * 1e-8;
    if (candidate.volume < previous.volume - volumeTolerance) return true;
    if (candidate.volume > previous.volume + volumeTolerance) return false;
    // Planar and nearly planar clouds need area to distinguish zero-volume boxes.
    const double areaTolerance = scale * scale * 1e-8;
    if (candidate.area < previous.area - areaTolerance) return true;
    if (candidate.area > previous.area + areaTolerance) return false;
    return candidate.perimeter < previous.perimeter - scale * 1e-8;
}

std::vector<glm::dvec3> representativePoints(const std::vector<glm::dvec3>& points, double scaleSquared) {
    // Farthest-point sampling preserves thin extremities without depending on
    // vertex density, UV seams, or any fixed world directions. Input order only
    // breaks equivalent-distance ties; a rigid transform keeps those ties.
    constexpr std::size_t count = 12;
    std::vector<glm::dvec3> result;
    std::vector<double> nearest(points.size(), std::numeric_limits<double>::infinity());
    std::size_t selected = 0;
    for (std::size_t iteration = 0; iteration < std::min(count, points.size()); ++iteration) {
        result.push_back(points[selected]);
        double largest = 0;
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto delta = points[index] - result.back();
            nearest[index] = std::min(nearest[index], glm::dot(delta, delta));
            if (nearest[index] > largest + scaleSquared * 1e-6) {
                largest = nearest[index]; selected = index;
            }
        }
        if (largest <= scaleSquared * 1e-12) break;
    }
    return result;
}

Box orientedBox(const std::vector<glm::vec3>& input) {
    if (input.empty()) throw std::invalid_argument("An item footprint needs physical geometry");
    if (!finite(input.front())) throw std::invalid_argument("Nonfinite item footprint position");
    const glm::dvec3 origin(input.front());
    std::vector<glm::dvec3> points;
    points.reserve(input.size());
    double scaleSquared = 0;
    for (const auto point : input) {
        if (!finite(point)) throw std::invalid_argument("Nonfinite item footprint position");
        const auto relative = glm::dvec3(point) - origin;
        points.push_back(relative);
        scaleSquared = std::max(scaleSquared, glm::dot(relative, relative));
    }
    if (scaleSquared == 0) return canonicalBox(glm::dmat3(1), glm::dvec3(0));
    const double scale = std::sqrt(scaleSquared);
    const auto samples = representativePoints(points, scaleSquared);
    std::vector<glm::dvec3> normals;
    const auto appendNormal = [&](glm::dvec3 normal) {
        const double length = glm::length(normal);
        if (length < 1e-12) return;
        normal /= length;
        if (std::none_of(normals.begin(), normals.end(), [&](auto previous) {
                return std::abs(glm::dot(previous, normal)) > 1 - 1e-6;
            })) normals.push_back(normal);
    };
    glm::dvec3 diameter(0);
    for (std::size_t a = 0; a < samples.size(); ++a) {
        for (std::size_t b = a + 1; b < samples.size(); ++b) {
            const auto edge = samples[b] - samples[a];
            if (glm::dot(edge, edge) > glm::dot(diameter, diameter) + scaleSquared * 1e-6) diameter = edge;
            for (std::size_t c = b + 1; c < samples.size(); ++c) {
                const auto normal = glm::cross(edge, samples[c] - samples[a]);
                if (glm::length(normal) > scaleSquared * 1e-8) appendNormal(normal);
            }
        }
    }
    appendNormal(diameter / scale);
    Box best;
    for (const auto normal : normals) {
        glm::dvec3 u(0);
        for (const auto sample : samples) {
            const auto projected = sample - normal * glm::dot(sample, normal);
            if (glm::dot(projected, projected) > glm::dot(u, u) + scaleSquared * 1e-6) u = projected;
        }
        if (glm::length(u) < scale * 1e-8) {
            // A line has no unique roll. This arbitrary zero-width basis cannot
            // change its dimensions or its storage footprint.
            const auto axis = std::abs(normal.x) < .8 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
            u = glm::cross(normal, axis);
        }
        u = glm::normalize(u);
        const auto v = glm::normalize(glm::cross(normal, u));
        double low = std::numeric_limits<double>::infinity(), high = -low;
        std::vector<glm::dvec2> projected;
        projected.reserve(points.size());
        for (const auto point : points) {
            projected.push_back({glm::dot(point, u), glm::dot(point, v)});
            const auto distance = glm::dot(point, normal);
            low = std::min(low, distance); high = std::max(high, distance);
        }
        const auto polygon = hull(std::move(projected));
        const auto directions = std::min<std::size_t>(64, polygon.size());
        for (std::size_t direction = 0; direction < directions; ++direction) {
            const auto index = direction * polygon.size() / directions;
            auto x = polygon[(index + 1) % polygon.size()] - polygon[index];
            if (glm::length(x) < scale * 1e-12) x = {1, 0};
            else x = glm::normalize(x);
            const glm::dvec2 y(-x.y, x.x);
            glm::dvec2 minimum(std::numeric_limits<double>::infinity()), maximum = -minimum;
            for (const auto point : polygon) {
                const glm::dvec2 value(glm::dot(point, x), glm::dot(point, y));
                minimum = glm::min(minimum, value); maximum = glm::max(maximum, value);
            }
            const auto extent = glm::max(maximum - minimum, glm::dvec2(0));
            const auto candidate = canonicalBox(glm::dmat3(u * x.x + v * x.y, u * y.x + v * y.y, normal),
                                                glm::dvec3(extent, std::max(0.0, high - low)));
            if (better(candidate, best, scale)) best = candidate;
        }
    }
    if (!std::isfinite(best.volume)) throw std::invalid_argument("Cannot bound item geometry");
    return best;
}

int cells(double length, float scale) {
    // Input meshes use float metres. Do not add a whole cell for tiny rigid-
    // rotation error at an exact cell boundary; this tolerance is in cells.
    const double rounded = std::max(1.0, std::ceil(length * double(scale) - 1e-5));
    if (!std::isfinite(rounded) || rounded > std::numeric_limits<int>::max())
        throw std::overflow_error("Physical item footprint exceeds the integer cell range");
    return static_cast<int>(rounded);
}
} // namespace

float calibrateItemFootprint(const std::vector<ItemFootprintReference>& references) {
    double physicalCells = 0, physicalSquared = 0;
    for (const auto& reference : references) {
        if (reference.width <= 0 || reference.height <= 0)
            throw std::invalid_argument("Footprint calibration needs positive authored cell sizes");
        if (reference.points.empty()) continue;
        const auto box = orientedBox(reference.points);
        const std::array<int, 2> authored{std::max(reference.width, reference.height),
                                         std::min(reference.width, reference.height)};
        for (int axis = 0; axis < 2; ++axis) {
            const auto length = box.dimensions[axis];
            if (authored[axis] <= 1 || length <= 0) continue;
            physicalCells += length * (authored[axis] - .5);
            physicalSquared += length * length;
        }
    }
    if (physicalSquared == 0) return 0;
    const auto scale = physicalCells / physicalSquared;
    if (!std::isfinite(scale) || scale > std::numeric_limits<float>::max())
        throw std::overflow_error("Footprint calibration exceeds the physical scale range");
    return static_cast<float>(scale);
}

ItemFootprint estimateItemFootprint(const std::vector<glm::vec3>& points, float cellsPerMetre) {
    if (!std::isfinite(cellsPerMetre) || cellsPerMetre <= 0)
        throw std::invalid_argument("Item footprint scale must be positive finite cells per metre");
    const auto box = orientedBox(points);
    const glm::vec3 dimensions(box.dimensions);
    if (!finite(dimensions)) throw std::overflow_error("Physical item dimensions exceed the metre range");
    return {cells(box.dimensions.y, cellsPerMetre), cells(box.dimensions.x, cellsPerMetre), dimensions,
            glm::normalize(glm::quat_cast(glm::mat3(box.axes)))};
}

std::vector<glm::vec3> itemModelFootprintPoints(const ItemModelConfig& model) {
    std::vector<glm::vec3> result;
    if (!model.mesh.empty()) {
        result.reserve(model.mesh.size());
        for (const auto& vertex : model.mesh) result.push_back(vertex.position);
        return result;
    }
    result.reserve(model.parts.size() * 8);
    for (const auto& part : model.parts) {
        for (const bool top : {false, true}) {
            const auto width = top && part.shape == ItemModelShape::Frustum ? part.topSize : glm::vec2(part.size.x, part.size.z);
            for (const float x : {-1.f, 1.f}) for (const float z : {-1.f, 1.f})
                result.push_back(part.center + part.rotation * glm::vec3(x * width.x * .5f,
                    (top ? .5f : -.5f) * part.size.y, z * width.y * .5f));
        }
    }
    return result;
}

} // namespace viewer
