#include "sleep_watch.hpp"
#include "sleep_plan.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void addAsset(viewer::ZombieLayer& zombies) {
    viewer::AnimatedModel asset;
    viewer::SkeletonBone head; head.name = "head";
    head.referenceWorld = glm::translate(glm::mat4(1), {0,1.7f,0});
    asset.skeleton.push_back(head);
    asset.ragdollBones[std::size_t(viewer::RagdollPart::head)] = 0;
    zombies.assets.push_back(std::move(asset));
}
void addZombie(viewer::ZombieLayer& zombies, glm::vec3 feet, glm::vec3 velocity) {
    viewer::ZombieInstance instance;
    instance.restRoot = instance.restTransform = instance.transform = glm::translate(glm::mat4(1), feet);
    instance.locomotionVelocity = velocity;
    zombies.instances.push_back(std::move(instance));
}
viewer::Model wall(float height) {
    viewer::Model model;
    viewer::Primitive primitive;
    for (const auto p : {glm::vec3(-100,0,-5), glm::vec3(100,0,-5),
        glm::vec3(100,height,-5), glm::vec3(-100,height,-5)}) {
        viewer::Vertex vertex; vertex.position = p; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0,1,2,0,2,3};
    model.primitives.push_back(std::move(primitive));
    model.draws.push_back({0,glm::mat4(1),"wall"});
    return model;
}
void rangeAndDirection() {
    using namespace viewer;
    require(sleepGuardSightRange(12) == 100 && sleepGuardSightRange(0) == 50, "Day/night guard range");
    require(sleepGuardSightRange(6) > 50 && sleepGuardSightRange(6) < 100, "Twilight must blend sight range");
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 16; ++i) {
        const double angle = i * pi / 8;
        require(sleepGuardBearing({float(std::sin(angle)),0,float(-std::cos(angle))}) == i,
            "Compass sectors must match X-east/Z-south coordinates");
    }
    require(std::string(sleepGuardDirection(1)) == "north-northeast", "Named intermediate compass direction");
    const auto north = sleepGuardLookAngles({0,1.7f,0}, {0,1.7f,-10});
    require(std::abs(north.x + 90) < .001f && north.y == 0, "Wake view must face north");
}
void sightAndCadence() {
    using namespace viewer;
    const CollisionWorld clear(Model{}), blocked(wall(4)), lowWall(wall(1.6f));
    ZombieLayer zombies; addAsset(zombies); addZombie(zombies, {0,0,-75}, {0,0,1}); zombies.rebuildSpatialIndex();
    SleepGuardWatch watch;
    const auto scan = [&](double time, double hour, const CollisionWorld& world) {
        return watch.update(zombies, world, {0,0,0}, {0,1.7f,0}, time, hour);
    };
    require(!scan(0,0,clear), "A 75-metre threat must be out of night range");
    require(!scan(4.999,12,clear), "Guard observed again before five simulated seconds");
    require(scan(5,12,clear).has_value(), "Approaching visible daytime threat was missed");
    watch.reset();
    require(!scan(6,12,blocked) && watch.visibilityChecks() == 1, "A wall must block the head ray");
    watch.reset();
    zombies.instances[0].locomotionVelocity = {0,0,-1};
    require(!scan(7,12,clear), "A retreating zombie must not trigger a guard alert");
    watch.reset(); zombies.instances[0].locomotionVelocity = {1,0,0};
    require(!scan(8,12,clear), "A zombie passing across the guard must not trigger an alert");
    watch.reset(); zombies.instances[0].locomotionVelocity = {};
    require(!scan(9,12,clear), "A stationary zombie must not trigger an approaching alert");
    watch.reset(); zombies.instances[0].locomotionVelocity = {.5f,0,1};
    require(scan(10,12,clear).has_value(), "A roughly incoming velocity should count as approaching");

    // Rendering stopped on a low head pose; authority has advanced to a head
    // visible above the low wall. The watch must use the new simulation pose.
    auto& asset = zombies.assets[0]; asset.frameCount = 2; asset.duration = 2;
    asset.boneFrames = {glm::translate(glm::mat4(1), {0,.5f,0}), glm::translate(glm::mat4(1), {0,2,0})};
    zombies.instances[0].displayedAnimationFrame = 0;
    watch.reset();
    const auto sight = scan(1,12,lowWall);
    require(sight && sight->head.y > 1.9f, "Guard used a stale displayed pose instead of the current head");
}
void nearestTwenty() {
    using namespace viewer;
    const CollisionWorld blocked(wall(4));
    ZombieLayer zombies; addAsset(zombies);
    // Insert the farther, visible actor first, so ID order cannot substitute
    // for actual nearest-twenty selection.
    addZombie(zombies, {30,0,0}, {-1,0,0});
    for (int i = 0; i < 20; ++i) addZombie(zombies, {0,0,float(-10-i)}, {0,0,1});
    zombies.rebuildSpatialIndex();
    SleepGuardWatch watch;
    require(!watch.update(zombies, blocked, {}, {0,1.7f,0}, 0, 12) && watch.visibilityChecks() == 20,
        "Guard must stop after the nearest twenty head visibility checks");
    zombies.instances[1].ragdoll = true; zombies.rebuildSpatialIndex(); watch.reset();
    const auto sight = watch.update(zombies, blocked, {}, {0,1.7f,0}, 1, 12);
    require(sight && sight->zombie == 0 && watch.visibilityChecks() == 20,
        "Dead zombies must not occupy a nearest-living scan slot");
}
}
int main() {
    try {
        rangeAndDirection(); sightAndCadence(); nearestTwenty();
        std::cout << "Automatic guard watch checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
