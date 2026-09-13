#include "zombie_placement.hpp"

#include "collision_world.hpp"
#include "model.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    const bool terrainOnly = world.stats().terrainTriangles != 0;
    const float minimumY = scene.boundsMin.y - bodyHeight - 1;
    const float maximumY = scene.boundsMax.y + bodyHeight + 1;
    const float slopeCosine = std::cos(glm::radians(25.0f));
    for (const auto& candidate : candidates) {
        const auto p = candidate.position;
        const auto ground = world.groundAt(p.x, p.y, minimumY, maximumY, terrainOnly);
        if (!ground || ground->normal.y < slopeCosine) continue;
        const glm::vec3 feet(p.x, ground->height, p.y);
        // The collision capsule needs a tangency lift on slopes. The rendered
        // model's feet remain on the actual triangle, without that extra lift.
        const auto probe = feet + glm::vec3(0, bodyRadius * (1 / ground->normal.y - 1) + 0.01f, 0);
        if (world.insideBuilding(probe, bodyRadius, bodyHeight)) continue;
        const auto contacts = world.contacts(probe, bodyRadius, bodyHeight);
        if (std::any_of(contacts.begin(), contacts.end(), [](const Contact& contact) {
                return contact.depth > 0.005f;
            })) continue;
        const auto transform = glm::rotate(glm::translate(glm::mat4(1), feet),
            unitFloat(candidate.seed ^ 0x68bc21ebu) * 6.28318530718f, glm::vec3(0, 1, 0));
        result.push_back(transform);
        if (result.size() == count) return result;
    }
    throw std::runtime_error("Cannot place " + std::to_string(count) + " zombies within " +
        std::to_string(radius) + " m: only " + std::to_string(result.size()) +
        " safe ground positions found; increase the crowd radius or reduce its count");
}

}  // namespace viewer
