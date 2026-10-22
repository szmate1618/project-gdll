#include "zombie_scenario.hpp"
#include "zombie_navigation.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

viewer::Model terrain() {
    viewer::Model model;
    viewer::Primitive primitive;
    for (const auto p : {glm::vec3(-100,5,-100), glm::vec3(-100,5,100),
                         glm::vec3(100,5,100), glm::vec3(100,5,-100)}) {
        viewer::Vertex vertex;
        vertex.position = p;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0,1,2,0,2,3};
    model.primitives.push_back(std::move(primitive));
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    model.boundsMin = {-100,5,-100};
    model.boundsMax = {100,5,100};
    return model;
}

viewer::navmesh_path::Mesh mesh() {
    using namespace viewer::navmesh_path;
    return Mesh({{-90,-90},{90,-90},{90,90},{-90,90}},
                std::vector<Triangle>{{{0,1,2},{noTriangle,noTriangle,1}},
                                      {{0,2,3},{0,noTriangle,noTriangle}}});
}

viewer::ZombieScenarioConfig config(std::size_t count = 1000) {
    viewer::ZombieScenarioConfig result;
    result.hordes.emplace_back();
    result.hordes.front().name = "test_horde";
    result.hordes.front().count = count;
    result.seed = 42;
    result.hordes.front().spawn = {{-60,-60,60,60}};
    return result;
}

struct Files {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("zombie-scenario-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() { std::filesystem::create_directories(directory); }
    ~Files() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }

    static void integer(std::vector<unsigned char>& bytes, std::size_t offset, std::uint64_t value, unsigned size) {
        for (unsigned i = 0; i < size; ++i) bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i));
    }
    static void real(std::vector<unsigned char>& bytes, std::size_t offset, double value) {
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        integer(bytes, offset, bits, 8);
    }
    std::filesystem::path navmesh() const {
        using namespace viewer::navmesh_path;
        std::vector<unsigned char> bytes(128 + 64 + 48);
        std::memcpy(bytes.data(), "GDNAV01\0", 8);
        integer(bytes, 8, 4, 4); integer(bytes, 12, 2, 4);
        real(bytes, 16, -90); real(bytes, 24, -90); real(bytes, 32, 90); real(bytes, 40, 90);
        real(bytes, 48, .3); real(bytes, 56, .002); real(bytes, 64, .05); real(bytes, 72, .005);
        integer(bytes, 80, 64, 8); integer(bytes, 88, 48, 8);
        const double coordinates[]{-90,-90,90,-90,90,90,-90,90};
        for (unsigned i = 0; i < 8; ++i) real(bytes, 128 + 8 * i, coordinates[i]);
        const std::uint32_t records[]{0,1,2,noTriangle,noTriangle,1,0,2,3,0,noTriangle,noTriangle};
        for (unsigned i = 0; i < 12; ++i) integer(bytes, 192 + 4 * i, records[i], 4);
        const auto path = directory / "map.navmesh";
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        require(file.good(), "Cannot write scenario navmesh fixture");
        return path;
    }
    std::filesystem::path asset() const {
        // One animated triangle supplies a real loader input without external assets.
        const float values[]{-.2f,0,0, .2f,0,0, 0,1.8f,0, 0,1, 0,0,0, 0,0,0};
        std::vector<unsigned char> bytes(sizeof(values));
        for (std::size_t i = 0; i < sizeof(values) / sizeof(float); ++i) {
            std::uint32_t bits;
            std::memcpy(&bits, &values[i], 4);
            integer(bytes, 4 * i, bits, 4);
        }
        std::ofstream binary(directory / "character.bin", std::ios::binary);
        binary.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        binary.close();
        const auto path = directory / "character.gltf";
        std::ofstream file(path);
        file << R"({
  "asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
  "nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
  "buffers":[{"uri":"character.bin","byteLength":68}],
  "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                 {"buffer":0,"byteOffset":36,"byteLength":8},
                 {"buffer":0,"byteOffset":44,"byteLength":24}],
  "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
               {"bufferView":1,"componentType":5126,"count":2,"type":"SCALAR"},
               {"bufferView":2,"componentType":5126,"count":2,"type":"VEC3"}],
  "animations":[{"name":"Idle","samplers":[{"input":1,"output":2}],
                 "channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]
})";
        require(binary.good() && file.good(), "Cannot write scenario character fixture");
        return path;
    }
};

