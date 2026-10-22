#include "zombie_scenario.hpp"
#include "scenario_coordinates.hpp"
#include "zombie_navigation.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

namespace viewer {
namespace {
constexpr float radius = ZombieCrowd::bodyRadius, height = ZombieCrowd::bodyHeight;
constexpr float spawnSpacing = radius * 2 + .002f;
constexpr std::size_t perZombieAttempts = 4096, totalAttemptLimit = 1'000'000;

class Random {
public:
    explicit Random(std::optional<std::uint32_t> seed)
        : engine_(seed ? *seed : std::random_device{}()) {}
    // Explicit conversion keeps seeded samples independent of the standard
    // library's distribution implementation, including zero but excluding one.
    double unit() { return double(engine_()) / 4294967296.; }
private:
    std::mt19937 engine_;
};

glm::vec2 sampleRectangle(const ScenarioCoordinates& coordinates, Random& random) {
    const double x = random.unit(), y = random.unit();
    return coordinates.sample(x, y);
}

class SpawnOccupancy {
public:
    bool clear(glm::vec2 p) const {
        const auto center = cell(p);
        for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z) {
            const auto found = cells_.find({center.first + x, center.second + z});
            if (found == cells_.end()) continue;
            for (const auto other : found->second)
                if (glm::length(p - other) < spawnSpacing) return false;
        }
        return true;
    }
    void add(glm::vec2 p) { cells_[cell(p)].push_back(p); }
private:
    using Cell = std::pair<double, double>;
    static Cell cell(glm::vec2 p) {
        return {std::floor(double(p.x) / spawnSpacing), std::floor(double(p.y) / spawnSpacing)};
    }
    std::map<Cell, std::vector<glm::vec2>> cells_;
};

std::optional<glm::vec3> safeFeet(glm::vec2 p, const CollisionWorld& world, const Model& scene) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return std::nullopt;
    const auto ground = world.groundAt(p.x, p.y, scene.boundsMin.y - height - 1,
        scene.boundsMax.y + height + 1, world.stats().terrainTriangles != 0);
    if (!ground || ground->normal.y < std::cos(glm::radians(25.f))) return std::nullopt;
    const glm::vec3 feet(p.x, ground->height, p.y);
    // Preserve rendered-triangle feet. Only the collision probe is raised for
    // rounded-capsule tangency on slopes, as in ordinary zombie placement.
    const auto probe = feet + glm::vec3(0, radius * (1 / ground->normal.y - 1) + .01f, 0);
    if (world.insideBuilding(probe, radius, height)) return std::nullopt;
    for (const auto& contact : world.contacts(probe, radius, height, .002f, .05f))
        if (contact.depth > .005f) return std::nullopt;
    return feet;
}

class NavmeshSampler {
public:
    explicit NavmeshSampler(const navmesh_path::Mesh& mesh) : components_(mesh.componentCount()) {
        for (std::uint32_t id = 0; id < mesh.polygons().size(); ++id) {
            const auto& polygon = mesh.polygons()[id];
            auto& component = components_[mesh.component(id)];
            const auto a = mesh.vertices()[mesh.edges()[polygon.firstEdge].vertex];
            for (std::uint32_t corner = 1; corner + 1 < polygon.edgeCount; ++corner) {
                const auto b = mesh.vertices()[mesh.edges()[polygon.firstEdge + corner].vertex];
                const auto c = mesh.vertices()[mesh.edges()[polygon.firstEdge + corner + 1].vertex];
                const double area = std::abs((b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x)) * .5;
                if (area <= 0) continue;
                component.area += area;
                component.triangles.push_back({a, b, c, component.area});
            }
        }
        for (const auto& component : components_) {
            totalArea_ += component.area;
            cumulativeAreas_.push_back(totalArea_);
        }
        if (!(totalArea_ > 0) || !std::isfinite(totalArea_))
            throw std::runtime_error("Cannot sample scenario endpoints: navmesh has no finite area");
    }
    glm::vec2 sample(Random& random) const {
        const auto found = std::upper_bound(cumulativeAreas_.begin(), cumulativeAreas_.end(),
                                           random.unit() * totalArea_);
        return sample(static_cast<std::uint32_t>(found - cumulativeAreas_.begin()), random);
    }
    glm::vec2 sample(std::uint32_t componentId, Random& random) const {
        const auto& component = components_.at(componentId);
        if (component.triangles.empty() || !std::isfinite(component.area))
            throw std::runtime_error("Cannot sample a scenario destination: navmesh component has no finite area");
        const double area = random.unit() * component.area;
        auto found = std::lower_bound(component.triangles.begin(), component.triangles.end(), area,
            [](const Triangle& triangle, double value) { return triangle.cumulativeArea < value; });
        if (found == component.triangles.end()) found = component.triangles.end() - 1;
        const double along = std::sqrt(random.unit()), across = random.unit();
        return {float(found->a.x * (1 - along) + found->b.x * along * (1 - across) + found->c.x * along * across),
                float(found->a.z * (1 - along) + found->b.z * along * (1 - across) + found->c.z * along * across)};
    }
private:
    struct Triangle { navmesh_path::Point a, b, c; double cumulativeArea; };
    struct Component { std::vector<Triangle> triangles; double area = 0; };
    std::vector<Component> components_;
    std::vector<double> cumulativeAreas_;
    double totalArea_ = 0;
};

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

