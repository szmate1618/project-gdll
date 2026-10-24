#include "vehicle_physics.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
glm::vec3 position(b3BodyId body) {
    const auto p = b3Body_GetPosition(body);
    return {p.x, p.y, p.z};
}
glm::quat rotation(b3BodyId body) {
    const auto q = b3Body_GetRotation(body);
    return {q.s, q.v.x, q.v.y, q.v.z};
}
b3Vec3 vector(glm::vec3 v) { return {v.x, v.y, v.z}; }
constexpr float maxSteeringAngle = .5235988f;
// Chassis-local meters. The lower box reaches the neutral tire support plane;
// the narrower cabin leaves the hood and trunk below the roof line.
constexpr glm::vec3 lowerCenter(0, -.18f, 0), lowerHalf(.85f, .5f, 1.8f);
constexpr glm::vec3 cabinCenter(0, .60f, .10f), cabinHalf(.72f, .28f, 1.05f);
glm::vec3 bodyScale(const VehicleConfig& config) { return config.dimensions / glm::vec3(1.7f, 1.56f, 3.6f); }
glm::vec3 fittedCenter(glm::vec3 center, const VehicleConfig& config) {
    // Scale about the tire support plane so every type keeps the same pivot.
    return (center + glm::vec3(0, vehiclePivotHeight, 0)) * bodyScale(config) - glm::vec3(0, vehiclePivotHeight, 0);
}
glm::vec3 activeHalf(const VehicleConfig& config) { return glm::vec3(.85f, .325f, 1.8f) * bodyScale(config); }
}
VehiclePhysics::VehiclePhysics(PhysicsWorld& w, const VehicleConfig& c, glm::vec3 center, glm::quat orientation)
    : world(w), config(c) {
    const auto q = glm::normalize(orientation);
    auto definition = b3DefaultBodyDef();
    definition.position = {center.x, center.y, center.z};
    definition.rotation = {{q.x, q.y, q.z}, q.w};
    body = b3CreateBody(world.id(), &definition);
    world.trackBodyMotion(body, revision);
    replaceBoxes(false);
    const auto axle = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 0, 1));
    for (std::size_t i = 0; i < 4; ++i)
        storedWheels[i] = glm::translate(glm::mat4(1), center + q * config.wheels[i]) * glm::mat4_cast(q * axle);
}
VehiclePhysics::~VehiclePhysics() {
    destroyWheels();
    if (b3Body_IsValid(body)) {
        world.untrackBodyMotion(body);
        b3DestroyBody(body);
    }
}
void VehiclePhysics::destroyWheels() {
    // Destroying a wheel also destroys its suspension joint.
    for (auto& wheel : wheels) if (b3Body_IsValid(wheel)) {
        world.untrackBodyMotion(wheel);
        b3DestroyBody(wheel);
        wheel = {};
    }
    joints = {}; tireShapes = {}; traction = {};
}
void VehiclePhysics::replaceBoxes(bool active) {
    for (auto& box : boxes) if (b3Shape_IsValid(box)) {
        b3DestroyShape(box, false);
        box = {};
    }
    auto shape = b3DefaultShapeDef();
    shape.filter.categoryBits = vehicleBodyCategory;
    shape.filter.maskBits = ~ragdollFloorCategory;
    shape.filter.groupIndex = world.allocateCollisionGroup();
    shape.baseMaterial.friction = .4f;
    const auto scale = bodyScale(config);
    if (active) {
        const auto half = activeHalf(config);
        shape.density = (config.weightKg - 100.f) / (8 * half.x * half.y * half.z);
        const auto hull = b3MakeBoxHull(half.x, half.y, half.z);
        boxes[0] = b3CreateHullShape(body, &shape, &hull.base);
        // Preserve the original lowered center of mass and unconstrained tilt.
        auto mass = b3Body_GetMassData(body);
        mass.center.y = -.18f * scale.y;
        b3Body_SetMassData(body, mass);
    } else {
        const auto lowerSize = lowerHalf * scale, cabinSize = cabinHalf * scale;
        const auto lower = b3MakeOffsetBoxHull(lowerSize.x, lowerSize.y, lowerSize.z, vector(fittedCenter(lowerCenter,config)));
        const auto cabin = b3MakeOffsetBoxHull(cabinSize.x, cabinSize.y, cabinSize.z, vector(fittedCenter(cabinCenter,config)));
        boxes[0] = b3CreateHullShape(body, &shape, &lower.base);
        boxes[1] = b3CreateHullShape(body, &shape, &cabin.base);
    }
}
void VehiclePhysics::activate(const b3HullData* tire) {
    if (detailed) return;
    b3Body_SetType(body, b3_dynamicBody);
    b3Body_SetBullet(body, true);
    b3Body_SetAngularDamping(body, .15f);
    b3Body_SetLinearDamping(body, .025f);
    replaceBoxes(true);
    auto shape = b3DefaultShapeDef();
    shape.filter = b3Shape_GetFilter(boxes[0]);
    shape.density = 25.0f / (3.14159265f * config.tireRadius * config.tireRadius * config.tireWidth);
    shape.baseMaterial.friction = rollingTireFriction;
    auto wheelDefinition = b3DefaultBodyDef();
    wheelDefinition.type = b3_dynamicBody;
    wheelDefinition.isBullet = true;
    wheelDefinition.linearDamping = .025f;
    wheelDefinition.allowFastRotation = true;
    wheelDefinition.angularDamping = .015f;
    const auto axle = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 0, 1));
    for (std::size_t i = 0; i < 4; ++i) {
        const auto p = glm::vec3(storedWheels[i][3]);
        const auto wheelQ = glm::quat_cast(glm::mat3(storedWheels[i]));
        wheelDefinition.position = {p.x, p.y, p.z};
        wheelDefinition.rotation = {{wheelQ.x, wheelQ.y, wheelQ.z}, wheelQ.w};
        wheels[i] = b3CreateBody(world.id(), &wheelDefinition);
        world.trackBodyMotion(wheels[i], revision);
        tireShapes[i] = b3CreateHullShape(wheels[i], &shape, tire);
        auto joint = b3DefaultWheelJointDef();
        joint.base.bodyIdA = body;
        joint.base.bodyIdB = wheels[i];
        joint.base.localFrameA.p = vector(config.wheels[i]);
        // Joint X = suspension up, Z = axle right, Y = car forward (+Z).
        const auto frame = glm::quat_cast(glm::mat3({0, 1, 0}, {0, 0, 1}, {1, 0, 0}));
        const auto localB = glm::conjugate(axle) * frame;
        joint.base.localFrameA.q = {{frame.x, frame.y, frame.z}, frame.w};
        joint.base.localFrameB.q = {{localB.x, localB.y, localB.z}, localB.w};
        joint.enableSuspensionSpring = true;
        joint.suspensionHertz = 6.f; joint.suspensionDampingRatio = .8f;
        joint.enableSuspensionLimit = true;
        joint.lowerSuspensionLimit = -.18f; joint.upperSuspensionLimit = .18f;
        joint.enableSteering = i < 2;
        joint.steeringHertz = 8; joint.steeringDampingRatio = 1;
        joint.maxSteeringTorque = 1800;
        joint.enableSteeringLimit = true;
        joint.lowerSteeringLimit = -maxSteeringAngle; joint.upperSteeringLimit = maxSteeringAngle;
        joint.enableSpinMotor = true;
        joints[i] = b3CreateWheelJoint(world.id(), &joint);
    }
    detailed = true;
    replicatedVelocity = {};
    settlingSeconds = 0;
    ++revision;
}
void VehiclePhysics::park() {
    if (!detailed) return;
    storedWheels = pose(0).wheels;
    destroyWheels();
    b3Body_SetLinearVelocity(body, b3Vec3_zero);
    b3Body_SetAngularVelocity(body, b3Vec3_zero);
    b3Body_SetType(body, b3_staticBody);
    b3Body_SetBullet(body, false);
    replaceBoxes(false);
    detailed = false;
    replicatedVelocity = {};
    throttle = steering = steeringAngle = opposingImpulse = 0;
    brake = true;
    settlingSeconds = 0;
    ++revision;
}
VehiclePose VehiclePhysics::pose(std::size_t index) const {
    VehiclePose result{{index, position(body), activeHalf(config), rotation(body)}, storedWheels};
    if (detailed) for (std::size_t i = 0; i < 4; ++i)
        result.wheels[i] = glm::translate(glm::mat4(1), position(wheels[i])) * glm::mat4_cast(rotation(wheels[i]));
    return result;
}
BoxPose VehiclePhysics::collider(std::size_t index, std::size_t part) const {
    const auto q = rotation(body);
    if (detailed) return {index, position(body), activeHalf(config), q};
    return {index, position(body) + q * fittedCenter(part == 0 ? lowerCenter : cabinCenter,config),
            (part == 0 ? lowerHalf : cabinHalf) * bodyScale(config), q};
}
void VehiclePhysics::setPose(const VehiclePose& pose, glm::vec3 velocity) {
    const auto valid = [](glm::vec3 p, glm::quat q) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            std::isfinite(glm::length(q)) && std::abs(glm::length(q) - 1) < .001f;
    };
    if (!valid(pose.chassis.center, pose.chassis.rotation) || !valid(velocity, glm::quat(1, 0, 0, 0)))
        throw std::invalid_argument("Invalid replicated vehicle pose");
    for (const auto& wheel : pose.wheels) if (!valid(glm::vec3(wheel[3]), glm::quat_cast(glm::mat3(wheel))))
        throw std::invalid_argument("Invalid replicated wheel pose");
    const auto set = [&](b3BodyId target, glm::vec3 p, glm::quat q) {
        if (position(target) != p || rotation(target) != q) {
            b3Body_SetTransform(target, {p.x, p.y, p.z}, {{q.x, q.y, q.z}, q.w});
            ++revision;
        }
        if (detailed) {
            b3Body_SetLinearVelocity(target, vector(velocity));
            b3Body_SetAngularVelocity(target, b3Vec3_zero);
        }
    };
    set(body, pose.chassis.center, pose.chassis.rotation);
    for (std::size_t i = 0; i < 4; ++i) {
        if (detailed) set(wheels[i], glm::vec3(pose.wheels[i][3]), glm::quat_cast(glm::mat3(pose.wheels[i])));
        else if (storedWheels[i] != pose.wheels[i]) ++revision;
    }
    storedWheels = pose.wheels;
    replicatedVelocity = velocity;
}
bool VehiclePhysics::canPark(float seconds) {
    if (driven) { settlingSeconds = 0; return false; }
    const auto v = b3Body_GetLinearVelocity(body), w = b3Body_GetAngularVelocity(body);
    const bool quiet = glm::length(glm::vec3(v.x, v.y, v.z)) < .15f && glm::length(glm::vec3(w.x, w.y, w.z)) < .15f;
    bool supported = !b3Body_IsAwake(body);
    if (quiet && !supported) {
        int grounded = 0;
        for (const auto& wheel : traction) grounded += wheel.motion != WheelMotion::Airborne;
        supported = grounded >= 2;
    }
    settlingSeconds = quiet && supported ? settlingSeconds + seconds : 0;
    return settlingSeconds >= .5f;
}
} // namespace viewer
