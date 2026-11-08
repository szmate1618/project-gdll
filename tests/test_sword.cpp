#include "sword_combat.hpp"
#include "sword_geometry.hpp"
#include "inventory/item_config.hpp"
#include "zombie_layer.hpp"
#include "collision_world.hpp"
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
        require(!sword.update(1.1), "A single hold must not repeat its cut");
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
}
int main() {
    try { gestures(); geometryAndItem(); contacts(); std::cout << "Sword gesture, geometry and contact checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
