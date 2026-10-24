#include "zombie_bone_collision.hpp"
#include "melee_combat.hpp"
#include "physics_world.hpp"
#include "player_body.hpp"
#include "zombie_layer.hpp"
#include "box_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::AnimatedModel fixture() {
    viewer::AnimatedModel asset;
    asset.skeleton.resize(2);
    asset.skeleton[1].referenceWorld = glm::translate(glm::mat4(1), {2, 0, 0});
    asset.skeleton[1].inverseBind = glm::inverse(asset.skeleton[1].referenceWorld);
    asset.model.primitives.emplace_back();
    asset.model.draws.emplace_back();
    auto& primitive = asset.model.primitives[0];
    const std::array<glm::vec3, 9> points{{{-1.4f, -.2f, 0}, {-.6f, -.2f, 0}, {-1, .6f, 0},
        {.6f, -.2f, 0}, {1.4f, -.2f, 0}, {1, .6f, 0}, {-.4f, 2.8f, 0}, {.4f, 2.8f, 0}, {0, 3.6f, 0}}};
    for (const auto p : points) {
        viewer::Vertex v; v.position = p;
        primitive.indices.push_back(static_cast<std::uint32_t>(primitive.vertices.size()));
        primitive.vertices.push_back(v);
    }
    asset.animation.resize(1); asset.animation[0].skinning.resize(points.size());
    for (int v = 0; v < 3; ++v) {
        auto& skin = asset.animation[0].skinning[v];
        skin.joints[0] = 0; skin.joints[1] = 1;
        skin.weights[0] = v ? .9f : .4f; skin.weights[1] = 1 - skin.weights[0];
    }
    for (int v = 3; v < 6; ++v) {
        auto& skin = asset.animation[0].skinning[v];
        skin.joints[7] = 1; skin.weights[7] = 1;
    }
    asset.frameCount = 2; asset.duration = 1;
    const auto turned = glm::translate(glm::mat4(1), {4, 0, 0}) *
        glm::rotate(glm::mat4(1), glm::radians(90.f), glm::vec3(0, 1, 0));
    asset.boneFrames = {glm::mat4(1), asset.skeleton[1].referenceWorld,
                       glm::translate(glm::mat4(1), {0, 2, 0}), turned};
    return asset;
}
b3RayResult at(viewer::PhysicsWorld& world, glm::vec3 point, glm::vec3 normal) {
    const auto start = point + normal * 2.f, delta = -normal * 4.f;
    auto filter = b3DefaultQueryFilter(); filter.categoryBits = viewer::playerBodyCategory;
    return b3World_CastRayClosest(world.id(), {start.x, start.y, start.z}, {delta.x, delta.y, delta.z}, filter);
}
void partitionsFollowBonesWithoutChangingGeometry(bool reflected) {
    const auto asset = fixture();
    require(viewer::ZombieBoneCollisionAsset::supported(asset), "Weighted skeletons must use rigid collision pieces");
    auto geometry = std::make_shared<viewer::ZombieBoneCollisionAsset>(asset);
    const auto& pieces = geometry->pieces();
    require(pieces.size() == 3 && pieces[0].bone == 0 && pieces[1].bone == 1 && pieces[2].bone == -1,
            "Triangles must use aggregate weights across all eight slots, with unweighted faces at the root");
    std::array<std::uint64_t, 3> hashes{};
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        require(pieces[i].triangles == 1 && pieces[i].mesh->triangleCount == 2,
                "Every original triangle must appear in exactly one two-sided piece");
        hashes[i] = pieces[i].mesh->hash;
    }
    viewer::PhysicsWorld world;
    viewer::ZombieInstance instance;
    instance.restTransform = instance.transform = glm::translate(glm::mat4(1), {10, 1, -8}) *
        glm::scale(glm::mat4(1), {reflected ? -2.f : 2.f, 2, 2});
    instance.displayedAnimationFrame = 0;
    const auto root = instance.transform;
    const auto check = [&](glm::vec3 point, glm::vec3 normal) {
        const auto p = glm::vec3(root * glm::vec4(point, 1));
        const auto n = glm::normalize(glm::mat3(root) * normal);
        require(at(world, p, n).hit && at(world, p, -n).hit,
                "Rigid collision pieces must be queryable from both sides with instance scale/reflection applied once");
    };
    {
        viewer::ZombieBoneContacts contacts(world.id(), geometry, root);
        const auto bodies = contacts.bodies();
        contacts.update(asset, instance, 0);
        check({-1, 0, 0}, {0, 0, 1}); check({1, 0, 0}, {0, 0, 1}); check({0, 3, 0}, {0, 0, 1});
        instance.displayedAnimationFrame = 1;
        contacts.update(asset, instance, 0);
        check({-1, 2, 0}, {0, 0, 1}); check({4, 0, 1}, {1, 0, 0}); check({0, 3, 0}, {0, 0, 1});
        require(!at(world, glm::vec3(root * glm::vec4(-1, 0, 0, 1)), {0, 0, 1}).hit,
                "Moving a piece must remove its old contact location");
        instance.ragdoll = true;
        instance.ragdollBones = {glm::translate(glm::mat4(1), {0, 4, 0}),
            asset.boneFrames[3] * asset.skeleton[1].inverseBind};
        contacts.update(asset, instance, 0);
        check({-1, 4, 0}, {0, 0, 1}); check({4, 0, 1}, {1, 0, 0});
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            require(hashes[i] == pieces[i].mesh->hash && B3_ID_EQUALS(bodies[i], contacts.bodies()[i]),
                    "Animation and ragdoll handoff must not rebuild meshes or replace bodies");
            b3ShapeId shape{};
            require(b3Body_GetShapes(bodies[i], &shape, 1) == 1 && b3Shape_GetMesh(shape).data == pieces[i].mesh.get(),
                    "Each body must reference the original shared immutable mesh");
        }
        viewer::ZombieBoneContacts second(world.id(), geometry, root);
        second.update(asset, instance, 0);
        require(b3World_GetCounters(world.id()).bodyCount == 6, "Two zombies must share meshes using separate bodies");
    }
    require(b3World_GetCounters(world.id()).bodyCount == 0, "Destroying contacts must release every bone body");
}
void cacheSurvivesHandoffAndUnequipping() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::ZombieLayer zombies;
    zombies.assets.push_back(fixture()); zombies.instances.emplace_back();
    auto& instance = zombies.instances[0];
    instance.displayedAnimationFrame = 0;
    zombies.attachPhysics(world); zombies.rebuildSpatialIndex();
    viewer::PlayerBody player(world, 1.7f);
    viewer::MeleeCombat melee(world);
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    melee.prepare(player, &zombies, 0);
    const auto initial = melee.contactStats();
    require(initial.cachedBoneMeshes == 3 && initial.activeBoneBodies == 3 && initial.deformingMeshes == 0,
            "Weighted assets must use bone meshes exclusively");
    require(zombies.meleeHit(0, {0, 0, -3}, {0, 1, 0}, 0), "Fixture must enter a spring ragdoll");
    for (int f = 0; f < 30; ++f) {
        world->step(1.f / 60, [&](float dt) {
            player.beforeStep(dt); melee.prepare(player, &zombies, 0);
        }, [&](float) { melee.finish(player, &zombies, 0); player.afterStep(); });
        const auto current = melee.contactStats();
        require(current.cachedBoneMeshes == initial.cachedBoneMeshes && current.activeBoneBodies == 3,
                "Spring pose changes must retain the cached bone meshes");
    }
    player.follow({0, 1.7f, 0}, {0, 0, -1}, false);
    melee.prepare(player, &zombies, 0);
    require(melee.contactStats().activeBoneBodies == 0 && melee.contactStats().cachedBoneMeshes == 3,
            "Unequipping must drop proxies while retaining immutable asset meshes");
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    melee.prepare(player, &zombies, 0);
    require(melee.contactStats().activeBoneBodies == 3 && melee.contactStats().cachedBoneMeshes == 3,
            "Reequipping must reuse the original asset cache");
    zombies.removeRagdoll(0); melee.prepare(player, &zombies, 0);
    require(melee.contactStats().activeBoneBodies == 0, "Removed zombies must not retain contact bodies");
}
void punchesHitBoneSurfacesAndReuseTheSpringRagdoll() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::AnimatedModel asset;
    asset.model = viewer::BoxLayer::unitCube(); asset.model.draws.emplace_back();
    auto& primitive = asset.model.primitives[0];
    for (auto& v : primitive.vertices) v.position = v.position * glm::vec3(.45f, 1.8f, .32f) + glm::vec3(0, .9f, 0);
    asset.skeleton.resize(1); asset.animation.resize(1);
    asset.animation[0].skinning.resize(primitive.vertices.size());
    for (auto& skin : asset.animation[0].skinning) skin.weights[0] = 1;
    viewer::ZombieLayer zombies; zombies.assets.push_back(std::move(asset)); zombies.instances.emplace_back();
    auto& zombie = zombies.instances[0];
    zombie.restRoot = zombie.restTransform = zombie.transform = glm::translate(glm::mat4(1), {0, 0, -.78f});
    zombies.attachPhysics(world); zombies.rebuildSpatialIndex();
    viewer::PlayerBody player(world, 1.7f); viewer::MeleeCombat melee(world);
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    const auto step = [&](bool& contacted) {
        world->step(1.f / 60, [&](float dt) { player.beforeStep(dt); melee.prepare(player, &zombies, 0); }, [&](float) {
            for (auto part : {viewer::PlayerBodyPart::leftFist, viewer::PlayerBodyPart::rightFist}) {
                if (!player.punching(part)) continue;
                const auto body = player.body(part);
                std::vector<b3ContactData> contacts(b3Body_GetContactCapacity(body));
                const auto n = b3Body_GetContactData(body, contacts.data(), static_cast<int>(contacts.size()));
                for (int c = 0; c < n; ++c) {
                    const auto& contact = contacts[c];
                    const auto other = B3_ID_EQUALS(b3Shape_GetBody(contact.shapeIdA), body) ? contact.shapeIdB : contact.shapeIdA;
                    if (b3Shape_GetFilter(other).categoryBits != viewer::meleeZombieCategory) continue;
                    for (int m = 0; m < contact.manifoldCount; ++m)
                        for (int p = 0; p < contact.manifolds[m].pointCount; ++p)
                            contacted |= contact.manifolds[m].points[p].totalNormalImpulse > .001f;
                }
            }
            melee.finish(player, &zombies, 0); player.afterStep();
        });
    };
    bool ignored = false;
    for (int f = 0; f < 3; ++f) step(ignored);
    require(!zombie.ragdoll, "An idle bone surface contact must not activate a ragdoll");
    for (int punch = 0; punch < 2; ++punch) {
        bool contacted = false;
        player.punch({0, 0, -1});
        for (int f = 0; f < 90; ++f) step(contacted);
        require(contacted && zombie.springRagdoll && zombies.ragdollCount() == 1,
                "Both fists must hit bone surfaces and transfer repeat impacts into the same spring rig");
        require(melee.contactStats().cachedBoneMeshes == 1 && melee.contactStats().activeBoneBodies == 1 &&
                melee.contactStats().deformingMeshes == 0, "Repeated hits must keep the single immutable bone mesh");
    }
}
}
int main() {
    try {
        partitionsFollowBonesWithoutChangingGeometry(false);
        partitionsFollowBonesWithoutChangingGeometry(true);
        cacheSurvivesHandoffAndUnequipping();
        punchesHitBoneSurfacesAndReuseTheSpringRagdoll();
        std::cout << "Rigid zombie bone collision checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
