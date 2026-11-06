#include "zombie_active_attack_fixture.hpp"
#include "zombie_doorway_fixture.hpp"
#include "zombie_door_collision.hpp"
#include "house_fixture.hpp"
#include "door_layer.hpp"
#include "floorplan_layer.hpp"
#include "house_navigation.hpp"
#include "physics_world.hpp"
#include "physics_impacts.hpp"
#include "physics_scene.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr float dt = 1.f / 60;
using viewer::ZombieState;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

viewer::AnimatedModel meleeAsset() {
    auto asset = zombie_attack_fixture::humanoid();
    // These cases exercise AI and baked hand traces against a stationary panel.
    // Physical arm/door response has its own shared-simulation coverage.
    asset.ragdollBones.fill(-1);
    return asset;
}

void addZombie(viewer::ZombieLayer& layer, glm::vec3 feet, ZombieState state, glm::vec3 target) {
    viewer::ZombieInstance instance;
    instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), glm::radians(180.f), {0, 1, 0});
    instance.transform = instance.restTransform = instance.restRoot;
    const auto id = layer.instances.size();
    layer.instances.push_back(instance);
    auto& behavior = layer.behaviors.create(id, 0, feet);
    behavior.state = state;
    behavior.target = target;
    layer.rebuildSpatialIndex();
}

struct DoorScene {
    viewer::Model model = viewer::test::doorway();
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors;
    viewer::CollisionWorld world;
    viewer::ZombieLayer zombies;
    viewer::ZombiePlayer player{{0, 0, -.4f}, 1.7f, 1.8f, 7};

    explicit DoorScene(bool haveDoor = true)
        : doors(haveDoor ? viewer::prepareDoorPanels(model) : std::vector<viewer::DoorPanel>{}, physics),
          world(model, {}, &doors) {
        viewer::SimulationClock::instance().reset();
        zombies.assets.push_back(meleeAsset());
        zombies.setPerceptionEnabled(true);
        zombies.setPerceptionStaggering(false);
    }

    void step(bool multiplayer = false) {
        auto& clock = viewer::SimulationClock::instance();
        clock.advance(dt);
        if (multiplayer) zombies.updateBehaviorForPlayers(dt, {player}, world, clock.seconds());
        else zombies.updateBehavior(dt, player, world, clock.seconds());
    }
};

void repeatedDoorSwings(ZombieState initial, bool multiplayer) {
    DoorScene scene;
    const glm::vec3 target{0, 0, -4};
    addZombie(scene.zombies, {0, 0, .5f}, initial, target);
    bool finished = false, recovered = false, playerChanged = false;
    int starts = 0;
    unsigned previousClip = 0, lastStartedClip = 0;
    double previousStart = -100;
    for (int tick = 0; tick < 300; ++tick) {
        scene.step(multiplayer);
        const auto& zombie = scene.zombies.instances[0];
        const auto* behavior = scene.zombies.behaviors.find(0);
        require(behavior && behavior->state == initial && behavior->target == target,
                "Door swings must retain the original chase/investigation state and destination");
        require(scene.zombies.attackHits().empty(), "A door swing injured a player through the closed panel");
        if (zombie.attackClip && !previousClip) {
            const double now = viewer::SimulationClock::instance().seconds();
            if (starts) {
                require(now - previousStart >= 1 + viewer::zombieAttackCooldownSeconds - 2 * dt,
                        "Repeated door swings skipped their normal cooldown");
                require(zombie.attackClip != lastStartedClip, "Repeated door swings must alternate hands");
            }
            require(zombie.restRoot[2].z < -.9f, "A door swing must face the contacted panel");
            previousStart = now;
            lastStartedClip = zombie.attackClip;
            ++starts;
        }
        recovered |= zombie.attackClip && zombie.attackSeconds > .6;
        finished |= previousClip && !zombie.attackClip;
        previousClip = zombie.attackClip;
        if (multiplayer && !playerChanged && zombie.attackClip &&
            zombie.attackSeconds > .1 && zombie.attackSeconds < .25) {
            scene.player.id = 23;
            playerChanged = true;
        }
    }
    require(starts >= 2 && recovered && finished,
            "A hidden player must not cancel door windup, recovery or subsequent swings");
    require(!multiplayer || playerChanged, "The multiplayer fixture must replace a player during door windup");

    scene.player.feet.z = -5;
    scene.doors.setAngle(0, 1.5707963f);
    for (int tick = 0; tick < 600 && scene.zombies.instances[0].restRoot[3].z > -.8f; ++tick)
        scene.step(multiplayer);
    require(scene.zombies.instances[0].restRoot[3].z < -.8f,
            "Opening the panel must let a door attacker resume movement through the doorway");
}

