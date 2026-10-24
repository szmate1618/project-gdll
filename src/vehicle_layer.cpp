#include "vehicle_layer.hpp"
#include "vehicle_physics.hpp"
#include <unordered_map>
#include "physics_world.hpp"
#include "collision_world.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
std::atomic<std::uint64_t> nextLayerId{1};
constexpr float forwardMotorSpeed = 85.f, reverseMotorSpeed = 20.f; // rad/s
constexpr float reverseThrustScale = .5f;
constexpr float maxSteeringAngle = .5235988f; // 30 degrees, radians
constexpr float steeringSpeedReduction = .05f; // per m/s
constexpr float steeringRate = 3.f, steeringReturnRate = 4.f; // rad/s
float advanceSteeringAngle(float current, float target, float seconds) {
    const auto approach = [](float from, float to, float step) {
        return from + std::clamp(to - from, -step, step);
    };
    // A direction reversal first returns to center, then turns the other way.
    // Split the elapsed time at center so response is independent of step size.
    if (current * target < 0) {
        const float centeringTime = std::min(seconds, std::abs(current) / steeringReturnRate);
        current = approach(current, 0, steeringReturnRate * centeringTime);
        seconds -= centeringTime;
    }
    const float rate = std::abs(target) < std::abs(current) ? steeringReturnRate : steeringRate;
    return approach(current, target, rate * seconds);
}
glm::vec3 vector(b3Vec3 v) { return {v.x, v.y, v.z}; }
b3Vec3 vector(glm::vec3 v) { return {v.x, v.y, v.z}; }
glm::quat rotation(b3BodyId b) { const auto q = b3Body_GetRotation(b); return {q.s, q.v.x, q.v.y, q.v.z}; }
}
struct VehicleLayer::Impl {
    const std::uint64_t identity = nextLayerId.fetch_add(1);
    std::shared_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<VehiclePhysics>> cars;
    std::vector<VehicleConfig> types;
    std::vector<std::size_t> simulated;
    std::unordered_map<std::uint64_t,std::size_t> owners;
    std::vector<b3HullData*> tires;
    ~Impl() { cars.clear(); for (auto* tire : tires) if(tire) b3DestroyHull(tire); }
    void activate(std::size_t index) {
        auto& car=*cars.at(index);
        if(car.detailed) return;
        auto*& tire = tires.at(car.type);
        if(!tire) tire=b3CreateCylinder(car.config.tireWidth,car.config.tireRadius,-car.config.tireWidth/2,24);
        if(!tire) throw std::runtime_error("Cannot build tire cylinder");
        car.activate(tire);
        for(auto wheel:car.wheels) owners.emplace(b3StoreBodyId(wheel),index);
        simulated.push_back(index);
    }
    void park(std::size_t index) {
        auto& car=*cars[index];
        for(auto wheel:car.wheels) owners.erase(b3StoreBodyId(wheel));
        car.park();
    }
};
VehicleLayer::VehicleLayer(std::shared_ptr<PhysicsWorld> world, std::vector<VehicleConfig> types) : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Vehicles require a physics world");
    if (types.empty() || types.size() > maxVehicleTypes) throw std::invalid_argument("Invalid vehicle type count");
    for (const auto& config : types) validateVehicleConfig(config);
    impl_->types = std::move(types);
    impl_->tires.resize(impl_->types.size(),nullptr);
    impl_->world = std::move(world);
}
VehicleLayer::~VehicleLayer() = default;
std::uint64_t VehicleLayer::instanceId() const { return impl_->identity; }
std::uint64_t VehicleLayer::poseRevision(std::size_t car) const { return impl_->cars.at(car)->revision; }
std::size_t VehicleLayer::count() const { return impl_->cars.size(); }
std::size_t VehicleLayer::type(std::size_t car) const { return impl_->cars.at(car)->type; }
const VehicleConfig& VehicleLayer::config(std::size_t car) const { return impl_->types.at(type(car)); }
const VehicleConfig& VehicleLayer::typeConfig(std::size_t type) const { return impl_->types.at(type); }
std::size_t VehicleLayer::typeCount() const { return impl_->types.size(); }
std::size_t VehicleLayer::add(glm::vec3 center, float yaw, std::size_t type) {
    if (!std::isfinite(yaw)) throw std::invalid_argument("Vehicle yaw must be finite");
    return add(center, glm::angleAxis(yaw, glm::vec3(0,1,0)), type);
}
std::size_t VehicleLayer::add(glm::vec3 center, glm::quat orientation, std::size_t type) {
    const auto& config = impl_->types.at(type);
    if (count() >= maxVehicleInstances) throw std::length_error("Vehicle population exceeds runtime budget");
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        !std::isfinite(glm::length(orientation)) || std::abs(glm::length(orientation)-1) > .001f)
        throw std::invalid_argument("Vehicle pose must be finite with a unit rotation");
    auto car = std::make_unique<VehiclePhysics>(*impl_->world,config,center,orientation);
    car->type=type;
    const auto id=impl_->cars.size();
    impl_->owners.emplace(b3StoreBodyId(car->body),id);
    impl_->cars.push_back(std::move(car));
    return impl_->cars.size() - 1;
}
const std::vector<std::size_t>& VehicleLayer::simulatedCars() const { return impl_->simulated; }
bool VehicleLayer::simulating(std::size_t index) const { return impl_->cars.at(index)->detailed; }
void VehicleLayer::setDriven(std::size_t index,bool driven) {
    auto& car=*impl_->cars.at(index);
    if(driven) impl_->activate(index);
    car.driven=driven;
    if(!driven) { car.throttle=car.steering=car.opposingImpulse=0; car.brake=true; }
}
VehiclePose VehicleLayer::pose(std::size_t index) const { return impl_->cars.at(index)->pose(index); }
glm::vec3 VehicleLayer::velocity(std::size_t index) const {
    const auto& car=*impl_->cars.at(index);
    return car.detailed?vector(b3Body_GetLinearVelocity(car.body)):car.replicatedVelocity;
}
void VehicleLayer::setPose(std::size_t index,const VehiclePose& pose,glm::vec3 velocity) {
    impl_->cars.at(index)->setPose(pose,velocity);
}
glm::vec3 VehicleLayer::pointVelocity(std::size_t index, glm::vec3 p) const {
    const auto& car=*impl_->cars.at(index);
    return car.detailed?vector(b3Body_GetWorldPointVelocity(car.body,{p.x,p.y,p.z})):car.replicatedVelocity;
}
void VehicleLayer::applyImpulse(std::size_t index, glm::vec3 impulse, glm::vec3 p) {
    impl_->activate(index);
    recordResistanceImpulse(index, impulse);
    b3Body_ApplyLinearImpulse(impl_->cars.at(index)->body, vector(impulse), {p.x, p.y, p.z}, true);
}
void VehicleLayer::recordResistanceImpulse(std::size_t index, glm::vec3 impulse) {
    auto& car = *impl_->cars.at(index);
    if (!car.brake && car.throttle != 0) {
        const auto driveDirection = rotation(car.body) * glm::vec3(0, 0, car.throttle > 0 ? -1.f : 1.f);
        car.opposingImpulse += std::max(0.f, -glm::dot(impulse, driveDirection));
    }
}
std::optional<std::size_t> VehicleLayer::carForBody(b3BodyId body) const {
    const auto found=impl_->owners.find(b3StoreBodyId(body));
    if(found==impl_->owners.end()) return {};
    return found->second;
}
void VehicleLayer::drive(std::size_t index, float throttle, float steering, bool brake) {
    setDriven(index,true);
    auto& c = *impl_->cars.at(index);
    const float nextThrottle = std::isfinite(throttle) ? std::clamp(throttle, -1.f, 1.f) : 0;
    if (brake || nextThrottle * c.throttle <= 0) c.opposingImpulse = 0;
    c.throttle = nextThrottle;
    c.steering = std::isfinite(steering) ? std::clamp(steering, -1.f, 1.f) : 0;
    c.brake = brake;
}
void VehicleLayer::beforeStep(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    for(std::size_t active=0;active<impl_->simulated.size();) {
        const auto id=impl_->simulated[active];
        const auto& c=impl_->cars[id];
        if(c->canPark(seconds)) {
            impl_->park(id);
            impl_->simulated.erase(impl_->simulated.begin()+active);
            continue;
        }
        ++active;
        const auto velocity = vector(b3Body_GetLinearVelocity(c->body));
        const float speed = glm::length(velocity);
        const auto forward = rotation(c->body) * glm::vec3(0, 0, -1);
        const bool reverse = c->throttle < 0;
        // Keep S braking at full strength while moving forward; reduce thrust
        // once stopped or backing up. Space always retains its normal brake.
        const float thrustScale = reverse && glm::dot(velocity, forward) <= .5f ? reverseThrustScale : 1.f;
        // Reduce steering lock at speed, without overwriting lateral momentum.
        const float targetAngle = -c->steering * maxSteeringAngle / (1 + speed * steeringSpeedReduction);
        c->steeringAngle = advanceSteeringAngle(c->steeringAngle, targetAngle, seconds);
        // Normal force is calibrated for the previous acceleration with the
        // higher grip. Reserve torque compensates resistance from the last fixed
        // step, rather than multiplying the unloaded throttle force as well.
        const float throttle = std::abs(c->throttle);
        const float force = std::min(fullThrust * thrustScale * throttle,
                                     normalThrust * thrustScale * throttle + c->opposingImpulse / seconds);
        c->opposingImpulse = 0;
        for (int i = 0; i < 4; ++i) {
            const auto j = c->joints[i];
            if (i < 2) b3WheelJoint_SetTargetSteeringAngle(j, c->steeringAngle);
            auto& traction = c->traction[i];
            traction = sampleWheelTraction(c->wheels[i], traction);
            b3Shape_SetFriction(c->tireShapes[i], traction.friction);
            // Cylinder-local Y is its axle. Spinning preserves this axis;
            // steering and chassis tilt rotate it into the current contact frame.
            b3Shape_SetFrictionAxle(c->tireShapes[i], {0, 1, 0}, traction.lateralFriction);
            const bool contact = traction.motion != WheelMotion::Airborne;
            // Equal independent torque budgets: airborne wheels supply no drive.
            const float torque = c->brake ? 1400.f : force * c->config.tireRadius * .25f;
            b3WheelJoint_SetMaxSpinTorque(j, contact ? torque : 0);
            b3WheelJoint_SetSpinMotorSpeed(j, c->brake ? 0 : -c->throttle * (reverse ? reverseMotorSpeed : forwardMotorSpeed));
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
    if(!simulating(index)) return 0;
    int count = 0;
    for (auto w : impl_->cars.at(index)->wheels)
        if (sampleWheelTraction(w, WheelMotion::Rolling).motion != WheelMotion::Airborne) ++count;
    return count;
}
bool VehicleLayer::overlaps(glm::vec3 minimum,glm::vec3 maximum) const {
    struct Query { const VehicleLayer* layer; bool found=false; } query{this};
    auto filter=b3DefaultQueryFilter(); filter.maskBits=vehicleBodyCategory;
    b3World_OverlapAABB(impl_->world->id(),{{minimum.x,minimum.y,minimum.z},{maximum.x,maximum.y,maximum.z}},filter,
        [](b3ShapeId shape,void* context) {
            auto& query=*static_cast<Query*>(context);
            query.found=bool(query.layer->carForBody(b3Shape_GetBody(shape)));
            return !query.found;
        },&query);
    return query.found;
}
void VehicleLayer::appendContacts(glm::vec3 feet, float radius, float height, float skin, std::vector<Contact>& contacts) const {
    // Physics broad-phase selects shapes; ownership lookup avoids a fleet scan.
    struct Query { const VehicleLayer* layer; glm::vec3 feet; float radius, height, skin; std::vector<Contact>* out; };
    Query q{this, feet, radius, height, skin, &contacts};
    auto filter = b3DefaultQueryFilter(); filter.maskBits = vehicleBodyCategory;
    const float r = radius + skin;
    b3World_OverlapAABB(impl_->world->id(), {{feet.x-r, feet.y-skin, feet.z-r}, {feet.x+r, feet.y+height+skin, feet.z+r}}, filter,
        [](b3ShapeId s, void* context) {
            auto& query = *static_cast<Query*>(context);
            const auto body = b3Shape_GetBody(s);
            const auto owner=query.layer->carForBody(body);
            if(!owner) return true;
            const auto& car=*query.layer->impl_->cars[*owner];
            for(std::size_t part=0;part<car.boxes.size();++part) {
                if(!B3_ID_EQUALS(s,car.boxes[part])) continue;
                const auto hits=boxContacts(car.collider(*owner,part),query.feet,query.radius,query.height,query.skin);
                query.out->insert(query.out->end(),hits.begin(),hits.end());
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
