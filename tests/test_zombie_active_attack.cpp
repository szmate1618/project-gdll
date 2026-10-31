#include "zombie_active_attack_fixture.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "simulation_clock.hpp"
#include "zombie_animation.hpp"
#include "interior_fixture.hpp"
#include <box3d/box3d.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::Model ground() {
    viewer::Model model; model.materials.push_back({});
    interior_fixture::quad(model, {-5, 0, -5}, {-5, 0, 5}, {5, 0, 5}, {5, 0, -5}, "terrain");
    interior_fixture::bounds(model); return model;
}
struct Fixture {
    viewer::Model model = ground();
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::CollisionWorld collision{model, {}};
    viewer::PhysicsScene scene{physics, collision, model.boundsMin, model.boundsMax};
    viewer::ZombieLayer zombies;
    viewer::ZombiePlayer player{{0, 0, .85f}, 1.7f, 1.8f, 7};
    int hits = 0;
    Fixture() {
        viewer::SimulationClock::instance().reset();
        zombies.attachPhysics(physics); zombies.assets.push_back(zombie_attack_fixture::humanoid());
        zombies.instances.emplace_back(); zombies.setPerceptionEnabled(true); zombies.setPerceptionStaggering(false);
        zombies.rebuildSpatialIndex();
        auto& behavior = zombies.behaviors.create(0, 0, {});
        behavior.state = viewer::ZombieState::chasing;
    }
    void step() {
        auto& clock = viewer::SimulationClock::instance(); clock.advance(1./60);
        zombies.updateBehavior(1.f/60, player, collision, clock.seconds());
        for (const auto& hit : zombies.attackHits()) { require(hit.player == 7 && hit.damage == 10, "Incorrect physical damage"); ++hits; }
        zombies.prepareAttackPhysics(scene); physics->step(1.f/60); zombies.synchronizePhysics();
    }
    glm::vec3 hand() const {
        const auto& z = zombies.instances[0]; const auto& a = zombies.assets[0];
        return glm::vec3(z.transform * z.ragdollBones.at(11) * glm::inverse(a.skeleton[11].inverseBind)[3]);
    }
};
void movingTargetsAndLifecycle() {
    Fixture f;
    f.step();
    const auto& z = f.zombies.instances[0];
    require(z.activeRagdoll && !z.ragdoll && z.physicsRagdoll && f.zombies.ragdollCount() == 0,
        "An attack must remain living and own a temporary physics rig");
    require(f.zombies.behaviors.contains(0), "Physical activation removed the living behavior");
    const auto first = f.hand();
    float farthest = first.z;
    for (int tick = 1; tick < 59; ++tick) {
        f.step();
        require(!z.ragdoll, "An attacking zombie died during its own swing");
        if (z.activeRagdoll) {
            const auto hand = f.hand(); farthest = std::max(farthest, hand.z);
            require(std::isfinite(glm::length(hand)) && glm::length(glm::vec3(z.restRoot[3])) < .001f,
                "The supported attack moved its root or produced a non-finite pose");
            const auto pose = viewer::sampleZombiePose(f.zombies.assets[0], z, 0);
            require(glm::distance(glm::vec3(z.restTransform * pose[11][3]), hand) < .0001f,
                "Shooting sampled an authored pose instead of the solved hand");
        }
    }
    require(farthest > first.z + .4f, "Moving animation targets failed to drive the dynamic arm");
    require(f.hits == 1, "A physical swipe must injure exactly once");
    f.zombies.setPerceptionEnabled(false);
    require(!z.activeRagdoll && !z.physicsRagdoll && !z.ragdoll && z.ragdollBones.empty(),
        "Cancelling an attack leaked its pose or changed the zombie into a corpse");
    const auto released = b3World_GetCounters(f.physics->id()).bodyCount;
    require(released <= 2, "Cancelling an attack leaked physical bodies/floor");
    f.zombies.setPerceptionEnabled(true);
    bool restarted = false;
    for (int i = 0; i < 150; ++i) { f.step(); restarted |= z.activeRagdoll && z.attackRevision > 1; }
    require(restarted && f.hits > 1, "A living attacker could not resume repeated attacks");
}
void physicalObstacleAndDeath() {
    Fixture f;
    auto body = b3DefaultBodyDef(); body.position = {0, 1.4f, .45f};
    const auto wall = b3CreateBody(f.physics->id(), &body);
    auto shape = b3DefaultShapeDef(); shape.filter.maskBits = viewer::zombieAttackBodyCategory;
    const auto box = b3MakeBoxHull(1, .7f, .06f); b3CreateHullShape(wall, &shape, &box.base);
    // This blocker exists only in Box3D. Authored-path hit traces cannot see it.
    for (int i = 0; i < 55; ++i) f.step();
    require(f.hits == 0, "Damage followed the authored hand through a physical blocker");
    const auto& z = f.zombies.instances[0];
    require(z.activeRagdoll, "Physical obstruction destroyed the living attack");
    const auto before = f.hand(); const auto handle = z.ragdollHandle;
    require(f.zombies.knockDown(0, {}, 55./60), "An attacking zombie could not be killed");
    require(z.ragdoll && !z.activeRagdoll && z.ragdollHandle == handle && f.zombies.ragdollCount() == 1,
        "Death failed to release and retain the active physical rig");
    require(glm::distance(before, f.hand()) < .0002f, "Death snapped away from the solved attack pose");
    require(!f.zombies.behaviors.contains(0), "A dead active attacker retained its behavior");
    b3DestroyBody(wall);
}
void impulsesDeflectLivingArms() {
    Fixture reference, struck;
    for (int tick = 0; tick < 20; ++tick) { reference.step(); struck.step(); }
    const auto point = struck.hand();
    require(struck.zombies.meleeHit(0, {0, 0, -8}, point, 20./60), "Living attack rejected a physical impulse");
    reference.step(); struck.step();
    require(glm::distance(reference.hand(), struck.hand()) > .005f,
        "A driven arm ignored a physical impulse and snapped directly to animation");
    require(struck.zombies.instances[0].activeRagdoll && !struck.zombies.instances[0].ragdoll,
        "Deflecting an attack arm killed the living zombie");
}
void preparedAssets(const std::filesystem::path& directory) {
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".glb") continue;
        Fixture f;
        auto loaded = viewer::loadZombieLayer(entry.path(), {glm::mat4(1)});
        loaded.attachPhysics(f.physics); loaded.setPerceptionEnabled(true); loaded.setPerceptionStaggering(false);
        loaded.releaseAnimationVertices();
        f.zombies = std::move(loaded);
        f.player.feet.z = 1;
        auto& behavior = f.zombies.behaviors.create(0, 0, {});
        behavior.state = viewer::ZombieState::chasing;
        bool left = false, right = false;
        for (int tick = 0; tick < 380; ++tick) {
            f.step(); const auto& z = f.zombies.instances[0];
            left |= z.activeRagdoll && z.attackClip == 1;
            right |= z.activeRagdoll && z.attackClip == 2;
        }
        if (!left || !right || f.hits < 2)
            throw std::runtime_error("Prepared physical attacks failed: " + entry.path().string() +
                " hits=" + std::to_string(f.hits));
        std::cout << entry.path().filename().string() << ": both physical attacks hit\n";
    }
}
}
int main(int argc, char** argv) {
    try { movingTargetsAndLifecycle(); physicalObstacleAndDeath(); impulsesDeflectLivingArms();
        if (argc > 1) preparedAssets(argv[1]);
        std::cout << "Living active ragdoll drives, physical contacts, damage, cancellation and death passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
