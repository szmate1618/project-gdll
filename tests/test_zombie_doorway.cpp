#include "zombie_doorway_fixture.hpp"
#include "zombie_doorway.hpp"
#include "zombie_chase.hpp"
#include "zombie_layer.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

struct Scene {
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics;
    viewer::DoorLayer doors;
    viewer::CollisionWorld world;
    glm::mat4 rotation;

    static viewer::Model geometry(bool outward, float width, float yaw) {
        auto model = viewer::test::doorway(outward, width);
        const auto rotation = glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0));
        for (auto& draw : model.draws) draw.transform = rotation * draw.transform;
        return model;
    }
    Scene(bool outward = true, float width = 1, float yaw = 0)
        : model(geometry(outward, width, yaw)), physics(std::make_shared<viewer::PhysicsWorld>()),
          doors(viewer::prepareDoorPanels(model), physics), world(model, {}, &doors),
          rotation(glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0))) {
        doors.setAngle(0, glm::radians(90.f));
    }
    glm::vec3 point(glm::vec3 local) const { return glm::vec3(rotation * glm::vec4(local, 1)); }
};

void eligibilityAndLifecycle() {
    Scene scene;
    viewer::ZombieDoorwayGuidance guidance;
    const glm::vec3 target(0, 0, -8);
    guidance.beginStep(1.f / 60);
    const auto owner = guidance.destination(0, {0, 0, .5f}, target, scene.world);
    const auto waiter = guidance.destination(1, {.7f, 0, .5f}, target, scene.world);
    require(owner && owner->z < 0 && waiter && waiter->z > 1 && waiter->x > .7f,
            "An aligned winner must cross while a side-by-side follower backs away from the opening");
    guidance.erase(0);
    const auto promoted = guidance.destination(1, {.7f, 0, .5f}, target, scene.world);
    require(promoted && std::abs(promoted->x) < 1e-5f,
            "Removing the winner must immediately release its passage reservation");

    guidance.reset();
    guidance.beginStep(1.f / 60);
    guidance.destination(0, {0, 0, .5f}, target, scene.world);
    guidance.destination(1, {.7f, 0, .5f}, target, scene.world);
    for (int step = 0; step < 2; ++step) {
        guidance.beginStep(1.f / 60);
        guidance.destination(1, {.7f, 0, .5f}, target, scene.world);
    }
    const auto staleRelease = guidance.destination(1, {.7f, 0, .5f}, target, scene.world);
    require(staleRelease && std::abs(staleRelease->x) < 1e-5f,
            "A winner that stops requesting movement must not leave a stale reservation");

    guidance.reset();
    bool yielded = false;
    for (int step = 0; step < 150; ++step) {
        guidance.beginStep(1.f / 60);
        const auto blocked = guidance.destination(0, {0, 0, .5f}, target, scene.world);
        const auto next = guidance.destination(1, {.7f, 0, .5f}, target, scene.world);
        yielded = yielded || (blocked && std::abs(blocked->x) > .5f && next && std::abs(next->x) < 1e-5f);
    }
    require(yielded, "A stalled reservation must yield to another waiting actor");
    require(!guidance.destination(0, {0, 0, .5f}, {0, 0, 8}, scene.world),
            "Changing the remembered goal to the same side must cancel doorway guidance");
    scene.doors.setAngle(0, 0);
    const auto closing = guidance.destination(1, {.7f, 0, .5f}, target, scene.world);
    require(closing && closing->z > 1.5f && std::abs(closing->x) > .7f,
            "A closing door must release passage priority and keep its queue outside the swing");
    scene.doors.setAngle(0, glm::radians(90.f));
    require(!guidance.destination(2, {0, 3, .5f}, {0, 3, -8}, scene.world),
            "An opening on another floor must not attract a pursuer");
    require(!guidance.destination(2, {3, 0, .5f}, {3, 0, -8}, scene.world),
            "Nearby doors must not redirect unrelated approaches through solid wall sections");
    Scene narrow(true, .6f);
    require(!guidance.destination(2, {0, 0, .5f}, target, narrow.world),
            "A door narrower than the body and wall clearances must not admit a passage request");
    require(!guidance.destination(2, {0, 0, .5f}, {0, 0, std::numeric_limits<float>::quiet_NaN()}, scene.world),
            "An invalid remembered target must not acquire a valid doorway waypoint");
}

