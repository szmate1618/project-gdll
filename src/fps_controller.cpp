#include "fps_controller.hpp"

#include "collision_world.hpp"
#include "character_step.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {

constexpr int kResolutionIterations = 16;
constexpr float kContactTolerance = 0.0001F;

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

}  // namespace

FPSController::FPSController(FPSConfig config) : config_(config) {
    const float values[] = {config.eyeHeight, config.height, config.radius, config.walkSpeed,
                            config.runSpeed, config.gravity, config.jumpVelocity,
                            config.maxSlopeDegrees, config.stepHeight, config.groundSnap,
                            config.skin, config.fixedDt, config.terminalVelocity,
                            config.crouchHeight, config.proneHeight, config.crouchSpeed, config.proneSpeed,
                            config.proneEyeHeight, config.proneWidth, config.proneLength,
                            config.crouchTransitionSeconds, config.proneTransitionSeconds,
                            config.exertion.runningDrain, config.exertion.strainRate,
                            config.exertion.walkingRecovery, config.exertion.crouchedRecoveryMultiplier,
                            config.exertion.seatedRecoveryMultiplier, config.exertion.proneRecoveryMultiplier,
                            config.exertion.loadDecaySeconds, config.exertion.loadDrainMultiplier,
                            config.exertion.directionLoad, config.exertion.jumpLoad, config.exertion.landingLoad};
    for (float value : values) {
        if (!std::isfinite(value) || value < 0.0F) {
            throw std::invalid_argument("FPS configuration must contain finite, nonnegative values");
        }
    }
    if (config.runSpeed <= config.walkSpeed || config.radius <= 0.0F || config.height < 2.0F * config.radius ||
        config.eyeHeight <= 0.0F || config.eyeHeight > config.height ||
        config.fixedDt <= 0.0F || config.fixedDt > 0.05F ||
        config.maxSlopeDegrees >= 89.0F || config.terminalVelocity <= 0.0F ||
        config.proneHeight <= 0 || config.crouchHeight < config.proneHeight ||
        config.proneEyeHeight <= 0 || config.proneEyeHeight > config.proneHeight ||
        config.proneWidth <= 0 || config.proneLength <= 0 ||
        config.crouchTransitionSeconds <= 0 || config.proneTransitionSeconds <= 0 ||
        config.exertion.loadDecaySeconds <= 0) {
        throw std::invalid_argument("Invalid FPS capsule, timestep, slope, or speed configuration (run must exceed walk)");
    }
}

bool FPSController::walkable(glm::vec3 normal) const {
    return normal.y >= std::cos(glm::radians(config_.maxSlopeDegrees));
}

void FPSController::teleport(glm::vec3 feet) {
    if (!finite(feet)) {
        throw std::invalid_argument("FPS position must be finite");
    }
    feet_ = feet;
    verticalVelocity_ = 0.0F;
    horizontalVelocity_ = {0, 0};
    movementIntent_ = {0, 0};
    groundNormal_ = {0.0F, 1.0F, 0.0F};
    accumulator_ = 0.0;
    grounded_ = false;
    jumpHeld_ = false;
    jumpQueued_ = false;
    stance_ = PlayerStance::Standing;
    takeoffStance_ = PlayerStance::Standing;
    proneRequested_ = proneQueued_ = false;
    bodyYawDegrees_ = -90;
    posture_ = 0;
    noiseEmitter_.reset();
    noises_.clear();
}

bool FPSController::spawn(glm::vec3 preferredEye, const CollisionWorld& world) {
    const auto position = world.findSpawn(preferredEye, config_.eyeHeight, config_.radius,
                                          config_.height, config_.maxSlopeDegrees);
    if (!position) {
        return false;
    }
    teleport(*position);
    snapToGround(world, config_.groundSnap);
    return true;
}

bool FPSController::startFalling(glm::vec3 eye, const CollisionWorld& world) {
    const glm::vec3 feet = eye - glm::vec3(0, config_.eyeHeight, 0);
    if (!finite(feet)) return false;
    for (const auto& contact : world.contacts(feet, config_.radius, config_.height)) {
        if (contact.depth > kContactTolerance) return false;
    }
    // Keep recovery for cameras outside the map or below its surface. Roofs
    // and indoor terrain count as support; vegetation never supplies ground.
    if (!world.groundAt(feet.x, feet.z, std::numeric_limits<float>::lowest(),
                        feet.y + config_.skin)) return false;
    teleport(feet);
    return true;
}

