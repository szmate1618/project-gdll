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

const std::string zombieSettings = "[zombies.crossing_horde]\ncount = 1000\n";
const std::string worldRectangle = "spawn = [-30, -10, 50, 70]\n";

void strictConfigParsing() {
    const ConfigFixture fixture;
    const auto world = fixture.parse("seed = 42\n" + zombieSettings + worldRectangle +
                                     "destination = [100, 200, 300, 400]\n");
    require(world.seed == 42 && world.count() == 1000 && world.hordes.size() == 1,
            "World config must preserve seed and horde population");
    const auto& horde = world.hordes.front();
    require(horde.name == "crossing_horde" && horde.count == 1000 &&
                horde.coordinateSystem == viewer::ZombieHordeConfig::CoordinateSystem::World &&
                horde.spawn == viewer::ZombieHordeConfig::Rectangle{-30, -10, 50, 70} &&
                horde.destination == viewer::ZombieHordeConfig::Rectangle{100, 200, 300, 400},
            "Named hordes must preserve independent flat world rectangles");
    const auto geographic = fixture.parse(zombieSettings + "coordinates = 'wgs84'\n"
        "spawn = [19.35, 47.60, 19.36, 47.61]\n"
        "destination = [19.36, 47.61, 19.37, 47.62]\n");
    require(!geographic.seed && geographic.count() == 1000 &&
                geographic.hordes.front().coordinateSystem == viewer::ZombieHordeConfig::CoordinateSystem::Wgs84,
            "WGS84 config must preserve its coordinate system and allow an omitted seed");
    const auto mapwide = fixture.parse(zombieSettings);
    require(!mapwide.player, "Older scenarios without [player] must preserve the default player spawn");
    const auto player = fixture.parse("[player]\nspawn = [700, -2600.5]\norientation = [-90, 12.5]\n" + zombieSettings);
    require(player.player && player.player->spawn == std::array<float, 2>{700, -2600.5f} &&
                player.player->orientation == std::array<float, 2>{-90, 12.5f},
            "Player spawn must preserve world X/Z meters and independent yaw/pitch degrees");
    for (const auto* settings : {"spawn = [1,2]\n", "orientation = [0,0]\n",
             "spawn = [1,2,3]\norientation = [0,0]\n", "spawn = [1,true]\norientation = [0,0]\n",
             "spawn = [nan,2]\norientation = [0,0]\n", "spawn = [1e8,2]\norientation = [0,0]\n",
             "spawn = [1,2]\norientation = [181,0]\n", "spawn = [1,2]\norientation = [0,-90]\n",
             "spawn = [1,2]\norientation = [0,inf]\n", "spawn = [1,2]\norientation = ['0',0]\n",
             "spawn = [1,2]\norientation = [0,0]\ntypos = 1\n"})
        fixture.invalid(std::string("[player]\n") + settings + zombieSettings);
    fixture.invalid("player = true\n" + zombieSettings);
    require(!mapwide.hordes.front().spawn && !mapwide.hordes.front().destination,
            "Omitting both rectangles must allow mapwide sampling");
    require(!fixture.parse(zombieSettings + worldRectangle).hordes.front().destination &&
                !fixture.parse(zombieSettings + "destination = [1,2,3,4]\n").hordes.front().spawn,
            "Spawn and destination bounds must be independently optional");
    const auto mixed = fixture.parse("version = 2\nseed = 0\n" + zombieSettings +
        "coordinates = 'world'\n[zombies.geographic]\ncount = 50\ncoordinates = 'wgs84'\n"
        "destination = [19.35,47.60,19.36,47.61]\n");
    require(mixed.seed == 0 && mixed.count() == 1050 && mixed.hordes.size() == 2 &&
                mixed.hordes[0].coordinateSystem != mixed.hordes[1].coordinateSystem,
            "Multiple hordes must retain independent coordinate systems and sum their counts");
    require(fixture.parse("seed = 4294967295\n" + zombieSettings).seed ==
                std::numeric_limits<std::uint32_t>::max(),
            "The full unsigned 32-bit seed range must be accepted");
    require(fixture.parse("[zombies.limit]\ncount = 100000\n").count() == 100000,
            "The maximum total population must be accepted");

    for (const auto& root : {"version = 1\n", "version = 2.0\n", "version = '2'\n", "typo = 1\n",
                             "seed = -1\n", "seed = 4294967296\n", "seed = 42.0\n", "seed = true\n"})
        fixture.invalid(std::string(root) + zombieSettings);
    for (const auto* endpoint : {"spawn", "destination"}) {
        for (const auto* bounds : {"[0,0,0,10]", "[0,10,20,0]", "[0,0,20]", "[0,0,20,'40']",
                                  "[0,0,20,true]", "[0,0,20,inf]", "[0,0,20,nan]", "[0,0,20,1e100]",
                                  "'random'", "'fixed'", "{ rectangle = [0,0,20,40] }"})
            fixture.invalid(zombieSettings + endpoint + " = " + bounds + "\n");
        for (const auto* bounds : {"[-181,0,20,40]", "[0,0,181,40]", "[0,-91,20,40]", "[0,0,20,91]"})
            fixture.invalid(zombieSettings + "coordinates = 'wgs84'\n" + endpoint + " = " + bounds + "\n");
    }
    for (const auto* setting : {"coordinates = 'meters'\n", "coordinates = true\n", "typo = true\n",
                                "spawn_rectangle_m = [0,0,20,40]\n",
                                "spawn_rectangle_wgs84 = [19,47,20,48]\n"})
        fixture.invalid(zombieSettings + setting);
    for (const auto* count : {"0", "-1", "100001", "1000.0", "true", "'10'"})
        fixture.invalid(std::string("[zombies.horde]\ncount = ") + count + "\n");
    fixture.invalid("[zombies.first]\ncount = 60000\n[zombies.second]\ncount = 60000\n");
    fixture.invalid("[zombies.horde]\n");
    fixture.invalid("[zombies]\n");
    fixture.invalid("[zombies]\ncount = 1000\nspawn = 'random'\ndestination = 'random'\n"
                    "spawn_rectangle_m = [0,0,20,40]\n");
    fixture.invalid("[zombies.'']\ncount = 1\n");
    fixture.invalid("[zombies.horde.nested]\ncount = 1\n");
    fixture.invalid("[[zombies.horde]]\ncount = 1\n");
    fixture.invalid("seed = 1\n");
    fixture.invalid("this is not TOML");
}

