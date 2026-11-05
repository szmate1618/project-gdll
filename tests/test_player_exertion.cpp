#include "fps_controller.hpp"
#include "collision_world.hpp"
#include "camera_rig.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message, float tolerance = .0001f) {
    require(std::abs(actual - expected) < tolerance, message);
}
viewer::Model floor() {
    viewer::Model model;
    viewer::Primitive primitive;
    for (const glm::vec3 position : {glm::vec3(-2000, 0, -2000), {-2000, 0, 2000},
                                    {2000, 0, 2000}, {2000, 0, -2000}}) {
        viewer::Vertex vertex; vertex.position = position; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(primitive);
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    return model;
}
viewer::FPSController spawn(const viewer::CollisionWorld& world, viewer::FPSConfig config = {}, float stamina = 1) {
    viewer::FPSController player(config);
    require(player.spawn({0, 1.7f, 0}, world), "Fixture must spawn on flat ground");
    auto state = player.movementState(); state.exertion.stamina = stamina; player.restoreMovement(state);
    return player;
}
void run(viewer::FPSController& player, const viewer::CollisionWorld& world, int ticks, viewer::FPSInput input = {}) {
    for (int i = 0; i < ticks; ++i) player.update(player.config().fixedDt, input, world);
}
void curvesAndDepletion() {
    viewer::PlayerExertion exertion;
    near(exertion.speedMultiplier(), 1, "Fresh running speed must be unchanged");
    for (const auto stamina : {1.f, .5f, .25f}) {
        exertion.stamina = stamina;
        near(exertion.accelerationMultiplier(), 1, "Acceleration must remain unchanged through 25% stamina");
        near(exertion.speedMultiplier(), 1, "Running speed must remain unchanged through 25% stamina");
    }
    exertion.stamina = .125f;
    near(exertion.speedMultiplier(), .92f, "Half exhaustion must retain 92% running speed");
    near(exertion.accelerationMultiplier(), .7125f, "Half exhaustion must retain 71.25% acceleration");
    exertion.stamina = 0;
    near(exertion.speedMultiplier(), .84f, "Exhausted running must retain 84% speed");
    near(exertion.accelerationMultiplier(), .425f, "Exhausted acceleration must retain 42.5%");

    viewer::PlayerExertionConfig config;
    exertion = {.02f, 0, 0};
    exertion.advance(1, true, 1, config);
    near(exertion.stamina, 0, "A tick crossing depletion must clamp stamina at zero");
    near(exertion.strain, config.strainRate * .5f, "Only time spent exhausted must accumulate strain");
    exertion = {0, 0, 2};
    exertion.advance(1, true, 1, config);
    near(exertion.strain, config.strainRate * 2, "Movement load must multiply exhausted strain");
    exertion = {1, 0, 2};
    exertion.advance(1, true, 1, config);
    near(exertion.stamina, .92f, "Movement load must multiply stamina consumption");
    const auto strain = exertion.strain;
    exertion.advance(1, false, 1, config);
    near(exertion.strain, strain, "Rest must preserve accumulated strain independently of stamina");
    exertion.decay(2, config);
    near(exertion.load, .7357589f, "Movement load must retain exp(-1) after two seconds");
}
void movementPercentages(const viewer::CollisionWorld& world) {
    viewer::FPSConfig config;
    config.walkSpeed = 4; config.runSpeed = 9;
    config.exertion.runningDrain = config.exertion.walkingRecovery = config.exertion.strainRate = 0;
    auto fresh = spawn(world, config), tired = spawn(world, config, 0);
    for (const auto direction : {glm::vec2(1, 0), {-1, 0}, {0, 1}, {1, 1}})
        near(tired.movementSpeed(true, direction.x, direction.y),
             fresh.movementSpeed(true, direction.x, direction.y) * .84f,
             "Fatigue must multiply every configured running direction");
    near(tired.movementSpeed(false), 4, "Fatigue must preserve configured walking speed");
    viewer::FPSInput input; input.yawDegrees = 0; input.forward = 1; input.run = true;
    run(fresh, world, 1, input); run(tired, world, 1, input);
    near(tired.movementState().horizontalVelocity.x, fresh.movementState().horizontalVelocity.x * .425f,
         "Starting acceleration must use the percentage of the configured base");
    for (const bool reverse : {false, true}) {
        for (auto* player : {&fresh, &tired}) {
            auto state = player->movementState(); state.horizontalVelocity = {3, 0}; player->restoreMovement(state);
        }
        input.forward = reverse ? -1 : 0;
        run(fresh, world, 1, input); run(tired, world, 1, input);
        near(3 - tired.movementState().horizontalVelocity.x, (3 - fresh.movementState().horizontalVelocity.x) * .425f,
             "Stopping and reversal must scale the existing braking acceleration");
    }
    input.forward = 1;
    run(tired, world, 3600, input);
    near(tired.movementState().horizontalVelocity.x, 7.56f, "Running must continue at 84% of custom speed with zero stamina");
}
void movementLoad(const viewer::CollisionWorld& world) {
    viewer::FPSConfig config;
    config.exertion.runningDrain = config.exertion.walkingRecovery = 0;
    auto fast = spawn(world, config), slow = spawn(world, config);
    for (auto* player : {&fast, &slow}) {
        auto state = player->movementState();
        state.horizontalVelocity = {player == &fast ? 6.f : .1f, 0};
        state.movementIntent = {1, 0}; player->restoreMovement(state);
    }
    viewer::FPSInput input; input.yawDegrees = 0; input.forward = -1; input.run = true;
    run(fast, world, 1, input); run(slow, world, 1, input);
    near(fast.exertion().load, .55f, "Full-speed reversal must add 0.55 load once");
    require(slow.exertion().load < .01f, "Reversing while barely moving must be cheap");
    run(fast, world, 120, input);
    near(fast.exertion().load, .3335919f, "Holding the reversal target must only decay existing load");

    auto jumper = spawn(world, config);
    input = {}; input.jump = true;
    run(jumper, world, 1, input);
    near(jumper.exertion().load, .4f, "Successful jump must add load");
    float beforeLanding = 0;
    while (!jumper.grounded()) {
        beforeLanding = jumper.exertion().load;
        run(jumper, world, 1, input);
    }
    require(jumper.exertion().load > beforeLanding + .14f, "Actual landing must add load once");
    run(jumper, world, 120, input);
    require(jumper.grounded(), "Holding jump must not repeatedly jump or charge landing load");
}
void recovery(const viewer::CollisionWorld& world) {
    auto walking = spawn(world, {}, 0), crouched = spawn(world, {}, 0), prone = spawn(world, {}, 0), seated = spawn(world, {}, 0);
    viewer::FPSInput input; input.forward = 1; input.yawDegrees = 0;
    run(walking, world, 7200, input);
    near(walking.exertion().stamina, .5f, "Walking must recover half stamina in one minute", .0002f);
    run(walking, world, 7200, input);
    near(walking.exertion().stamina, 1, "Walking must fully recover stamina in two minutes", .0002f);
    input = {}; input.crouch = true;
    run(crouched, world, 3600, input);
    near(crouched.exertion().stamina, .5f, "Crouching must recover twice as fast", .0002f);
    input = {}; input.toggleProne = true; run(prone, world, 1, input);
    input.toggleProne = false; run(prone, world, 1800, input);
    near(prone.exertion().stamina, .5f, "Prone must recover four times as fast", .002f);
    for (int i = 0; i < 3600; ++i) seated.restSeated(1.f / 120);
    near(seated.exertion().stamina, .5f, "Car seats must recover twice as fast", .0002f);
    const auto stamina = seated.exertion().stamina;
    seated.teleport({1, 0, 1});
    near(seated.exertion().stamina, stamina, "Repositioning must not refill stamina");
}
void fixedStepsAndRestore(const viewer::CollisionWorld& world) {
    viewer::FPSState reference;
    for (const int fps : {30, 60, 144}) {
        auto player = spawn(world, {}, .3f);
        // Derive frame duration from the same float timestep as the controller.
        // Nominal 1/144 otherwise ends one substep short of 1200 float ticks.
        const float frameDt = static_cast<float>(double(player.config().fixedDt) * 120 / fps);
        for (int frame = 0; frame < fps * 10; ++frame) {
            viewer::FPSInput input; input.yawDegrees = 0; input.run = true;
            input.forward = (frame / fps) % 2 ? -1 : 1;
            input.jump = frame % fps == 0;
            player.update(frameDt, input, world);
        }
        const auto state = player.movementState();
        if (fps == 30) reference = state;
        else {
            near(state.exertion.stamina, reference.exertion.stamina, "Stamina must be independent of render frame rate");
            near(state.exertion.load, reference.exertion.load, "Load must be independent of render frame rate");
            near(state.exertion.strain, reference.exertion.strain, "Strain must be independent of render frame rate");
            require(glm::length(state.feet - reference.feet) < .001f, "Fatigued movement must be independent of render frame rate");
        }
        auto restored = spawn(world); restored.restoreMovement(state);
        viewer::FPSInput input; input.forward = -1; input.run = true; input.yawDegrees = 90;
        run(player, world, 120, input); run(restored, world, 120, input);
        require(player.feet() == restored.feet() && player.exertion().load == restored.exertion().load &&
                player.exertion().strain == restored.exertion().strain, "Restore must retain all fatigue and intent history for replay");
    }
    auto player = spawn(world);
    for (const float invalid : {-1.f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
        auto state = player.movementState(); state.exertion.stamina = invalid;
        bool rejected = false;
        try { player.restoreMovement(state); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Movement restore must reject invalid stamina");
    }
}
void cameraModePreservesExertion(const viewer::CollisionWorld& world) {
    viewer::CameraRig rig(world, {-20, 0, -20}, {20, 3, 20}, 1);
    require(rig.enterWalking(glm::vec3(0, 1.7f, 0)), "Camera fixture must start walking");
    for (int i = 0; i < 120; ++i) rig.update(1.f / 120, 1, 0, 0, true, false);
    const auto stamina = rig.player().exertion().stamina;
    require(stamina < .97f, "Camera fixture must expend stamina before changing modes");
    rig.enterFreeFly();
    rig.restSeated(1);
    const auto recovered = rig.player().exertion().stamina;
    require(recovered > stamina, "Standalone car rest must recover the walking player's stamina");
    require(rig.enterWalking(glm::vec3(2, 1.7f, 0)), "Camera must return to walking after leaving the car");
    near(rig.player().exertion().stamina, recovered, "Recreating the walking controller must preserve recovered stamina");
}
} // namespace

int main() {
    try {
        const auto model = floor(); const viewer::CollisionWorld world(model);
        curvesAndDepletion(); movementPercentages(world); movementLoad(world); recovery(world); fixedStepsAndRestore(world);
        cameraModePreservesExertion(world);
        std::cout << "Player exertion tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player exertion failed: " << error.what() << '\n'; return 1;
    }
}
