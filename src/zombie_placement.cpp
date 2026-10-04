#include "zombie_placement.hpp"

#include "collision_world.hpp"
#include "model.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace viewer {
namespace {

constexpr float gridSpacing = 2.4f;
constexpr float gridJitter = 0.14f;  // Leaves at least 2.12 m between neighbors.
constexpr float bodyRadius = 0.45f;
constexpr float bodyHeight = 2.0f;
constexpr float centerClearing = 2.0f;
constexpr std::size_t candidateLimit = 1000000;

// Fixed integer arithmetic avoids implementation-dependent random distributions.
std::uint32_t mix(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float unitFloat(std::uint32_t value) {
    return static_cast<float>(mix(value) >> 8) * (1.0f / 16777216.0f);
}

struct Candidate {
    glm::vec2 position;
    float distanceSquared;
    std::uint32_t seed;
};

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

std::optional<glm::mat4> groundTransform(const CollisionWorld& world, const Model& scene,
                                       glm::vec2 p, std::uint32_t seed) {
    const auto ground = world.groundAt(p.x, p.y, scene.boundsMin.y - bodyHeight - 1,
        scene.boundsMax.y + bodyHeight + 1, world.stats().terrainTriangles != 0);
    if (!ground || ground->normal.y < std::cos(glm::radians(25.0f))) return std::nullopt;
    const glm::vec3 feet(p.x, ground->height, p.y);
    // Lift only the collision probe for capsule tangency; feet stay on triangles.
    const auto probe = feet + glm::vec3(0, bodyRadius * (1 / ground->normal.y - 1) + 0.01f, 0);
    if (world.insideBuilding(probe, bodyRadius, bodyHeight)) return std::nullopt;
    const auto contacts = world.contacts(probe, bodyRadius, bodyHeight);
    if (std::any_of(contacts.begin(), contacts.end(), [](const Contact& contact) {
            return contact.depth > 0.005f;
        })) return std::nullopt;
    return glm::rotate(glm::translate(glm::mat4(1), feet),
        unitFloat(seed ^ 0x68bc21ebu) * 6.28318530718f, glm::vec3(0, 1, 0));
}

bool insideRegion(glm::vec2 p, const std::vector<glm::vec2>& boundary) {
    bool inside = false;
    for (std::size_t i = 0, j = boundary.size() - 1; i < boundary.size(); j = i++) {
        const auto a = boundary[j], b = boundary[i], edge = b - a;
        const float lengthSquared = glm::dot(edge, edge);
        const auto closest = a + edge * (lengthSquared > 0 ?
            glm::clamp(glm::dot(p - a, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f);
        if (glm::length(p - closest) < bodyRadius) return false;
        if ((a.y > p.y) != (b.y > p.y) &&
            p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y)) inside = !inside;
    }
    return inside;
}

}  // namespace

std::vector<glm::mat4> placeZombies(const CollisionWorld& world, const Model& scene,
                                   glm::vec2 center, std::size_t count, float radius) {
    if (count == 0) return {};
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(radius) || radius <= 0) {
        throw std::invalid_argument("Zombie placement requires a finite center and positive radius");
    }
    if (!finite(scene.boundsMin) || !finite(scene.boundsMax) ||
        glm::any(glm::greaterThan(scene.boundsMin, scene.boundsMax)) ||
        world.stats().triangles == 0) {
        throw std::runtime_error("Cannot place zombies: the scene has no valid ground bounds");
    }

    const glm::vec2 sceneLow(scene.boundsMin.x, scene.boundsMin.z);
    const glm::vec2 sceneHigh(scene.boundsMax.x, scene.boundsMax.z);
    if (glm::any(glm::lessThan(center, sceneLow)) ||
        glm::any(glm::greaterThan(center, sceneHigh))) {
        center = sceneLow * 0.5f + sceneHigh * 0.5f;
    }
    const auto low = glm::max(sceneLow + bodyRadius, center - radius);
    const auto high = glm::min(sceneHigh - bodyRadius, center + radius);
    std::vector<Candidate> candidates;
    if (glm::all(glm::lessThanEqual(low, high))) {
        // Bound the lattice before allocating or converting floating coordinates
        // to integers. Even an impossible request must finish in bounded work.
        const auto first = glm::ceil((low - center - gridJitter) / gridSpacing);
        const auto last = glm::floor((high - center + gridJitter) / gridSpacing);
        const double width = static_cast<double>(last.x) - first.x + 1;
        const double depth = static_cast<double>(last.y) - first.y + 1;
        if (!std::isfinite(width * depth) || width * depth > candidateLimit ||
            std::abs(static_cast<double>(first.x)) > candidateLimit ||
            std::abs(static_cast<double>(first.y)) > candidateLimit ||
            std::abs(static_cast<double>(last.x)) > candidateLimit ||
            std::abs(static_cast<double>(last.y)) > candidateLimit) {
            throw std::runtime_error("Cannot place zombies: search region exceeds one million grid candidates");
        }
        if (width > 0 && depth > 0) candidates.reserve(static_cast<std::size_t>(width * depth));
        for (int z = static_cast<int>(first.y); z <= static_cast<int>(last.y); ++z) {
            for (int x = static_cast<int>(first.x); x <= static_cast<int>(last.x); ++x) {
                const auto seed = mix(static_cast<std::uint32_t>(x) * 0x9e3779b9u ^
                                      static_cast<std::uint32_t>(z) * 0x85ebca6bu ^ 0x5a17b3d9u);
                const glm::vec2 jitter(unitFloat(seed) * 2 - 1, unitFloat(seed ^ 0xa511e9b3u) * 2 - 1);
                const auto position = center + glm::vec2(x, z) * gridSpacing + jitter * gridJitter;
                if (glm::any(glm::lessThan(position, low)) || glm::any(glm::greaterThan(position, high))) continue;
                const auto offset = position - center;
                const float distanceSquared = glm::dot(offset, offset);
                if (distanceSquared < centerClearing * centerClearing || distanceSquared > radius * radius) continue;
                candidates.push_back({position, distanceSquared, seed});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.distanceSquared != b.distanceSquared) return a.distanceSquared < b.distanceSquared;
        if (a.position.x != b.position.x) return a.position.x < b.position.x;
        return a.position.y < b.position.y;
    });

    std::vector<glm::mat4> result;
    result.reserve(std::min(count, candidates.size()));
    for (const auto& candidate : candidates) {
        const auto transform = groundTransform(world, scene, candidate.position, candidate.seed);
        if (!transform) continue;
        result.push_back(*transform);
        if (result.size() == count) return result;
    }
    throw std::runtime_error("Cannot place " + std::to_string(count) + " zombies within " +
        std::to_string(radius) + " m: only " + std::to_string(result.size()) +
        " safe ground positions found; increase the crowd radius or reduce its count");
}

std::vector<glm::mat4> placeZombiesInRegion(const CollisionWorld& world, const Model& scene,
                                          const std::vector<glm::vec2>& boundary,
                                          std::size_t count) {
    if (count == 0) return {};
    if (boundary.size() < 3) throw std::invalid_argument("Zombie region needs at least three vertices");
    glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
    for (const auto p : boundary) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            throw std::invalid_argument("Zombie region must contain finite coordinates");
        low = glm::min(low, p);
        high = glm::max(high, p);
    }
    if (!finite(scene.boundsMin) || !finite(scene.boundsMax) ||
        glm::any(glm::greaterThan(scene.boundsMin, scene.boundsMax)) || world.stats().triangles == 0)
        throw std::runtime_error("Cannot place zombies: the scene has no valid ground bounds");
    low = glm::max(low, glm::vec2(scene.boundsMin.x, scene.boundsMin.z) + bodyRadius);
    high = glm::min(high, glm::vec2(scene.boundsMax.x, scene.boundsMax.z) - bodyRadius);
    const auto size = glm::floor((high - low) / gridSpacing) + 1.0f;
    if (!std::isfinite(size.x * size.y) || size.x > candidateLimit || size.y > candidateLimit ||
        (size.x > 0 && size.y > 0 && static_cast<double>(size.x) * size.y > candidateLimit))
        throw std::runtime_error("Cannot place zombies: search region exceeds one million grid candidates");
    std::vector<glm::mat4> safe;
    for (int z = 0; z < size.y; ++z) {
        for (int x = 0; x < size.x; ++x) {
            const auto seed = mix(static_cast<std::uint32_t>(x) * 0x9e3779b9u ^
                                  static_cast<std::uint32_t>(z) * 0x85ebca6bu ^ 0x5a17b3d9u);
            const auto p = low + glm::vec2(x, z) * gridSpacing;
            if (!insideRegion(p, boundary)) continue;
            const auto transform = groundTransform(world, scene, p, seed);
            if (transform) safe.push_back(*transform);
        }
    }
    if (safe.size() < count)
        throw std::runtime_error("Cannot place " + std::to_string(count) +
            " zombies in park: only " + std::to_string(safe.size()) + " safe ground positions found");
    // Each next point fills the largest remaining gap across the entire region.
    // Update cached distances in O(candidates * count), with stable tie breaks.
    std::vector<float> nearest(safe.size(), std::numeric_limits<float>::max());
    std::vector<glm::mat4> result;
    result.reserve(count);
    std::size_t selected = safe.size() / 2;
    for (std::size_t placed = 0; placed < count; ++placed) {
        result.push_back(safe[selected]);
        const glm::vec2 p(safe[selected][3].x, safe[selected][3].z);
        float farthest = -1;
        std::size_t next = 0;
        for (std::size_t i = 0; i < safe.size(); ++i) {
            const auto delta = glm::vec2(safe[i][3].x, safe[i][3].z) - p;
            nearest[i] = std::min(nearest[i], glm::dot(delta, delta));
            if (nearest[i] > farthest) { farthest = nearest[i]; next = i; }
        }
        selected = next;
    }
    return result;
}

}  // namespace viewer