void worldCoordinateFrames() {
    const viewer::ZombieHordeConfig::Rectangle rectangle{-30, -10, 50, 70};
    viewer::Model scene;
    scene.boundsMin = {10, -5, -160};
    scene.boundsMax = {210, 50, 40};
    const viewer::ScenarioCoordinates local(viewer::ZombieHordeConfig::CoordinateSystem::World, rectangle, scene);
    require(glm::distance(local.sample(0, 0), glm::vec2(-30, -10)) < 0.0001f &&
                glm::distance(local.sample(1, 1), glm::vec2(50, 70)) < 0.0001f,
            "World coordinates must be absolute X/Z meters without recentering or flipping Z");
    require(local.contains({20, 30}) && !local.contains({20, 71}) && !local.contains({-31, 30}),
            "Final snapped points outside the authored rectangle must be rejected");
    scene.geographicFrame = viewer::Model::GeographicFrame{"EPSG:32634", {375000, 5273000}};
    const viewer::ScenarioCoordinates georeferenced(viewer::ZombieHordeConfig::CoordinateSystem::World, rectangle, scene);
    require(georeferenced.sample(0, 0) == local.sample(0, 0),
            "World coordinates must be independent of geographic metadata");
    rejects([&] { (void)local.sample(-0.1, 0); }, "Negative sample coordinates must be rejected");
    rejects([&] { (void)local.sample(0, std::numeric_limits<double>::quiet_NaN()); },
            "Nonfinite sample coordinates must be rejected");
    require(!local.contains({std::numeric_limits<float>::infinity(), 0}),
            "Nonfinite placements cannot be inside a rectangle");
}

void geographicCoordinateFrames() {
    constexpr auto wgs84 = viewer::ZombieHordeConfig::CoordinateSystem::Wgs84;
    const viewer::ZombieHordeConfig::Rectangle rectangle{19.35, 47.60, 19.36, 47.61};
    viewer::Model scene;
    rejects([&] { viewer::ScenarioCoordinates missingFrame(wgs84, rectangle, scene); },
            "WGS84 coordinates require a geographic scene frame");
    scene.geographicFrame = viewer::Model::GeographicFrame{"EPSG:32634", {375000, 5273000}};
    const viewer::ScenarioCoordinates coordinates(wgs84, rectangle, scene);
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
    const viewer::ScenarioCoordinates eov(wgs84, rectangle, scene);
    require(glm::distance(eov.sample(0.5, 0.5), glm::vec2(125.41166f, -281.61332f)) < 2.0f,
            "Geographic rectangles must honor a non-UTM map CRS");
}

} // namespace

int main() {
    try {
        strictConfigParsing();
        worldCoordinateFrames();
        geographicCoordinateFrames();
        std::cout << "Zombie scenario TOML and geographic coordinate tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