void randomPopulationAndColdLoad() {
    const auto scene = terrain();
    const viewer::CollisionWorld world(scene, {{{0,0,0}, 4, 15}});
    const auto navmesh = mesh();
    auto settings = config();
    const auto first = viewer::generateZombieScenario(world, scene, navmesh, settings);
    const auto repeated = viewer::generateZombieScenario(world, scene, navmesh, settings);
    require(first.placements.size() == 1000 && first.destinations.size() == 1000, "Scenario silently reduced its population");
    std::size_t outsideSpawn = 0;
    for (std::size_t i = 0; i < first.placements.size(); ++i) {
        require(first.placements[i] == repeated.placements[i] && first.destinations[i] == repeated.destinations[i],
                "A seeded scenario is not repeatable");
        const glm::vec3 p(first.placements[i][3]), target(first.destinations[i]);
        require(p.y == 5 && target.y == 5, "Scenario endpoints do not follow the rendered ground");
        require(std::abs(p.x) <= 60 && std::abs(p.z) <= 60, "Spawn escaped its rectangle");
        require(glm::length(glm::vec2(p.x,p.z)) > 4.29f && glm::length(glm::vec2(target.x,target.z)) > 4.29f,
                "Scenario endpoint overlaps a tree trunk");
        require(navmesh.locate({target.x,target.z}) != viewer::navmesh_path::noPolygon,
                "Scenario target is outside the navmesh");
        if (std::abs(target.x) > 60 || std::abs(target.z) > 60) ++outsideSpawn;
        for (std::size_t j = 0; j < i; ++j)
            require(glm::length(glm::vec2(p.x - first.placements[j][3].x, p.z - first.placements[j][3].z)) >= .6f,
                    "Random spawn bodies overlap");
    }
    require(outsideSpawn > 200, "Random destinations were confined to the spawn rectangle");
    settings.seed = 43;
    require(viewer::generateZombieScenario(world, scene, navmesh, settings).placements[0] != first.placements[0],
            "The scenario seed does not affect placement");
    settings.seed = 42;
    const Files files;
    auto layer = viewer::loadZombieScenario(files.asset(), world, scene, files.navmesh(), settings);
    require(layer.instances.size() == 1000 && layer.navigation(), "Cold scenario load did not create the population and navigation");
    require(layer.navigation()->stats().foundPaths == 1000 && layer.navigation()->stats().requests == 1000,
            "Scenario paths were deferred until the first simulation step");
    for (std::size_t id = 0; id < layer.instances.size(); ++id) {
        require(layer.navigation()->pathFor(id) != 0 && layer.scenarioDestination(id), "A loaded scenario zombie has no route");
        require(*layer.scenarioDestination(id) == first.destinations[id], "Cold loader changed seeded destination selection");
    }
}