std::vector<glm::vec3> sideBySide(bool outward, float yaw, bool reversePool) {
    Scene scene(outward, 1.f, yaw);
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    const auto target = scene.point({.3f, 0, -60});
    for (int row = 0; row < 3; ++row) for (const float x : {-.35f, .35f}) {
        viewer::ZombieInstance instance;
        const auto feet = scene.point({x, 0, 1.4f + row * .75f});
        instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), yaw + glm::radians(180.f), {0, 1, 0});
        instance.transform = instance.restTransform = instance.restRoot;
        layer.instances.push_back(instance);
    }
    for (std::size_t index = 0; index < layer.instances.size(); ++index) {
        const auto id = reversePool ? layer.instances.size() - 1 - index : index;
        auto& behavior = layer.behaviors.create(id, 0, glm::vec3(layer.instances[id].restRoot[3]));
        behavior.state = viewer::ZombieState::chasing;
        behavior.target = target;
    }
    layer.rebuildSpatialIndex();
    constexpr float dt = 1.f / 60;
    std::vector<glm::vec3> replay;
    std::size_t entered = 0;
    for (int step = 0; step < 900 && entered < layer.instances.size(); ++step) {
        std::vector<glm::vec3> previous;
        for (const auto& instance : layer.instances) previous.emplace_back(instance.restRoot[3]);
        clock.advance(dt);
        // Suspend perception so only the remembered goal drives movement.
        layer.chase(dt, scene.point({300, 0, 300}), scene.world);
        require(layer.behaviorStats().sightRays == 0 && layer.behaviorStats().attackRays == 0,
                "Doorway guidance must not introduce sight rays or track a distant player's position");
        entered = 0;
        for (std::size_t id = 0; id < layer.instances.size(); ++id) {
            const auto feet = glm::vec3(layer.instances[id].restRoot[3]);
            const auto local = glm::vec3(glm::inverse(scene.rotation) * glm::vec4(feet, 1));
            entered += local.z < -1.5f;
            replay.push_back(feet);
            require(glm::length(feet - previous[id]) <= 3 * dt + .0002f,
                    "Queue yielding and passage must preserve the running speed limit");
            const auto* behavior = layer.behaviors.find(id);
            require(behavior && behavior->target == target && behavior->state == viewer::ZombieState::chasing,
                    "Doorway waypoints must leave the remembered target and chasing state unchanged");
            for (const auto& contact : scene.world.contacts(feet, .3f, 1.8f, 0, .05f))
                require(contact.terrainSupport || contact.depth <= .001f || contact.normal.y > .7f,
                        "Doorway guidance must not pass through a panel or jamb");
            for (std::size_t other = 0; other < id; ++other)
                require(glm::length(feet - glm::vec3(layer.instances[other].restRoot[3])) >= .5998f,
                        "Side-by-side followers must yield without overlapping each other");
        }
    }
    std::cout << "Doorway side-by-side: outward=" << outward << ", yaw=" << yaw << ", entered=" << entered << "/6\n";
    if (entered != layer.instances.size()) {
        for (const auto& instance : layer.instances) {
            const auto local = glm::vec3(glm::inverse(scene.rotation) * instance.restRoot[3]);
            std::cout << "  position " << local.x << ", " << local.y << ", " << local.z << '\n';
        }
    }
    require(entered == layer.instances.size(), "All side-by-side pursuers must clear a fully open doorway");
    return replay;
}

void arrivalUsesOriginalTarget() {
    Scene scene;
    viewer::ZombieCrowd crowd;
    crowd.reset(1);
    viewer::ZombieInstance instance;
    instance.restRoot = glm::translate(glm::mat4(1), {0, 0, .5f});
    instance.transform = instance.restTransform = instance.restRoot;
    const viewer::AnimatedModel asset;
    const glm::vec3 target(0, 0, -.2f);
    bool arrived = false;
    for (int step = 0; step < 120 && !arrived; ++step) {
        crowd.beginStep(1.f / 60);
        crowd.update(0, glm::vec3(instance.restRoot[3]));
        arrived = viewer::moveZombieToward(instance, asset, target, 3, 1.f / 60, scene.world, 0, &crowd, 0);
        if (arrived)
            require(glm::length(glm::vec3(instance.restRoot[3]) - target) < 1e-4f,
                    "Reaching a doorway hint must never report arrival at the remembered target");
    }
    require(arrived && glm::length(glm::vec3(instance.restRoot[3]) - target) < 1e-4f,
            "An exit hint must not overshoot a remembered goal just beyond the threshold");
}

void gridRelocationAndRemoval() {
    Scene scene;
    viewer::ZombieCrowd crowd;
    crowd.reset(2);
    crowd.beginStep(1.f / 60);
    crowd.update(0, {0, 0, .5f});
    crowd.update(1, {.7f, 0, .5f});
    const glm::vec3 target(0, 0, -8);
    crowd.doorwayDestination(0, target, scene.world);
    crowd.doorwayDestination(1, target, scene.world);
    crowd.update(0, {0, 0, -.2f}); // Cross both the door plane and the grid-cell boundary.
    const auto crossing = crowd.doorwayDestination(0, target, scene.world);
    require(crossing && crossing->z < -.8f,
            "Grid relocation must retain the winner's entry side until its body clears the door");
    auto copied = crowd;
    crowd.erase(0);
    const auto next = crowd.doorwayDestination(1, target, scene.world);
    const auto stillWaiting = copied.doorwayDestination(1, target, scene.world);
    require(next && std::abs(next->x) < 1e-5f && stillWaiting && stillWaiting->x > .7f,
            "Body removal must release passage immediately without changing a copied crowd's reservation");
}
}

int main() {
    try {
        eligibilityAndLifecycle();
        arrivalUsesOriginalTarget();
        gridRelocationAndRemoval();
        for (const bool outward : {true, false}) for (const float yaw : {0.f, .73f}) {
            const auto first = sideBySide(outward, yaw, false);
            require(first == sideBySide(outward, yaw, true),
                    "Doorway passage must replay independently of behavior-pool insertion order");
        }
        std::cout << "Zombie doorway guidance tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie doorway guidance failed: " << error.what() << '\n';
        return 1;
    }
}
