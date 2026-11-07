#include "player_injury_movement.hpp"
#include "player_vitals.hpp"
#include "camera_rig.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace viewer;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < .00001f, message);
}
Model floor() {
    Model model;
    Primitive primitive;
    for (const glm::vec3 position : {glm::vec3(-100, 0, -100), {-100, 0, 100},
                                    {100, 0, 100}, {100, 0, -100}}) {
        Vertex vertex; vertex.position = position; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(primitive);
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    return model;
}
FPSController spawn(const CollisionWorld& world, const PlayerHealth& health, float stamina = 1) {
    FPSConfig config;
    config.exertion.runningDrain = config.exertion.walkingRecovery = config.exertion.strainRate = 0;
    FPSController player(config);
    require(player.spawn({0, 1.7f, 0}, world), "Fixture must spawn on flat ground");
    player.setMovementHealth(health);
    auto state = player.movementState(); state.exertion.stamina = stamina; player.restoreMovement(state);
    return player;
}
void bodyPartsAndAggregation() {
    const InjuryBodyPart left[] = {InjuryBodyPart::LeftFoot, InjuryBodyPart::LeftAnkle,
        InjuryBodyPart::LeftLowerLeg, InjuryBodyPart::LeftKnee, InjuryBodyPart::LeftUpperLeg, InjuryBodyPart::LeftHip};
    const InjuryBodyPart right[] = {InjuryBodyPart::RightFoot, InjuryBodyPart::RightAnkle,
        InjuryBodyPart::RightLowerLeg, InjuryBodyPart::RightKnee, InjuryBodyPart::RightUpperLeg, InjuryBodyPart::RightHip};
    for (int part = 0; part < static_cast<int>(InjuryBodyPart::Count); ++part) {
        const auto bodyPart = static_cast<InjuryBodyPart>(part);
        bool leftLeg = false, rightLeg = false;
        for (const auto legPart : left) leftLeg |= bodyPart == legPart;
        for (const auto legPart : right) rightLeg |= bodyPart == legPart;
        PlayerHealth health;
        health.addInjury({bodyPart, InjuryType::Bruise, 0, 1});
        const auto movement = playerInjuryMovement(health);
        near(movement.speed, leftLeg || rightLeg ? .75f : 1.f, "Only leg parts may reduce movement speed");
        near(movement.forwardAcceleration, leftLeg || rightLeg ? .5f : 1.f, "Either leg must affect forward acceleration");
        near(movement.leftAcceleration, rightLeg ? .5f : 1.f, "Only right-leg injuries may reduce left acceleration");
        near(movement.rightAcceleration, leftLeg ? .5f : 1.f, "Only left-leg injuries may reduce right acceleration");
    }
    PlayerHealth health;
    health.setPainTolerance(.5f);
    health.addInjury({InjuryBodyPart::LeftAnkle, InjuryType::Sprain, .9f, .2f});
    health.addInjury({InjuryBodyPart::LeftFoot, InjuryType::Cut, .2f, .7f});
    health.addInjury({InjuryBodyPart::RightKnee, InjuryType::Bruise, .4f, .3f});
    auto movement = playerInjuryMovement(health);
    near(movement.speed, .8f, "Largest pain must be reduced by tolerance/5 before dividing by four");
    near(movement.forwardAcceleration, .6f, "Acceleration must divide the same adjusted pain by two");
    near(movement.rightAcceleration, .6f, "Pain and weakness from different left-leg injuries must be compared independently");
    near(movement.leftAcceleration, .85f, "The opposite leg's weaker injury must retain its own acceleration penalty");
    health.addInjury({InjuryBodyPart::LeftAnkle, InjuryType::Bruise, .7f, .5f});
    movement = playerInjuryMovement(health);
    near(movement.speed, .8f, "Multiple injuries must use maxima without adding penalties");
    health.addInjury({InjuryBodyPart::RightHip, InjuryType::Fracture, 0, .9f});
    movement = playerInjuryMovement(health);
    near(movement.speed, .775f, "Weakness must win when worse than pain");
    near(movement.forwardAcceleration, .55f, "Forward and backward acceleration must use the worse leg");
    near(movement.leftAcceleration, .55f, "Right-leg weakness must limit left acceleration");

    PlayerHealth tolerant;
    tolerant.setPainTolerance(1);
    tolerant.addInjury({InjuryBodyPart::LeftFoot, InjuryType::Cut, .1f, 0});
    near(playerInjuryMovement(tolerant).speed, 1, "Pain below tolerance/5 must clamp to zero penalty");
    near(playerInjuryMovement(tolerant).rightAcceleration, 1, "Pain tolerance must never grant an acceleration boost");
}
void speedsAndStamina(const CollisionWorld& world) {
    PlayerHealth health;
    health.addInjury({InjuryBodyPart::LeftKnee, InjuryType::Sprain, 0, .8f});
    auto player = spawn(world, health, 0);
    near(player.movementSpeed(), 2.4f, "Leg weakness must reduce walking speed");
    near(player.movementSpeed(true), 4.8f, "The worse injury must win over stamina without multiplying penalties");
    near(player.movementSpeed(true, -1, 0), 3.6f, "Backward speed must use both legs");
    near(player.movementSpeed(true, 0, 1), 4.2f, "Rightward speed must include the injured left leg");
    near(player.movementSpeed(true, 0, -1), 4.2f, "Leftward speed must include the injured left leg");
    FPSInput input; input.forward = 1; input.yawDegrees = 0; input.run = true;
    player.update(1.f / 120, input, world);
    near(player.movementState().horizontalVelocity.x, .034f, "Stamina must win when worse than the injury acceleration penalty");

    PlayerHealth mild;
    mild.addInjury({InjuryBodyPart::RightFoot, InjuryType::Cut, .2f, .1f});
    auto tired = spawn(world, mild, 0);
    near(tired.movementSpeed(true), 5.04f, "Stamina must win when worse than injury speed penalties");
    auto fresh = spawn(world, health);
    fresh.update(1.f / 120, input, world);
    near(fresh.movementState().horizontalVelocity.x, .048f, "Injury acceleration must win over healthy stamina");

    for (const auto stance : {PlayerStance::Crouched, PlayerStance::Prone}) {
        auto state = player.movementState(); state.stance = state.takeoffStance = stance;
        player.restoreMovement(state);
        near(player.movementSpeed(true), stance == PlayerStance::Crouched ? 1.2f : .6f,
             "Injury penalties must also apply to crouching and crawling");
    }
}
void directionalAcceleration(const CollisionWorld& world) {
    for (const bool injuredLeft : {false, true}) {
        PlayerHealth health;
        health.addInjury({injuredLeft ? InjuryBodyPart::LeftKnee : InjuryBodyPart::RightKnee, InjuryType::Sprain, 0, 1});
        for (const float yaw : {0.f, 37.f, 90.f}) {
            const float radians = glm::radians(yaw);
            const glm::vec2 forward(std::cos(radians), std::sin(radians));
            const glm::vec2 right(-forward.y, forward.x);
            for (const float direction : {-1.f, 1.f}) {
                auto player = spawn(world, health);
                FPSInput input; input.yawDegrees = yaw; input.right = direction;
                player.update(1.f / 120, input, world);
                const bool useInjury = injuredLeft == (direction > 0);
                near(glm::dot(player.movementState().horizontalVelocity, right), direction * (useInjury ? .04f : .08f),
                     "Lateral acceleration must use the opposite leg relative to the current camera yaw");
                near(glm::dot(player.movementState().horizontalVelocity, forward), 0, "Strafing must not inject forward movement");
            }
        }
    }
    PlayerHealth health;
    health.addInjury({InjuryBodyPart::LeftFoot, InjuryType::Cut, 0, 1});
    auto player = spawn(world, health);
    FPSInput input; input.forward = 1; input.right = -1; input.yawDegrees = 0;
    player.update(1.f / 120, input, world);
    near(player.movementState().horizontalVelocity.x, .028284272f, "Diagonal forward acceleration must share effort at its injured rate");
    near(player.movementState().horizontalVelocity.y, -.056568544f, "Diagonal left acceleration must share effort at its healthy rate");
    auto state = player.movementState(); state.horizontalVelocity = {0, .24f}; player.restoreMovement(state);
    input.forward = input.right = 0;
    player.update(1.f / 120, input, world);
    near(player.movementState().horizontalVelocity.y, .08f, "Braking rightward momentum uses leftward force from the right leg");
    state = player.movementState(); state.horizontalVelocity = {0, -.24f}; player.restoreMovement(state);
    player.update(1.f / 120, input, world);
    near(player.movementState().horizontalVelocity.y, -.16f, "Braking leftward momentum must retain double the injured rightward acceleration");
}
void ownershipAndRestore(const CollisionWorld& world) {
    PlayerVitals vitals;
    vitals.healthState().addInjury({InjuryBodyPart::RightAnkle, InjuryType::Sprain, 1, .4f});
    vitals.healthState().setPainTolerance(.5f);
    vitals.applyDamage(10);
    near(vitals.healthState().health(), 90, "Combat damage and medical records must share one health owner");
    require(vitals.healthState().injuries().size() == 1, "Combat damage must retain existing injuries");
    auto player = spawn(world, vitals.healthState());
    const auto checkpoint = player.movementState();
    FPSController restored; restored.restoreMovement(checkpoint);
    FPSInput input; input.right = -1; input.yawDegrees = 37;
    for (int tick = 0; tick < 90; ++tick) {
        player.update(1.f / 120, input, world); restored.update(1.f / 120, input, world);
    }
    require(player.feet() == restored.feet() && player.movementState().horizontalVelocity == restored.movementState().horizontalVelocity,
            "Movement restore must retain injury penalties for deterministic replay");
    for (const float invalid : {-1.f, 0.f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = player.movementState(); bad.injuryMovement.leftAcceleration = invalid;
        bool rejected = false;
        try { player.restoreMovement(bad); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Restore must reject invalid injury movement multipliers");
    }
    CameraRig rig(world, {-100, 0, -100}, {100, 5, 100}, 1);
    rig.setMovementHealth(vitals.healthState());
    require(rig.enterWalking(glm::vec3(0, 1.7f, 0)), "Camera fixture must enter walking");
    near(rig.player().movementSpeed(), 2.325f, "Entering walking mode must preserve injury penalties");
    rig.enterFreeFly();
    require(rig.enterWalking(glm::vec3(2, 1.7f, 0)), "Camera fixture must reenter walking");
    near(rig.player().movementSpeed(), 2.325f, "Recreating the controller must not clear injuries");
}
} // namespace

int main() {
    try {
        const auto model = floor(); const CollisionWorld world(model);
        bodyPartsAndAggregation(); speedsAndStamina(world); directionalAcceleration(world); ownershipAndRestore(world);
        std::cout << "Player injury movement tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player injury movement test failed: " << error.what() << '\n';
        return 1;
    }
}
