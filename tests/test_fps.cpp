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
    require(std::abs(thirty.feet().x - 11.543667F) < 0.03F, "Walking start must ramp to 3 m/s in 0.3125 seconds");
    require(glm::length(thirty.feet() - sixty.feet()) < 0.03F &&
                glm::length(thirty.feet() - fast.feet()) < 0.03F,
            "Walking displacement depends on render rate");
    require(std::abs(thirty.eyePosition().y - 1.7F) < 0.01F,
            "Horizontal movement changes eye height on flat terrain");

    input.right = 1.0F;
    auto diagonal = spawned(world);
    run(diagonal, world, 2.0F, input);
    require(std::abs(glm::length(glm::vec2(diagonal.feet().x, diagonal.feet().z)) - 5.543667F) < 0.01F,
            "Diagonal walking is faster than straight walking");
    auto sprint = spawned(world);
    input.right = 0.0F;
    input.run = true;
    run(sprint, world, 2.0F, input);
    require(std::abs(sprint.feet().x - 10.15F) < 0.02F, "Running from rest must use the same acceleration as walking");
}

void testDirectionalSpeedsAndAcceleration() {
    viewer::Model model; floor(model);
    const viewer::CollisionWorld world(model);
    struct Direction { float forward, right, running; };
    for (const auto direction : {Direction{1, 0, 6}, {-1, 0, 4.5f}, {0, 1, 5.25f}, {0, -1, 5.25f},
                                 {1, 1, 5.625f}, {1, -1, 5.625f}, {-1, 1, 4.875f}, {-1, -1, 4.875f}}) {
        for (const bool running : {false, true}) {
            auto player = spawned(world);
            viewer::FPSInput input;
            input.forward = direction.forward; input.right = direction.right;
            input.yawDegrees = 0; input.run = running;
            run(player, world, 6, input);
            const auto before = player.feet();
            run(player, world, .5f, input);
            const float speed = glm::length(glm::vec2(player.feet().x - before.x, player.feet().z - before.z)) * 2;
            require(std::abs(speed - (running ? direction.running : 3.f)) < .002f,
                    "Cardinal/diagonal movement must reach the specified directional speed without a diagonal boost");
        }
    }

    auto player = spawned(world);
    viewer::FPSInput input; input.forward = 1; input.yawDegrees = 0;
    run(player, world, 38.f / 120, input);
    require(std::abs(player.movementState().horizontalVelocity.x - 3) < .0001f, "Walking must reach 3 m/s");
    input.run = true;
    run(player, world, .15f, input);
    require(std::abs(player.movementState().horizontalVelocity.x - 4.44f) < .0001f, "Walk-to-run must gain 1.44 m/s in 0.15 seconds");
    const auto checkpoint = player.movementState();
    viewer::FPSController restored;
    restored.restoreMovement(checkpoint);
    run(player, world, 20.f / 120, input);
    run(restored, world, 20.f / 120, input);
    require(std::abs(player.movementState().horizontalVelocity.x - 6) < .0001f &&
            glm::length(player.feet() - restored.feet()) < .0001f,
            "Walk-to-run must reach running speed within 38 ticks and replay identically from a partial acceleration");
    input.run = false;
    run(player, world, .075f, input);
    require(std::abs(player.movementState().horizontalVelocity.x - 4.56f) < .0001f, "Braking must be twice as fast");
    run(player, world, 10.f / 120, input);
    require(std::abs(player.movementState().horizontalVelocity.x - 3) < .0001f, "Run-to-walk must reach walking speed within 19 ticks");
    auto reversing = player.movementState(); reversing.horizontalVelocity = {3.2f, 0};
    player.restoreMovement(reversing);
    input.forward = -1;
    run(player, world, 1.f / 6, input);
    require(glm::length(player.movementState().horizontalVelocity) < .0001f, "Reversing must brake to rest first");
    run(player, world, .15f, input);
    require(std::abs(player.movementState().horizontalVelocity.x + 1.44f) < .0001f, "Reversing must accelerate at the same limit");
    input.forward = 0;
    run(player, world, .075f, input);
    require(glm::length(player.movementState().horizontalVelocity) < .0001f, "Releasing input must brake to rest");
    const auto stopped = player.feet();
    run(player, world, .5f);
    require(glm::length(player.feet() - stopped) < .0001f, "Finished braking must not drift");
    player.restoreMovement(checkpoint);
    input = {}; input.forward = 1; input.yawDegrees = 0; input.run = true; input.crouch = true;
    run(player, world, .125f, input);
    require(std::abs(player.movementState().horizontalVelocity.x - 2.04f) < .0001f,
            "Entering crouch must brake gradually at the shared limit");
    input.crouch = false; input.toggleProne = true;
    player.update(1.f / 120, input, world);
    input.toggleProne = false;
    run(player, world, .125f - 1.f / 120, input);
    require(std::abs(player.movementState().horizontalVelocity.x - .75f) < .0001f,
            "Entering prone must keep the same braking limit without snapping to crawl speed");
    player.restoreMovement(checkpoint);
    input = {}; input.right = 1; input.yawDegrees = 0; input.run = true;
    player.update(1.f / 120, input, world);
    require(player.movementState().horizontalVelocity.x > 4.32f &&
            player.movementState().horizontalVelocity.y > 0 && player.movementState().horizontalVelocity.y < .08f,
            "Changing direction must steer gradually rather than replace the velocity instantly");
    player.teleport({0, .002f, 0});
    require(player.movementState().horizontalVelocity == glm::vec2(0), "Teleport must clear movement momentum");

    for (const auto stance : {viewer::PlayerStance::Standing, viewer::PlayerStance::Crouched, viewer::PlayerStance::Prone}) {
        for (const auto direction : {Direction{1, 0, 6}, {-1, 0, 4.5f}, {0, 1, 5.25f}, {0, -1, 5.25f}, {1, 1, 5.625f}}) {
            auto moving = spawned(world);
            auto state = moving.movementState();
            state.stance = state.takeoffStance = stance;
            state.posture = static_cast<float>(stance);
            state.proneRequested = stance == viewer::PlayerStance::Prone;
            moving.restoreMovement(state);
            input = {}; input.forward = direction.forward; input.right = direction.right;
            input.run = true; input.crouch = stance == viewer::PlayerStance::Crouched;
            run(moving, world, .025f, input);
            require(std::abs(glm::length(moving.movementState().horizontalVelocity) - .24f) < .0001f,
                    "Every direction and stance must share the same acceleration");
            input.forward = input.right = 0;
            run(moving, world, 2.f / 120, input);
            require(glm::length(moving.movementState().horizontalVelocity) < .0001f,
                    "Every direction and stance must share double-rate braking");
        }
    }
}