void glancingContactStillTriggers() {
    DoorScene scene;
    const glm::vec3 start{-.15f, 0, .322f};
    addZombie(scene.zombies, start, ZombieState::investigating, {3, 0, -1});
    scene.step();
    const auto& zombie = scene.zombies.instances[0];
    require(zombie.restRoot[3].x > start.x + .01f && zombie.restRoot[3].z > .31f,
            "The glancing fixture must successfully slide sideways along the panel");
    require(zombie.attackClip != 0,
            "The prototype must swing on a door contact even when sliding made progress");
}

void physicalSwingsPushDoor() {
    auto model = viewer::test::doorway(false, 1.6f);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    viewer::PhysicsScene scene(physics, world, {-30, 0, -30}, {30, 3, 30});
    viewer::ZombieLayer zombies;
    zombies.attachPhysics(physics);
    zombies.assets.push_back(zombie_attack_fixture::humanoid());
    zombies.setPerceptionEnabled(true); zombies.setPerceptionStaggering(false);
    addZombie(zombies, {0, 0, .5f}, ZombieState::investigating, {0, 0, -4});
    viewer::ZombiePlayer player{{0, 0, -5}, 1.7f, 1.8f, 7};
    doors.activateNear({0, 0, 0});
    auto& clock = viewer::SimulationClock::instance(); clock.reset();
    bool physicalAttack = false;
    bool impactNoise = false;
    float maximumAngle = 0;
    for (int tick = 0; tick < 180; ++tick) {
        clock.advance(dt); zombies.updateBehavior(dt, player, world, clock.seconds());
        const auto& zombie = zombies.instances[0];
        physicalAttack |= zombie.attackClip && zombie.activeRagdoll && !zombie.ragdoll;
        zombies.prepareAttackPhysics(scene);
        physics->step(dt);
        for (const auto& impact : physics->impacts().take())
            impactNoise |= impact.audience == viewer::NoiseAudience::playersOnly;
        zombies.synchronizePhysics();
        maximumAngle = std::max(maximumAngle, std::abs(doors.angle(0)));
    }
    require(physicalAttack, "A door-contact swing must activate the existing living physical attack rig");
    require(maximumAngle > .02f, "A physical door swing failed to transfer force to the shared door body");
    require(impactNoise,"Physical zombie strikes on a door must produce player-only impact noise");
}

void sustainedWalkingPressure() {
    // The longer approach lets the door-only proxy sleep before reaching the
    // panel. Accepted movement must wake it and sustain the same physical load.
    for (const float start : {.5f, 1.2f}) {
        DoorScene scene;
        scene.zombies.attachPhysics(scene.physics);
        scene.zombies.assets[0].attackLeftClip.reset();
        scene.zombies.assets[0].attackRightClip.reset();
        scene.player.feet.z = -5;
        addZombie(scene.zombies, {0, 0, start}, ZombieState::investigating, {0, 0, -4});
        viewer::ZombieDoorCollision blockers(scene.physics);
        scene.doors.activateNear({0, 0, 0});
        double brokenAt = -1;
        for (int tick = 0; tick < 40 * 60; ++tick) {
            scene.step();
            scene.physics->step(dt, [&](float) { blockers.prepare(scene.doors, scene.zombies); });
            require(!scene.zombies.instances[0].attackClip,
                    "The walking-pressure fixture must not gain help from melee attacks");
            if (scene.doors.restraint(0).broken) { brokenAt = (tick + 1) * double(dt); break; }
        }
        require(brokenAt >= 27 && brokenAt <= 35,
                "A sustained walking push must take roughly thirty seconds to release the restraint");
    }
}