ZombieScenarioPopulation generateZombieScenario(const CollisionWorld& world, const Model& scene,
                                               const navmesh_path::Mesh& mesh,
                                               const ZombieScenarioConfig& config) {
    const auto count = config.count();
    if (!finite(scene.boundsMin) || !finite(scene.boundsMax) ||
        glm::any(glm::greaterThan(scene.boundsMin, scene.boundsMax)) || !world.stats().triangles)
        throw std::runtime_error("Cannot load zombie scenario: scene has no valid ground bounds");
    if (mesh.polygons().empty()) throw std::runtime_error("Cannot load zombie scenario: navmesh is empty");
    Random random(config.seed);
    const NavmeshSampler sampler(mesh);
    SpawnOccupancy occupancy;
    ZombieScenarioPopulation result;
    result.placements.reserve(count);
    result.destinations.reserve(count);
    std::size_t spawnAttempts = 0, destinationAttempts = 0;
    for (const auto& horde : config.hordes) {
        std::optional<ScenarioCoordinates> spawnCoordinates, destinationCoordinates;
        if (horde.spawn) spawnCoordinates.emplace(horde.coordinateSystem, *horde.spawn, scene);
        if (horde.destination) destinationCoordinates.emplace(horde.coordinateSystem, *horde.destination, scene);
        for (std::size_t id = 0; id < horde.count; ++id) {
            std::optional<glm::vec3> spawn;
            std::uint32_t spawnPolygon = navmesh_path::noPolygon;
            for (std::size_t attempt = 0; attempt < perZombieAttempts && spawnAttempts < totalAttemptLimit; ++attempt) {
                ++spawnAttempts;
                const auto p = spawnCoordinates ? sampleRectangle(*spawnCoordinates, random) : sampler.sample(random);
                spawnPolygon = mesh.locate({p.x, p.y});
                if (spawnPolygon == navmesh_path::noPolygon ||
                    (spawnCoordinates && !spawnCoordinates->contains(p)) || !occupancy.clear(p)) continue;
                spawn = safeFeet(p, world, scene);
                if (spawn) break;
            }
            if (!spawn)
                throw std::runtime_error("Cannot place zombie " + std::to_string(id + 1) + " of " +
                    std::to_string(horde.count) + " in horde '" + horde.name +
                    "': spawn region has insufficient safe nonoverlapping navmesh ground");
            std::optional<glm::vec3> destination;
            const auto component = mesh.component(spawnPolygon);
            for (std::size_t attempt = 0; attempt < perZombieAttempts && destinationAttempts < totalAttemptLimit; ++attempt) {
                ++destinationAttempts;
                const auto p = destinationCoordinates ? sampleRectangle(*destinationCoordinates, random)
                                                     : sampler.sample(component, random);
                const auto polygon = mesh.locate({p.x, p.y});
                if (polygon == navmesh_path::noPolygon || mesh.component(polygon) != component ||
                    (destinationCoordinates && !destinationCoordinates->contains(p)) ||
                    glm::length(p - glm::vec2(spawn->x, spawn->z)) <= 1e-4f) continue;
                destination = safeFeet(p, world, scene);
                if (destination) break;
            }
            if (!destination)
                throw std::runtime_error("Cannot pick a safe reachable destination in the destination region for zombie " +
                    std::to_string(id + 1) + " in horde '" + horde.name + "'");
            occupancy.add({spawn->x, spawn->z});
            result.placements.push_back(glm::rotate(glm::translate(glm::mat4(1), *spawn),
                float(random.unit() * 6.283185307179586), glm::vec3(0, 1, 0)));
            result.destinations.push_back(*destination);
        }
    }
    return result;
}

ZombieLayer loadZombieScenario(const std::filesystem::path& source,
                              const CollisionWorld& world, const Model& scene,
                              const std::filesystem::path& navmeshPath,
                              const ZombieScenarioConfig& config) {
    auto navigation = std::make_unique<ZombieNavigation>(navmeshPath, config.count());
    const auto population = generateZombieScenario(world, scene, navigation->mesh(), config);
    auto layer = loadZombieLayer(source, population.placements);
    layer.enableNavigation(std::move(navigation));
    layer.setScenarioDestinations(population.destinations);
    return layer;
}
} // namespace viewer