void multipleHordesAndOptionalRegions() {
    const auto scene = terrain();
    const viewer::CollisionWorld world(scene);
    const auto navmesh = mesh();
    const Files files;
    const auto path = files.directory / "scenario.toml";
    std::ofstream(path) << R"(seed = 42
[zombies.crossing]
count = 80
spawn = [-80, -70, -40, -20]
destination = [40, 20, 80, 70]
[zombies.destination_only]
count = 80
destination = [-80, 20, -40, 70]
[zombies.mapwide]
count = 80
[zombies.spawn_only]
count = 80
spawn = [-80, -70, -40, -20]
)";
    const auto settings = viewer::loadZombieScenarioConfig(path);
    const auto population = viewer::generateZombieScenario(world, scene, navmesh, settings);
    require(population.placements.size() == 320 && population.destinations.size() == 320,
            "Named horde populations were not combined");
    std::size_t id = 0, unrestrictedSpawns = 0, unrestrictedDestinations = 0;
    for (const auto& horde : settings.hordes) {
        for (std::size_t member = 0; member < horde.count; ++member, ++id) {
            const auto p = glm::vec3(population.placements[id][3]);
            const auto goal = population.destinations[id];
            if (horde.spawn)
                require(p.x >= -80 && p.x <= -40 && p.z >= -70 && p.z <= -20,
                        "Horde spawn escaped its asymmetric world X/Z rectangle");
            else if (p.x > 0) ++unrestrictedSpawns;
            if (horde.name == "crossing")
                require(goal.x >= 40 && goal.x <= 80 && goal.z >= 20 && goal.z <= 70,
                        "Horde destination escaped its independent rectangle");
            else if (horde.name == "destination_only")
                require(goal.x >= -80 && goal.x <= -40 && goal.z >= 20 && goal.z <= 70,
                        "Destination-only bounds were ignored");
            else if (goal.x > 0) ++unrestrictedDestinations;
            for (std::size_t other = 0; other < id; ++other) {
                const auto q = glm::vec3(population.placements[other][3]);
                require(glm::length(glm::vec2(p.x - q.x, p.z - q.z)) >= .6f,
                        "Bodies from separate hordes overlap");
            }
        }
    }
    require(unrestrictedSpawns > 20 && unrestrictedDestinations > 20,
            "Omitted rectangles did not allow positions across the map");
    const auto layer = viewer::loadZombieScenario(files.asset(), world, scene, files.navmesh(), settings);
    require(layer.instances.size() == 320 && layer.navigation()->stats().foundPaths == 320,
            "Cold loader did not preplan every named horde");
    for (std::size_t member = 0; member < 320; ++member)
        require(*layer.scenarioDestination(member) == population.destinations[member],
                "Cold loading changed a named horde's seeded destinations");
}

void geographicRegions() {
    auto scene = terrain();
    // Independent pyproj reference places 19.35E, 47.60N at the runtime origin.
    scene.geographicFrame = viewer::Model::GeographicFrame{"EPSG:32634", {375970.52835, 5273162.42161}};
    const viewer::CollisionWorld world(scene);
    auto settings = config(40);
    auto& horde = settings.hordes.front();
    horde.coordinateSystem = viewer::ZombieHordeConfig::CoordinateSystem::Wgs84;
    horde.spawn = {{19.3492,47.5994,19.3496,47.5998}};
    horde.destination = {{19.3504,47.6002,19.3508,47.6006}};
    const auto population = viewer::generateZombieScenario(world, scene, mesh(), settings);
    for (std::size_t id = 0; id < horde.count; ++id) {
        const auto p = glm::vec3(population.placements[id][3]);
        const auto goal = population.destinations[id];
        require(p.x < -20 && p.z > 20 && goal.x > 20 && goal.z < -20,
                "WGS84 endpoint rectangles lost longitude/latitude axes or north-to-Z inversion");
    }
}