void testAxisAccelerationBudget() {
    viewer::Model model; floor(model);
    const viewer::CollisionWorld world(model);
    struct Case {
        glm::vec2 velocity, input, expected;
        const char* message;
    };
    // One 1/120 s tick: full single-axis acceleration changes speed by .08 m/s.
    const Case cases[] = {
        {{0, 0}, {1, 1}, {.056568544f, .056568544f},
         "Diagonal acceleration must share the straight-line budget"},
        {{3, 3}, {0, 0}, {2.886862912f, 2.886862912f},
         "Diagonal braking must share the double-strength budget"},
        {{3, 0}, {0, 1}, {2.886862912f, .056568544f},
         "A right turn must brake forward at twice the sideways acceleration"},
        {{0, -3}, {-1, 0}, {-.056568544f, -2.886862912f},
         "Negative-axis movement must use the same mixed braking and acceleration"},
        {{.04f, 0}, {-1, 0}, {-.06f, 0},
         "Reversal within a tick must brake to zero before using the remaining time to accelerate"},
        {{.04f, 0}, {-1, 1}, {-.036568544f, .056568544f},
         "Reversing one axis must share effort with acceleration on the other"},
        {{1.78f, 0}, {.6f, .8f}, {1.799402848f, .0776114f},
         "An axis near its target must leave more effort for sideways acceleration without overshooting"},
        {{.04f, 0}, {0, 1}, {.001194296f, .0776114f},
         "An axis almost stopped must leave more effort for sideways acceleration"},
        {{0, 0}, {.0003f, .0004f}, {.0009f, .0012f},
         "Small analog targets within the budget must be reached without overshooting"},
        {{.003f, .004f}, {0, 0}, {0, 0},
         "Braking within the budget must stop both axes exactly"},
        {{3, 0}, {1, 0}, {3, 0},
         "An axis already at target must preserve its world momentum"},
    };
    for (const float yaw : {0.f, 37.f, 90.f, -123.f}) {
        const float radians = glm::radians(yaw);
        const glm::vec2 forward(std::cos(radians), std::sin(radians));
        const glm::vec2 right(-forward.y, forward.x);
        for (const auto& check : cases) {
            auto player = spawned(world);
            auto state = player.movementState();
            state.horizontalVelocity = forward * check.velocity.x + right * check.velocity.y;
            player.restoreMovement(state);
            viewer::FPSInput input;
            input.yawDegrees = yaw; input.forward = check.input.x; input.right = check.input.y;
            player.update(1.f / 120, input, world);
            const auto expected = forward * check.expected.x + right * check.expected.y;
            require(glm::length(player.movementState().horizontalVelocity - expected) < .000002f, check.message);
            if (check.expected == glm::vec2(0))
                require(player.movementState().horizontalVelocity == glm::vec2(0), check.message);
        }
    }

    auto player = spawned(world);
    auto state = player.movementState();
    state.horizontalVelocity = {3, 0};
    state.movementIntent = {1, 0};
    player.restoreMovement(state);
    viewer::FPSInput input; input.forward = 1; input.yawDegrees = 90;
    player.update(1.f / 120, input, world);
    require(glm::length(player.movementState().horizontalVelocity - glm::vec2(2.886862912f, .056568544f)) < .000002f,
            "Turning the camera must steer existing world momentum gradually");
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
    run(player, world, 3.0F, toward, 30);
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
    run(sliding, thinWorld, 6.0F, toward, 30);
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
    run(player, world, 4.0F, walk);
    const float expectedLift = player.config().radius * (std::sqrt(1.16F) - 1.0F);
    require(player.feet().x > 5.8F && player.grounded(), "Player cannot climb a moderate slope");
    require(std::abs(player.feet().y - player.feet().x * 0.4F - expectedLift) < 0.015F,
            "Capsule fails to follow the sloped ground height");
    require(player.groundNormal().y > 0.92F && player.groundNormal().y < 0.94F,
            "Slope ground normal is incorrect");
    walk.forward = -1.0F;
    run(player, world, 8.0F, walk);
    require(player.feet().x < -5.7F && player.grounded() &&
                std::abs(player.feet().y - player.feet().x * 0.4F - expectedLift) < 0.015F,
            "Player floats or loses ground contact while descending a slope");
    run(player, world, 1.25F); // Finish braking before checking idle slope stability.
    const glm::vec3 resting = player.feet();
    run(player, world, 3.0F);
    require(glm::length(player.feet() - resting) < 0.015F && player.grounded(),
            "Player drifts or jitters while standing on a slope");

    viewer::Model edge;
    floor(edge, -20, 0, 0.4F);
    viewer::CollisionWorld edgeWorld(edge);
    auto offEdge = spawned(edgeWorld, {-1,1.3F,0});
    walk.forward = 1.0F;
    run(offEdge, edgeWorld, 2.5F, walk);
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
    require(!player.startFalling({0,10,0}, world), "Falling placement succeeds without ground below");
    const glm::vec3 before = player.feet();
    player.update(-1.0F, {}, world);
    player.update(std::numeric_limits<float>::quiet_NaN(), {}, world);
    require(player.feet() == before, "Invalid delta time changes player position");

    viewer::Model model;
    floor(model);
    const viewer::CollisionWorld terrain(model);
    require(player.startFalling({0,10,0}, terrain), "Cannot start falling above terrain");
    run(player, terrain, .1f);
    const auto feet = player.feet();
    const auto velocity = player.verticalVelocity();
    for (const auto eye : {glm::vec3(0, std::numeric_limits<float>::quiet_NaN(), 0),
                           glm::vec3(std::numeric_limits<float>::infinity(), 10, 0),
                           glm::vec3(0,1,0), glm::vec3(200,10,0)}) {
        require(!player.startFalling(eye, terrain) && player.feet() == feet &&
                    player.verticalVelocity() == velocity && !player.grounded(),
                "Rejected falling placement changed an active player");
    }
}

