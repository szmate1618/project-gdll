#include "collision_world.hpp"
#include "fps_controller.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void quad(viewer::Model& model, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const std::string& name = "geometry") {
    viewer::Primitive primitive;
    for (glm::vec3 point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    viewer::DrawInstance draw;
    draw.primitive = model.primitives.size();
    draw.name = name;
    model.primitives.push_back(std::move(primitive));
    model.draws.push_back(std::move(draw));
}

void floor(viewer::Model& model, float minimum = -100.0F, float maximum = 100.0F,
           float slope = 0.0F) {
    quad(model, {minimum, minimum * slope, -100}, {minimum, minimum * slope, 100},
         {maximum, maximum * slope, 100}, {maximum, maximum * slope, -100}, "terrain");
}

void box(viewer::Model& model, glm::vec3 minimum, glm::vec3 maximum) {
    // One draw per box allows the loader's building classification to see the
    // closed volume exactly as it sees generated extruded building primitives.
    const auto start = model.primitives.size();
    const float x = minimum.x, y = minimum.y, z = minimum.z;
    const float X = maximum.x, Y = maximum.y, Z = maximum.z;
    quad(model, {x,y,z}, {x,y,Z}, {x,Y,Z}, {x,Y,z});
    quad(model, {X,y,Z}, {X,y,z}, {X,Y,z}, {X,Y,Z});
    quad(model, {X,y,z}, {x,y,z}, {x,Y,z}, {X,Y,z});
    quad(model, {x,y,Z}, {X,y,Z}, {X,Y,Z}, {x,Y,Z});
    quad(model, {x,Y,z}, {x,Y,Z}, {X,Y,Z}, {X,Y,z});
    quad(model, {x,y,Z}, {x,y,z}, {X,y,z}, {X,y,Z});
    viewer::Primitive combined;
    for (std::size_t i = start; i < model.primitives.size(); ++i) {
        const auto& part = model.primitives[i];
        const auto offset = static_cast<std::uint32_t>(combined.vertices.size());
        combined.vertices.insert(combined.vertices.end(), part.vertices.begin(), part.vertices.end());
        for (auto index : part.indices) {
            combined.indices.push_back(offset + index);
        }
    }
    model.primitives.resize(start);
    model.draws.resize(start);
    model.primitives.push_back(std::move(combined));
    viewer::DrawInstance draw;
    draw.primitive = start;
    draw.name = "building_test";
    model.draws.push_back(std::move(draw));
}

void run(viewer::FPSController& player, const viewer::CollisionWorld& world, float seconds,
         viewer::FPSInput input = {}, int rate = 120) {
    const int frames = static_cast<int>(std::lround(seconds * static_cast<float>(rate)));
    for (int frame = 0; frame < frames; ++frame) {
        player.update(1.0F / static_cast<float>(rate), input, world);
    }
}

viewer::FPSController spawned(const viewer::CollisionWorld& world, glm::vec3 eye = {0, 1.7F, 0}) {
    viewer::FPSController player;
    require(player.spawn(eye, world), "Could not spawn on synthetic terrain");
    require(player.grounded(), "Spawn does not establish ground contact");
    return player;
}

void testFlatMovementAndTime() {
    viewer::Model model;
    floor(model);
    viewer::CollisionWorld world(model);
    auto player = spawned(world);
    require(std::abs(player.eyePosition().y - 1.7F) < 0.01F, "Flat eye height is not 1.70 meters");
    run(player, world, 3.0F);
    require(player.grounded() && std::abs(player.feet().y) < 0.01F,
            "Standing player drifts or sinks into terrain");
    require(player.verticalVelocity() == 0.0F, "Grounded player accumulates gravity");

    viewer::FPSInput input;
    input.forward = 1.0F;
    input.yawDegrees = 0.0F;
    auto thirty = spawned(world);
    auto sixty = spawned(world);
    auto fast = spawned(world);
    run(thirty, world, 4.0F, input, 30);
    run(sixty, world, 4.0F, input, 60);
    run(fast, world, 4.0F, input, 144);
    require(std::abs(thirty.feet().x - 12.0F) < 0.03F, "Walking speed is incorrect");
    require(glm::length(thirty.feet() - sixty.feet()) < 0.03F &&
                glm::length(thirty.feet() - fast.feet()) < 0.03F,
            "Walking displacement depends on render rate");
    require(std::abs(thirty.eyePosition().y - 1.7F) < 0.01F,
            "Horizontal movement changes eye height on flat terrain");

    input.right = 1.0F;
    auto diagonal = spawned(world);
    run(diagonal, world, 2.0F, input);
    require(std::abs(glm::length(glm::vec2(diagonal.feet().x, diagonal.feet().z)) - 6.0F) < 0.01F,
            "Diagonal walking is faster than straight walking");
    auto sprint = spawned(world);
    input.right = 0.0F;
    input.run = true;
    run(sprint, world, 2.0F, input);
    require(std::abs(sprint.feet().x - 12.0F) < 0.02F, "Running speed is incorrect");
}