bool FPSController::resolve(const CollisionWorld& world, bool horizontal, glm::vec3 previous) {
    bool supported = false;
    for (int iteration = 0; iteration < kResolutionIterations; ++iteration) {
        const auto contacts = contactsAt(feet_, posture_, world, config_.skin);
        const Contact* deepest = nullptr;
        for (const auto& contact : contacts) {
            if (contact.depth > kContactTolerance &&
                (!deepest || contact.depth > deepest->depth)) {
                deepest = &contact;
            }
        }
        if (!deepest) {
            break;
        }

        const glm::vec3 normal = deepest->normal;
        glm::vec3 correction = normal * deepest->depth;
        if (horizontal && normal.y > 0.0F && !walkable(normal)) {
            // Treat steep uphill contacts as walls. A full normal correction here
            // would convert horizontal input into climbing a forbidden slope.
            const float horizontalLength2 = normal.x * normal.x + normal.z * normal.z;
            if (horizontalLength2 > 0.0001F) {
                correction = glm::vec3(normal.x, 0.0F, normal.z) *
                             (deepest->depth / horizontalLength2);
            }
        }
        feet_ += correction;
        if (walkable(normal) && verticalVelocity_ <= 0.0F) {
            supported = true;
            groundNormal_ = normal;
        }
        if (!horizontal && normal.y < -0.1F && verticalVelocity_ > 0.0F) {
            verticalVelocity_ = 0.0F;
        }
    }

    // Extremely tight corners may not converge within the iteration budget.
    // Keep the previous valid position instead of admitting deep penetration.
    for (const auto& contact : contactsAt(feet_, posture_, world)) {
        if (contact.depth > config_.skin + 0.001F) {
            feet_ = previous;
            if (!horizontal) {
                verticalVelocity_ = std::min(verticalVelocity_, 0.0F);
            }
            break;
        }
    }
    return supported;
}

bool FPSController::snapToGround(const CollisionWorld& world, float distance) {
    if (posture_ > 1) {
        const auto support = proneSupport(feet_, posture_, world, feet_.y - distance - bodyHeight(),
                                          feet_.y + config_.stepHeight + config_.skin);
        if (!support) return false;
        const float target = support->first + config_.skin;
        const float difference = target - feet_.y;
        if (difference < -distance || difference > config_.stepHeight + config_.skin) return false;
        auto candidate = feet_; candidate.y = target;
        for (const auto& contact : contactsAt(candidate, posture_, world))
            if (contact.depth > config_.skin + kContactTolerance) return false;
        feet_ = candidate;
        grounded_ = true;
        verticalVelocity_ = 0;
        groundNormal_ = support->second;
        return true;
    }
    const auto ground = world.walkingGroundAt(feet_.x, feet_.z,
                                       feet_.y - distance - config_.radius,
                                       feet_.y + config_.stepHeight + config_.skin);
    if (!ground || !walkable(ground->normal)) {
        return false;
    }

    // The bottom of a capsule touching a sloped plane sits slightly above its
    // height at the capsule center. This avoids repeatedly penetrating slopes.
    const float target = ground->height +
                         config_.radius * (1.0F / ground->normal.y - 1.0F) +
                         config_.skin / ground->normal.y;
    const float difference = target - feet_.y;
    if (difference < -distance || difference > config_.stepHeight + config_.skin) {
        return false;
    }
    const glm::vec3 candidate(feet_.x, target, feet_.z);
    for (const auto& contact : world.contacts(candidate, config_.radius, bodyHeight())) {
        if (contact.depth > config_.skin + kContactTolerance) {
            return false;
        }
    }
    feet_ = candidate;
    grounded_ = true;
    verticalVelocity_ = 0.0F;
    groundNormal_ = ground->normal;
    return true;
}

