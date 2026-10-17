#include "vehicle_layer.hpp"
#include "physics_world.hpp"
#include "collision_world.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
glm::vec3 vector(b3Vec3 v) { return {v.x, v.y, v.z}; }
b3Vec3 vector(glm::vec3 v) { return {v.x, v.y, v.z}; }
glm::vec3 position(b3BodyId b) { const auto p = b3Body_GetPosition(b); return {p.x, p.y, p.z}; }
glm::quat rotation(b3BodyId b) { const auto q = b3Body_GetRotation(b); return {q.s, q.v.x, q.v.y, q.v.z}; }
}
struct VehicleLayer::Impl {
    struct Car {
        b3BodyId body{};
        std::array<b3BodyId, 4> wheels{};
        std::array<b3JointId, 4> joints{};
        std::array<b3ShapeId, 4> tireShapes{};
        std::array<WheelTraction, 4> traction{};
        float throttle = 0, steering = 0, opposingImpulse = 0;
        bool brake = true;
        ~Car() {
            // Destroying bodies also destroys their joints.
            for (auto w : wheels) if (b3Body_IsValid(w)) b3DestroyBody(w);
            if (b3Body_IsValid(body)) b3DestroyBody(body);
        }
    };
    std::shared_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<Car>> cars;
    b3HullData* tire = nullptr;
    ~Impl() { cars.clear(); if (tire) b3DestroyHull(tire); }
};
VehicleLayer::VehicleLayer(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Vehicles require a physics world");
    impl_->world = std::move(world);
    // Convex cylinder approximation; denser than the intentionally 12-gonal visual.
    impl_->tire = b3CreateCylinder(wheelWidth, wheelRadius, -wheelWidth / 2, 24);
    if (!impl_->tire) throw std::runtime_error("Cannot build tire cylinder");
}
VehicleLayer::~VehicleLayer() = default;
std::size_t VehicleLayer::count() const { return impl_->cars.size(); }
std::size_t VehicleLayer::add(glm::vec3 center, float yaw) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(yaw))
        throw std::invalid_argument("Vehicle pose must be finite");
    auto car = std::make_unique<Impl::Car>();
    auto body = b3DefaultBodyDef();
    body.type = b3_dynamicBody;
    body.position = {center.x, center.y, center.z};
    const auto q = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    body.rotation = {{q.x, q.y, q.z}, q.w};
    body.angularDamping = .15f;
    body.linearDamping = .025f;
    car->body = b3CreateBody(impl_->world->id(), &body);
    auto shape = b3DefaultShapeDef();
    shape.filter.categoryBits = vehicleBodyCategory;
    shape.filter.maskBits = ~ragdollFloorCategory;
    shape.filter.groupIndex = impl_->world->allocateCollisionGroup();
    shape.density = 1000.0f / (1.7f * .65f * 3.6f);
    shape.baseMaterial.friction = .4f;
    const auto hull = b3MakeBoxHull(.85f, .325f, 1.8f);
    b3CreateHullShape(car->body, &shape, &hull.base);
    // Lower the center of mass without freezing pitch/roll: slopes and crashes
    // still tilt or overturn the vehicle.
    auto mass = b3Body_GetMassData(car->body);
    mass.center.y = -.18f;
    b3Body_SetMassData(car->body, mass);
    shape.density = 25.0f / (3.14159265f * wheelRadius * wheelRadius * wheelWidth);
    shape.baseMaterial.friction = rollingTireFriction;
    body.allowFastRotation = true;
    body.angularDamping = .015f;
    const auto axle = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 0, 1));
    const auto wheelQ = q * axle;
    body.rotation = {{wheelQ.x, wheelQ.y, wheelQ.z}, wheelQ.w};
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 anchor(i % 2 ? .83f : -.83f, -.32f, i < 2 ? -1.15f : 1.15f);
        const auto p = center + q * anchor;
        body.position = {p.x, p.y, p.z};
        car->wheels[i] = b3CreateBody(impl_->world->id(), &body);
        car->tireShapes[i] = b3CreateHullShape(car->wheels[i], &shape, impl_->tire);
        auto joint = b3DefaultWheelJointDef();
        joint.base.bodyIdA = car->body; joint.base.bodyIdB = car->wheels[i];
        joint.base.localFrameA.p = vector(anchor);
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
        joint.lowerSteeringLimit = -.5f; joint.upperSteeringLimit = .5f;
        joint.enableSpinMotor = true;
        car->joints[i] = b3CreateWheelJoint(impl_->world->id(), &joint);
    }
    impl_->cars.push_back(std::move(car));
    return impl_->cars.size() - 1;
}
VehiclePose VehicleLayer::pose(std::size_t index) const {
    const auto& car = *impl_->cars.at(index);
    VehiclePose result{{index, position(car.body), {.85f, .325f, 1.8f}, rotation(car.body)}, {}};
    for (int i = 0; i < 4; ++i)
        result.wheels[i] = glm::translate(glm::mat4(1), position(car.wheels[i])) * glm::mat4_cast(rotation(car.wheels[i]));
    return result;
}
glm::vec3 VehicleLayer::velocity(std::size_t index) const { return vector(b3Body_GetLinearVelocity(impl_->cars.at(index)->body)); }
glm::vec3 VehicleLayer::pointVelocity(std::size_t index, glm::vec3 p) const {
    return vector(b3Body_GetWorldPointVelocity(impl_->cars.at(index)->body, {p.x, p.y, p.z}));
}
void VehicleLayer::applyImpulse(std::size_t index, glm::vec3 impulse, glm::vec3 p) {
    auto& car = *impl_->cars.at(index);
    if (!car.brake && car.throttle != 0) {
        const auto driveDirection = rotation(car.body) * glm::vec3(0, 0, car.throttle > 0 ? -1.f : 1.f);
        car.opposingImpulse += std::max(0.f, -glm::dot(impulse, driveDirection));
    }
    b3Body_ApplyLinearImpulse(car.body, vector(impulse), {p.x, p.y, p.z}, true);
}
void VehicleLayer::drive(std::size_t index, float throttle, float steering, bool brake) {
    auto& c = *impl_->cars.at(index);
    const float nextThrottle = std::isfinite(throttle) ? std::clamp(throttle, -1.f, 1.f) : 0;
    if (brake || nextThrottle * c.throttle <= 0) c.opposingImpulse = 0;
    c.throttle = nextThrottle;
    c.steering = std::isfinite(steering) ? std::clamp(steering, -1.f, 1.f) : 0;
    c.brake = brake;
}
void VehicleLayer::beforeStep(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    for (const auto& c : impl_->cars) {
        const float speed = glm::length(vector(b3Body_GetLinearVelocity(c->body)));
        // Reduce steering lock at speed, without overwriting lateral momentum.
        const float angle = -c->steering * .48f / (1 + speed * .035f);
        // Normal force is calibrated for the previous acceleration with the
        // higher grip. Reserve torque compensates resistance from the last fixed
        // step, rather than multiplying the unloaded throttle force as well.
        const float throttle = std::abs(c->throttle);
        const float force = std::min(fullThrust * throttle, normalThrust * throttle + c->opposingImpulse / seconds);
        c->opposingImpulse = 0;
        for (int i = 0; i < 4; ++i) {
            const auto j = c->joints[i];
            if (i < 2) b3WheelJoint_SetTargetSteeringAngle(j, angle);
            auto& traction = c->traction[i];
            traction = sampleWheelTraction(c->wheels[i], traction.motion);
            b3Shape_SetFriction(c->tireShapes[i], traction.friction);
            const bool contact = traction.motion != WheelMotion::Airborne;
            // Equal independent torque budgets: airborne wheels supply no drive.
            const float torque = c->brake ? 1400.f : force * wheelRadius * .25f;
            b3WheelJoint_SetMaxSpinTorque(j, contact ? torque : 0);
            b3WheelJoint_SetSpinMotorSpeed(j, c->brake ? 0 : -c->throttle * 85.f);
            if (std::abs(c->throttle) > 0 || std::abs(c->steering) > 0) {
                b3Body_SetAwake(c->body, true);
                b3Body_SetAwake(c->wheels[i], true);
            }
        }
    }
}
std::array<WheelTraction, 4> VehicleLayer::wheelTraction(std::size_t index) const {
    return impl_->cars.at(index)->traction;
}
int VehicleLayer::groundedWheels(std::size_t index) const {
    int count = 0;
    for (auto w : impl_->cars.at(index)->wheels)
        if (sampleWheelTraction(w, WheelMotion::Rolling).motion != WheelMotion::Airborne) ++count;
    return count;
}
void VehicleLayer::appendContacts(glm::vec3 feet, float radius, float height, float skin, std::vector<Contact>& contacts) const {
    // The number of prototype cars is small. Rigid body broad-phase rejects
    // distant chassis before the capsule/box narrow phase.
    struct Query { const VehicleLayer* layer; glm::vec3 feet; float radius, height, skin; std::vector<Contact>* out; };
    Query q{this, feet, radius, height, skin, &contacts};
    auto filter = b3DefaultQueryFilter(); filter.maskBits = vehicleBodyCategory;
    const float r = radius + skin;
    b3World_OverlapAABB(impl_->world->id(), {{feet.x-r, feet.y-skin, feet.z-r}, {feet.x+r, feet.y+height+skin, feet.z+r}}, filter,
        [](b3ShapeId s, void* context) {
            auto& query = *static_cast<Query*>(context);
            const auto body = b3Shape_GetBody(s);
            for (std::size_t i = 0; i < query.layer->count(); ++i) {
                if (!B3_ID_EQUALS(body, query.layer->impl_->cars[i]->body)) continue;
                const auto hits = boxContacts(query.layer->pose(i).chassis, query.feet, query.radius, query.height, query.skin);
                query.out->insert(query.out->end(), hits.begin(), hits.end());
            }
            return true;
        }, &q);
}
std::optional<GroundHit> VehicleLayer::groundAt(float x, float z, float minY, float maxY) const {
    auto filter = b3DefaultQueryFilter(); filter.maskBits = vehicleBodyCategory;
    const auto hit = b3World_CastRayClosest(impl_->world->id(), {x, maxY + .001f, z},
                                           {0, minY - maxY - .002f, 0}, filter);
    if (!hit.hit || hit.normal.y < .001f || hit.point.y < minY || hit.point.y > maxY) return {};
    return GroundHit{hit.point.y, vector(hit.normal)};
}
} // namespace viewer
