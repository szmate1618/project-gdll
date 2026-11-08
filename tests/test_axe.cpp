#include "axe_combat.hpp"
#include "axe_geometry.hpp"
#include "collision_world.hpp"
#include "inventory/item_config.hpp"
#include "sword_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::Camera eye() {
    viewer::Camera camera; camera.setPosition({0, 1.5f, 0}); camera.setOrientation(-90, 0);
    return camera;
}
viewer::ZombieLayer bodies() {
    viewer::ZombieLayer zombies;
    viewer::AnimatedModel asset; asset.model.boundsMin = {-.12f, 0, -.12f}; asset.model.boundsMax = {.12f, 1.8f, .12f};
    zombies.assets.push_back(std::move(asset));
    for (float x : {.25f, -.45f}) {
        viewer::ZombieInstance zombie;
        zombie.restRoot = zombie.restTransform = zombie.transform = glm::translate(glm::mat4(1), {x, 0, -1.45f});
        zombies.instances.push_back(zombie);
    }
    zombies.rebuildSpatialIndex(); return zombies;
}
void geometry() {
    const auto item = viewer::loadItem(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage/weapons", "axe");
    require(item.gameplay().melee && item.hasTag("bladed") && item.gameplay().ammoPerShot == 0,
            "Axe must be a bladed melee skill without ammunition");
    const auto model = viewer::makeAxeModel();
    require(model.primitives.size() == 3, "Axe needs a handle, solid head and cutting wedge");
    for (const auto& mesh : model.primitives) {
        double volume = 0;
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto a = mesh.vertices.at(mesh.indices[i]).position;
            const auto b = mesh.vertices.at(mesh.indices[i + 1]).position;
            const auto c = mesh.vertices.at(mesh.indices[i + 2]).position;
            require(glm::length(glm::cross(b - a, c - a)) > 1e-8f, "Axe has a degenerate face");
            volume += glm::dot(glm::dvec3(a), glm::cross(glm::dvec3(b), glm::dvec3(c))) / 6;
        }
        require(volume > 0, "Axe geometry must have outward winding");
    }
    for (const glm::vec2 direction : {glm::vec2(-1, 0), {1, 0}, {0, -1}, {0, 1}, glm::normalize(glm::vec2(1, 1))}) {
        const auto frame = viewer::swordTransform({direction, .20f});
        const auto ahead = frame * glm::vec4(viewer::axeEdgeX, .86f, 0, 1);
        const auto body = frame * glm::vec4(viewer::axeHeadFront, .86f, 0, 1);
        const auto future = viewer::swordTransform({direction, .2001f}) * glm::vec4(viewer::axeHeadFront, .86f, 0, 1);
        require(std::abs(glm::distance(ahead, body) - .05f) < 1e-6f && glm::dot(ahead - body, future - body) > 0,
                "The dismemberment edge must lead the main collider by 5 cm in every direction");
    }
}
void stoppedCuts() {
    float reference = -1;
    for (const bool coarse : {true, false}) {
        auto zombies = bodies(); const auto camera = eye();
        const viewer::CollisionWorld world(viewer::Model{});
        viewer::SwordMelee axe; viewer::AxeCombat combat;
        axe.trigger(true); axe.drag({-40, 0}); require(axe.update(1), "Chop did not start");
        if (coarse) combat.update(axe, camera, 2, 2, world, &zombies, nullptr);
        else for (int step = 1; step <= 60; ++step)
            combat.update(axe, camera, 1 + step / 120., 1 + step / 120., world, &zombies, nullptr);
        const auto pose = axe.pose(1.3);
        require(pose.stoppedElapsed >= viewer::swordWindupSeconds && pose.stoppedElapsed < .30f,
                "The main collider must stop the chop at its first body contact, including a frame stall");
        const auto* injury = zombies.damage().injury(0);
        require(injury && std::abs(injury->health - .8f) < 1e-6f, "The first zombie must take ordinary damage once");
        require(!zombies.damage().injury(1), "A stopped axe must not hit the zombie behind the first");
        const auto frozen = viewer::swordTransform(pose);
        require(frozen == viewer::swordTransform(axe.pose(1.34)), "A blocked axe must hold its contact pose");
        require(frozen != viewer::swordTransform(axe.pose(1.5)), "Recovery must return the blocked axe to guard");
        combat.update(axe, camera, 3, 3, world, &zombies, nullptr);
        require(std::abs(zombies.damage().injury(0)->health - .8f) < 1e-6f && !zombies.damage().injury(1),
                "A blocked chop must not resume damage during recovery or after it");
        if (reference < 0) reference = pose.stoppedElapsed;
        else require(std::abs(reference - pose.stoppedElapsed) < .003f, "Contact time must be independent of frame cadence");
        axe.drag({40, 0}); require(axe.update(3) && axe.pose(3).stoppedElapsed < 0, "A fresh held drag must clear the previous stop");
        axe.cancel(); require(axe.pose(3).elapsed < 0 && axe.pose(3).stoppedElapsed < 0, "Cancellation must clear the stopped pose");
    }
}
void leadingEdge() {
    auto zombies = sword_fixture::population(); const auto camera = eye();
    zombies.dismembermentAssets[0].joints.resize(1);
    zombies.dismembermentAssets[0].meshes.resize(1);
    // Align a shoulder with the edge's arc. Keep the torso behind it so that
    // ordinary head contact must not prevent the earlier edge from severing.
    const auto frame = glm::inverse(camera.view()) * viewer::swordTransform({{-1, 0}, .20f});
    const auto shoulder = glm::vec3(frame * glm::vec4(viewer::axeEdgeX, .86f, 0, 1));
    auto& instance = zombies.instances[0];
    instance.restRoot = instance.restTransform = instance.transform = glm::translate(glm::mat4(1), shoulder - glm::vec3(.24f, 1.44f, 0)) *
        glm::rotate(glm::mat4(1), glm::radians(180.f), glm::vec3(0, 1, 0));
    zombies.rebuildSpatialIndex();
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::SwordMelee axe; viewer::AxeCombat combat;
    axe.trigger(true); axe.drag({-40, 0}); axe.update(1);
    combat.update(axe, camera, 1.4, 1.4, world, &zombies, nullptr);
    require(instance.severedJoint == 0 && instance.ragdoll, "The leading edge must sever a shoulder before main contact");
}
void handleCannotSever() {
    auto zombies = sword_fixture::population(); const auto camera = eye();
    zombies.dismembermentAssets[0].joints.resize(1);
    zombies.dismembermentAssets[0].meshes.resize(1);
    const auto frame = glm::inverse(camera.view()) * viewer::swordTransform({{-1, 0}, .20f});
    const auto shoulder = glm::vec3(frame * glm::vec4(0, .4f, 0, 1));
    auto& instance = zombies.instances[0];
    instance.restRoot = instance.restTransform = instance.transform = glm::translate(glm::mat4(1), shoulder - glm::vec3(.24f, 1.44f, 0)) *
        glm::rotate(glm::mat4(1), glm::radians(180.f), glm::vec3(0, 1, 0));
    zombies.rebuildSpatialIndex();
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::SwordMelee axe; viewer::AxeCombat combat;
    axe.trigger(true); axe.drag({-40, 0}); axe.update(1);
    combat.update(axe, camera, 1.4, 1.4, world, &zombies, nullptr);
    require(axe.pose(1.3).stoppedElapsed >= viewer::swordWindupSeconds && instance.severedJoint == -1,
            "A handle contact must stop the chop without dismembering the joint");
}
void occlusionAndCancellation() {
    for (const bool wall : {false, true}) {
        auto zombies = bodies(); viewer::Model scene;
        if (wall) {
            viewer::Primitive mesh;
            for (const glm::vec3 point : {glm::vec3(-4, -1, -.6f), {4, -1, -.6f}, {4, 4, -.6f}, {-4, 4, -.6f}}) {
                viewer::Vertex v; v.position = point; mesh.vertices.push_back(v);
            }
            mesh.indices = {0, 1, 2, 0, 2, 3}; scene.primitives.push_back(mesh); scene.draws.emplace_back();
        }
        viewer::CollisionWorld world(scene); viewer::SwordMelee axe; viewer::AxeCombat combat;
        axe.trigger(true); axe.drag({-40, 0}); axe.update(1);
        if (!wall) axe.cancel();
        combat.update(axe, eye(), 2, 2, world, &zombies, nullptr);
        require(!zombies.damage().injury(0) && axe.pose(1.3).stoppedElapsed < 0,
                "A cancelled chop or occluded target must not damage or stop against a zombie");
    }
}
}
int main() {
    try { geometry(); stoppedCuts(); leadingEdge(); handleCannotSever(); occlusionAndCancellation();
        std::cout << "Axe geometry, leading edge and blocked swing checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
