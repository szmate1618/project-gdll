#include "bird_flock.hpp"
#include "collision_world.hpp"
#include "player_vitals.hpp"
#include "tree_layer.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void triangle(viewer::Primitive& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const auto point : {a, b, c}) {
        viewer::Vertex vertex;
        vertex.position = point;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}

void quad(viewer::Primitive& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    triangle(mesh, a, b, c);
    triangle(mesh, a, c, d);
}

viewer::Model buildingScene() {
    viewer::Model scene;
    viewer::Primitive mesh;
    const glm::vec3 a(-4, 0, -4), b(-4, 0, 4), c(4, 0, 4), d(4, 0, -4), up(0, 12, 0);
    quad(mesh, a, d, c, b);
    quad(mesh, a + up, b + up, c + up, d + up);
    quad(mesh, a, a + up, d + up, d);
    quad(mesh, b, c, c + up, b + up);
    quad(mesh, a, b, b + up, a + up);
    quad(mesh, d, d + up, c + up, c);
    scene.primitives.push_back(std::move(mesh));
    scene.draws.push_back({0, glm::mat4(1), "building_flight_test_walls"});
    scene.boundsMin = {-120, 0, -120};
    scene.boundsMax = {120, 80, 120};
    return scene;
}

void sweptContacts() {
    viewer::BirdFlockPlayer player;
    require(viewer::birdHitsPlayer({-5, 1, 0}, {5, 1, 0}, player),
            "A fast bird crossing the body must hit even when both frame endpoints are outside");
    require(!viewer::birdHitsPlayer({-5, 1, .46f}, {5, 1, .46f}, player),
            "A parallel near miss must not damage the player");
    require(viewer::birdHitsPlayer({0, .9f, 0}, {0, .9f, 0}, player),
            "Stationary overlap must count without a nonzero segment direction");
    require(viewer::birdHitsPlayer({-1, -.1f, 0}, {1, -.1f, 0}, player) &&
            !viewer::birdHitsPlayer({-1, -.16f, 0}, {1, -.16f, 0}, player),
            "The lower rounded capsule must include bird radius without extending indefinitely below the feet");
    require(viewer::birdHitsPlayer({-1, 1.9f, 0}, {1, 1.9f, 0}, player) &&
            !viewer::birdHitsPlayer({-1, 1.96f, 0}, {1, 1.96f, 0}, player),
            "The upper endcap must stop at the player's head plus bird radius");
    auto ducking = player;
    ducking.height = .8f;
    require(!viewer::birdHitsPlayer({-5, 1.2f, 0}, {5, 1.2f, 0}, ducking) &&
            viewer::birdHitsPlayer({-5, 1.2f, 0}, {5, 1.2f, 0}, player),
            "Ducking must let a head-height pass miss the reduced body capsule");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(!viewer::birdHitsPlayer({nan, 1, 0}, {0, 1, 0}, player),
            "Invalid bird coordinates must never report a collision");
}

void flightSpace() {
    const auto scene = buildingScene();
    const viewer::CollisionWorld world(scene);
    viewer::TreeLayer trees;
    viewer::TreeInstance tree;
    tree.boundsMin = {12, 0, -4};
    tree.boundsMax = {20, 18, 4};
    tree.collidable = false; // Foliage still blocks birds without a walking trunk.
    trees.instances.push_back(tree);
    const viewer::BirdFlightSpace space(world, &trees);
    require(!space.clearPoint({0, 5, 0}) && space.clearPoint({0, 14, 0}),
            "A target inside a building must be rejected while clear air above the roof remains usable");
    require(!space.clearPoint({16, 10, 0}) && space.clearPoint({16, 20, 0}),
            "Targets must avoid canopy volume as well as building geometry");
    require(!space.clearSegment({8, 10, 0}, {24, 10, 0}) &&
            space.clearSegment({8, 20, 0}, {24, 20, 0}),
            "Flight corridors must reject canopy crossings and allow routes above the crown");
    require(!space.lineOfSight({8, 10, 0}, {24, 10, 0}) &&
            space.lineOfSight({8, 20, 0}, {24, 20, 0}),
            "The single sight ray must include canopy occlusion without relying on trunk colliders");
    require(!space.clearSegment({-8, 5, 0}, {8, 5, 0}),
            "A pair of clear endpoints must not permit a route through a building");
    trees.instances.clear();
    require(!space.clearPoint({16, 10, 0}),
            "The static flight-space index must retain copied canopy bounds independently of its source layer");

    viewer::Model elevatedTerrain;
    viewer::Primitive ground;
    quad(ground, {-20, 10, -20}, {-20, 10, 20}, {20, 10, 20}, {20, 10, -20});
    elevatedTerrain.primitives.push_back(std::move(ground));
    elevatedTerrain.draws.push_back({0, glm::mat4(1), "terrain"});
    const viewer::CollisionWorld raisedWorld(elevatedTerrain);
    const viewer::BirdFlightSpace raisedSpace(raisedWorld, nullptr);
    require(!raisedSpace.clearPoint({0, 5, 0}) && raisedSpace.clearPoint({0, 12, 0}),
            "Targets below an open terrain sheet must be rejected even without touching its triangles");
    require(raisedSpace.clearPoint({30, 5, 0}),
            "Finite terrain must not reject clear air outside its actual footprint");
}

