#include "house_fixture.hpp"
#include "zombie_layer.hpp"
#include "floorplan_layer.hpp"
#include "house_navigation.hpp"
#include "building_footprints.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::ZombieLayer noisePopulation(bool reverse) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back(); layer.instances.resize(1002);
    for (auto& instance : layer.instances) instance.removed = true;
    layer.rebuildSpatialIndex();
    for (std::size_t index = 0; index < 1002; ++index) {
        const auto id = reverse ? 1001 - index : index;
        const glm::vec3 feet(40 + float(id % 32) * .8f, 0, float(id / 32) * .8f);
        const auto initial = id == 1000 ? viewer::ZombieState::hiding : id == 1001 ? viewer::ZombieState::midnight : viewer::ZombieState::idle;
        layer.activateInteriorZombie(id, glm::translate(glm::mat4(1), feet), initial, std::numeric_limits<double>::infinity());
    }
    return layer;
}
void noiseChance() {
    viewer::SimulationClock::instance().reset();
    auto first = noisePopulation(false), second = noisePopulation(true);
    const glm::vec3 sound(-10, 0, -10);
    first.makeNoise(sound, 100); second.makeNoise(sound, 100);
    std::size_t investigated = 0;
    for (std::size_t id = 0; id < 1000; ++id) {
        const auto* a = first.behaviors.find(id); const auto* b = second.behaviors.find(id);
        require(a && b && a->state == b->state, "Noise choices must replay independently of pool insertion order");
        if (a->state == viewer::ZombieState::investigating) {
            ++investigated; require(a->target == sound, "Hearing investigation must remember the actual sound position");
        } else require(a->state == viewer::ZombieState::looking, "Other ordinary hearers must keep the normal noise turn");
    }
    std::cout << "Noise-only investigations: " << investigated << "/1000\n";
    require(investigated >= 70 && investigated <= 130, "Noise-only investigation probability must be approximately ten percent");
    require(first.behaviors.find(1000)->state == viewer::ZombieState::hiding &&
            first.behaviors.find(1001)->state == viewer::ZombieState::midnight,
            "Noise investigation must not override hiding or midnight spawn policies");
}

struct Scene {
    viewer::Model model = house_fixture::house();
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors{viewer::prepareDoorPanels(model), physics};
    viewer::CollisionWorld world{model, {}, &doors};
    viewer::FloorplanLayer floors{model, doors, world};
    Scene() {
        doors.setAngle(1, .7f); floors.update({{0, 1.7f, 8}});
        for (std::size_t id = 0; id < doors.count(); ++id) if (doors.present(id))
            doors.setAngle(id, doors.panel(id).inward ? -1.5707963f : 1.5707963f);
    }
};
viewer::ZombieLayer investigator(Scene& scene, glm::vec3 from, glm::vec3 sound) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back(); layer.instances.resize(1); layer.instances[0].removed = true;
    layer.rebuildSpatialIndex(); layer.activateInteriorZombie(0, glm::translate(glm::mat4(1), from));
    layer.setHouseNavigation(&scene.floors);
    auto* behavior = layer.behaviors.find(0);
    behavior->state = viewer::ZombieState::investigating; behavior->target = sound;
    return layer;
}

