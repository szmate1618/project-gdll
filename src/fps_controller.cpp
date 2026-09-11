#include "fps_controller.hpp"

#include "collision_world.hpp"

#include <algorithm>
#include <cmath>
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
                            config.skin, config.fixedDt, config.terminalVelocity};
    for (float value : values) {
        if (!std::isfinite(value) || value < 0.0F) {
            throw std::invalid_argument("FPS configuration must contain finite, nonnegative values");
        }
    }
    if (config.radius <= 0.0F || config.height < 2.0F * config.radius ||
        config.eyeHeight <= 0.0F || config.eyeHeight > config.height ||
        config.fixedDt <= 0.0F || config.fixedDt > 0.05F ||
        config.maxSlopeDegrees >= 89.0F || config.terminalVelocity <= 0.0F) {
        throw std::invalid_argument("Invalid FPS capsule, timestep, slope, or fall-speed configuration");
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
    groundNormal_ = {0.0F, 1.0F, 0.0F};
    accumulator_ = 0.0;
    grounded_ = false;
    jumpHeld_ = false;
    jumpQueued_ = false;
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

bool FPSController::resolve(const CollisionWorld& world, bool horizontal, glm::vec3 previous) {
    bool supported = false;
    for (int iteration = 0; iteration < kResolutionIterations; ++iteration) {
        const auto contacts = world.contacts(feet_, config_.radius, config_.height, config_.skin);
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
    for (const auto& contact : world.contacts(feet_, config_.radius, config_.height)) {
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
    const auto ground = world.groundAt(feet_.x, feet_.z,
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
    for (const auto& contact : world.contacts(candidate, config_.radius, config_.height)) {
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
    if (jump && grounded_) {
        verticalVelocity_ = config_.jumpVelocity;
        grounded_ = false;
    }

    const float yaw = glm::radians(input.yawDegrees);
    const glm::vec3 forward(std::cos(yaw), 0.0F, std::sin(yaw));
    const glm::vec3 right(-std::sin(yaw), 0.0F, std::cos(yaw));
    glm::vec3 movement = forward * input.forward + right * input.right;
    const float length = glm::length(movement);
    if (length > 1.0F) {
        movement /= length;
    }
    movement *= input.run ? config_.runSpeed : config_.walkSpeed;

    // Limit displacement in addition to timestep, including fast falls. Each
    // capsule move is much smaller than its radius, so thin walls cannot be
    // crossed between discrete collision queries at normal or running speeds.
    const float maxTravel = (glm::length(movement) + std::abs(verticalVelocity_) +
                             config_.gravity * dt) * dt;
    const int subdivisions = std::max(1, static_cast<int>(std::ceil(
        maxTravel / (config_.radius / 3.0F))));
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
        resolve(world, true, previous);

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
}

void FPSController::update(float dt, const FPSInput& input, const CollisionWorld& world) {
    if (!std::isfinite(dt) || dt <= 0.0F || !std::isfinite(input.forward) ||
        !std::isfinite(input.right) || !std::isfinite(input.yawDegrees)) {
        return;
    }
    if (input.jump && !jumpHeld_ && grounded_) {
        jumpQueued_ = true;
    }
    jumpHeld_ = input.jump;

    // One second already covers unusually stalled interactive frames. Dropping
    // excess time after a debugger pause avoids an unbounded catch-up loop.
    accumulator_ += std::min(dt, 1.0F);
    while (accumulator_ + 1.0e-9 >= static_cast<double>(config_.fixedDt)) {
        step(config_.fixedDt, input, world, jumpQueued_);
        jumpQueued_ = false;
        accumulator_ -= config_.fixedDt;
    }
}

}  // namespace viewer
