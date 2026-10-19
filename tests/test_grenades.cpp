#include "grenades.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "box_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "interior_fixture.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::Model room() {
    viewer::Model model;
    model.materials.push_back({});
    interior_fixture::quad(model, {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30}, "terrain");
    interior_fixture::quad(model, {-10, 0, -3}, {10, 0, -3}, {10, 5, -3}, {-10, 5, -3}, "wall");
    interior_fixture::bounds(model);
    return model;
}
struct Fixture {
    viewer::Model model = room();
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes{physics};
    viewer::DoorLayer doors{viewer::prepareDoorPanels(model), physics};
    viewer::CollisionWorld world{model, {}, &doors};
    viewer::PhysicsScene scene{physics, world, model.boundsMin, model.boundsMax};
    viewer::Grenades grenades{physics};
    Fixture() { world.setBoxes(&boxes); }
    void step(double now) {
        scene.prepare(boxes); grenades.prepare(scene, doors);
        physics->step(1.0f / 60); grenades.update(now, world);
    }
};

void collisionAndFuse() {
    Fixture f;
    require(!f.grenades.canThrow({0, 1, -2.99f}, f.world), "Cannot spawn a grenade intersecting a nearby wall");
    require(f.grenades.throwFrom({0, 1.7f, 0}, {0, 0, -1}, {0, 0, 0}, 0, {3, 8, 30}, f.world), "Throw must create a body");
    bool bounced = false;
    float previousZ = 0;
    for (int i = 1; i <= 150; ++i) {
        f.step(i / 60.0);
        const auto p = f.grenades.poses().at(0).center;
        require(p.z > -3 && p.y > .02f, "Fast grenade must collide with the streamed wall and floor");
        bounced |= previousZ < -2 && p.z > previousZ;
        previousZ = p.z;
    }
    require(bounced, "Grenade must bounce off the wall using shared rigid-body physics");
    f.grenades.update(2.999, f.world);
    require(f.grenades.poses().size() == 1 && f.grenades.explosions(2.999).empty(), "Fuse must not detonate early");
    f.grenades.update(3, f.world);
    require(f.grenades.poses().empty() && f.grenades.explosions(3).size() == 1, "Fuse removes the body and publishes one explosion");
    const auto light = f.grenades.lights(3);
    require(light.size() == 1 && light[0].color.r > 20 && light[0].range == 12, "Explosion reuses a stronger fading point light");
    require(f.grenades.lights(3.1).at(0).color.r < light[0].color.r && f.grenades.lights(3.2).empty(), "Explosion light fades to zero");
    f.grenades.update(3.1, f.world);
    require(f.grenades.explosions(3.1).size() == 1, "Expired grenades cannot explode twice");
    f.grenades.update(4.1, f.world);
    require(f.grenades.explosions(4.1).empty(), "Old explosion events must be retired");
}

void propCollisionAndBlast() {
    Fixture f;
    // An 8 kg prop moves visibly; a 55 kg crate mostly reflects the 450 g grenade.
    f.boxes.add({0, .2f, 0}, {.4f, .4f, .4f});
    require(f.grenades.throwFrom({0, .15f, 2}, {0, 0, -1}, {0, 0, 0}, 0, {3, 8, 14}, f.world), "Throw toward box");
    for (int i = 1; i < 45; ++i) f.step(i / 60.0);
    require(f.boxes.pose(0).center.z < -.005f, "Grenade must transfer collision momentum to an existing box");

    Fixture blast;
    const auto exposed = blast.boxes.add({2, .5f, 0}, {1, 1, 1});
    const auto shielded = blast.boxes.add({0, .5f, -4.5f}, {1, 1, 1});
    require(blast.grenades.throwFrom({0, 1, 0}, {1, 0, 0}, {0, 0, 0}, 0, {.2f, 8, .01f}, blast.world), "Arm blast fixture");
    blast.grenades.update(.21, blast.world);
    require(blast.boxes.velocity(exposed).x > 1, "An exposed prop receives an outward explosion impulse");
    require(glm::length(blast.boxes.velocity(shielded)) < .001f, "A wall must shield props from blast impulses");
}

