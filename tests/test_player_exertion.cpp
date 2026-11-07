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
    exertion.decay(2, config);
    near(exertion.load, .7357589f, "Movement load must retain exp(-1) after two seconds");
}
void strainPenalties(const viewer::CollisionWorld& world) {
    viewer::PlayerExertionConfig config;
    config.strainRecoveryPerDay = 0; // Isolate stamina penalties at constant strain.
    struct Case { float strain, maximum, regeneration; };
    for (const auto test : {Case{0, 1, 1}, {.25f, .95f, .875f}, {.5f, .9f, .75f}, {1, .8f, .5f}, {2, .8f, .5f}}) {
        viewer::PlayerExertion exertion{0, test.strain, 0};
        near(exertion.maximumStamina(), test.maximum, "Strain must linearly reduce stamina capacity, stopping at 80%");
        near(exertion.regenerationMultiplier(), test.regeneration, "Strain must linearly reduce regeneration, stopping at 50%");
        for (const float postureMultiplier : {1.f, 2.f, 4.f}) {
            exertion.stamina = 0;
            exertion.advance(1, false, postureMultiplier, config);
            near(exertion.stamina, config.walkingRecovery * postureMultiplier * test.regeneration,
                 "Strain must multiply each existing recovery rate");
            exertion.advance(600, false, postureMultiplier, config);
            near(exertion.stamina, test.maximum, "Rest must stop at the strain-limited stamina capacity");
            require(exertion.valid(), "Recovery must leave a valid movement checkpoint");
        }
    }
    viewer::PlayerExertion capped{1, 1, 0};
    require(!capped.valid(), "Movement checkpoints must reject stamina above the strain-limited capacity");
    capped.advance(0, true, 1, config);
    near(capped.stamina, .8f, "Exertion updates must clamp excess stamina even while running");
    capped.stamina = .125f;
    near(capped.speedMultiplier(), .92f, "Strain must preserve the absolute low-stamina movement thresholds");

    viewer::FPSConfig fixedStrain;
    fixedStrain.exertion = config;
    auto walking = spawn(world, fixedStrain, 0), crouched = spawn(world, fixedStrain, 0),
         prone = spawn(world, fixedStrain, 0), seated = spawn(world, fixedStrain, 0);
    for (auto* player : {&walking, &crouched, &prone, &seated}) {
        auto state = player->movementState(); state.exertion.strain = 1; player->restoreMovement(state);
    }
    viewer::FPSInput input; input.forward = 1; input.yawDegrees = 0;
    run(walking, world, 7200, input);
    near(walking.exertion().stamina, .25f, "At 100% strain walking must recover 25 percentage points per minute", .0002f);
    input = {}; input.crouch = true; run(crouched, world, 3600, input);
    near(crouched.exertion().stamina, .25f, "Strain must halve grounded crouch recovery", .0002f);
    input = {}; input.toggleProne = true; run(prone, world, 1, input);
    input.toggleProne = false; run(prone, world, 1800, input);
    near(prone.exertion().stamina, .25f, "Strain must halve grounded prone recovery", .002f);
    for (int tick = 0; tick < 3600; ++tick) seated.restSeated(1.f / 120);
    near(seated.exertion().stamina, .25f, "Strain must halve seated recovery", .0002f);
    run(walking, world, 15840, viewer::FPSInput{});
    near(walking.exertion().stamina, .8f, "At 100% strain walking must reach its 80% capacity after 192 seconds", .0002f);

    auto invalid = walking.movementState(); invalid.exertion.stamina = 1;
    bool rejected = false;
    try { walking.restoreMovement(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Reconciliation must reject stamina above the strain-limited maximum");
}
void strainRecovery(const viewer::CollisionWorld& world) {
    viewer::PlayerExertionConfig config;
    for (const double daySeconds : {1200., 2400.}) {
        config.dayLengthSeconds = daySeconds;
        viewer::PlayerExertion awake{.5f, 1, 0}, asleep = awake;
        // Eight game hours: 25 percentage points awake, 50 asleep.
        awake.advance(static_cast<float>(daySeconds / 3), false, 1, config);
        asleep.advance(static_cast<float>(daySeconds / 3), false, 4, config, 2);
        near(awake.strain, .75f, "Awake strain recovery must follow the configured in-game day");
        near(asleep.strain, .5f, "Sleep must double base strain recovery");
        awake.advance(static_cast<float>(daySeconds), false, 1, config);
        near(awake.strain, 0, "Strain must recover fully and stop at zero");
        require(awake.valid() && asleep.valid(), "Recovery must respect the expanding stamina capacity");
    }
    config = {};
    viewer::PlayerExertion continuous{.5f, 1, 0};
    for (int tick = 0; tick < 120 * 1200; ++tick) continuous.advance(1.f / 120, false, 1, config);
    near(continuous.strain, .25f, "Fixed steps must recover 75 percentage points per game day", .004f);

    config.dayLengthSeconds = 24;
    config.walkingRecovery = 0;
    viewer::PlayerExertion exhausted{0, 1, 0};
    exhausted.advance(8, false, 1, config);
    near(exhausted.strain, 1, "Zero stamina must block recovery when stamina cannot regenerate");
    exhausted.stamina = .001f;
    exhausted.advance(8, false, 1, config);
    near(exhausted.strain, .75f, "Any positive stamina must allow strain recovery");
    exhausted = {0, 1, 0};
    config.walkingRecovery = viewer::PlayerExertionConfig{}.walkingRecovery;
    exhausted.advance(8, false, 1, config);
    near(exhausted.strain, .75f, "Regaining stamina must resume strain recovery immediately");

    viewer::PlayerExertion runner{.02f, 1, 0};
    runner.advance(1, true, 1, config);
    near(runner.strain, 1 - .015625f + config.strainRate * .5f,
         "Depletion must split fueled recovery from exhausted strain accumulation");
    config.dayLengthSeconds = 0;
    exhausted = {.5f, 1, 0};
    exhausted.advance(8, false, 1, config);
    near(exhausted.strain, 1, "A frozen calendar must freeze calendar-based strain recovery");

    // All controller paths share the same calendar rate, including seated rest
    // and automatic guard duty. Stamina posture multipliers do not affect strain.
    for (int mode = 0; mode < 6; ++mode) {
        auto player = spawn(world);
        player.setDayLengthSeconds(24);
        auto state = player.movementState(); state.exertion = {.5f, 1, 0}; player.restoreMovement(state);
        viewer::FPSInput input; input.forward = 1;
        input.crouch = mode == 1;
        for (int tick = 0; tick < 120 * 8; ++tick) {
            input.toggleProne = mode == 2 && tick == 0;
            if (mode < 3) player.update(1.f / 120, input, world);
            else if (mode == 3) player.restSeated(1.f / 120);
            else if (mode == 4) player.standGuard(1.f / 120);
            else player.restSleeping(1.f / 120);
        }
        near(player.exertion().strain, mode == 5 ? .5f : .75f,
             "Every controller path must apply its calendar-based strain recovery", .0001f);
        require(player.exertion().valid(), "Strain recovery must preserve movement checkpoint validity");
    }
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
        curvesAndDepletion(); strainPenalties(world); strainRecovery(world);
        movementPercentages(world); movementLoad(world); recovery(world); fixedStepsAndRestore(world);
        cameraModePreservesExertion(world);
        std::cout << "Player exertion tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player exertion failed: " << error.what() << '\n'; return 1;
    }
}