void disconnectedDestinationsAndBoundedFailure() {
    using namespace viewer::navmesh_path;
    const auto scene = terrain();
    const viewer::CollisionWorld world(scene);
    for (const auto count : {std::size_t(0), viewer::zombieScenarioMaximumCount + 1}) {
        try { (void)viewer::generateZombieScenario(world, scene, mesh(), config(count)); }
        catch (const std::invalid_argument&) { continue; }
        throw std::runtime_error("An unbounded direct scenario population was accepted");
    }
    Mesh islands({{-90,-90},{-2,-90},{-2,90},{-90,90},{2,-90},{90,-90},{90,90},{2,90}},
                 {{0,4},{4,4}}, {{0,noPolygon},{1,noPolygon},{2,noPolygon},{3,noPolygon},
                                 {4,noPolygon},{5,noPolygon},{6,noPolygon},{7,noPolygon}});
    auto settings = config(30);
    settings.hordes.front().spawn = viewer::ZombieHordeConfig::Rectangle{-60,-60,-20,60};
    const auto population = viewer::generateZombieScenario(world, scene, islands, settings);
    for (const auto target : population.destinations)
        require(target.x <= -2, "A scenario zombie received a destination on another island");
    settings = config(100);
    settings.hordes.front().spawn.reset();
    const auto mapwide = viewer::generateZombieScenario(world, scene, islands, settings);
    std::size_t left = 0, right = 0;
    for (std::size_t id = 0; id < mapwide.placements.size(); ++id) {
        const auto p = glm::vec3(mapwide.placements[id][3]);
        const auto goal = mapwide.destinations[id];
        const auto startPolygon = islands.locate({p.x, p.z});
        const auto goalPolygon = islands.locate({goal.x, goal.z});
        require(startPolygon != noPolygon && goalPolygon != noPolygon &&
                    islands.component(startPolygon) == islands.component(goalPolygon),
                "Mapwide spawning assigned a disconnected destination");
        if (p.x < 0) ++left; else ++right;
    }
    require(left > 20 && right > 20, "Unbounded spawning did not cover all navmesh components");
    settings = config(1);
    settings.hordes.front().spawn = viewer::ZombieHordeConfig::Rectangle{-1,-1,1,1};
    const viewer::CollisionWorld blocked(scene, {{{0,0,0}, 4, 15}});
    const auto fails = [&](const viewer::CollisionWorld& collision, const viewer::ZombieScenarioConfig& input) {
        try { (void)viewer::generateZombieScenario(collision, scene, islands, input); }
        catch (const std::runtime_error& error) {
            require(std::string(error.what()).find("spawn region") != std::string::npos,
                    "Impossible rectangle did not report a useful bounded-placement error");
            return;
        }
        throw std::runtime_error("An impossible scenario rectangle was accepted");
    };
    fails(blocked, settings);
    settings.hordes.front().count = 2;
    settings.hordes.front().spawn = viewer::ZombieHordeConfig::Rectangle{-20.01,-.01,-19.99,.01};
    fails(world, settings); // One safe point fits, but two bodies cannot fit.
    settings = config(1);
    settings.hordes.front().spawn = {{-60,-60,-20,60}};
    const auto destinationFails = [&](const viewer::CollisionWorld& collision) {
        try { (void)viewer::generateZombieScenario(collision, scene, islands, settings); }
        catch (const std::runtime_error& error) {
            const std::string message(error.what());
            require(message.find("destination region") != std::string::npos &&
                        message.find("test_horde") != std::string::npos,
                    "Impossible destinations must identify the failing region and horde");
            return;
        }
        throw std::runtime_error("An impossible destination rectangle was accepted");
    };
    settings.hordes.front().destination = {{20,-60,60,60}};
    destinationFails(world); // Safe ground, but a disconnected island.
    settings.hordes.front().destination = {{-50,-5,-40,5}};
    const viewer::CollisionWorld blockedTarget(scene, {{{-45,0,0}, 15, 40}});
    destinationFails(blockedTarget); // Connected ground entirely blocked by a trunk.
    settings.hordes.front().destination = {{200,200,300,300}};
    destinationFails(world); // Entirely outside the map.
    settings = config(60000);
    settings.hordes.push_back(settings.hordes.front());
    try { (void)viewer::generateZombieScenario(world, scene, islands, settings); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("A direct multi-horde population exceeded the aggregate count limit");
}

viewer::ZombieLayer scripted(const std::vector<glm::vec3>& starts, const std::vector<glm::vec3>& goals) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    for (const auto start : starts) {
        viewer::ZombieInstance instance;
        instance.transform = instance.restTransform = instance.restRoot = glm::translate(glm::mat4(1), start);
        layer.instances.push_back(instance);
    }
    layer.rebuildSpatialIndex();
    layer.enableNavigation(std::make_unique<viewer::ZombieNavigation>(mesh(), starts.size()));
    layer.setScenarioDestinations(goals);
    return layer;
}

