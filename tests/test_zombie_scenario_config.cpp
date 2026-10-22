#include "model.hpp"
#include "scenario_coordinates.hpp"
#include "zombie_scenario_config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void rejects(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

class ConfigFixture {
public:
    ConfigFixture() : path_(std::filesystem::current_path() / "scenario-config-test-fixtures/scenario.toml") {
        std::filesystem::create_directories(path_.parent_path());
    }

    viewer::ZombieScenarioConfig parse(const std::string& text) const {
        std::ofstream(path_) << text;
        return viewer::loadZombieScenarioConfig(path_);
    }

    void invalid(const std::string& text) const {
        try {
            (void)parse(text);
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()).find(path_.string()) != std::string::npos,
                    "Invalid scenarios must identify their source path");
            return;
        }
        throw std::runtime_error("Malformed scenario was accepted: " + text);
    }

private:
    std::filesystem::path path_;
};

const std::string zombieSettings = "[zombies]\ncount = 1000\nspawn = 'random'\ndestination = 'random'\n";
const std::string meterRectangle = "spawn_rectangle_m = [-30, -10, 50, 70]\n";

void strictConfigParsing() {
    const ConfigFixture fixture;
    const auto meters = fixture.parse("seed = 42\n" + zombieSettings + meterRectangle);
    require(meters.seed == 42 && meters.count == 1000 &&
                meters.coordinateSystem == viewer::ZombieScenarioConfig::CoordinateSystem::Meters &&
                meters.spawnRectangle == std::array<double, 4>{-30, -10, 50, 70},
            "Meter config must preserve seed, count, and east/north rectangle coordinates");
    const auto geographic = fixture.parse(zombieSettings + "spawn_rectangle_wgs84 = [19.35, 47.60, 19.36, 47.61]\n");
    require(!geographic.seed && geographic.count == 1000 &&
                geographic.coordinateSystem == viewer::ZombieScenarioConfig::CoordinateSystem::Wgs84,
            "WGS84 config must preserve its coordinate system and allow an omitted seed");
    require(fixture.parse("seed = 4294967295\n" + zombieSettings + meterRectangle).seed ==
                std::numeric_limits<std::uint32_t>::max(),
            "The full unsigned 32-bit seed range must be accepted");

    for (const auto& root : {"version = 1\n", "seed = -1\n", "seed = 4294967296\n",
                             "seed = 42.0\n", "seed = true\n"})
        fixture.invalid(std::string(root) + zombieSettings + meterRectangle);
    for (const auto& rectangle : {"", "spawn_rectangle_m = [0,0,0,10]\n",
                                  "spawn_rectangle_m = [0,10,20,0]\n",
                                  "spawn_rectangle_m = [0,0,20]\n",
                                  "spawn_rectangle_m = [0,0,20,'40']\n",
                                  "spawn_rectangle_m = [0,0,20,true]\n",
                                  "spawn_rectangle_m = [0,0,20,inf]\n",
                                  "spawn_rectangle_m = [0,0,20,nan]\n",
                                  "spawn_rectangle_wgs84 = [-181,0,20,40]\n",
                                  "spawn_rectangle_wgs84 = [0,0,181,40]\n",
                                  "spawn_rectangle_wgs84 = [0,-91,20,40]\n",
                                  "spawn_rectangle_wgs84 = [0,0,20,91]\n",
                                  "spawn_rectangle_gps = [0,0,20,40]\n"})
        fixture.invalid(zombieSettings + rectangle);
    fixture.invalid(zombieSettings + meterRectangle + "spawn_rectangle_wgs84 = [19,47,20,48]\n");
    fixture.invalid(zombieSettings + meterRectangle + "typo = true\n");
    fixture.invalid("[zombies]\ncount = 0\nspawn = 'random'\ndestination = 'random'\n" + meterRectangle);
    fixture.invalid("[zombies]\ncount = 100001\nspawn = 'random'\ndestination = 'random'\n" + meterRectangle);
    fixture.invalid("[zombies]\ncount = 1000.0\nspawn = 'random'\ndestination = 'random'\n" + meterRectangle);
    fixture.invalid("[zombies]\ncount = 1000\nspawn = 'fixed'\ndestination = 'random'\n" + meterRectangle);
    fixture.invalid("[zombies]\ncount = 1000\nspawn = 'random'\ndestination = 'fixed'\n" + meterRectangle);
    fixture.invalid("[zombies]\ncount = 1000\nspawn = 'random'\n" + meterRectangle);
    fixture.invalid("seed = 1\n");
    fixture.invalid("this is not TOML");
}

