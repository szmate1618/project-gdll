#include "player_body.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {
constexpr std::size_t count = playerBodyPartCount;
// Inspection speed relative to the previous 3 N*s impulse / 2.2 m/s drive.
constexpr float punchSpeedScale = 4.f;
constexpr float maxReturnAngularSpeed = 48.f;
// Let an unobstructed arm settle into its exact guard, but do not make
// collision-free joint alignment a prerequisite for the next attack.
constexpr float returnSettlingSeconds = .25f;
constexpr glm::vec3 fistOffset(0, .015f, -.045f);
constexpr float fullPunchTravel = .31f + .28f - fistOffset.z - .08f;
struct GuardArm {
    glm::vec3 shoulder, elbow, wrist;
};
GuardArm guardArm(bool right) {
    const float sign = right ? 1.f : -1.f;
    const glm::vec3 shoulder(sign * .225f, -.22f, -.11f);
    // Upper arms hang vertically. Keep both segment lengths and the 8 cm
    // forward fist offset; the fists finish just below shoulder height.
    constexpr float upperLength = .31f, lowerLength = .28f;
    const auto elbow = shoulder + glm::vec3(0, -upperLength, 0);
    glm::vec3 wrist(sign * .215f, 0, shoulder.z - .08f - fistOffset.z);
    const float dx = wrist.x - elbow.x, dz = wrist.z - elbow.z;
    wrist.y = elbow.y + std::sqrt(lowerLength * lowerLength - dx * dx - dz * dz);
    return {shoulder, elbow, wrist};
}
std::size_t index(PlayerBodyPart p) { return static_cast<std::size_t>(p); }
b3Vec3 vec(glm::vec3 p) { return {p.x, p.y, p.z}; }
b3Pos pos(glm::vec3 p) { return {p.x, p.y, p.z}; }
b3Quat quat(glm::quat q) { return {{q.x, q.y, q.z}, q.w}; }
glm::quat rotation(glm::vec3 from, glm::vec3 to) {
    if (glm::dot(from, to) < -.999999f) {
        // A downward upper arm is exactly opposite the box's local Y axis.
        // Pick a perpendicular axis rather than normalizing a zero quaternion.
        auto axis = glm::cross(from, glm::vec3(1, 0, 0));
        if (glm::length(axis) < .0001f) axis = glm::cross(from, glm::vec3(0, 0, 1));
        return glm::quat(0.f, glm::normalize(axis));
    }
    const auto axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1 + glm::dot(from, to), axis.x, axis.y, axis.z));
}
glm::quat quat(b3Quat q) { return {q.s, q.v.x, q.v.y, q.v.z}; }
glm::vec3 position(b3BodyId body) {
    auto p = b3Body_GetPosition(body); return {p.x, p.y, p.z};
}
b3Transform frame(b3BodyId body, glm::vec3 pivot, glm::quat orientation) {
    auto result = b3Transform_identity;
    result.p = b3Body_GetLocalPoint(body, pos(pivot));
    result.q = b3InvMulQuat(b3Body_GetRotation(body), quat(orientation));
    return result;
}
}
struct PlayerBody::Impl {
    enum class ArmState { guard, striking, retractPause, returning };
    struct Arm {
        b3SphericalJointDef shoulder{}, wrist{};
        b3RevoluteJointDef elbow{};
        std::array<b3JointId, 3> joints{};
        std::array<glm::quat, 2> stoppedTorso{}; // Joint frames relative to camera yaw at strike end.
        std::array<glm::quat, 2> returnTorso{}; // Parent-relative frames at retraction start.
        glm::vec3 punchDirection{0, 0, -1};
        glm::vec3 punchOrigin{0}, punchAnchor{0}, aimTarget{0};
        float extendedAngle = 0, seconds = 0, returnDuration = 0, pauseSeconds = 0;
        ArmState state = ArmState::guard;
        bool contacted = false, hasAimTarget = false;
    };
    std::shared_ptr<PhysicsWorld> world;
    b3BodyId anchor{};
    std::array<b3BodyId, count> bodies{};
    std::array<glm::vec3, count> sizes{}, centers{};
    std::array<glm::quat, count> rotations{};
    std::array<Arm, 2> arms{}; // Left, right.
    std::array<b3JointId, 2> torsoJoints{}; // Pelvis/lower torso, lower/upper torso.
    std::vector<b3ContactData> contacts;
    glm::vec3 target{0};
    glm::quat facing{1, 0, 0, 0};
    float scale = 1;
    bool active = false, right = true;
    void pullFist(std::size_t side, float seconds) {
        const auto& arm = arms[side];
        const auto fist = bodies[side ? index(PlayerBodyPart::rightFist) : index(PlayerBodyPart::leftFist)];
        const auto velocity = b3Body_GetLinearVelocity(fist);
        // Carry walking translation only. Chest point velocity also contains
        // torso turn/lean, which would steer the punch away from its aim line.
        const auto carried = b3Body_GetLinearVelocity(anchor);
        const glm::vec3 relativeVelocity(velocity.x - carried.x, velocity.y - carried.y, velocity.z - carried.z);
        const auto origin = arm.punchOrigin + position(anchor) - arm.punchAnchor;
        auto direction = arm.punchDirection;
        if (arm.hasAimTarget && glm::distance(arm.aimTarget, origin) > .0001f)
            direction = glm::normalize(arm.aimTarget - origin);
        const auto travel = position(fist) - origin;
        auto correction = -(travel - direction * glm::dot(travel, direction)) / .01f;
        const float speed = 2.2f * punchSpeedScale;
        if (glm::length(correction) > speed * 2.f) correction *= speed * 2.f / glm::length(correction);
        // Drive the original world-space strike line and correct sideways
        // displacement caused by joint reactions. Real contact still wins.
        auto force = (direction * speed + correction - relativeVelocity) * (b3Body_GetMass(fist) / seconds);
        // Force scales with speed squared to accelerate over the same distance.
        const float maxForce = 30.f * punchSpeedScale * punchSpeedScale * scale * scale * scale;
        if (glm::length(force) > maxForce) force *= maxForce / glm::length(force);
        b3Body_ApplyForceToCenter(fist, vec(force), true);
    }
    void rebuildArm(std::size_t side, bool striking) {
        auto& arm = arms[side];
        for (auto joint : arm.joints) if (b3Joint_IsValid(joint)) b3DestroyJoint(joint, false);
        arm.state = striking ? ArmState::striking : ArmState::guard;
        arm.seconds = 0; arm.contacted = false;
        if (striking) {
            arm.joints[0] = b3CreateSphericalJoint(world->id(), &arm.shoulder);
            arm.joints[1] = b3CreateRevoluteJoint(world->id(), &arm.elbow);
            arm.joints[2] = b3CreateSphericalJoint(world->id(), &arm.wrist);
        } else {
            // Rigid constraints hold the captured guard offsets without springs.
            const std::array<b3JointDef, 3> frames{arm.shoulder.base, arm.elbow.base, arm.wrist.base};
            for (std::size_t j = 0; j < frames.size(); ++j) {
                auto weld = b3DefaultWeldJointDef(); weld.base = frames[j];
                arm.joints[j] = b3CreateWeldJoint(world->id(), &weld);
            }
        }
    }
    void positionGuardArm(std::size_t side) {
        const auto chest = bodies[index(PlayerBodyPart::upperTorso)];
        const auto q = quat(b3Body_GetRotation(chest));
        const auto first = side ? index(PlayerBodyPart::rightUpperArm) : index(PlayerBodyPart::leftUpperArm);
        for (std::size_t part = first; part < first + 3; ++part) {
            const auto p = position(chest) + q * (centers[part] - centers[index(PlayerBodyPart::upperTorso)]);
            b3Body_SetTransform(bodies[part], pos(p), quat(q * rotations[part]));
            b3Body_SetLinearVelocity(bodies[part], b3Body_GetWorldPointVelocity(chest, pos(p)));
            b3Body_SetAngularVelocity(bodies[part], b3Body_GetAngularVelocity(chest));
        }
    }
    void returnArm(std::size_t side) {
        positionGuardArm(side);
        rebuildArm(side, false);
    }
    void beginReturn(std::size_t side) {
        auto& arm = arms[side];
        arm.returnDuration = 3.f * arm.seconds;
        arm.seconds = 0;
        arm.pauseSeconds = 0;
        arm.state = ArmState::retractPause;
        for (std::size_t i = 0; i < torsoJoints.size(); ++i) {
            const auto joint = torsoJoints[i];
            const auto qb = quat(b3Body_GetRotation(b3Joint_GetBodyB(joint))) * quat(b3Joint_GetLocalFrameB(joint).q);
            arm.stoppedTorso[i] = glm::normalize(glm::conjugate(facing) * qb);
            // End the forward drive in the same solved step as the arm ends.
            if (arms[1 - side].state != ArmState::striking)
                b3SphericalJoint_SetMotorVelocity(joint, b3Vec3_zero);
        }
        const float torqueScale = scale * scale * scale * scale * scale;
        for (const auto j : {arm.joints[0], arm.joints[2]}) {
            b3SphericalJoint_SetMaxMotorTorque(j, (B3_ID_EQUALS(j, arm.joints[0]) ? 240.f : 24.f) * torqueScale);
            b3SphericalJoint_SetMotorVelocity(j, b3Vec3_zero);
            b3SphericalJoint_EnableMotor(j, true);
        }
        b3RevoluteJoint_SetMaxMotorTorque(arm.joints[1], 100.f * torqueScale);
        b3RevoluteJoint_SetMotorSpeed(arm.joints[1], 0);
        b3RevoluteJoint_EnableMotor(arm.joints[1], true);
    }
    glm::vec3 jointRotationError(b3JointId joint, glm::quat relativeTarget = glm::quat(1, 0, 0, 0)) const {
        const auto a = b3Joint_GetBodyA(joint), b = b3Joint_GetBodyB(joint);
        const auto qa = quat(b3Body_GetRotation(a)) * quat(b3Joint_GetLocalFrameA(joint).q);
        const auto qb = quat(b3Body_GetRotation(b)) * quat(b3Joint_GetLocalFrameB(joint).q);
        auto delta = glm::normalize(qa * relativeTarget * glm::conjugate(qb));
        if (delta.w < 0) delta = -delta;
        const glm::vec3 axis(delta.x, delta.y, delta.z);
        const float length = glm::length(axis);
        return length > .000001f ? axis * (2 * std::atan2(length, delta.w) / length) : glm::vec3(0);
    }
    void retractArm(std::size_t side, float seconds) {
        auto& arm = arms[side];
        const float remaining = std::max(seconds, arm.returnDuration - arm.seconds);
        // Drive existing joints back to their neutral guard frames with
        // velocity motors, preserving anatomical connections and collisions.
        // No arm springs or pose teleports are needed during the return.
        for (const auto j : {arm.joints[0], arm.joints[2]}) {
            auto velocity = jointRotationError(j) / remaining;
            if (glm::length(velocity) > maxReturnAngularSpeed) velocity *= maxReturnAngularSpeed / glm::length(velocity);
            b3SphericalJoint_SetMotorVelocity(j, vec(velocity));
        }
        b3RevoluteJoint_SetMotorSpeed(arm.joints[1], std::clamp(-b3RevoluteJoint_GetAngle(arm.joints[1]) / remaining,
                                                           -maxReturnAngularSpeed, maxReturnAngularSpeed));
        arm.seconds += seconds;
    }
    bool returnFinished(std::size_t side) const {
        const auto& arm = arms[side];
        if (arm.seconds < arm.returnDuration ||
            glm::length(jointRotationError(arm.joints[0])) >= .01f ||
            std::abs(b3RevoluteJoint_GetAngle(arm.joints[1])) >= .01f ||
            glm::length(jointRotationError(arm.joints[2])) >= .01f) return false;
        // Contact can leave positional constraint error even when all joint
        // angles match guard. Do not project that deflection through a blocker.
        const auto chest = bodies[index(PlayerBodyPart::upperTorso)];
        const auto q = quat(b3Body_GetRotation(chest));
        const auto first = side ? index(PlayerBodyPart::rightUpperArm) : index(PlayerBodyPart::leftUpperArm);
        for (std::size_t part = first; part < first + 3; ++part) {
            const auto guard = position(chest) + q * (centers[part] - centers[index(PlayerBodyPart::upperTorso)]);
            if (glm::distance(position(bodies[part]), guard) > .005f * scale) return false;
        }
        return true;
    }
    std::array<glm::quat, 2> strikeTorsoTargets(std::size_t side) const {
        const auto& arm = arms[side];
        std::array<glm::quat, 2> result{};
        const auto fist = side ? index(PlayerBodyPart::rightFist) : index(PlayerBodyPart::leftFist);
        const auto guard = arm.punchOrigin + position(anchor) - arm.punchAnchor;
        // Measure from the carried strike origin, independently of chest turn.
        // Advance the shoulder through its target by two thirds of extension;
        // the short strike must leave time for the bounded motors to track it.
        const float progress = std::clamp(1.5f * glm::dot(position(bodies[fist]) - guard, arm.punchDirection) / (fullPunchTravel * scale), 0.f, 1.f);
        const auto direction = glm::conjugate(facing) * arm.punchDirection;
        const float aimYaw = std::atan2(-direction.x, -direction.z);
        const float aimPitch = std::atan2(direction.y, glm::length(glm::vec2(direction.x, direction.z)));
        const float sign = side ? 1.f : -1.f;
        const std::array<float, 2> yaw{
            progress * std::clamp(sign * glm::radians(14.f) + .4f * aimYaw, -glm::radians(24.f), glm::radians(24.f)),
            progress * std::clamp(sign * glm::radians(21.f) + .6f * aimYaw, -glm::radians(30.f), glm::radians(30.f))};
        const std::array<float, 2> pitch{
            progress * std::clamp(.4f * aimPitch - glm::radians(4.f), -glm::radians(10.f), glm::radians(10.f)),
            progress * std::clamp(.6f * aimPitch - glm::radians(6.f), -glm::radians(12.f), glm::radians(12.f))};
        for (std::size_t i = 0; i < torsoJoints.size(); ++i)
            result[i] = glm::angleAxis(yaw[i], glm::vec3(0, 1, 0)) * glm::angleAxis(pitch[i], glm::vec3(1, 0, 0));
        return result;
    }
    void followPunchWithTorso() {
        const bool striking = arms[0].state == ArmState::striking || arms[1].state == ArmState::striking;
        const bool paused = !striking && (arms[0].state == ArmState::retractPause || arms[1].state == ArmState::retractPause);
        std::array<glm::quat, 2> targets{glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)};
        float weight = 0;
        for (std::size_t side = 0; side < arms.size(); ++side) {
            const auto& arm = arms[side];
            if (arm.state == ArmState::guard || (striking && arm.state != ArmState::striking) ||
                (paused && arm.state != ArmState::retractPause)) continue;
            std::array<glm::quat, 2> armTargets{};
            if (arm.state != ArmState::striking) {
                // Hold the actual end pose during the pause, then unwind it.
                // An extended fist must never keep advancing the torso here.
                const float recovery = arm.state == ArmState::returning
                    ? std::clamp(arm.seconds / std::max(arm.returnDuration, .0001f), 0.f, 1.f) : 0.f;
                for (std::size_t i = 0; i < torsoJoints.size(); ++i) {
                    const auto joint = torsoJoints[i];
                    const auto qa = quat(b3Body_GetRotation(b3Joint_GetBodyA(joint))) * quat(b3Joint_GetLocalFrameA(joint).q);
                    armTargets[i] = arm.state == ArmState::returning
                        ? glm::slerp(arm.returnTorso[i], glm::quat(1, 0, 0, 0), recovery)
                        : glm::normalize(glm::conjugate(qa) * facing * arm.stoppedTorso[i]);
                }
            } else {
                armTargets = strikeTorsoTargets(side);
            }
            weight += 1;
            for (std::size_t i = 0; i < torsoJoints.size(); ++i)
                targets[i] = glm::slerp(targets[i], armTargets[i], 1.f / weight);
        }
        for (std::size_t joint = 0; joint < torsoJoints.size(); ++joint) {
            const float torqueScale = scale * scale * scale * scale * scale;
            b3SphericalJoint_SetMaxMotorTorque(torsoJoints[joint], (joint ? 160.f : 240.f) * (paused ? 4.f : 1.f) * torqueScale);
            auto velocity = jointRotationError(torsoJoints[joint], targets[joint]) / (striking || paused ? .02f : .04f);
            if (paused) {
                // Cancel carried parent rotation and damp remaining world spin.
                // Motor torque stays bounded: impacts can still deflect the rig.
                const auto parentVelocity = b3Body_GetAngularVelocity(b3Joint_GetBodyA(torsoJoints[joint]));
                const auto bodyVelocity = b3Body_GetAngularVelocity(b3Joint_GetBodyB(torsoJoints[joint]));
                velocity -= glm::vec3(parentVelocity.x, parentVelocity.y, parentVelocity.z);
                velocity -= glm::vec3(bodyVelocity.x, bodyVelocity.y, bodyVelocity.z);
            }
            if (glm::length(velocity) > 8.f) velocity *= 8.f / glm::length(velocity);
            b3SphericalJoint_SetMotorVelocity(torsoJoints[joint], vec(velocity));
        }
    }
    ~Impl() {
        for (auto b : bodies) if (b3Body_IsValid(b)) b3DestroyBody(b);
        if (b3Body_IsValid(anchor)) b3DestroyBody(anchor);
    }
};
PlayerBody::PlayerBody(std::shared_ptr<PhysicsWorld> world, float eyeHeight) : impl_(std::make_unique<Impl>()) {
    if (!world || !std::isfinite(eyeHeight) || eyeHeight < .5f || eyeHeight > 3)
        throw std::invalid_argument("Player body needs a world and standing eye height in meters");
    auto& d = *impl_; d.world = std::move(world); d.scale = eyeHeight / 1.70f;
    // Approximate 1.8 m, 78 kg male; legs/head are carried by the existing
    // walking controller. Listed physical upper-body mass totals 45.6 kg.
    const std::array<float, count> masses{11, 9, 17, 2.2f, 1.5f, .6f, 2.2f, 1.5f, .6f};
    d.centers = {{{0, -.75f, .03f}, {0, -.56f, .03f}, {0, -.32f, .03f},
                  {}, {}, {}, {}, {}, {}}};
    d.sizes = {{{.30f, .20f, .21f}, {.28f, .19f, .19f}, {.38f, .30f, .23f},
                {}, {}, {.095f, .11f, .14f}, {}, {}, {.095f, .11f, .14f}}};
    d.rotations.fill(glm::quat(1, 0, 0, 0));
    for (int side = 0; side < 2; ++side) {
        const std::size_t upper = side ? 6 : 3;
        const auto [shoulder, elbow, wrist] = guardArm(side != 0);
        for (int segment = 0; segment < 2; ++segment) {
            const auto a = segment ? elbow : shoulder, b = segment ? wrist : elbow;
            const auto i = upper + segment;
            d.centers[i] = (a + b) * .5f;
            d.rotations[i] = rotation(glm::vec3(0, 1, 0), glm::normalize(b - a));
            d.sizes[i] = {.10f - segment * .015f, glm::length(b - a), .10f - segment * .015f};
        }
        d.centers[upper + 2] = wrist + fistOffset;
    }
    auto anchor = b3DefaultBodyDef(); anchor.type = b3_kinematicBody;
    anchor.position = pos(d.centers[0] * d.scale);
    d.anchor = b3CreateBody(d.world->id(), &anchor);
    const int group = d.world->allocateCollisionGroup();
    for (std::size_t i = 0; i < count; ++i) {
        d.centers[i] *= d.scale; d.sizes[i] *= d.scale;
        auto body = b3DefaultBodyDef(); body.type = b3_dynamicBody;
        body.position = pos(d.centers[i]); body.rotation = quat(d.rotations[i]);
        // The walking anchor stays upright: lock pelvis pitch/roll in world
        // space, while leaving yaw free for the existing rotational spring.
        if (i == index(PlayerBodyPart::hip)) {
            body.motionLocks.angularX = true;
            body.motionLocks.angularZ = true;
        }
        body.linearDamping = .2f; body.angularDamping = .4f;
        body.enableSleep = false; body.enableContactRecycling = false;
        d.bodies[i] = b3CreateBody(d.world->id(), &body);
        auto shape = b3DefaultShapeDef();
        shape.filter.categoryBits = playerBodyCategory;
        shape.filter.maskBits = ordinaryBodyCategory | interiorBoxCategory | doorBodyCategory | vehicleBodyCategory | meleeZombieCategory;
        shape.filter.groupIndex = group; // All parts may overlap, even non-neighbors.
        const auto s = d.sizes[i]; shape.density = masses[i] * d.scale * d.scale * d.scale / (s.x * s.y * s.z);
        shape.baseMaterial.friction = .45f;
        const auto hull = b3MakeBoxHull(s.x * .5f, s.y * .5f, s.z * .5f);
        b3CreateHullShape(d.bodies[i], &shape, &hull.base);
    }
    const auto ball = [&](b3BodyId parent, std::size_t child, glm::vec3 pivot, float cone, float twist) {
        auto j = b3DefaultSphericalJointDef();
        j.base.bodyIdA = parent; j.base.bodyIdB = d.bodies[child];
        j.base.localFrameA = frame(parent, pivot * d.scale, glm::quat(1, 0, 0, 0));
        j.base.localFrameB = frame(d.bodies[child], pivot * d.scale, glm::quat(1, 0, 0, 0));
        j.enableConeLimit = true; j.coneAngle = glm::radians(cone);
        j.enableTwistLimit = true; j.lowerTwistAngle = -glm::radians(twist); j.upperTwistAngle = glm::radians(twist);
        return j;
    };
    {
        auto j = ball(d.anchor, 0, {0, -.75f, .03f}, 35, 35);
        // Pitch/roll are motion-locked; retain the existing yaw response.
        j.enableSpring = true; j.hertz = 6.f * std::sqrt(2.f); j.dampingRatio = .8f;
        b3CreateSphericalJoint(d.world->id(), &j);
    }
    for (std::size_t i = 0; i < d.torsoJoints.size(); ++i) {
        auto joint = ball(d.bodies[i], i + 1, i ? glm::vec3(0, -.465f, .03f) : glm::vec3(0, -.65f, .03f),
                          i ? 35.f : 30.f, i ? 35.f : 30.f);
        joint.enableMotor = true;
        joint.maxMotorTorque = (i ? 160.f : 240.f) * d.scale * d.scale * d.scale * d.scale * d.scale;
        d.torsoJoints[i] = b3CreateSphericalJoint(d.world->id(), &joint);
    }
    for (int side = 0; side < 2; ++side) {
        const std::size_t upper = side ? 6 : 3;
        auto& arm = d.arms[side];
        const auto [shoulder, elbow, wrist] = guardArm(side != 0);
        arm.shoulder = ball(d.bodies[2], upper, shoulder, 110, 80);
        const auto a = d.rotations[upper] * glm::vec3(0, 1, 0);
        const auto b = d.rotations[upper + 1] * glm::vec3(0, 1, 0);
        const auto hingeFrame = rotation(glm::vec3(0, 0, 1), glm::normalize(glm::cross(a, b)));
        arm.extendedAngle = -std::acos(std::clamp(glm::dot(a, b), -1.f, 1.f));
        auto& j = arm.elbow;
        j = b3DefaultRevoluteJointDef();
        j.base.bodyIdA = d.bodies[upper]; j.base.bodyIdB = d.bodies[upper + 1];
        j.base.localFrameA = frame(d.bodies[upper], elbow * d.scale, hingeFrame);
        j.base.localFrameB = frame(d.bodies[upper + 1], elbow * d.scale, hingeFrame);
        // Limits are relative to this tightly folded guard, not a fixed angle
        // that would stop the elbow before it can straighten.
        j.enableLimit = true; j.lowerAngle = arm.extendedAngle - glm::radians(2.f);
        j.upperAngle = std::min(glm::radians(10.f), glm::radians(178.f) + arm.extendedAngle);
        arm.wrist = ball(d.bodies[upper + 1], upper + 2, wrist, 25, 25);
        d.rebuildArm(side, false);
    }
    for (auto b : d.bodies) b3Body_Disable(b);
    b3Body_Disable(d.anchor);
}
PlayerBody::~PlayerBody() = default;
void PlayerBody::follow(glm::vec3 eye, glm::vec3 forward, bool enabled) {
    auto& d = *impl_;
    const auto horizontal = glm::vec2(forward.x, forward.z);
    if (glm::length(horizontal) > .001f)
        d.facing = glm::angleAxis(std::atan2(-forward.x, -forward.z), glm::vec3(0, 1, 0));
    d.target = eye + d.facing * d.centers[0];
    const bool reset = enabled && (!d.active || glm::distance(position(d.anchor), d.target) > 2);
    if (enabled != d.active) {
        for (auto b : d.bodies) { if (enabled) b3Body_Enable(b); else b3Body_Disable(b); }
        if (enabled) b3Body_Enable(d.anchor); else b3Body_Disable(d.anchor);
    }
    d.active = enabled;
    if (reset) {
        b3Body_SetTransform(d.anchor, pos(d.target), quat(d.facing));
        for (std::size_t i = 0; i < count; ++i) {
            b3Body_SetTransform(d.bodies[i], pos(eye + d.facing * d.centers[i]), quat(d.facing * d.rotations[i]));
            b3Body_SetLinearVelocity(d.bodies[i], b3Vec3_zero);
            b3Body_SetAngularVelocity(d.bodies[i], b3Vec3_zero);
        }
        d.returnArm(0); d.returnArm(1);
    }
}
void PlayerBody::beforeStep(float seconds) {
    auto& d = *impl_; if (!d.active || seconds <= 0) return;
    b3Body_SetLinearVelocity(d.anchor, vec((d.target - position(d.anchor)) / seconds));
    // Approximate muscular weight support separately from posture stiffness.
    // Without this feed-forward force, soft springs collapse under the entire
    // torso/arm chain's weight. Contacts and punch impulses still use real mass.
    for (auto b : d.bodies)
        b3Body_ApplyForceToCenter(b, {0, 9.8f * b3Body_GetMass(b), 0}, true);
    for (std::size_t side = 0; side < d.arms.size(); ++side) {
        auto& arm = d.arms[side];
        if (arm.state == Impl::ArmState::retractPause) {
            // Hold zero relative joint velocity for six complete 60 Hz steps
            // before commanding retraction. Contacts may still move the rig.
            if (arm.pauseSeconds + .000001f < .1f) {
                arm.pauseSeconds += seconds;
                continue;
            }
            arm.state = Impl::ArmState::returning;
            for (std::size_t i = 0; i < d.torsoJoints.size(); ++i) {
                const auto joint = d.torsoJoints[i];
                const auto qa = quat(b3Body_GetRotation(b3Joint_GetBodyA(joint))) * quat(b3Joint_GetLocalFrameA(joint).q);
                const auto qb = quat(b3Body_GetRotation(b3Joint_GetBodyB(joint))) * quat(b3Joint_GetLocalFrameB(joint).q);
                arm.returnTorso[i] = glm::normalize(glm::conjugate(qa) * qb);
            }
        }
        if (arm.state == Impl::ArmState::returning) {
            d.retractArm(side, seconds);
            continue;
        }
        if (arm.state != Impl::ArmState::striking) continue;
        // Constrain the strike line from its first solve. An unchecked initial
        // impulse otherwise swings the folded forearm downward before tracking.
        d.pullFist(side, seconds);
        arm.seconds += seconds;
    }
    d.followPunchWithTorso();
    auto delta = glm::normalize(d.facing * glm::conjugate(quat(b3Body_GetRotation(d.anchor))));
    if (delta.w < 0) delta = -delta;
    const float angle = 2 * std::acos(glm::clamp(delta.w, -1.f, 1.f));
    const auto axis = glm::vec3(delta.x, delta.y, delta.z);
    b3Body_SetAngularVelocity(d.anchor, vec(glm::length(axis) > .00001f ? glm::normalize(axis) * angle / seconds : glm::vec3(0)));
}
void PlayerBody::afterStep() {
    auto& d = *impl_; if (!d.active) return;
    for (std::size_t side = 0; side < d.arms.size(); ++side) {
        const auto& arm = d.arms[side];
        if (arm.state == Impl::ArmState::retractPause) continue;
        if (arm.state == Impl::ArmState::returning) {
            if (d.returnFinished(side)) d.returnArm(side);
            continue;
        }
        if (arm.state == Impl::ArmState::guard) {
            // Project small solver errors out of an intentionally fixed guard.
            d.positionGuardArm(side);
            continue;
        }
        bool contact = arm.contacted;
        const std::size_t first = side ? 6 : 3;
        for (std::size_t part = first; part < first + 3; ++part) {
            const auto body = d.bodies[part];
            d.contacts.resize(b3Body_GetContactCapacity(body));
            const int n = b3Body_GetContactData(body, d.contacts.data(), static_cast<int>(d.contacts.size()));
            for (int c = 0; c < n; ++c)
                for (int m = 0; m < d.contacts[c].manifoldCount; ++m)
                    for (int p = 0; p < d.contacts[c].manifolds[m].pointCount; ++p)
                        contact |= d.contacts[c].manifolds[m].points[p].totalNormalImpulse > .001f;
        }
        // Five degrees short of straight avoids driving the elbow into its stop.
        const bool extended = b3RevoluteJoint_GetAngle(arm.joints[1]) <= arm.extendedAngle + glm::radians(5.f);
        // Glancing impulses can rotate the whole bent arm instead of extending
        // it. Bound those misses so an arm cannot remain loose indefinitely.
        if (contact || extended || arm.seconds >= .5f / punchSpeedScale) d.beginReturn(side);
    }
}
bool PlayerBody::canPunch() const {
    const auto& d = *impl_;
    const auto& arm = d.arms[d.right ? 1u : 0u];
    // An obstructed arm keeps retracting through its existing physical joints
    // until it reaches guard or is used again. Never snap it through a blocker
    // or weld a deflected pose merely to make the hand ready.
    return d.active && (arm.state == Impl::ArmState::guard ||
        (arm.state == Impl::ArmState::returning && arm.seconds >= arm.returnDuration + returnSettlingSeconds));
}
float PlayerBody::estimatedPunchReach() const {
    const auto& d = *impl_;
    // Add the guard's eye-forward offset to the arm's straightening travel.
    // Both come from the same anatomy used to construct the physical rig.
    return fullPunchTravel * d.scale - d.centers[index(PlayerBodyPart::rightFist)].z;
}
bool PlayerBody::punch(glm::vec3 direction, const glm::vec3* target) {
    auto& d = *impl_; if (!canPunch()) return false;
    const auto fist = d.right ? PlayerBodyPart::rightFist : PlayerBodyPart::leftFist;
    if (target) direction = *target - fistPosition(d.right);
    if (!std::isfinite(glm::length(direction)) || glm::length(direction) < .0001f) return false;
    const auto side = d.right ? 1u : 0u;
    d.rebuildArm(side, true);
    d.arms[side].punchDirection = glm::normalize(direction);
    d.arms[side].punchOrigin = fistPosition(d.right);
    d.arms[side].punchAnchor = position(d.anchor);
    d.arms[side].hasAimTarget = target != nullptr;
    if (target) d.arms[side].aimTarget = *target;
    b3Body_ApplyLinearImpulseToCenter(body(fist), vec(d.arms[side].punchDirection * (3.f * punchSpeedScale * d.scale * d.scale * d.scale)), true);
    d.right = !d.right;
    return true;
}
bool PlayerBody::punching(PlayerBodyPart part) const {
    const auto p = index(part);
    if (p < index(PlayerBodyPart::leftUpperArm) || p >= count) return false;
    return impl_->active && impl_->arms[p >= index(PlayerBodyPart::rightUpperArm) ? 1 : 0].state == Impl::ArmState::striking;
}
bool PlayerBody::returning(PlayerBodyPart part) const {
    const auto p = index(part);
    if (p < index(PlayerBodyPart::leftUpperArm) || p >= count) return false;
    const auto state = impl_->arms[p >= index(PlayerBodyPart::rightUpperArm) ? 1 : 0].state;
    return impl_->active && (state == Impl::ArmState::retractPause || state == Impl::ArmState::returning);
}
void PlayerBody::recordArmContact(PlayerBodyPart part) {
    if (punching(part)) impl_->arms[index(part) >= index(PlayerBodyPart::rightUpperArm) ? 1 : 0].contacted = true;
}
PlayerBodyPose PlayerBody::pose() const {
    PlayerBodyPose result;
    result.enabled = enabled();
    if (!result.enabled) return result;
    result.eye = impl_->target - impl_->facing * impl_->centers[0];
    for (std::size_t i=0; i<result.parts.size(); ++i) result.parts[i] = transform(static_cast<PlayerBodyPart>(i));
    for (std::size_t side=0; side<2; ++side) {
        const auto fist = side ? PlayerBodyPart::rightFist : PlayerBodyPart::leftFist;
        if (punching(fist)) result.striking |= 1u << side;
        if (returning(fist)) result.returning |= 1u << side;
    }
    return result;
}
bool PlayerBody::enabled() const { return impl_->active; }
glm::vec3 PlayerBody::eyePosition() const {
    const auto& d = *impl_;
    return position(d.anchor) - quat(b3Body_GetRotation(d.anchor)) * d.centers[index(PlayerBodyPart::hip)];
}
b3BodyId PlayerBody::body(PlayerBodyPart part) const { return impl_->bodies.at(index(part)); }
glm::mat4 PlayerBody::transform(PlayerBodyPart part) const {
    const auto b = body(part);
    return glm::translate(glm::mat4(1), position(b)) * glm::mat4_cast(quat(b3Body_GetRotation(b))) *
           glm::scale(glm::mat4(1), impl_->sizes.at(index(part)));
}
glm::vec3 PlayerBody::fistPosition(bool right) const { return position(body(right ? PlayerBodyPart::rightFist : PlayerBodyPart::leftFist)); }
} // namespace viewer
