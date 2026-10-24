#include "vehicle_player.hpp"
#include "physics_world.hpp"
#include "interior_fixture.hpp"
#include <glm/gtc/constants.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F run) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid vehicle config was accepted");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Fixture directory missing");
        const auto types = viewer::loadVehicleConfigs(TEST_VEHICLE_CONFIGS);
        require(types.size() == 16 && types[0].id == "toyota_corolla_1999", "Missing car types or unstable default");
        for (const auto& c : types) require(c.seats.size() <= 4 && c.seats.size() >= 1 && !c.name.empty(), "Invalid car seats/name");
        std::filesystem::create_directories(argv[1]);
        const auto path = std::filesystem::path(argv[1]) / "car.toml";
        std::ifstream source(std::filesystem::path(TEST_VEHICLE_CONFIGS) / "toyota_corolla_1999.toml");
        const std::string valid((std::istreambuf_iterator<char>(source)), {});
        require(!valid.empty(), "Missing config fixture");
        for (const auto& change : {std::pair<std::string,std::string>{"seat_count = 4","seat_count = 5"},
                 {"seat_count = 4","seat_count = 3"}, {"entry_radius = 2.0","entry_radius = -1.0"},
                 {"weight_kg = 1100.0","weight_kg = nan"}, {"[-0.38, 0.9, -0.25]","[0.38, 0.9, -0.25]"}}) {
            auto text = valid;
            const auto pos = text.find(change.first);
            require(pos != std::string::npos, "Expected field missing from config fixture");
            text.replace(pos,change.first.size(),change.second);
            { std::ofstream file(path); file << text; }
            rejects([&] { viewer::loadVehicleConfig(path); });
        }
        auto custom = types[0]; custom.weightKg = 2100;
        custom.seats[0].entryRadius = .25f;
        custom.wheels[0] = {-.95f,-.3f,-1.4f};
        custom.seats[1].eyePosition = {.5f,.85f,-.4f};
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::VehicleLayer cars(physics,{custom});
        const glm::vec3 center(4,2,7);
        cars.add(center,glm::half_pi<float>());
        const auto pose = cars.pose(0);
        require(glm::distance(glm::vec3(pose.wheels[0][3]),center+pose.chassis.rotation*custom.wheels[0])<1e-5f,
                "Wheel position ignored config or applied node rotation twice");
        require(glm::distance(viewer::vehicleSeat(cars,0,1),center+pose.chassis.rotation*custom.seats[1].eyePosition)<1e-5f,
                "Passenger eye ignored config or vehicle rotation");
        cars.applyImpulse(0,{2000,0,0},center);
        require(std::abs(cars.velocity(0).x-1)<1e-4f,"Configured chassis mass did not affect rigid-body momentum");
        viewer::Model terrain; terrain.materials.push_back({});
        interior_fixture::quad(terrain, {-40,0,-40}, {-40,0,40}, {40,0,40}, {40,0,-40}, "terrain");
        interior_fixture::bounds(terrain);
        viewer::CollisionWorld clear(terrain);
        const auto door = center + pose.chassis.rotation * custom.seats[0].entryPosition;
        const auto onlyDriver = [](std::size_t, std::size_t seat) { return seat == 0; };
        const auto nearby = viewer::vehicleEntry(cars,door+glm::vec3(.24f,0,0),clear,onlyDriver);
        require(nearby && nearby->seat == 0, "Configured entry radius rejected an eye inside its boundary");
        require(!viewer::vehicleEntry(cars,door+glm::vec3(.26f,0,0),clear,onlyDriver),
                "Configured entry radius allowed an eye beyond its boundary");
        require(!viewer::vehicleEntry(cars,door,clear,[](std::size_t,std::size_t) { return false; }),
                "Entry action ignored a fully occupied car");
        const float wallX=door.x+.1f;
        interior_fixture::quad(terrain,{wallX,door.y-1,door.z-1},{wallX,door.y-1,door.z+1},
            {wallX,door.y+1,door.z+1},{wallX,door.y+1,door.z-1},"building_entry_wall",glm::mat4(1),true);
        interior_fixture::bounds(terrain);
        viewer::CollisionWorld blocked(terrain);
        require(!viewer::vehicleEntry(cars,door+glm::vec3(.24f,0,0),blocked,onlyDriver),
                "Vehicle entry action reached through a wall");
        custom.weightKg = 3000;
        require(cars.config(0).weightKg == 2100,"Vehicle definitions were not captured for the running session");
        std::cout << "Vehicle configuration, mass, wheel and seat checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
