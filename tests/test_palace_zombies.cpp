#include "authored_floorplan.hpp"
#include "interior_zombies.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "house_polygon.hpp"
#include "physics_world.hpp"
#include "zombie_layer.hpp"
#include "zombie_crowd.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(const std::filesystem::path& path) {
    const std::string palace = "building_way_107918884_0";
    auto model = viewer::loadModel(path);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, collision);
    std::size_t entryId = doors.count();
    for (std::size_t id = 0; id < doors.count(); ++id)
        if (model.draws.at(doors.panel(id).draw).name.rfind(palace + "_doors", 0) == 0) { entryId = id; break; }
    require(entryId < doors.count(), "Palace fixture must have an exterior entrance");
    const auto entry = doors.panel(entryId).center;
    const auto load = [&] { doors.setAngle(entryId, .9f); floors.update({entry}); };
    load();
    const auto& plan = floors.resident().at(palace);
    require(viewer::hasInteriorZombieEncounter(palace), "Palace population must bypass the ordinary 20 percent roll");
    const auto positions = viewer::placeInteriorZombies(plan, doors, collision);
    const auto repeated = viewer::placeInteriorZombies(plan, doors, collision);
    require(positions.size() == 200 && repeated.size() == 200, "Real palace geometry must fit all 200 zombies");
    std::set<std::size_t> rooms;
    std::size_t ground = 0, upper = 0;
    for (std::size_t id = 0; id < positions.size(); ++id) {
        const auto& position = positions[id];
        const auto feet = glm::vec3(position.transform[3]);
        const auto& room = plan.rooms.at(position.room);
        require(position.transform == repeated[id].transform && position.room == repeated[id].room,
                "Palace positions and facings must be deterministic");
        require(viewer::house_polygon::contains(room.polygon, {feet.x, feet.z}) && plan.contains({feet.x, feet.z}),
                "Palace zombies must stay inside rooms and outside courtyard voids");
        require(!room.staircase && std::abs(feet.y - plan.floorHeight(room.storey)) < .02f,
                "Palace zombies must stand on their floor away from the stair flights");
        const auto support = collision.walkingGroundAt(feet.x, feet.z, feet.y - .04f, feet.y + .04f);
        require(support && support->normal.y > .99f, "Every palace zombie needs flat walking support");
        for (const auto& contact : collision.contacts(feet, viewer::ZombieCrowd::bodyRadius,
                                                    viewer::ZombieCrowd::bodyHeight, .002f, .05f))
            require(contact.depth <= .005f || (contact.normal.y > .7f && contact.point.y <= feet.y + .01f),
                    "Palace zombie capsules must clear walls, ceilings and closed doors");
        for (std::size_t other = 0; other < id; ++other)
            require(glm::distance(feet, glm::vec3(positions[other].transform[3])) >= .75f,
                    "Palace zombies must not overlap one another");
        rooms.insert(position.room);
        if (room.storey) ++upper; else ++ground;
    }
    require(ground >= 50 && upper >= 50 && rooms.size() >= 40,
            "Population must spread over many rooms on both palace floors");

    // Block two chosen positions with a player and an existing outdoor actor.
    // The fixed encounter must find alternatives rather than silently shrink.
    viewer::ZombieLayer zombies;
    zombies.assets.emplace_back(); zombies.instances.resize(202);
    for (auto& actor : zombies.instances) actor.removed = true;
    zombies.rebuildSpatialIndex();
    const auto outdoor = positions[1].transform;
    zombies.activateInteriorZombie(0, outdoor);
    const auto observer = glm::vec3(positions[0].transform[3]);
    viewer::InteriorZombies population(1, 201);
    population.update(floors, zombies, collision, {observer});
    require(std::count_if(zombies.instances.begin(), zombies.instances.end(), [](const auto& z) { return !z.removed; }) == 201,
            "First palace activation must add exactly 200 to the existing population despite blocked candidates");
    require(zombies.instances[0].restRoot == outdoor && zombies.instances.back().removed,
            "Palace activation must preserve outdoor actors and leave unused pool slots dormant");
    for (std::size_t id = 1; id <= 200; ++id) {
        const auto feet = glm::vec3(zombies.instances[id].restRoot[3]);
        require(glm::distance(feet, observer) >= 1.2f && glm::distance(feet, glm::vec3(outdoor[3])) >= .75f,
                "Palace activation must avoid observers and living actors");
        const auto* behavior = zombies.behaviors.find(id);
        require(behavior && behavior->state == (feet.y > plan.upperFloors.front() - .1f ?
                viewer::ZombieState::midnight : viewer::ZombieState::idle),
                "Palace crowd must retain ordinary ground-floor and midnight upstairs policies");
    }
    const auto survivor = zombies.instances[3].restRoot;
    zombies.setInteriorSleeping(1, true); // Model an actor already removed by corpse cleanup.
    const auto escaped = glm::translate(glm::mat4(1), entry + glm::vec3(1000, 0, 1000));
    zombies.instances[2].restRoot = zombies.instances[2].restTransform = zombies.instances[2].transform = escaped;
    zombies.rebuildSpatialIndex();
    floors.update({}); population.update(floors, zombies, collision, {});
    require(zombies.instances[3].removed && !zombies.instances[2].removed,
            "Eviction must sleep palace survivors while escaped actors remain active");
    for (int visit = 0; visit < 2; ++visit) {
        load(); population.update(floors, zombies, collision, {entry});
        require(zombies.instances[1].removed && !zombies.instances[3].removed && zombies.instances[3].restRoot == survivor,
                "Palace reentry must wake the same survivors without respawning a removed actor");
        require(zombies.instances[2].restRoot == escaped && zombies.instances.back().removed,
                "Palace reentry must not teleport escaped actors or consume fresh pool IDs");
        floors.update({}); population.update(floors, zombies, collision, {});
    }
    std::cout << "Palace population: 200 zombies in " << rooms.size() << " rooms (" << ground << " ground, " << upper
              << " upper); blocked positions and streaming lifecycle verified\n";
}
}
int main(int argc, char** argv) {
    try { check(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(PALACE_FIXTURE)); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