void walkingRoute(bool upstairs) {
    Scene scene;
    const auto& plan = scene.floors.resident().begin()->second;
    const auto* graph = scene.floors.navigationGraph(plan.name);
    glm::vec3 from(0), goal(0); bool haveFrom = false, haveGoal = false;
    float farthest = -1;
    for (const auto& node : graph->nodes()) if (node.kind == viewer::HouseNavigationGraph::Kind::RoomCenter && node.storey == 0) {
        from = node.point; haveFrom = true; break;
    }
    for (const auto& node : graph->nodes()) if (node.kind == viewer::HouseNavigationGraph::Kind::RoomCenter && node.storey == (upstairs ? 1u : 0u)) {
        const float distance = glm::distance(from, node.point);
        if (distance > farthest) { farthest = distance; goal = node.point; haveGoal = true; }
    }
    require(haveFrom && haveGoal, "Walking fixture must contain its source and target rooms");
    const auto sound = goal + glm::vec3(0, 1.65f, 0);
    auto layer = investigator(scene, from, sound);
    // Deliberately retain a cached house while the observer is out of sight;
    // an investigation must follow the heard room, not track the live player.
    viewer::ZombiePlayer player{{300, 0, 300}}; player.house = plan.name;
    auto& clock = viewer::SimulationClock::instance(); clock.reset();
    constexpr float dt = 1.f / 60;
    bool entered = false, finished = false;
    for (int step = 0; step < 6000; ++step) {
        const auto before = glm::vec3(layer.instances[0].restRoot[3]);
        clock.advance(dt); layer.updateBehavior(dt, player, scene.world);
        const auto feet = glm::vec3(layer.instances[0].restRoot[3]);
        require(glm::length(glm::vec2(feet.x - before.x, feet.z - before.z)) <= 1.2f * dt + .0002f,
                "Graph walking must preserve the normal investigation speed");
        for (const auto& contact : scene.world.contacts(feet, .3f, 1.8f, 0, .05f))
            require(contact.terrainSupport || contact.normal.y > .7f || contact.depth < .005f,
                    "Graph walking must not cross a wall or closed panel");
        if (const auto* behavior = layer.behaviors.find(0)) {
            require(behavior->state == viewer::ZombieState::houseInvestigating && behavior->target == sound,
                    "A same-house investigation must retain its special state and the heard sound position");
            entered = true;
        } else { finished = true; break; } // Completed idle entry is evicted for the distant observer.
    }
    const auto feet = glm::vec3(layer.instances[0].restRoot[3]);
    std::cout << "House walk upstairs=" << upstairs << ": feet " << feet.x << ',' << feet.y << ',' << feet.z
              << " goal " << goal.x << ',' << goal.y << ',' << goal.z << " finished=" << finished << '\n';
    require(entered && finished && glm::distance(feet, goal) < .3f,
            "House investigation must reach the sound's room through its doors and ramps");
}
void eligibilityAndEviction() {
    Scene scene;
    const auto& plan = scene.floors.resident().begin()->second;
    const auto house = plan.name;
    const auto* graph = scene.floors.navigationGraph(plan.name);
    std::vector<glm::vec3> centers;
    for (const auto& node : graph->nodes()) if (node.kind == viewer::HouseNavigationGraph::Kind::RoomCenter) centers.push_back(node.point);
    auto layer = investigator(scene, centers.front(), centers.back());
    viewer::ZombiePlayer player{{300, 0, 300}};
    viewer::SimulationClock::instance().reset();
    layer.updateBehavior(.01f, player, scene.world);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::investigating,
            "An outside player must not activate special house investigation");
    player.house = house;
    layer.updateBehavior(.01f, player, scene.world);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::houseInvestigating,
            "A cached inside player and a loaded same-house graph must activate special investigation");
    scene.floors.update({});
    layer.updateBehavior(.01f, player, scene.world);
    require(layer.behaviors.find(0)->state == viewer::ZombieState::investigating,
            "An evicted graph must be released safely and fall back to ordinary investigation");
}
void hearingEntersHouseRoute() {
    Scene scene;
    const auto& plan = scene.floors.resident().begin()->second;
    const auto* graph = scene.floors.navigationGraph(plan.name);
    glm::vec3 from(0);
    for (const auto& node : graph->nodes()) if (node.kind == viewer::HouseNavigationGraph::Kind::RoomCenter && node.storey == 0) {
        from = node.point; break;
    }
    const auto sound = from + glm::vec3(0, 4.5f, 0); // A different room directly upstairs.
    auto layer = investigator(scene, from, sound);
    auto* behavior = layer.behaviors.find(0); behavior->state = viewer::ZombieState::idle;
    viewer::ZombiePlayer player{from + glm::vec3(0, 0, -140)}; player.house = plan.name;
    viewer::SimulationClock::instance().reset();
    layer.updateBehavior(.001f, player, scene.world); // Publish cached player membership without visual contact.
    bool investigated = false;
    for (int event = 0; event < 100 && !investigated; ++event) {
        layer.makeNoise(sound, 100);
        behavior = layer.behaviors.find(0);
        investigated = behavior->state == viewer::ZombieState::houseInvestigating;
        if (!investigated) behavior->state = viewer::ZombieState::idle;
    }
    require(investigated && behavior->target == sound && graph->roomAt(sound) != graph->roomAt(from),
            "Hearing alone, including a sound directly upstairs, must select the sound's room through the loaded house graph");
}
}
int main() {
    try {
        noiseChance(); eligibilityAndEviction(); hearingEntersHouseRoute(); walkingRoute(false); walkingRoute(true);
        std::cout << "Noise and graph-based house investigation tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
