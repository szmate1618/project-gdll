#include "zombie_scenario.hpp"
#include "scenario_coordinates.hpp"
#include "scenario_density.hpp"
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

glm::vec2 sampleDensity(const ScenarioDensity& density, Random& random) {
    // Keep RNG evaluation order explicit for repeatability across compilers.
    const double selection = random.unit(), x = random.unit(), z = random.unit();
    return density.sample(selection, x, z);
}

// Try the intended target, then increasingly distant rings. This is a bounded
// discrete search, not an exact geometric nearest-point query. Every probe uses
// the same constraints and consumes the existing endpoint attempt budgets.
template<class ValidPosition>
std::optional<glm::vec3> nearbyPosition(glm::vec2 target, float searchRadius, Random& random,
    std::size_t& attempts, std::size_t& totalAttempts, const ValidPosition& valid) {
    const auto available = [&] { return attempts < perZombieAttempts && totalAttempts < totalAttemptLimit; };
    const auto probe = [&](glm::vec2 point) {
        ++attempts;
        ++totalAttempts;
        return valid(point);
    };
    if (!available()) return std::nullopt;
    if (auto feet = probe(target)) return feet;
    if (searchRadius == 0 || !available()) return std::nullopt;
    constexpr double tau = 6.283185307179586;
    double phase = random.unit() * tau;
    for (double distance = std::min(.25, double(searchRadius)); available();) {
        const int probes = int(std::clamp(std::ceil(tau * distance / .5), 8.0, 64.0));
        for (int i = 0; i < probes && available(); ++i) {
            const double angle = phase + tau * i / probes;
            const glm::vec2 point(glm::dvec2(target) + distance * glm::dvec2(std::cos(angle), std::sin(angle)));
            // Float rounding must not extend the authored maximum adjustment.
            if (glm::distance(glm::dvec2(point), glm::dvec2(target)) > searchRadius) continue;
            if (auto feet = probe(point)) return feet;
        }
        if (distance == searchRadius) break;
        distance = std::min(double(searchRadius), distance * 1.5);
        phase += 2.399963229728653; // Rotate rings to avoid aligned radial gaps.
    }
    return std::nullopt;
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
                                               const navmesh_path::Mesh* mesh,
                                               const ZombieScenarioConfig& config) {
    const auto count = config.count();
    if (!finite(scene.boundsMin) || !finite(scene.boundsMax) ||
        glm::any(glm::greaterThan(scene.boundsMin, scene.boundsMax)) || !world.stats().triangles)
        throw std::runtime_error("Cannot load zombie scenario: scene has no valid ground bounds");
    const bool needsNavigation = config.needsNavigation();
    if (needsNavigation && (!mesh || mesh->polygons().empty()))
        throw std::runtime_error("Cannot load zombie scenario: travel hordes require a nonempty navmesh");
    Random random(config.seed);
    std::optional<NavmeshSampler> sampler;
    if (needsNavigation) sampler.emplace(*mesh);
    SpawnOccupancy occupancy;
    ZombieScenarioPopulation result;
    result.placements.reserve(count);
    result.destinations.reserve(count);
    std::size_t spawnAttempts = 0, destinationAttempts = 0;
    const ZombieHordeConfig::Rectangle mapBounds{scene.boundsMin.x, scene.boundsMin.z,
                                                scene.boundsMax.x, scene.boundsMax.z};
    for (const auto& horde : config.hordes) {
        const bool travel = horde.movement == ZombieScenarioMovement::travel;
        const auto spawnBounds = horde.spawn.value_or(mapBounds);
        const auto destinationBounds = horde.destination.value_or(mapBounds);
        const ScenarioCoordinates spawnCoordinates(spawnBounds), destinationCoordinates(destinationBounds);
        std::optional<ScenarioDensity> spawnDensity, destinationDensity;
        try {
            if (!horde.spawnDensity.empty()) spawnDensity.emplace(horde.spawnDensity, spawnBounds);
            if (!horde.destinationDensity.empty()) destinationDensity.emplace(horde.destinationDensity, destinationBounds);
        } catch (const std::exception& error) {
            throw std::runtime_error("Zombie horde '" + horde.name + "': " + error.what());
        }
        const auto densityDescription = [](const std::filesystem::path& path) {
            return path.empty() ? std::string{} : " using density image '" + path.u8string() + "'";
        };
        for (std::size_t id = 0; id < horde.count; ++id) {
            std::optional<glm::vec3> spawn;
            std::uint32_t spawnPolygon = navmesh_path::noPolygon;
            std::size_t attempts = 0;
            while (attempts < perZombieAttempts && spawnAttempts < totalAttemptLimit) {
                const auto target = spawnDensity ? sampleDensity(*spawnDensity, random) :
                    (horde.spawn || !travel ? sampleRectangle(spawnCoordinates, random) : sampler->sample(random));
                spawn = nearbyPosition(target, horde.placementSearchRadius, random, attempts, spawnAttempts,
                    [&](glm::vec2 p) -> std::optional<glm::vec3> {
                        if (!spawnCoordinates.contains(p) || (spawnDensity && !spawnDensity->allows(p)) ||
                            !occupancy.clear(p)) return std::nullopt;
                        if (travel && mesh->locate({p.x, p.y}) == navmesh_path::noPolygon) return std::nullopt;
                        return safeFeet(p, world, scene);
                    });
                if (spawn) break;
            }
            if (!spawn)
                throw std::runtime_error("Cannot place zombie " + std::to_string(id + 1) + " of " +
                    std::to_string(horde.count) + " in horde '" + horde.name +
                    "': sampling/nearby search exhausted; spawn region has insufficient safe nonoverlapping " +
                    (travel ? "navmesh ground" : "ground") + densityDescription(horde.spawnDensity));
            std::optional<glm::vec3> destination;
            if (travel) {
                spawnPolygon = mesh->locate({spawn->x, spawn->z});
                const auto component = mesh->component(spawnPolygon);
                attempts = 0;
                while (attempts < perZombieAttempts && destinationAttempts < totalAttemptLimit) {
                    const auto target = destinationDensity ? sampleDensity(*destinationDensity, random) :
                        (horde.destination ? sampleRectangle(destinationCoordinates, random) : sampler->sample(component, random));
                    destination = nearbyPosition(target, horde.placementSearchRadius, random, attempts, destinationAttempts,
                        [&](glm::vec2 p) -> std::optional<glm::vec3> {
                            if (!destinationCoordinates.contains(p) || (destinationDensity && !destinationDensity->allows(p)) ||
                                glm::length(p - glm::vec2(spawn->x, spawn->z)) <= 1e-4f) return std::nullopt;
                            const auto polygon = mesh->locate({p.x, p.y});
                            if (polygon == navmesh_path::noPolygon || mesh->component(polygon) != component) return std::nullopt;
                            return safeFeet(p, world, scene);
                        });
                    if (destination) break;
                }
                if (!destination)
                    throw std::runtime_error("Cannot pick a safe reachable destination in the destination region for zombie " +
                        std::to_string(id + 1) + " in horde '" + horde.name +
                        "': sampling/nearby search exhausted" + densityDescription(horde.destinationDensity));
            }
            occupancy.add({spawn->x, spawn->z});
            result.placements.push_back(glm::rotate(glm::translate(glm::mat4(1), *spawn),
                float(random.unit() * 6.283185307179586), glm::vec3(0, 1, 0)));
            result.destinations.push_back(destination);
        }
    }
    return result;
}

ZombieScenarioPopulation generateZombieScenario(const CollisionWorld& world, const Model& scene,
                                               const navmesh_path::Mesh& mesh,
                                               const ZombieScenarioConfig& config) {
    return generateZombieScenario(world, scene, &mesh, config);
}

ZombieLayer loadZombieScenario(const std::filesystem::path& source,
                              const CollisionWorld& world, const Model& scene,
                              const std::filesystem::path& navmeshPath,
                              const ZombieScenarioConfig& config) {
    std::unique_ptr<ZombieNavigation> navigation;
    if (config.needsNavigation()) navigation = std::make_unique<ZombieNavigation>(navmeshPath, config.count());
    const auto population = generateZombieScenario(world, scene, navigation ? &navigation->mesh() : nullptr, config);
    auto layer = loadZombieLayer(source, population.placements);
    if (navigation) {
        layer.enableNavigation(std::move(navigation));
        layer.setScenarioRoutes(population.destinations);
    }
    return layer;
}
} // namespace viewer
