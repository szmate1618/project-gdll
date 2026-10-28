#include "scenario_density.hpp"
#include "zombie_scenario.hpp"
#include "zombie_navmesh.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

template<class Function>
void rejects(Function&& function, const std::string& expected) {
    try { function(); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(expected) != std::string::npos, "Placement error lost its source/context");
        return;
    }
    throw std::runtime_error("Invalid density/placement was accepted");
}

struct Files {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("scenario-density-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() { std::filesystem::create_directories(directory); }
    ~Files() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    std::filesystem::path png(int width, int height, int channels, const std::vector<unsigned char>& pixels,
                              const char* name = "density.png") const {
        const auto path = directory / name;
        require(pixels.size() == std::size_t(width * height * channels), "Invalid PNG test fixture");
        require(stbi_write_png(path.u8string().c_str(), width, height, channels, pixels.data(), width * channels),
                "Cannot write density fixture");
        return path;
    }
};

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

viewer::ZombieScenarioConfig config(std::size_t count) {
    viewer::ZombieScenarioConfig result;
    result.seed = 42;
    result.hordes.emplace_back();
    result.hordes.front().name = "density_horde";
    result.hordes.front().count = count;
    return result;
}

void imageMappingAndWeights(const std::filesystem::path& sixteenBit, const std::filesystem::path& supplied) {
    const Files files;
    const auto path = files.png(2, 2, 1, {0,64,128,255});
    const viewer::ScenarioDensity density(path, {10,-50,210,10});
    require(density.sample(0, .5, .5) == glm::vec2(160,-35) &&
            density.sample(.2, .5, .5) == glm::vec2(60,-5) &&
            density.sample(.9, .5, .5) == glm::vec2(160,-5),
            "Pixel weights/orientation must stretch independently over asymmetric X/Z bounds");
    require(!density.allows({60,-35}) && density.allows({160,-35}) && density.allows({210,10}) &&
            !density.allows({211,10}) && !density.allows({10,-51}),
            "Black pixels and points outside the density rectangle must be excluded");
    int topRight = 0, bottomLeft = 0, bottomRight = 0;
    for (int i = 0; i < 10000; ++i) {
        const auto p = density.sample((i + .5) / 10000., .5, .5);
        require(density.allows(p), "A weighted sample landed in a black pixel");
        if (p.y < -20) ++topRight;
        else if (p.x < 110) ++bottomLeft;
        else ++bottomRight;
    }
    require(std::abs(topRight - 10000. * 64 / 447) < 2 &&
            std::abs(bottomLeft - 10000. * 128 / 447) < 2 &&
            std::abs(bottomRight - 10000. * 255 / 447) < 2,
            "Sampling frequency must follow raw grayscale weights without gamma correction");
    const viewer::ScenarioDensity precision(sixteenBit, {0,0,2,1});
    require(precision.allows({.5f,.5f}) && precision.sample(0,.5,.5) == glm::vec2(.5,.5) &&
            precision.sample(.5,.5,.5) == glm::vec2(1.5,.5),
            "16-bit positive weights must not be truncated to 8-bit black");
    const auto rgba = files.png(1,1,4,{128,128,128,255}, "rgba.png");
    require(viewer::ScenarioDensity(rgba, {0,0,1,1}).allows({.5,.5}),
            "Opaque RGB grayscale exports must work without modifying the source image");
    const viewer::ScenarioDensity userImage(supplied, {-100,-100,100,100});
    require(userImage.allows(userImage.sample(.5,.5,.5)), "The supplied Gray-Scott asset must load as a density map");
    rejects([&] { viewer::ScenarioDensity invalid(files.directory / "missing.png", {0,0,1,1}); }, "missing.png");
    rejects([&] { viewer::ScenarioDensity invalid(files.png(1,1,1,{0}), {0,0,1,1}); }, "no positive density");
    rejects([&] { viewer::ScenarioDensity invalid(files.png(1,1,3,{1,2,3}), {0,0,1,1}); }, "RGB channels must be equal");
    rejects([&] { viewer::ScenarioDensity invalid(files.png(1,1,2,{255,0}), {0,0,1,1}); }, "fully opaque");
}

void weightedPopulation() {
    const Files files;
    const auto path = files.png(2,2,1,{0,255,0,0});
    const auto scene = terrain();
    const viewer::CollisionWorld world(scene);
    auto settings = config(80);
    auto& horde = settings.hordes.front();
    horde.spawnDensity = path;
    const auto mapwide = viewer::generateZombieScenario(world, scene, nullptr, settings);
    for (const auto& matrix : mapwide.placements)
        require(matrix[3].x >= 0 && matrix[3].x <= 100 && matrix[3].z >= -100 && matrix[3].z <= 0,
                "An unbounded image must cover the whole map with north at the top");
    horde.spawn = {{20,10,80,30}};
    const auto stretched = viewer::generateZombieScenario(world, scene, nullptr, settings);
    for (std::size_t i = 0; i < stretched.placements.size(); ++i) {
        const auto p = glm::vec3(stretched.placements[i][3]);
        require(p.x >= 50 && p.x <= 80 && p.z >= 10 && p.z <= 20 && p.y == 5,
                "Weighted spawn/nearby repair escaped its positive pixels or ground");
        for (std::size_t j = 0; j < i; ++j) {
            const auto q = glm::vec3(stretched.placements[j][3]);
            require(glm::length(glm::vec2(p.x-q.x,p.z-q.z)) >= .6f, "Weighted repaired spawns overlap");
        }
    }
    const auto navmesh = mesh();
    horde.count = 40;
    horde.movement = viewer::ZombieScenarioMovement::travel;
    horde.spawn = {{-60,-60,-20,-20}};
    horde.destination = {{20,20,80,80}};
    horde.destinationDensity = files.png(2,2,1,{0,0,255,0}, "destination.png");
    const auto travelers = viewer::generateZombieScenario(world, scene, navmesh, settings);
    const auto repeated = viewer::generateZombieScenario(world, scene, navmesh, settings);
    require(travelers.placements == repeated.placements && travelers.destinations == repeated.destinations,
            "Weighted sampling and nearby repair must be repeatable with the same seed");
    for (std::size_t i = 0; i < travelers.placements.size(); ++i) {
        const auto p = glm::vec3(travelers.placements[i][3]), goal = travelers.destinations[i].value();
        require(p.x >= -40 && p.x <= -20 && p.z >= -60 && p.z <= -40 &&
                goal.x >= 20 && goal.x <= 50 && goal.z >= 50 && goal.z <= 80 && goal.y == 5,
                "Spawn and destination densities must use independent stretched rectangles");
    }
    horde.destination.reset();
    const auto mapwideGoals = viewer::generateZombieScenario(world, scene, navmesh, settings);
    for (const auto& goal : mapwideGoals.destinations)
        require(goal->x <= 0 && goal->z >= 0, "Unbounded destination density must cover map bounds");
}

void nearbyRepairForBothEndpoints() {
    const Files files;
    const auto scene = terrain();
    const viewer::CollisionWorld clear(scene);
    const auto navmesh = mesh();
    for (const bool weighted : {false, true}) {
        for (const bool repairSpawn : {false, true}) {
            auto settings = config(1);
            auto& horde = settings.hordes.front();
            horde.movement = viewer::ZombieScenarioMovement::travel;
            horde.spawn = {{-60,-60,-20,-20}};
            horde.destination = {{20,20,60,60}};
            if (weighted) horde.spawnDensity = horde.destinationDensity = files.png(1,1,1,{255});
            const auto original = viewer::generateZombieScenario(clear, scene, navmesh, settings);
            const auto target = repairSpawn ? glm::vec3(original.placements[0][3]) : original.destinations[0].value();
            const viewer::CollisionWorld blocked(scene, {{{target.x,0,target.z}, 1, 15}});
            const auto repaired = viewer::generateZombieScenario(blocked, scene, navmesh, settings);
            const auto repeated = viewer::generateZombieScenario(blocked, scene, navmesh, settings);
            const auto p = repairSpawn ? glm::vec3(repaired.placements[0][3]) : repaired.destinations[0].value();
            const auto distance = glm::length(glm::vec2(p.x-target.x,p.z-target.z));
            require(distance >= 1.29f && distance < 2.5f && p.y == 5,
                    "A blocked uniform/weighted spawn/destination must move nearby before resampling");
            require(repaired.placements == repeated.placements && repaired.destinations == repeated.destinations,
                    "Seeded nearby search must be repeatable");
            horde.placementSearchRadius = 1.4f;
            const auto bounded = viewer::generateZombieScenario(blocked, scene, navmesh, settings);
            const auto q = repairSpawn ? glm::vec3(bounded.placements[0][3]) : bounded.destinations[0].value();
            require(glm::length(glm::vec2(q.x-target.x,q.z-target.z)) <= 1.40001f,
                    "Nearby repair exceeded its configured maximum adjustment");
            horde.placementSearchRadius = 0;
            const auto resampled = viewer::generateZombieScenario(blocked, scene, navmesh, settings);
            const auto r = repairSpawn ? glm::vec3(resampled.placements[0][3]) : resampled.destinations[0].value();
            require(glm::length(glm::vec2(r.x-target.x,r.z-target.z)) > 3,
                    "Zero search radius must fall back to resampling instead of nearby repair");
        }
    }
}

void constrainedFailures() {
    using namespace viewer::navmesh_path;
    const Files files;
    const auto scene = terrain();
    const viewer::CollisionWorld world(scene);
    Mesh islands({{-90,-90},{-2,-90},{-2,90},{-90,90},{2,-90},{90,-90},{90,90},{2,90}},
                 {{0,4},{4,4}}, {{0,noPolygon},{1,noPolygon},{2,noPolygon},{3,noPolygon},
                                 {4,noPolygon},{5,noPolygon},{6,noPolygon},{7,noPolygon}});
    auto settings = config(1);
    auto& horde = settings.hordes.front();
    horde.movement = viewer::ZombieScenarioMovement::travel;
    horde.spawn = {{-60,-60,-20,60}};
    horde.destinationDensity = files.png(2,1,1,{0,255});
    rejects([&] { (void)viewer::generateZombieScenario(world, scene, islands, settings); }, "density.png");
    horde.destinationDensity.clear();
    horde.movement = viewer::ZombieScenarioMovement::idle;
    horde.spawn = {{-1,-1,1,1}};
    const viewer::CollisionWorld blocked(scene, {{{0,0,0}, 2, 15}});
    rejects([&] { (void)viewer::generateZombieScenario(blocked, scene, nullptr, settings); }, "density_horde");
    horde.spawnDensity = files.png(1,1,1,{255});
    rejects([&] { (void)viewer::generateZombieScenario(blocked, scene, nullptr, settings); }, "density.png");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected 16-bit fixture and supplied Gray-Scott image paths");
        imageMappingAndWeights(argv[1], argv[2]);
        weightedPopulation();
        nearbyRepairForBothEndpoints();
        constrainedFailures();
        std::cout << "Density weighting, mapping, nearby spawn/destination repair and constraints passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