void onlyDoorContactsTrigger() {
    for (const auto state : {ZombieState::idle, ZombieState::hiding, ZombieState::midnight}) {
        DoorScene scene;
        addZombie(scene.zombies, {0, 0, .31f}, state, {0, 0, -4});
        for (int tick = 0; tick < 120; ++tick) {
            scene.step();
            require(!scene.zombies.instances[0].attackClip,
                    "An idle, hidden or midnight zombie must not start a door attack");
        }
    }
    {
        DoorScene scene(false);
        addZombie(scene.zombies, {0, 0, .5f}, ZombieState::investigating, {0, 0, -4});
        for (int tick = 0; tick < 180; ++tick) {
            scene.step();
            require(!scene.zombies.instances[0].attackClip, "A generic wall contact started a door attack");
        }
        require(scene.zombies.instances[0].restRoot[3].z > 0,
                "The wall fixture must actually obstruct the attempted movement");
    }
    {
        DoorScene scene;
        addZombie(scene.zombies, {0, 0, .95f}, ZombieState::investigating, {0, 0, -4});
        addZombie(scene.zombies, {0, 0, .31f}, ZombieState::idle, {});
        for (int tick = 0; tick < 120; ++tick) {
            scene.step();
            require(!scene.zombies.instances[0].attackClip,
                    "Waiting behind another zombie without touching a panel started a door attack");
        }
    }
}

void houseRouteSurvivesDoorSwing() {
    auto model = house_fixture::house();
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, world);
    doors.setAngle(1, .7f);
    floors.update({{0, 1.7f, 8}});
    const auto& plan = floors.resident().begin()->second;
    const auto* graph = floors.navigationGraph(plan.name);
    glm::vec3 from{0}, goal{0};
    bool selected = false;
    for (const auto& source : graph->nodes()) {
        if (source.kind != viewer::HouseNavigationGraph::Kind::RoomCenter || source.storey != 0) continue;
        for (const auto& destination : graph->nodes()) {
            if (destination.kind != viewer::HouseNavigationGraph::Kind::RoomCenter || destination.storey != 0 ||
                source.point == destination.point) continue;
            const auto route = graph->route(source.point, destination.point);
            for (const auto& waypoint : route.points) if (waypoint.door) {
                from = source.point; goal = destination.point; selected = true; break;
            }
            if (selected) break;
        }
        if (selected) break;
    }
    require(selected, "The house fixture must offer an investigation route through an interior door");
    for (std::size_t id = 0; id < doors.count(); ++id) if (doors.present(id)) doors.setAngle(id, 0);

    viewer::ZombieLayer zombies;
    zombies.assets.push_back(meleeAsset());
    zombies.setPerceptionEnabled(true); zombies.setPerceptionStaggering(false);
    zombies.setHouseNavigation(&floors);
    const auto sound = goal + glm::vec3(0, 1.65f, 0);
    addZombie(zombies, from, ZombieState::investigating, sound);
    viewer::ZombiePlayer player{{300, 0, 300}}; player.house = plan.name;
    auto& clock = viewer::SimulationClock::instance(); clock.reset();
    bool recovered = false, finished = false;
    unsigned previousClip = 0;
    for (int tick = 0; tick < 2400 && !finished; ++tick) {
        clock.advance(dt); zombies.updateBehavior(dt, player, world, clock.seconds());
        const auto* behavior = zombies.behaviors.find(0);
        require(behavior && behavior->state == ZombieState::houseInvestigating && behavior->target == sound,
                "Door attacks must preserve the house investigation route and heard room target");
        const auto& zombie = zombies.instances[0];
        recovered |= zombie.attackClip && zombie.attackSeconds > .6;
        finished |= previousClip && !zombie.attackClip;
        previousClip = zombie.attackClip;
    }
    require(recovered && finished, "The house investigator did not complete a door swing");

    for (std::size_t id = 0; id < doors.count(); ++id) if (doors.present(id))
        doors.setAngle(id, doors.panel(id).inward ? -1.5707963f : 1.5707963f);
    for (int tick = 0; tick < 6000 && zombies.behaviors.contains(0); ++tick) {
        clock.advance(dt); zombies.updateBehavior(dt, player, world, clock.seconds());
    }
    require(glm::distance(glm::vec3(zombies.instances[0].restRoot[3]), goal) < .3f,
            "After a door opens, house investigation must complete its original room route");
}
} // namespace

int main() {
    try {
        repeatedDoorSwings(ZombieState::chasing, false);
        repeatedDoorSwings(ZombieState::investigating, false);
        repeatedDoorSwings(ZombieState::investigating, true);
        glancingContactStillTriggers();
        physicalSwingsPushDoor();
        sustainedWalkingPressure();
        onlyDoorContactsTrigger();
        houseRouteSurvivesDoorSwing();
        std::cout << "Zombie door-contact attacks, cooldowns, player shielding and investigation resumption passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
