#include "grenades.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "box_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "interior_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
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
        collisionAndFuse(); propCollisionAndBlast(); zombieBlast();
        std::cout << "Grenade CCD, wall/floor/prop collision, fuse, blast cover and light tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
