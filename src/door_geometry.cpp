#include "door_geometry.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>

namespace viewer {
namespace {
std::vector<Primitive> components(const Primitive& source) {
    std::vector<std::size_t> parent(source.vertices.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t i) {
        while (i != parent[i]) { parent[i] = parent[parent[i]]; i = parent[i]; }
        return i;
    };
    const auto join = [&](std::size_t a, std::size_t b) { parent[root(b)] = root(a); };
    // Support both indexed exports and older triangle-soup exports.
    std::map<std::array<float, 3>, std::size_t> positions;
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        const auto p = source.vertices[i].position;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            throw std::runtime_error("Door vertices must be finite");
        const auto inserted = positions.emplace(std::array<float, 3>{p.x, p.y, p.z}, i);
        if (!inserted.second) join(i, inserted.first->second);
    }
    if (source.indices.size() % 3) throw std::runtime_error("Door indices must contain triangles");
    for (std::size_t i = 0; i < source.indices.size(); i += 3) {
        for (int j = 0; j < 3; ++j) if (source.indices[i + j] >= parent.size())
            throw std::runtime_error("Invalid door vertex index");
        join(source.indices[i], source.indices[i + 1]);
        join(source.indices[i], source.indices[i + 2]);
    }
    std::map<std::size_t, Primitive> parts;
    std::map<std::pair<std::size_t, std::uint32_t>, std::uint32_t> remap;
    for (const auto index : source.indices) {
        const auto group = root(index);
        auto& part = parts[group];
        part.material = source.material;
        const auto inserted = remap.emplace(std::make_pair(group, index), static_cast<std::uint32_t>(part.vertices.size()));
        if (inserted.second) part.vertices.push_back(source.vertices[index]);
        part.indices.push_back(inserted.first->second);
    }
    std::vector<Primitive> result;
    for (auto& entry : parts) result.push_back(std::move(entry.second));
    return result;
}

DoorPanel describe(const Primitive& primitive, const DrawInstance& draw, std::size_t index) {
    const auto normal = glm::inverseTranspose(glm::mat3(draw.transform)) * primitive.vertices.front().normal;
    if (!std::isfinite(glm::length(normal)) || glm::length(normal) < .5f || std::abs(normal.y) > .001f * glm::length(normal))
        throw std::runtime_error("Door panel requires a horizontal outward normal");
    const auto outward = glm::normalize(normal);
    const glm::vec3 up(0, 1, 0);
    const auto right = glm::normalize(glm::cross(up, outward));
    const auto origin = glm::vec3(draw.transform * glm::vec4(primitive.vertices.front().position, 1));
    float left = std::numeric_limits<float>::max(), edge = -left, bottom = left, top = edge;
    std::vector<glm::vec3> points;
    for (const auto& vertex : primitive.vertices) {
        const auto p = glm::vec3(draw.transform * glm::vec4(vertex.position, 1));
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::abs(glm::dot(p - origin, outward)) > .02f)
            throw std::runtime_error("Door panel must be finite and planar");
        const float x = glm::dot(p - origin, right);
        left = std::min(left, x); edge = std::max(edge, x);
        bottom = std::min(bottom, p.y); top = std::max(top, p.y);
        points.push_back(p);
    }
    // Find actual contour edges, excluding internal foundation-band seams.
    // Weld positions here too: older triangle-soup exports duplicate vertices.
    std::map<std::array<float, 3>, std::size_t> unique;
    std::vector<std::size_t> canonical;
    for (const auto p : points)
        canonical.push_back(unique.emplace(std::array<float, 3>{p.x, p.y, p.z}, canonical.size()).first->second);
    std::map<std::pair<std::size_t, std::size_t>, int> edges;
    for (std::size_t i = 0; i < primitive.indices.size(); i += 3) for (int j = 0; j < 3; ++j) {
        const auto a = canonical[primitive.indices[i + j]], b = canonical[primitive.indices[i + (j + 1) % 3]];
        ++edges[std::minmax(a, b)];
    }
    for (const auto& edgeCount : edges) {
        if (edgeCount.second != 1) continue;
        const auto a = points[edgeCount.first.first], b = points[edgeCount.first.second];
        if (a.y >= top - .001f || b.y >= top - .001f || std::abs(glm::dot(b - a, right)) < .00001f) continue;
        bottom = std::max(bottom, std::max(a.y, b.y));
    }
    // Generated wall bases are 8 cm below terrain. Inset the physical panel by
    // 10 cm above the highest base sample to clear ground; keep all drawn vertices.
    bottom += .10f;
    if (edge - left < .15f || top - bottom < .2f) throw std::runtime_error("Door panel dimensions are too small");
    auto hinge = origin + right * left;
    hinge.y = (bottom + top) * .5f;
    DoorPanel result;
    result.draw = index;
    result.closedTransform = draw.transform;
    result.hinge = hinge;
    result.center = hinge + right * ((edge - left) * .5f);
    result.halfExtents = {(edge - left) * .5f, (top - bottom) * .5f, .02f};
    result.outward = outward;
    result.rotation = glm::normalize(glm::quat_cast(glm::mat3(right, up, outward)));
    return result;
}
} // namespace

std::vector<DoorPanel> prepareDoorPanels(Model& model) {
    std::vector<DoorPanel> panels;
    std::vector<DrawInstance> draws;
    for (const auto& source : model.draws) {
        if (source.name.rfind("building_", 0) != 0 || source.name.size() < 6 ||
            source.name.compare(source.name.size() - 6, 6, "_doors") != 0 || source.lowDetail) {
            draws.push_back(source);
            continue;
        }
        auto parts = components(model.primitives.at(source.primitive));
        for (auto& part : parts) {
            auto draw = source;
            draw.primitive = model.primitives.size();
            try {
                const auto panel = describe(part, draw, draws.size());
                // A fixed BVH bounds the entire horizontal hinge sweep. Moving
                // panels remain conservatively culled in normal and scope views.
                draw.horizontalMotionRadius = panel.halfExtents.x * 4 + .04f;
                panels.push_back(panel);
            } catch (const std::runtime_error& error) {
                model.warnings.push_back(source.name + ": static door retained: " + error.what());
            }
            model.primitives.push_back(std::move(part));
            draws.push_back(std::move(draw));
        }
    }
    model.draws = std::move(draws);
    // Drop superseded batched primitives before GPU upload. No source asset is
    // rewritten; this keeps splitting from uploading both copies of each door.
    std::vector<std::size_t> remap(model.primitives.size(), std::numeric_limits<std::size_t>::max());
    std::vector<Primitive> used;
    used.reserve(model.primitives.size());
    for (auto& draw : model.draws) {
        auto& index = remap.at(draw.primitive);
        if (index == std::numeric_limits<std::size_t>::max()) {
            index = used.size();
            used.push_back(std::move(model.primitives.at(draw.primitive)));
        }
        draw.primitive = index;
    }
    model.primitives = std::move(used);
    return panels;
}
} // namespace viewer