void deterministicMotionAndBudget() {
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::BirdFlockSettings settings;
    settings.flockCount = 3;
    settings.birdsPerFlock = 32;
    viewer::BirdFlocks first(world, nullptr, {-120, 0, -120}, {120, 80, 120}, {0, 15, 0}, settings);
    viewer::BirdFlocks second(world, nullptr, {-120, 0, -120}, {120, 80, 120}, {0, 15, 0}, settings);
    require(first.birds().size() == 96 && first.flocks().size() == 3,
            "An open world must create the requested fixed flock population");
    const auto* storage = first.birds().data();
    const auto original = first.birds().front().position;
    viewer::BirdFlockPlayer player;
    player.feet = {0, 12, 0};
    for (int tick = 0; tick < 600; ++tick) {
        const auto firstHits = first.update(1.f / 60, player);
        const auto secondHits = second.update(1.f / 60, player);
        require(firstHits == secondHits && first.stats().sightChecks <= 1,
                "Each update may inspect at most one random bird globally regardless of flock size");
        require(first.birds().data() == storage && first.birds().size() == 96,
                "Steady-state movement must retain fixed population storage");
        for (std::size_t bird = 0; bird < first.birds().size(); ++bird) {
            const auto& a = first.birds()[bird];
            const auto& b = second.birds()[bird];
            require(finite(a.position) && finite(a.velocity) && a.position == b.position && a.velocity == b.velocity,
                    "Matching seeds and timesteps must produce finite identical bird motion");
            require(glm::length(a.velocity) < 60,
                    "Flock steering must bound speed during pursuit and turns");
        }
    }
    require(glm::length(first.birds().front().position - original) > 1,
            "Wandering and attack simulation must actually advance the flock");
    const auto beforeInvalid = first.birds().front().position;
    for (const float dt : {0.f, -1.f, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
        require(first.update(dt, player) == 0 && first.stats().sightChecks == 0 &&
                first.birds().front().position == beforeInvalid,
                "Zero and invalid timesteps must preserve positions and perform no visibility work");
    }
    settings.flockCount = 0;
    viewer::BirdFlocks disabled(world, nullptr, {-10, 0, -10}, {10, 20, 10}, {0, 5, 0}, settings);
    require(disabled.birds().empty() && disabled.flocks().empty() && disabled.update(.02f, player) == 0,
            "A zero-flock configuration must have no simulation work or damage");
}

void waypointClearance() {
    const auto scene = buildingScene();
    const viewer::CollisionWorld world(scene);
    viewer::TreeLayer trees;
    viewer::TreeInstance tree;
    tree.boundsMin = {-22, 0, -22};
    tree.boundsMax = {-8, 60, 22};
    trees.instances.push_back(tree);
    const viewer::BirdFlightSpace space(world, &trees);
    viewer::BirdFlockSettings settings;
    settings.flockCount = 2;
    settings.birdsPerFlock = 24;
    viewer::BirdFlocks flocks(world, &trees, scene.boundsMin, scene.boundsMax, {-15, 12, 0}, settings);
    require(!flocks.birds().empty(), "Mixed buildings and trees must still allow flocks in nearby clear air");
    for (const auto& bird : flocks.birds())
        require(space.clearPoint(bird.position, .14f), "Every initial bird must spawn outside solid buildings and tree crowns");
    viewer::BirdFlockPlayer spectator;
    spectator.targetable = false;
    for (int tick = 0; tick < 900; ++tick) {
        require(flocks.update(1.f / 30, spectator) == 0,
                "Spectator cameras must not receive bird damage");
        for (const auto& flock : flocks.flocks())
            require(space.clearPoint(flock.target, .2f), "Every chosen wandering target must be outside buildings and canopy bounds");
    }
    tree.boundsMin = {-1000, -1000, -1000};
    tree.boundsMax = {1000, 1000, 1000};
    trees.instances.assign(1, tree);
    viewer::BirdFlocks blocked(world, &trees, {-10, 0, -10}, {10, 30, 10}, {0, 10, 0}, settings);
    require(blocked.birds().empty(), "An entirely blocked volume must finish bounded placement attempts without unsafe fallback birds");
}

void committedAttackAndDamage() {
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::BirdFlockSettings settings;
    settings.flockCount = 1;
    settings.birdsPerFlock = 1;
    viewer::BirdFlocks flocks(world, nullptr, {-120, 0, -120}, {120, 80, 120}, {0, 10, 0}, settings);
    viewer::BirdFlockPlayer player;
    player.feet = flocks.birds().front().position + glm::vec3(30, -.9f, 0);
    bool gathering = false, swooping = false;
    for (int tick = 0; tick < 600; ++tick) {
        flocks.update(1.f / 60, player);
        gathering |= flocks.flocks().front().mode == viewer::BirdFlockMode::gathering;
        if (flocks.flocks().front().mode == viewer::BirdFlockMode::swooping) { swooping = true; break; }
    }
    require(gathering && swooping, "A visible nearby player must trigger a telegraphed gathering phase before the attack pass");
    const auto committed = flocks.flocks().front().target;
    player.feet = flocks.birds().front().position - glm::vec3(0, .9f, 0);
    viewer::PlayerVitals vitals;
    const auto hit = flocks.update(.001f, player);
    require(hit == 1 && vitals.applyDamage(static_cast<float>(hit)) == 1 && vitals.health() == 99,
            "One overlapping bird must cause exactly one HP of damage");
    require(flocks.flocks().front().target == committed,
            "Moving the player after commitment must not redirect the attack's fixed exit target");
    for (int tick = 0; tick < 60 && flocks.flocks().front().mode == viewer::BirdFlockMode::swooping; ++tick) {
        player.feet = flocks.birds().front().position - glm::vec3(0, .9f, 0);
        require(flocks.update(1.f / 60, player) == 0,
                "The same bird must not repeatedly damage an overlapping player during one pass");
    }
    player.targetable = false;
    bool recovering = false;
    for (int tick = 0; tick < 900; ++tick) {
        flocks.update(1.f / 60, player);
        recovering |= flocks.flocks().front().mode == viewer::BirdFlockMode::recovering;
        if (recovering && flocks.flocks().front().mode == viewer::BirdFlockMode::wandering) break;
    }
    require(recovering && flocks.flocks().front().mode == viewer::BirdFlockMode::wandering,
            "A completed pass must provide recovery time and resume wandering");
}

void shelteredPlayer() {
    const auto scene = buildingScene();
    const viewer::CollisionWorld world(scene);
    viewer::BirdFlockSettings settings;
    settings.flockCount = 1;
    settings.birdsPerFlock = 12;
    viewer::BirdFlocks flocks(world, nullptr, scene.boundsMin, scene.boundsMax, {15, 12, 0}, settings);
    viewer::BirdFlockPlayer player;
    player.feet = {0, 2, 0};
    require(world.insideBuilding(player.feet, player.radius, player.height),
            "Shelter fixture must place the player inside a closed building");
    bool checkedSight = false;
    for (int tick = 0; tick < 900; ++tick) {
        require(flocks.update(1.f / 60, player) == 0,
                "Birds outside a closed building must not damage the sheltered player");
        checkedSight |= flocks.stats().sightChecks != 0;
        require(flocks.flocks().front().mode == viewer::BirdFlockMode::wandering,
                "A nearby player behind the building shell must not trigger attack acquisition");
    }
    require(checkedSight, "Shelter fixture must exercise sight rejection while the birds are nearby");
}
} // namespace

int main() {
    try {
        sweptContacts();
        flightSpace();
        deterministicMotionAndBudget();
        waypointClearance();
        committedAttackAndDamage();
        shelteredPlayer();
        std::cout << "Bird flock tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Bird flock test failed: " << error.what() << '\n';
        return 1;
    }
}
