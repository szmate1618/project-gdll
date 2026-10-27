#include "physics_prop.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <glm/geometric.hpp>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 p) { return {p.x,p.y,p.z}; }
template<class V> glm::vec3 vector(V p) { return {float(p.x),float(p.y),float(p.z)}; }
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
}
PhysicsProp::PhysicsProp(std::shared_ptr<PhysicsWorld> world, b3BodyId body)
    : world_(std::move(world)), body_(body) {}
bool PhysicsProp::valid() const { return world_ && b3Body_IsValid(body_); }
bool PhysicsProp::grabbed() const { return valid() && world_->isGrabbed(body_); }
bool PhysicsProp::awake() const { return valid() && b3Body_IsAwake(body_); }
bool PhysicsProp::operator==(const PhysicsProp& other) const {
    return world_ == other.world_ && B3_ID_EQUALS(body_,other.body_);
}
b3BodyId PhysicsProp::body() const {
    if (!valid()) throw std::logic_error("Prop body no longer exists");
    return body_;
}
glm::vec3 PhysicsProp::center() const { return vector(b3Body_GetPosition(body())); }
float PhysicsProp::mass() const { return b3Body_GetMass(body()); }
PropPointState PhysicsProp::pointState(glm::vec3 localPoint) const {
    if (!finite(localPoint)) throw std::invalid_argument("Invalid prop grip point");
    const auto id = body();
    const auto point = b3Body_GetWorldPoint(id,vector(localPoint));
    return {vector(point),vector(b3Body_GetWorldPointVelocity(id,point))};
}
void PhysicsProp::applyImpulse(glm::vec3 impulse, glm::vec3 point) const {
    if (!finite(impulse) || !finite(point)) throw std::invalid_argument("Invalid prop impulse or point");
    b3Body_ApplyLinearImpulse(body(),vector(impulse),{point.x,point.y,point.z},true);
}
std::optional<PropRayHit> raycastPhysicsProp(std::shared_ptr<PhysicsWorld> world,
        glm::vec3 origin, glm::vec3 direction, float reach) {
    const float length = glm::length(direction);
    if (!world || !finite(origin) || !finite(direction) || !std::isfinite(length) || length < 1e-6f ||
        !std::isfinite(reach) || reach <= 0 || reach > 1024) return std::nullopt;
    auto filter = b3DefaultQueryFilter();
    filter.maskBits = interiorBoxCategory | zombieContentsCategory;
    const auto hit = b3World_CastRayClosest(world->id(),{origin.x,origin.y,origin.z},vector(direction/length*reach),filter);
    if (!hit.hit) return std::nullopt;
    const auto body = b3Shape_GetBody(hit.shapeId);
    return PropRayHit{PhysicsProp(std::move(world),body),vector(hit.point),
                      vector(b3Body_GetLocalPoint(body,hit.point)),hit.fraction*reach};
}
} // namespace viewer
