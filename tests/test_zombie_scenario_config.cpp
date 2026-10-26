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

template<class Exception = std::runtime_error, class Function>
void rejects(Function&& function, const char* message) {
    try {
        function();
    } catch (const Exception&) {
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

    void invalid(const std::string& text, const std::string& expected = "") const {
        try {
            (void)parse(text);
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()).find(path_.string()) != std::string::npos,
                    "Invalid scenarios must identify their source path");
            require(std::string(error.what()).find(expected) != std::string::npos,
                    "Invalid scenarios must explain the rejected setting");
            return;
        }
        throw std::runtime_error("Malformed scenario was accepted: " + text);
    }

private:
    std::filesystem::path path_;
};

const std::string zombieSettings = "[zombies.crossing_horde]\ncount = 1000\nmovement = 'travel'\n";
const std::string worldRectangle = "spawn = [-30, -10, 50, 70]\n";

void strictConfigParsing() {
    const ConfigFixture fixture;
    const auto world = fixture.parse("seed = 42\n" + zombieSettings + worldRectangle +
                                     "destination = [100, 200, 300, 400]\n");
    require(world.seed == 42 && world.count() == 1000 && world.hordes.size() == 1,
            "World config must preserve seed and horde population");
    const auto& horde = world.hordes.front();
    require(horde.name == "crossing_horde" && horde.count == 1000 &&
                horde.spawn == viewer::ZombieHordeConfig::Rectangle{-30, -10, 50, 70} &&
                horde.destination == viewer::ZombieHordeConfig::Rectangle{100, 200, 300, 400},
            "Named hordes must preserve independent flat world rectangles");
    const auto mapwide = fixture.parse(zombieSettings);
    require(!mapwide.seed && !mapwide.player, "Omitted seed and player settings must preserve their defaults");
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
    const auto multiple = fixture.parse("version = 4\nseed = 0\n" + zombieSettings + worldRectangle +
        "[zombies.other]\ncount = 50\nmovement = 'travel'\ndestination = [100,200,300,400]\n");
    require(multiple.seed == 0 && multiple.count() == 1050 && multiple.hordes.size() == 2 &&
                multiple.hordes[0].spawn && !multiple.hordes[0].destination &&
                !multiple.hordes[1].spawn && multiple.hordes[1].destination,
            "Multiple hordes must retain independent rectangles and sum their counts");
    require(fixture.parse("seed = 4294967295\n" + zombieSettings).seed ==
                std::numeric_limits<std::uint32_t>::max(),
            "The full unsigned 32-bit seed range must be accepted");
    require(fixture.parse("[zombies.limit]\ncount = 100000\n").count() == 100000,
            "The maximum total population must be accepted");

    for (const auto& root : {"version = 4.0\n", "version = '4'\n", "typo = 1\n",
                             "seed = -1\n", "seed = 4294967296\n", "seed = 42.0\n", "seed = true\n"})
        fixture.invalid(std::string(root) + zombieSettings);
    for (const auto* version : {"1", "2", "5"})
        fixture.invalid(std::string("version = ") + version + "\n" + zombieSettings,
                        "expected version = 4 with world X/Z meters only");
    fixture.invalid("version = 3\n" + zombieSettings, "movement = 'travel'");
    fixture.invalid("version = 3\n" + zombieSettings, "set version = 4");
    for (const auto* version : {"", "version = 4\n"})
        for (const auto* coordinates : {"'wgs84'", "'world'", "'meters'", "true"})
            fixture.invalid(std::string(version) + zombieSettings + "coordinates = " + coordinates + "\n" +
                            "spawn = [19.35,47.60,19.36,47.61]\n",
                            "coordinates is no longer supported");
    for (const auto* endpoint : {"spawn", "destination"}) {
        for (const auto* bounds : {"[0,0,0,10]", "[0,10,20,0]", "[0,0,20]", "[0,0,20,'40']",
                                  "[0,0,20,true]", "[0,0,20,inf]", "[0,0,20,nan]", "[0,0,20,1e100]",
                                  "'random'", "'fixed'", "{ rectangle = [0,0,20,40] }"})
            fixture.invalid(zombieSettings + endpoint + " = " + bounds + "\n");
    }
    for (const auto* setting : {"typo = true\n",
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

void movementModes() {
    const ConfigFixture fixture;
    const std::string idle = "[zombies.still]\ncount = 100000\n";
    const auto defaults = fixture.parse(idle);
    require(defaults.count() == 100000 && !defaults.needsNavigation() &&
                defaults.hordes.front().movement == viewer::ZombieScenarioMovement::idle &&
                !defaults.hordes.front().spawn && !defaults.hordes.front().destination,
            "Omitted movement/version must select version 4 idle without navigation or endpoint bounds");
    const auto explicitIdle = fixture.parse("version = 4\n" + idle + "movement = 'idle'\n" + worldRectangle);
    require(!explicitIdle.needsNavigation() && explicitIdle.hordes.front().spawn &&
                explicitIdle.hordes.front().movement == viewer::ZombieScenarioMovement::idle,
            "An explicit idle group must allow authored spawn bounds without navigation");
    const auto travel = fixture.parse(zombieSettings);
    require(travel.needsNavigation() && travel.hordes.front().movement == viewer::ZombieScenarioMovement::travel &&
                !travel.hordes.front().spawn && !travel.hordes.front().destination,
            "Travel must request navigation even when both endpoint regions are omitted");
    const auto mixed = fixture.parse("[zombies.still]\ncount = 2\n"
        "[zombies.travelers]\ncount = 3\nmovement = 'travel'\ndestination = [1,2,3,4]\n");
    require(mixed.count() == 5 && mixed.needsNavigation() && mixed.hordes.size() == 2 &&
                mixed.hordes[0].movement == viewer::ZombieScenarioMovement::idle &&
                mixed.hordes[1].movement == viewer::ZombieScenarioMovement::travel,
            "Mixed scenarios must preserve each group's movement and initialize navigation for travelers");
    fixture.invalid(idle + "destination = [1,2,3,4]\n", "destination");
    fixture.invalid(idle + "movement = 'idle'\ndestination = [1,2,3,4]\n", "movement = 'idle'");
    for (const auto* movement : {"'wander'", "'Idle'", "0", "true", "['idle']", "{ mode = 'idle' }"})
        fixture.invalid(idle + "movement = " + movement + "\n", "movement must be 'idle' or 'travel'");

    viewer::ZombieScenarioConfig direct;
    direct.hordes.push_back({"still", 1, std::nullopt, std::nullopt});
    require(direct.count() == 1 && !direct.needsNavigation(),
            "Direct C++ configurations must default to idle");
    direct.hordes.front().destination = viewer::ZombieHordeConfig::Rectangle{1, 2, 3, 4};
    rejects<std::invalid_argument>([&] { (void)direct.count(); },
        "Direct C++ idle groups must reject destinations");
    direct.hordes.front().movement = viewer::ZombieScenarioMovement::travel;
    require(direct.count() == 1 && direct.needsNavigation(),
            "Direct C++ travel groups must accept destination bounds");
    direct.hordes.front().movement = static_cast<viewer::ZombieScenarioMovement>(-1);
    rejects<std::invalid_argument>([&] { (void)direct.count(); },
        "Direct C++ groups must reject unsupported movement modes");
}

void worldCoordinateFrames() {
    const viewer::ZombieHordeConfig::Rectangle rectangle{-30, -10, 50, 70};
    const viewer::ScenarioCoordinates local(rectangle);
    require(glm::distance(local.sample(0, 0), glm::vec2(-30, -10)) < 0.0001f &&
                glm::distance(local.sample(1, 1), glm::vec2(50, 70)) < 0.0001f,
            "World coordinates must be absolute X/Z meters without recentering or flipping Z");
    require(local.contains({20, 30}) && !local.contains({20, 71}) && !local.contains({-31, 30}),
            "Final snapped points outside the authored rectangle must be rejected");
    require(local.sample(0.5, 0.25) == glm::vec2(10, 10),
            "World samples must interpolate asymmetric X/Z bounds in meters");
    rejects([&] { (void)local.sample(-0.1, 0); }, "Negative sample coordinates must be rejected");
    rejects([&] { (void)local.sample(0, std::numeric_limits<double>::quiet_NaN()); },
            "Nonfinite sample coordinates must be rejected");
    rejects([&] { (void)local.sample(0, 1.1); }, "Samples beyond the rectangle must be rejected");
    require(!local.contains({std::numeric_limits<float>::infinity(), 0}),
            "Nonfinite placements cannot be inside a rectangle");
}

void invalidRectangles() {
    for (const auto& bounds : {viewer::ZombieHordeConfig::Rectangle{0, 0, 0, 10},
                              viewer::ZombieHordeConfig::Rectangle{0, 10, 20, 0},
                              viewer::ZombieHordeConfig::Rectangle{0, 0, 20, std::numeric_limits<double>::infinity()}})
        rejects([&] { viewer::ScenarioCoordinates invalid(bounds); },
                "Direct C++ callers must reject unordered or nonfinite rectangles");
}

} // namespace

int main(int argc, char** argv) {
    try {
        strictConfigParsing();
        movementModes();
        worldCoordinateFrames();
        invalidRectangles();
        for (int i = 1; i < argc; ++i)
            require(viewer::loadZombieScenarioConfig(argv[i]).count() > 0,
                    "Checked-in scenarios must load with the current format");
        std::cout << "Zombie scenario TOML and world coordinate tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