void FPSController::step(float dt, const FPSInput& input, const CollisionWorld& world, bool jump) {
    exertion_.decay(dt, config_.exertion);
    // Upright capsules are yaw invariant; prone boxes retain an independently
    // reconciled orientation so a blocked turn cannot tunnel through a wall.
    if (posture_ <= 1) bodyYawDegrees_ = std::remainder(input.yawDegrees, 360.f);
    if (proneQueued_) {
        proneRequested_ = !proneRequested_;
        proneQueued_ = false;
    }
    // Space first gets up from prone; another press can jump after standing.
    if (jump && stance_ == PlayerStance::Prone) {
        proneRequested_ = false;
        jump = false;
    }
    if (jump && grounded_) {
        takeoffStance_ = stance_;
        verticalVelocity_ = config_.jumpVelocity;
        grounded_ = false;
        exertion_.load += config_.exertion.jumpLoad;
    }
    updateStance(dt, input.crouch, world);
    if (posture_ > 1) turnBody(input.yawDegrees, world);
    if (grounded_) takeoffStance_ = stance_;

    const bool airborne = !grounded_;
    const glm::vec3 movement = updateHorizontalMovement(dt, input);

    // Limit displacement in addition to timestep, including fast falls. Each
    // capsule move is much smaller than its radius, so thin walls cannot be
    // crossed between discrete collision queries at normal or running speeds.
    const float maxTravel = (glm::length(movement) + std::abs(verticalVelocity_) +
                             config_.gravity * dt) * dt;
    const int subdivisions = std::max(1, static_cast<int>(std::ceil(
        maxTravel / (bodyRadius() / 3.0F))));
    const float subDt = dt / static_cast<float>(subdivisions);
    for (int substep = 0; substep < subdivisions; ++substep) {
        const bool wasGrounded = grounded_;
        glm::vec3 horizontal = movement * subDt;
        if (wasGrounded && walkable(groundNormal_)) {
            horizontal.y = -(horizontal.x * groundNormal_.x +
                             horizontal.z * groundNormal_.z) / groundNormal_.y;
        }
        glm::vec3 previous = feet_;
        feet_ += horizontal;
        const auto requested = feet_;
        // Step before edge depenetration can lift the rounded capsule off its
        // ground support. Waiting for lost horizontal travel can leave the
        // player airborne and unable to attempt the otherwise valid step.
        if (wasGrounded && verticalVelocity_ <= 0) {
            if (tryStep(previous, requested, world)) continue;
        }
        resolve(world, true, previous);
        // Apply pushing only after selecting the movement. A successful step
        // onto a low prop should not also shove it sideways.
        if (posture_ > 1) world.pushBoxes(proneBox(requested, bodyYawDegrees_, posture_), movement, subDt);
        else world.pushBoxes(requested, config_.radius, bodyHeight(), movement, subDt);

        // Preserve an existing support constraint before applying gravity. If a
        // resting capsule first falls a tiny distance and is pushed out along a
        // slope normal every tick, those sideways corrections accumulate into
        // visible downhill drift even when there is no input.
        if (wasGrounded && verticalVelocity_ <= 0.0F &&
            snapToGround(world, config_.groundSnap)) {
            continue;
        }

        verticalVelocity_ = std::max(-config_.terminalVelocity,
                                     verticalVelocity_ - config_.gravity * subDt);
        previous = feet_;
        feet_.y += verticalVelocity_ * subDt;
        grounded_ = resolve(world, false, previous);
        if (grounded_ && verticalVelocity_ <= 0.0F) {
            verticalVelocity_ = 0.0F;
        }
        if (verticalVelocity_ <= 0.0F) {
            // Only a previously supported player gets the longer downhill snap;
            // an airborne player must actually reach the surface to land.
            const float snap = wasGrounded ? config_.groundSnap : config_.skin * 2.0F;
            snapToGround(world, snap);
        }
    }
    if (airborne && grounded_) exertion_.load += config_.exertion.landingLoad;
    const auto movingStance = grounded_ ? stance_ : takeoffStance_;
    const bool running = input.run && movingStance == PlayerStance::Standing &&
        (input.forward != 0 || input.right != 0);
    const float recovery = grounded_ && stance_ == PlayerStance::Prone ? config_.exertion.proneRecoveryMultiplier :
        grounded_ && stance_ == PlayerStance::Crouched ? config_.exertion.crouchedRecoveryMultiplier : 1;
    exertion_.advance(dt, running, recovery, config_.exertion);
}