void meterCoordinateFrames() {
    viewer::ZombieScenarioConfig config;
    config.spawnRectangle = {-30, -10, 50, 70};
    viewer::Model scene;
    scene.boundsMin = {10, -5, -160};
    scene.boundsMax = {210, 50, 40};
    const viewer::ScenarioCoordinates local(config, scene);
    require(glm::distance(local.sample(0, 0), glm::vec2(80, -50)) < 0.0001f &&
                glm::distance(local.sample(1, 1), glm::vec2(160, -130)) < 0.0001f,
            "Unreferenced meter coordinates must use bounds center and invert north into runtime Z");
    require(local.contains({120, -90}) && !local.contains({120, -131}) && !local.contains({79, -90}),
            "Final snapped points outside the authored rectangle must be rejected");
    scene.geographicFrame = viewer::Model::GeographicFrame{"EPSG:32634", {375000, 5273000}};
    const viewer::ScenarioCoordinates georeferenced(config, scene);
    require(glm::distance(georeferenced.sample(0, 0), glm::vec2(-30, 10)) < 0.0001f,
            "Geographic meter offsets must use the recorded map origin rather than asymmetric bounds");
    rejects([&] { (void)local.sample(-0.1, 0); }, "Negative sample coordinates must be rejected");
    rejects([&] { (void)local.sample(0, std::numeric_limits<double>::quiet_NaN()); },
            "Nonfinite sample coordinates must be rejected");
    require(!local.contains({std::numeric_limits<float>::infinity(), 0}),
            "Nonfinite placements cannot be inside a rectangle");
}

void geographicCoordinateFrames() {
    viewer::ZombieScenarioConfig config;
    config.coordinateSystem = viewer::ZombieScenarioConfig::CoordinateSystem::Wgs84;
    config.spawnRectangle = {19.35, 47.60, 19.36, 47.61};
    viewer::Model scene;
    rejects([&] { viewer::ScenarioCoordinates missingFrame(config, scene); },
            "WGS84 coordinates require a geographic scene frame");
    scene.geographicFrame = viewer::Model::GeographicFrame{"EPSG:32634", {375000, 5273000}};
    const viewer::ScenarioCoordinates coordinates(config, scene);
    // Independent generator pyproj reference, always_xy=True; this detects
    // longitude/latitude swapping, north/south inversion, and lost map origins.
    require(glm::distance(coordinates.sample(0, 0), glm::vec2(970.52835f, -162.42161f)) < 0.002f &&
                glm::distance(coordinates.sample(1, 1), glm::vec2(1745.70253f, -1257.82133f)) < 0.002f &&
                glm::distance(coordinates.sample(0.5, 0.5), glm::vec2(1358.15076f, -710.10900f)) < 0.002f,
            "WGS84 samples must match the generator's projected map frame");
    for (int east = 0; east <= 5; ++east)
        for (int north = 0; north <= 5; ++north)
            require(coordinates.contains(coordinates.sample(east / 5.0, north / 5.0)),
                    "Every source-rectangle sample, including its edges, must pass the inverse geographic check");
    require(!coordinates.contains(coordinates.sample(0, 0) - glm::vec2(20, 0)),
            "A point beyond the geographic west edge must fail rectangle containment");

    scene.geographicFrame = viewer::Model::GeographicFrame{"EPSG:23700", {673000, 251000}};
    const viewer::ScenarioCoordinates eov(config, scene);
    require(glm::distance(eov.sample(0.5, 0.5), glm::vec2(125.41166f, -281.61332f)) < 2.0f,
            "Geographic rectangles must honor a non-UTM map CRS");
}

} // namespace

int main() {
    try {
        strictConfigParsing();
        meterCoordinateFrames();
        geographicCoordinateFrames();
        std::cout << "Zombie scenario TOML and geographic coordinate tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