void autonomousMovementAndStop() {
    const viewer::CollisionWorld world(terrain());
    auto layer = scripted({{-40,5,-20}}, {{40,5,-20}});
    const auto route = layer.navigation()->pathFor(0);
    require(route != 0, "Scripted movement was not preplanned");
    const auto revision = layer.renderUpdates().since(0).revision;
    layer.updateBehavior(.1f, viewer::ZombiePlayer{{10000,5,10000}}, world);
    require(layer.instances[0].restRoot[3].x > -40 && layer.behaviors.entries().empty(),
            "A distant scenario zombie was suspended by player perception");
    for (int step = 0; step < 10; ++step) {
        const auto feet = glm::vec3(layer.instances[0].restRoot[3]);
        const auto behind = feet - glm::vec3(layer.instances[0].restRoot[2]) * 10.f;
        layer.updateBehavior(.1f, viewer::ZombiePlayer{behind}, world);
        require(std::abs(glm::distance(feet, glm::vec3(layer.instances[0].restRoot[3])) - .12f) < .0001f &&
                layer.instances[0].walking && layer.navigation()->pathFor(0) == route,
                "Idle perception reset a preplanned route or moved it twice in one step");
    }
    viewer::SimulationClock::instance().advance(11);
    const auto beforeExpiration = glm::vec3(layer.instances[0].restRoot[3]);
    layer.updateBehavior(.1f, viewer::ZombiePlayer{beforeExpiration - glm::vec3(layer.instances[0].restRoot[2]) * 10.f}, world);
    require(layer.navigation()->pathFor(0) == route && layer.instances[0].restRoot[3].x > beforeExpiration.x,
            "Expiring an idle behavior stopped initial scenario travel");
    require(layer.renderUpdates().since(revision).revision > revision, "Scenario movement did not publish render changes");
    for (int step = 0; step < 1000 && !layer.scenarioFinished(0); ++step)
        layer.updateBehaviorForPlayers(.1f, {}, world, 0);
    const auto final = glm::vec3(layer.instances[0].restRoot[3]);
    require(layer.scenarioFinished(0) && glm::length(final - glm::vec3(40,5,-20)) <= 1e-4f && !layer.instances[0].walking,
            "No-player scenario movement failed to arrive and stop");
    require(layer.navigation()->stats().requests == 1 && !layer.navigation()->pathFor(0),
            "A fixed scenario route was needlessly replanned or leaked on arrival");
    std::vector<std::size_t> ids;
    layer.queryLivingNear(final, .1f, ids);
    require(ids == std::vector<std::size_t>{0}, "Scenario locomotion did not update the spatial index");
    layer.queryLivingNear({-40,5,-20}, .1f, ids);
    require(ids.empty(), "Scenario movement left stale spatial membership");
    layer.setPerceptionStaggering(false);
    layer.updateBehavior(.1f, viewer::ZombiePlayer{{50,5,-20}}, world);
    require(layer.instances[0].restRoot[3].x > final.x && layer.behaviors.find(0) &&
            layer.behaviors.find(0)->state == viewer::ZombieState::chasing,
            "An arrived scenario zombie no longer reacts to normal player sight");
}

