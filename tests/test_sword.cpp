#include "sword_combat.hpp"
#include "sword_geometry.hpp"
#include "inventory/item_config.hpp"
#include "zombie_layer.hpp"
#include "collision_world.hpp"
#include "sword_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void gestures() {
    for (const glm::vec2 drag : {glm::vec2(-40, 0), {40, 0}, {0, 40}, {0, -40},
            {-30, 30}, {30, 30}, {-30, -30}, {30, -30}}) {
        viewer::SwordMelee sword;
        sword.drag(drag); require(!sword.update(1), "Mouse look must not initiate a cut");
        sword.trigger(true); sword.drag(drag * .1f);
        require(!sword.update(1), "Small mouse jitter must not initiate a cut");
        sword.drag(drag);
        require(sword.update(1), "Held directional drag must initiate a cut");
        require(!sword.update(1.1), "Holding still must not repeat a cut");
        const auto direction = glm::normalize(drag);
        auto pose = sword.pose(1);
        const auto start = glm::vec3(viewer::swordTransform(pose) * glm::vec4(0, viewer::swordBladeTip, 0, 1));
        pose.elapsed = viewer::swordWindupSeconds + viewer::swordCutSeconds;
        const auto end = glm::vec3(viewer::swordTransform(pose) * glm::vec4(0, viewer::swordBladeTip, 0, 1));
        const glm::vec2 pivot(0, -.06f);
        require(glm::dot(glm::vec2(start) - pivot, direction) < -.8f,
                "Sword must snap to the side opposite the drag");
        require(glm::dot(glm::vec2(end) - pivot, direction) > .8f,
                "Sword must finish its cut in the drag direction");
        sword.trigger(false);
        require(sword.pose(1.2).elapsed > 0, "Release must allow the current cut to finish");
        sword.trigger(true); sword.drag(drag);
        require(!sword.update(1.3) && !sword.update(2), "Gestures during recovery must not queue a later cut");
        sword.trigger(false); sword.trigger(true); sword.drag(drag);
        require(sword.update(2), "A fresh press after recovery must allow another cut");
        sword.cancel(); sword.drag(drag);
        require(!sword.update(3) && sword.pose(3).elapsed < 0, "Cancellation must clear active and pending cuts");
        sword.trigger(true); sword.drag(drag);
        require(!sword.update(4), "Cancellation must require a physical release before rearming");
    }
}
void heldSwings() {
    for (const glm::vec2 drag : {glm::vec2(-40, 0), {40, 0}, {0, 40}, {0, -40},
            {-30, 30}, {30, 30}, {-30, -30}, {30, -30}}) {
        viewer::SwordMelee sword;
        sword.trigger(true); sword.drag(drag);
        require(sword.update(1), "The initial held drag must start a cut");
        const double ready = 1.0 + viewer::swordDurationSeconds;
        sword.drag(-drag);
        require(!sword.update(1.2), "Moving during a cut must not interrupt it");
        sword.drag(glm::normalize(drag) * 5.f);
        require(!sword.update(ready - .001) && !sword.update(ready + .001),
                "Recovery motion, including small movements, must not queue the next cut");
        require(!sword.update(2), "Holding still after recovery must not repeat a cut");
        // Repeated held input is how the multiplayer server samples a hold.
        sword.trigger(true);
        const auto nextDirection = -glm::normalize(drag);
        sword.drag(nextDirection * 6.f);
        require(!sword.update(2), "Each cut must require its own drag threshold");
        sword.drag(nextDirection * 8.f);
        require(sword.update(2) && glm::distance(sword.pose(2).direction, nextDirection) < 1e-6f,
                "A fresh reverse drag must start another cut without releasing the button");
        require(!sword.update(3), "A held gesture must be consumed exactly once");
        sword.drag(drag);
        require(sword.update(3), "Another fresh drag must allow a third cut in the same hold");
        sword.cancel(); sword.trigger(true); sword.drag(drag);
        require(!sword.update(4), "Cancellation must still suppress a held button until release");
        sword.trigger(false); sword.trigger(true); sword.drag(drag);
        require(sword.update(4), "A physical release must rearm a cancelled hold");
    }
}
void geometryAndItem() {
    const auto item = viewer::loadItem(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage/weapons", "sword");
    require(item.gameplay().melee && item.gameplay().ammoPerShot == 0 && item.hasTag("bladed"),
            "Sword must load as a bladed melee item without ammunition");
    const auto model = viewer::makeSwordModel();
    const auto& blade = model.primitives.at(0);
    require(blade.indices.size() == 60 && model.boundsMax.y > 1,
            "Blade must be a closed extruded narrow hexagon in metres");
    double volume = 0;
    for (std::size_t i = 0; i < blade.indices.size(); i += 3) {
        const auto a = blade.vertices.at(blade.indices[i]).position;
        const auto b = blade.vertices.at(blade.indices[i + 1]).position;
        const auto c = blade.vertices.at(blade.indices[i + 2]).position;
        require(glm::length(glm::cross(b - a, c - a)) > 1e-8f, "Sword must not contain degenerate faces");
        volume += glm::dot(glm::dvec3(a), glm::cross(glm::dvec3(b), glm::dvec3(c))) / 6;
    }
    require(volume > 0, "Blade must have outward winding");
}
void contacts() {
    const auto run = [](bool wall, bool far, bool cancel) {
        viewer::Model scene;
        if (wall) {
            viewer::Primitive p;
            for (const glm::vec3 point : {glm::vec3(-4, -1, -.6f), {4, -1, -.6f},
                    {4, 4, -.6f}, {-4, 4, -.6f}}) {
                viewer::Vertex v; v.position = point; p.vertices.push_back(v);
            }
            p.indices = {0, 1, 2, 0, 2, 3}; scene.primitives.push_back(p); scene.draws.emplace_back();
        }
        viewer::CollisionWorld world(scene);
        viewer::ZombieLayer zombies;
        viewer::AnimatedModel asset; asset.model.boundsMin = {-.3f, 0, -.3f}; asset.model.boundsMax = {.3f, 1.8f, .3f};
        zombies.assets.push_back(std::move(asset));
        viewer::ZombieInstance zombie;
        zombie.restRoot = zombie.restTransform = zombie.transform = glm::translate(glm::mat4(1), {0, 0, far ? -5.f : -1.2f});
        zombies.instances.push_back(zombie); zombies.rebuildSpatialIndex();
        viewer::Camera camera; camera.setPosition({0, 1.5f, 0}); camera.setOrientation(-90, 0);
        viewer::SwordMelee sword; viewer::SwordCombat combat;
        sword.trigger(true); sword.drag({-40, 0}); require(sword.update(1), "Test cut failed to start");
        const auto hp = [&] { const auto* injury = zombies.damage().injury(0); return injury ? injury->health : 1.f; };
        combat.update(sword, camera, 1.05, 1.05, world, &zombies, nullptr);
        require(hp() == 1.f, "Windup must not damage actors");
        if (cancel) sword.cancel();
        // One late update must consume the full finite cut, including a frame stall.
        combat.update(sword, camera, 2, 2, world, &zombies, nullptr);
        const auto health = hp();
        require((health < 1.f) == (!wall && !far && !cancel), "Sword reach, wall occlusion or cancellation failed");
        combat.update(sword, camera, 3, 3, world, &zombies, nullptr);
        require(hp() == health, "A cut must not damage the same actor twice");
    };
    run(false, false, false); run(true, false, false); run(false, true, false); run(false, false, true);
}

void bodyThenJoint() {
    const viewer::CollisionWorld world(viewer::Model{});
    for (const bool back : {false, true}) for (const bool fatal : {false, true}) {
        auto zombies = sword_fixture::population(back ? 180.f : 0.f);
        if (fatal) sword_fixture::weaken(zombies);
        viewer::Camera camera; camera.setPosition({0, 1.5f, 0}); camera.setOrientation(-90, 0);
        viewer::SwordMelee sword; viewer::SwordCombat combat;
        sword.trigger(true); sword.drag({back ? 40.f : -40.f, 0}); sword.update(1);
        combat.update(sword, camera, 1.22, 1.22, world, &zombies, nullptr);
        const auto& instance = zombies.instances[0];
        require(instance.severedJoint == -1 && instance.ragdoll == fatal,
                "First torso contact must damage immediately, before the joint reaches the blade");
        if (!fatal) require(zombies.damage().injury(0) && std::abs(zombies.damage().injury(0)->health - .8f) < 1e-6f,
                            "A sword cut must apply ordinary torso damage only once");
        const auto handle = instance.ragdollHandle, version = instance.ragdollPoseVersion;
        const auto bursts = zombies.bloodMist().recent(1.22).size();
        if (fatal) zombies.update(1.f / 120);
        combat.update(sword, camera, 1.35, 1.35, world, &zombies, nullptr);
        require(instance.ragdoll && instance.severedJoint == 0,
                "A later shoulder contact must sever after a torso hit from either side");
        require(!zombies.damage().injury(0) && zombies.bloodMist().recent(1.35).size() == bursts + 1 &&
                zombies.bloodMist().recent(1.35).back().size == viewer::BloodMistSize::medium,
                "Severing must create blood feedback without another ordinary hit");
        if (fatal) require(instance.ragdollHandle == handle && instance.ragdollPoseVersion > version && zombies.ragdollCount() == 1,
                           "A fatal first hit must retain its corpse rig and publish the later sever");
        const auto severedVersion = instance.ragdollPoseVersion;
        combat.update(sword, camera, 2, 2, world, &zombies, nullptr);
        require(instance.severedJoint == 0 && instance.ragdollPoseVersion == severedVersion,
                "The later elbow must not sever a second joint during the same cut");
        sword.trigger(false); sword.trigger(true); sword.drag({40, 0}); sword.update(3);
        combat.update(sword, camera, 3.4, 3.4, world, &zombies, nullptr);
        require(instance.ragdollPoseVersion == severedVersion, "A fresh cut must not acquire an existing corpse");
    }
}

void cancelAfterBody() {
    auto zombies = sword_fixture::population();
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::Camera camera; camera.setPosition({0, 1.5f, 0}); camera.setOrientation(-90, 0);
    viewer::SwordMelee sword; viewer::SwordCombat combat;
    sword.trigger(true); sword.drag({-40, 0}); sword.update(1);
    combat.update(sword, camera, 1.22, 1.22, world, &zombies, nullptr);
    sword.cancel();
    combat.update(sword, camera, 1.4, 1.4, world, &zombies, nullptr);
    require(!zombies.instances[0].ragdoll && zombies.instances[0].severedJoint == -1,
            "Cancelling after body damage must stop further joint checks");
}

void finiteBladeQueries() {
    auto zombies = sword_fixture::population();
    auto& joints = zombies.dismembermentAssets[0].joints;
    joints.resize(1);
    joints[0].bone = 1; joints[0].part = viewer::RagdollPart::torso;
    joints[0].center = {0, .39f, -1};
    zombies.rebuildSpatialIndex();
    const glm::vec3 start(0, 1.44f, 0), end(0, 1.44f, -1.1f);
    const auto bullet = zombies.traceShot(0, zombies.instances[0], start, {0, 0, -1}, 1);
    const auto blade = zombies.traceMeleeBody(0, start, end, 1);
    require(bullet && bullet->claim.joint == 0 && bullet->distance > glm::distance(start, end),
            "Bullet tracing must retain its existing joint precedence");
    require(blade && blade->claim.joint == -1 && !zombies.traceMeleeJoint(0, start, end, 1),
            "A joint beyond the blade must neither sever nor hide an in-range body hit");
    const glm::vec3 center(0, 1.44f, -2.25f);
    const auto inside = zombies.traceMeleeJoint(0, center, center + glm::vec3(.001f, 0, 0), 1);
    require(inside && inside->distance == 0, "A short blade segment inside a joint must count as contact");
    require(!zombies.traceMeleeJoint(0, start, start, 1), "A zero-length blade query must be ignored");
}

void wallAfterBody() {
    auto zombies = sword_fixture::population();
    viewer::Model model; viewer::Primitive wall;
    for (const glm::vec3 point : {glm::vec3(-.1f, 0, 0), {-.1f, 3, 0}, {-.1f, 3, -3}, {-.1f, 0, -3}}) {
        viewer::Vertex vertex; vertex.position = point; wall.vertices.push_back(vertex);
    }
    wall.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(wall); model.draws.emplace_back();
    const viewer::CollisionWorld world(model);
    viewer::Camera camera; camera.setPosition({0, 1.5f, 0}); camera.setOrientation(-90, 0);
    viewer::SwordMelee sword; viewer::SwordCombat combat;
    sword.trigger(true); sword.drag({-40, 0}); sword.update(1);
    combat.update(sword, camera, 1.4, 1.4, world, &zombies, nullptr);
    require(!zombies.instances[0].ragdoll && zombies.damage().injury(0) &&
            std::abs(zombies.damage().injury(0)->health - .8f) < 1e-6f,
            "An exposed torso hit must not allow severing a joint behind a wall");
}

void corpseMovementAndLifetime() {
    const viewer::CollisionWorld world(viewer::Model{});
    for (const bool cancel : {false, true}) {
        auto zombies = sword_fixture::population(); sword_fixture::weaken(zombies);
        viewer::Camera camera; camera.setPosition({0, 1.5f, 0}); camera.setOrientation(-90, 0);
        viewer::SwordMelee sword; viewer::SwordCombat combat;
        sword.trigger(true); sword.drag({-40, 0}); sword.update(1);
        combat.update(sword, camera, 1.22, 1.22, world, &zombies, nullptr);
        require(zombies.instances[0].ragdoll && zombies.instances[0].severedJoint == -1,
                "The initial body hit must create an intact corpse");
        if (cancel) {
            sword.cancel(); combat.update(sword, camera, 1.4, 1.4, world, &zombies, nullptr);
            sword.trigger(false); sword.trigger(true); sword.drag({-40, 0}); sword.update(2);
            combat.update(sword, camera, 2.4, 2.4, world, &zombies, nullptr);
        } else {
            zombies.translateRagdoll(0, {3, 0, 0});
            combat.update(sword, camera, 1.4, 1.4, world, &zombies, nullptr);
        }
        require(zombies.instances[0].severedJoint == -1,
                "Joint checks must follow the physical corpse and expire with the owning cut");
    }
}
}
int main() {
    try { gestures(); heldSwings(); geometryAndItem(); contacts(); bodyThenJoint(); cancelAfterBody();
        finiteBladeQueries(); wallAfterBody(); corpseMovementAndLifetime();
        std::cout << "Sword gesture, geometry and contact checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