void treeRebounds() {
    for (const float speed : {14.0f, 30.0f}) for (const float height : {1.3f, 8.0f}) {
        auto model = room();
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
        viewer::CollisionWorld world(model, {{{0, 0, -1.5f}, .25f, 12}, {{50, 0, 50}, .3f, 10}});
        viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
        viewer::Grenades grenade(physics);
        require(grenade.throwFrom({0, height, 0}, {0, 0, -1}, {0, 0, 0}, 0, {3, 8, speed}, world), "Throw toward trunk");
        scene.ensureNear(model.boundsMin, model.boundsMax);
        const auto before = b3World_GetCounters(physics->id()).bodyCount;
        float previousZ = 0;
        bool bounced = false;
        for (int i = 1; i <= 30; ++i) {
            grenade.prepare(scene, doors); physics->step(1.0f / 60);
            grenade.update(i / 60.0, world);
            const auto p = grenade.poses().at(0).center;
            require(p.z > -1.5f, "Fast grenade must not tunnel through the trunk");
            if (speed == 14) require(p.z > -1.25f, "Normal-speed throw must remain outside the trunk");
            bounced |= previousZ < -.8f && p.z > previousZ;
            previousZ = p.z;
        }
        require(bounced, "Grenade must rebound from the trunk's rounded collider");
        require(b3World_GetCounters(physics->id()).bodyCount == before, "Tree rebounds must not create any Box3D trunk bodies");

        viewer::Grenades miss(physics);
        require(miss.throwFrom({1, height, 0}, {0, 0, -1}, {0, 0, 0}, 0, {3, 8, 14}, world), "Throw beside trunk");
        for (int i = 0; i < 9; ++i) {
            miss.prepare(scene, doors); physics->step(1.0f / 60);
            miss.update((i + 31) / 60.0, world);
        }
        require(miss.poses().at(0).center.z < -1.6f, "A throw beside the trunk must stay clear of the rendering-only canopy");
    }
}

void treeReflection() {
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    const viewer::CollisionWorld world({}, {{{0, 0, 0}, .25f, 12}});
    viewer::Grenades grenade(physics);
    require(grenade.throwFrom({.5f, 1.3f, 0}, {-1, 0, 0}, {}, 0, {3, 8, 14}, world), "Set up reflection fixture");
    b3BodyId body{};
    auto filter = b3DefaultQueryFilter(); filter.maskBits = viewer::grenadeBodyCategory;
    b3World_OverlapAABB(physics->id(), {{-1, 0, -1}, {1, 2, 1}}, filter,
        [](b3ShapeId shape, void* context) {
            *static_cast<b3BodyId*>(context) = b3Shape_GetBody(shape); return false;
        }, &body);
    require(b3Body_IsValid(body), "Reflection fixture must find the live grenade body");
    b3Body_SetTransform(body, {.3f, 1.3f, 0}, b3Quat_identity);
    b3Body_SetLinearVelocity(body, {-10, 3, 7});
    grenade.update(.1, world);
    auto velocity = b3Body_GetLinearVelocity(body);
    require(std::abs(velocity.x - 3.5f) < 1e-5f && velocity.y == 3 && velocity.z == 7,
            "A side impact must reverse/dampen inward speed while preserving vertical and tangent speed");
    require(b3Body_GetPosition(body).x > .34f, "Reflection must separate the grenade from the trunk");
    // Already separating overlaps must be moved out without reversing again.
    b3Body_SetTransform(body, {.3f, 1.3f, 0}, b3Quat_identity);
    grenade.update(.2, world);
    require(b3Body_GetLinearVelocity(body).x == velocity.x, "Separating grenades must not rebound back into the trunk");
    b3Body_SetTransform(body, {0, 12.05f, 0}, b3Quat_identity);
    b3Body_SetLinearVelocity(body, {2, -10, 3});
    grenade.update(.3, world);
    velocity = b3Body_GetLinearVelocity(body);
    require(velocity.x == 2 && std::abs(velocity.y - 3.5f) < 1e-5f && velocity.z == 3,
            "A rounded trunk cap must rebound vertically with tangent speed preserved");
    require(b3Body_GetPosition(body).y > 12.09f, "Endcap reflection must clear the rounded trunk top");
}

void zombieBlast() {
    Fixture f;
    viewer::ZombieLayer zombies;
    zombies.attachPhysics(f.physics);
    zombies.assets.emplace_back();
    for (const auto feet : {glm::vec3(2, 0, 0), {7.99f, 0, 0}, {8.01f, 0, 0}, {0, 0, -4.5f}}) {
        viewer::ZombieInstance instance;
        instance.transform = instance.restRoot = instance.restTransform = glm::translate(glm::mat4(1), feet);
        zombies.instances.push_back(instance);
    }
    require(zombies.explode({0, .9f, 0}, 8, 0, f.world) == 2, "Blast kills exposed zombies inside its lethal radius");
    require(zombies.instances[0].ragdoll && zombies.instances[1].ragdoll &&
            !zombies.instances[2].ragdoll && !zombies.instances[3].ragdoll,
            "Range boundaries and wall cover must protect zombies");
    require(zombies.explode({0, .9f, 0}, 8, 0, f.world) == 0, "Dead zombies must not be activated twice");
}
}
int main() {
    try {
        collisionAndFuse(); propCollisionAndBlast(); treeRebounds(); treeReflection(); zombieBlast();
        std::cout << "Grenade CCD, wall/floor/prop/trunk collision, fuse, blast cover and light tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