void testGravityJumpAndCeiling() {
    viewer::Model model;
    floor(model);
    viewer::CollisionWorld world(model);
    viewer::FPSController falling;
    falling.teleport({0, 10, 0});
    run(falling, world, 0.25F);
    require(falling.feet().y < 9.75F && falling.verticalVelocity() < -2.0F && !falling.grounded(),
            "Gravity does not accelerate an airborne player");
    run(falling, world, 3.0F);
    require(falling.grounded() && falling.feet().y >= -0.001F,
            "Falling player passes below the terrain");

    auto player = spawned(world);
    viewer::FPSInput held;
    held.jump = true;
    float apex = player.feet().y;
    for (int frame = 0; frame < 240; ++frame) {
        player.update(1.0F / 120.0F, held, world);
        apex = std::max(apex, player.feet().y);
    }
    require(apex > 0.5F && apex < 1.0F, "Jump apex is outside the human-sized range");
    require(player.grounded(), "Holding jump repeats jumps after landing");
    run(player, world, 0.01F);
    player.update(1.0F / 120.0F, held, world);
    require(player.verticalVelocity() > 3.0F, "Releasing and pressing jump does not jump again");
    run(player, world, 0.10F);
    const float before = player.verticalVelocity();
    player.update(1.0F / 120.0F, held, world);
    require(player.verticalVelocity() < before, "Jump key grants an additional jump in the air");

    // Long falls exercise displacement-based subdivisions at terminal velocity.
    falling.teleport({0, 300, 0});
    run(falling, world, 10.0F, {}, 30);
    require(falling.grounded() && falling.feet().y >= 0.0F,
            "A terminal-velocity fall tunnels through a thin floor");

    quad(model, {-20,2.2F,-20}, {20,2.2F,-20}, {20,2.2F,20}, {-20,2.2F,20}, "ceiling");
    viewer::CollisionWorld lowWorld(model);
    auto low = spawned(lowWorld);
    apex = 0.0F;
    for (int frame = 0; frame < 120; ++frame) {
        low.update(1.0F / 120.0F, held, lowWorld);
        apex = std::max(apex, low.feet().y);
    }
    require(apex <= 0.403F && apex > 0.30F && low.grounded(),
            "Jump does not resolve a low ceiling collision");
}

void testWallsAndBuildingSpawn() {
    viewer::Model model;
    floor(model);
    box(model, {3,0,-3}, {6,5,3});
    viewer::CollisionWorld world(model);
    auto player = spawned(world);
    viewer::FPSInput toward;
    toward.forward = 1.0F;
    toward.yawDegrees = 0.0F;
    toward.run = true;
    run(player, world, 2.0F, toward, 30);
    require(player.feet().x > 2.60F && player.feet().x < 2.701F,
            "Running player penetrates a building wall or stops too far away");
    require(!world.insideBuilding(player.feet(), 0.29F, 1.8F), "Player center enters building volume");

    auto inside = spawned(world, {4.5F,1.7F,0});
    require(!world.insideBuilding(inside.feet(), 0.30F, 1.80F),
            "Spawn inside a building was not relocated outside it");

    viewer::Model thin;
    floor(thin);
    quad(thin, {3,0,-100}, {3,0,100}, {3,10,100}, {3,10,-100}, "thin_wall");
    viewer::CollisionWorld thinWorld(thin);
    auto sliding = spawned(thinWorld);
    toward.right = 1.0F;
    run(sliding, thinWorld, 3.0F, toward, 30);
    require(sliding.feet().x < 2.701F && sliding.feet().x > 2.65F && sliding.feet().z > 12.0F,
            "Diagonal motion tunnels through a thin wall or cannot slide along it");
    require(sliding.grounded() && std::abs(sliding.feet().y) < 0.01F,
            "Diagonal wall sliding jitters away from the floor");

    quad(thin, {-100,0,3}, {100,0,3}, {100,10,3}, {-100,10,3}, "other_wall");
    viewer::CollisionWorld cornerWorld(thin);
    auto corner = spawned(cornerWorld);
    run(corner, cornerWorld, 5.0F, toward, 30);
    require(corner.feet().x < 2.701F && corner.feet().z < 2.701F && corner.grounded(),
            "Iterative resolution fails at a diagonal wall corner");
}