void testInvalidPostureConfigurationAndState() {
    for (const bool prone : {false, true}) {
        for (const float seconds : {0.f, -.01f, std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity()}) {
            viewer::FPSConfig config;
            (prone ? config.proneTransitionSeconds : config.crouchTransitionSeconds) = seconds;
            bool rejected = false;
            try { viewer::FPSController candidate(config); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Each posture transition duration must be finite and positive");
        }
    }

    viewer::FPSController player;
    player.teleport({2, 3, 4});
    const auto before = player.movementState();
    for (const float posture : {-.01f, 2.01f, std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity()}) {
        auto invalid = before;
        invalid.feet = {100, 100, 100};
        invalid.posture = posture;
        bool rejected = false;
        try { player.restoreMovement(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && player.feet() == before.feet && player.movementState().posture == before.posture,
                "Movement restore must reject nonfinite or out-of-range posture without modifying the controller");
    }
    for (const float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto invalid = before;
        invalid.feet = {100, 100, 100};
        invalid.horizontalVelocity.x = value;
        bool rejected = false;
        try { player.restoreMovement(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && player.feet() == before.feet,
                "Movement restore must reject nonfinite acceleration velocity without modifying the controller");
    }
}

}  // namespace

int main() {
    try {
        testFlatMovementAndTime();
        testDirectionalSpeedsAndAcceleration();
        testAxisAccelerationBudget();
        testGravityJumpAndCeiling();
        testWallsAndBuildingSpawn();
        testSlopesAndEdges();
        testInvalidInputsAndMissingGround();
        testInvalidPostureConfigurationAndState();
        std::cout << "FPS flat ground, frame rates, gravity, jumps, ceilings, walls, sliding, "
                     "spawn, slopes, steep slopes, and mesh edges passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FPS test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