FPSState FPSController::movementState() const {
    return {feet_, groundNormal_, verticalVelocity_, accumulator_, stance_, takeoffStance_,
            grounded_, jumpHeld_, jumpQueued_, proneRequested_, proneQueued_, bodyYawDegrees_, posture_, horizontalVelocity_,
            exertion_, movementIntent_};
}

void FPSController::restoreMovement(const FPSState& state) {
    const auto finiteVector = [](glm::vec3 p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    };
    if (!finiteVector(state.feet) || !finiteVector(state.groundNormal) ||
        !std::isfinite(state.verticalVelocity) || !std::isfinite(state.accumulator) ||
        !std::isfinite(state.bodyYawDegrees) ||
        !std::isfinite(state.horizontalVelocity.x) || !std::isfinite(state.horizontalVelocity.y) ||
        !state.exertion.valid() || !std::isfinite(state.movementIntent.x) || !std::isfinite(state.movementIntent.y) ||
        glm::length(state.movementIntent) > 1.001f ||
        !std::isfinite(state.posture) || state.posture < 0 || state.posture > 2 ||
        state.accumulator < 0 || state.accumulator > 1 ||
        state.stance < PlayerStance::Standing || state.stance > PlayerStance::Prone ||
        state.takeoffStance < PlayerStance::Standing || state.takeoffStance > PlayerStance::Prone)
        throw std::invalid_argument("Invalid movement state");
    feet_ = state.feet;
    groundNormal_ = state.groundNormal;
    verticalVelocity_ = state.verticalVelocity;
    horizontalVelocity_ = state.horizontalVelocity;
    exertion_ = state.exertion;
    movementIntent_ = state.movementIntent;
    accumulator_ = state.accumulator;
    stance_ = state.stance;
    takeoffStance_ = state.takeoffStance;
    grounded_ = state.grounded;
    jumpHeld_ = state.jumpHeld;
    jumpQueued_ = state.jumpQueued;
    proneRequested_ = state.proneRequested;
    proneQueued_ = state.proneQueued;
    bodyYawDegrees_ = std::remainder(state.bodyYawDegrees, 360.f);
    posture_ = state.posture;
    noises_.clear();
}

void FPSController::restSeated(float dt) {
    noises_.clear();
    if (!std::isfinite(dt) || dt <= 0) return;
    movementIntent_ = {0, 0};
    accumulator_ += std::min(dt, 1.f);
    while (accumulator_ + 1.0e-9 >= static_cast<double>(config_.fixedDt)) {
        exertion_.decay(config_.fixedDt, config_.exertion);
        exertion_.advance(config_.fixedDt, false, config_.exertion.seatedRecoveryMultiplier, config_.exertion);
        accumulator_ -= config_.fixedDt;
    }
}

void FPSController::update(float dt, const FPSInput& input, const CollisionWorld& world) {
    noises_.clear();
    if (!std::isfinite(dt) || dt <= 0.0F || !std::isfinite(input.forward) ||
        !std::isfinite(input.right) || !std::isfinite(input.yawDegrees)) {
        return;
    }
    if (input.jump && !jumpHeld_ && grounded_) {
        jumpQueued_ = true;
    }
    jumpHeld_ = input.jump;
    if (input.toggleProne) proneQueued_ = !proneQueued_;

    // One second already covers unusually stalled interactive frames. Dropping
    // excess time after a debugger pause avoids an unbounded catch-up loop.
    accumulator_ += std::min(dt, 1.0F);
    while (accumulator_ + 1.0e-9 >= static_cast<double>(config_.fixedDt)) {
        const PlayerNoiseMotion before{feet_, grounded_};
        step(config_.fixedDt, input, world, jumpQueued_);
        const auto emitted = noiseEmitter_.update(config_.fixedDt, before, {feet_, grounded_},
            input.run && stance_ == PlayerStance::Standing);
        noises_.insert(noises_.end(), emitted.begin(), emitted.end());
        jumpQueued_ = false;
        accumulator_ -= config_.fixedDt;
    }
}

}  // namespace viewer