void gameplayOverridesInitialTravel() {
    const viewer::CollisionWorld world(terrain());
    auto layer = scripted({{-20,5,0}}, {{20,5,0}});
    const auto planned = layer.navigation()->pathFor(0);
    const auto before = glm::vec3(layer.instances[0].restRoot[3]);
    layer.updateBehavior(.1f, viewer::ZombiePlayer{{-20,5,10}}, world);
    const auto after = glm::vec3(layer.instances[0].restRoot[3]);
    require(!layer.scenarioDestination(0) && layer.behaviors.find(0) &&
            layer.behaviors.find(0)->state == viewer::ZombieState::chasing &&
            layer.behaviors.find(0)->target == glm::vec3(-20,5,10),
            "Normal player sight did not replace the initial scenario destination");
    require(layer.navigation()->pathFor(0) != planned && layer.navigation()->stats().requests == 2 &&
            std::abs(glm::distance(before, after) - .3f) < .0001f && after.z > .29f,
            "Gameplay takeover used the old route or advanced both routes in one step");

    auto investigate = scripted({{-20,5,0}}, {{20,5,0}});
    auto& behavior = investigate.behaviors.create(0, 0);
    behavior.state = viewer::ZombieState::investigating;
    behavior.target = {-20,5,10};
    investigate.updateBehavior(.1f, viewer::ZombiePlayer{{-20,5,-10}}, world);
    require(!investigate.scenarioDestination(0) && investigate.instances[0].restRoot[3].z > .11f &&
            std::abs(investigate.instances[0].restRoot[3].x + 20) < .0001f,
            "An investigation goal did not replace initial scenario travel");

    auto listener = scripted({{-20,5,0}}, {{20,5,0}});
    listener.updateBehavior(.1f, viewer::ZombiePlayer{{-20,5,-10}}, world);
    const auto feet = glm::vec3(listener.instances[0].restRoot[3]);
    const auto initial = listener.navigation()->pathFor(0);
    listener.makeNoise(feet + glm::vec3(0,0,10), 150);
    require(listener.behaviors.find(0)->state == viewer::ZombieState::looking,
            "An initially travelling zombie no longer reacts to noise");
    listener.updateBehaviorForPlayers(.1f, {}, world, 0);
    require(glm::vec3(listener.instances[0].restRoot[3]) == feet && listener.navigation()->pathFor(0) == initial,
            "A looking reaction lost the initial route or travelled while turning");
    for (int step = 0; step < 20; ++step) listener.updateBehaviorForPlayers(.1f, {}, world, 0);
    require(listener.scenarioDestination(0) && listener.instances[0].restRoot[3].x > feet.x &&
            listener.navigation()->pathFor(0) == initial && listener.navigation()->stats().requests == 1,
            "Initial travel did not resume after a look reaction without a gameplay destination");
}

void teleportDeathAndCrowd() {
    const viewer::CollisionWorld world(terrain());
    auto layer = scripted({{-20,5,0}}, {{20,5,0}});
    layer.updateBehaviorForPlayers(.1f, {}, world, 0);
    auto& instance = layer.instances[0];
    instance.transform = instance.restTransform = instance.restRoot = glm::translate(glm::mat4(1), glm::vec3(-40,5,0));
    layer.rebuildSpatialIndex();
    layer.updateBehaviorForPlayers(.1f, {}, world, 0);
    require(instance.restRoot[3].x > -40 && layer.navigation()->pathFor(0), "A teleported scenario zombie did not rebuild its route");
    layer.setRagdollPalette(0, {});
    const auto deadPosition = glm::vec3(instance.restRoot[3]);
    layer.updateBehaviorForPlayers(1, {}, world, 0);
    require(layer.scenarioFinished(0) && glm::vec3(instance.restRoot[3]) == deadPosition && !layer.navigation()->pathFor(0),
            "Scenario movement continued after ragdoll/death");
    auto crowd = scripted({{-4,5,0},{4,5,0}}, {{4,5,0},{-4,5,0}});
    for (int step = 0; step < 120; ++step) {
        crowd.updateBehaviorForPlayers(.1f, {}, world, 0);
        require(glm::length(glm::vec3(crowd.instances[0].restRoot[3] - crowd.instances[1].restRoot[3])) >= .5999f,
                "Scripted zombies bypassed crowd collision");
    }
    require(crowd.crowdStats().neighborChecks > 0, "Scripted movement did not consult the existing crowd guard");
}
}

int main() {
    try {
        randomPopulationAndColdLoad();
        multipleHordesAndOptionalRegions();
        geographicRegions();
        disconnectedDestinationsAndBoundedFailure();
        autonomousMovementAndStop();
        gameplayOverridesInitialTravel();
        teleportDeathAndCrowd();
        std::cout << "scenario seeded placement/cold routes/autonomous stop/collision tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