void testSlopesAndEdges() {
    viewer::Model model;
    floor(model, -100, 100, 0.4F);
    viewer::CollisionWorld world(model);
    auto player = spawned(world);
    viewer::FPSInput walk;
    walk.forward = 1.0F;
    walk.yawDegrees = 0.0F;
    run(player, world, 2.0F, walk);
    const float expectedLift = player.config().radius * (std::sqrt(1.16F) - 1.0F);
    require(player.feet().x > 5.8F && player.grounded(), "Player cannot climb a moderate slope");
    require(std::abs(player.feet().y - player.feet().x * 0.4F - expectedLift) < 0.015F,
            "Capsule fails to follow the sloped ground height");
    require(player.groundNormal().y > 0.92F && player.groundNormal().y < 0.94F,
            "Slope ground normal is incorrect");
    walk.forward = -1.0F;
    run(player, world, 4.0F, walk);
    require(player.feet().x < -5.7F && player.grounded() &&
                std::abs(player.feet().y - player.feet().x * 0.4F - expectedLift) < 0.015F,
            "Player floats or loses ground contact while descending a slope");
    const glm::vec3 resting = player.feet();
    run(player, world, 3.0F);
    require(glm::length(player.feet() - resting) < 0.015F && player.grounded(),
            "Player drifts or jitters while standing on a slope");

    viewer::Model edge;
    floor(edge, -20, 0, 0.4F);
    viewer::CollisionWorld edgeWorld(edge);
    auto offEdge = spawned(edgeWorld, {-1,1.3F,0});
    walk.forward = 1.0F;
    run(offEdge, edgeWorld, 1.0F, walk);
    require(offEdge.feet().x > 1.8F && offEdge.feet().y < -0.5F &&
                offEdge.verticalVelocity() < -2.0F && !offEdge.grounded(),
            "Walking off a sloped mesh edge does not cause falling");

    viewer::Model steep;
    floor(steep, -20, 0);
    floor(steep, 0, 20, std::sqrt(3.0F));
    viewer::CollisionWorld steepWorld(steep);
    auto blocked = spawned(steepWorld, {-1.5F,1.7F,0});
    walk.run = true;
    run(blocked, steepWorld, 3.0F, walk);
    require(blocked.feet().x < 0.2F && blocked.feet().y < 0.4F,
            "Sprinting player climbs a slope above the configured maximum angle");
}

void testInvalidInputsAndMissingGround() {
    viewer::Model empty;
    viewer::CollisionWorld world(empty);
    viewer::FPSController player;
    require(!player.spawn({0,1.7F,0}, world), "Spawn succeeds in an empty collision world");
    const glm::vec3 before = player.feet();
    player.update(-1.0F, {}, world);
    player.update(std::numeric_limits<float>::quiet_NaN(), {}, world);
    require(player.feet() == before, "Invalid delta time changes player position");
}

}  // namespace

int main() {
    try {
        testFlatMovementAndTime();
        testGravityJumpAndCeiling();
        testWallsAndBuildingSpawn();
        testSlopesAndEdges();
        testInvalidInputsAndMissingGround();
        std::cout << "FPS flat ground, frame rates, gravity, jumps, ceilings, walls, sliding, "
                     "spawn, slopes, steep slopes, and mesh edges passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FPS test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
