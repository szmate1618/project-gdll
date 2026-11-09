#include "player_interaction.hpp"
#include "collision_world.hpp"
#include "object_throw.hpp"
#include "weapon_config.hpp"
#include "throw_charge.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
void PlayerInteraction::cancel() { chargeStarted_.reset(); objects_.cancel(); doors_.release(); }
void PlayerInteraction::release(Hand hand, const Camera& camera, DoorLayer& doors, BoxLayer& boxes,
                                const CollisionWorld& world, double seconds) {
    if (hand == Hand::Left) {
        objects_.cancel(); doors_.release();
        return;
    }
    if (hand == Hand::Right) { objects_.endRotation(); return; }
    if (!chargeStarted_) return;
    const double duration = seconds - *chargeStarted_;
    chargeStarted_.reset();
    if (!std::isfinite(duration) || duration < 0) { cancel(); return; }
    const bool held = grabbing();
    if (chargingGrip_ && !held) { cancel(); return; }
    // A tap drops a held object; empty-handed taps give a small shove.
    // Free props use a mass-dependent throw; hinged doors retain their shove.
    const float charge = throwChargePower(duration);
    const float strength = held ? charge : std::max(.2f, charge);
    const auto doorImpulse = (camera.direction() * GrenadeConfig{}.throwSpeed + glm::vec3(0, 2, 0)) * (.45f * strength);
    if (const auto prop = objects_.heldProp()) {
        const auto center = prop->center();
        objects_.cancel(); // Remove the constraint before applying momentum.
        if (glm::distance(center, camera.position()) <= 5)
            prop->applyImpulse(objectThrowImpulse(camera.direction(), prop->mass(), strength), center);
    } else if (const auto heldDoor = doors_.grabbedDoor()) {
        const auto hit = doors.raycast(camera.position(), camera.direction(), 1.5f);
        doors_.release();
        if (hit && hit->door == *heldDoor && world.lineOfSight(camera.position(), hit->point))
            doors.applyImpulse(*heldDoor, doorImpulse, hit->point);
    } else {
        // Resolve at release, so charging never stores a remote or occluded target.
        const auto prop = raycastPhysicsProp(boxes.physicsWorld(), camera.position(), camera.direction(),
                                            ObjectInteraction::rayReach(camera.direction()));
        const auto door = doors.raycast(camera.position(), camera.direction(), 1.5f);
        if (door && (!prop || door->distance <= prop->distance)) {
            if (world.lineOfSight(camera.position(), door->point)) doors.applyImpulse(door->door, doorImpulse, door->point);
        } else if (prop && world.lineOfSight(camera.position(), prop->point)) {
            prop->prop.applyImpulse(objectThrowImpulse(camera.direction(), prop->prop.mass(), strength), prop->point);
        }
    }
}
bool PlayerInteraction::press(Hand hand, const Camera& camera, DoorLayer& doors, BoxLayer& boxes,
                              const CollisionWorld& world, double seconds) {
    if (!std::isfinite(seconds)) return false;
    if (hand == Hand::Right) return !charging() && objects_.beginRotation(camera);
    if (hand == Hand::Throw) {
        objects_.endRotation();
        if (!chargeStarted_) { chargeStarted_ = seconds; chargingGrip_ = grabbing(); }
        return true;
    }
    if (charging() || grabbing()) return false;
    const auto prop = raycastPhysicsProp(boxes.physicsWorld(),camera.position(),camera.direction(),ObjectInteraction::rayReach(camera.direction()));
    const auto door = doors.raycast(camera.position(), camera.direction(), 1.5f);
    if (door && (!prop || door->distance <= prop->distance)) return doors_.press(camera, doors, world);
    return objects_.press(camera, boxes, world);
}
void PlayerInteraction::look(Camera& camera, float dx, float dy) {
    if (rotating()) { objects_.rotateMouse(dx, dy); return; }
    moveGrip(dx, dy);
    if (doors_.grabbing()) doors_.look(camera, dx, dy);
    else camera.look(dx, dy);
}
void PlayerInteraction::moveGrip(float dx, float dy) {
    if (rotating()) objects_.rotateMouse(dx, dy);
    else objects_.moveMouse(dx, dy);
}
void PlayerInteraction::update(const Camera& camera, float dt, DoorLayer& doors, BoxLayer& boxes,
                               const CollisionWorld& world, double seconds) {
    doors_.update(camera, dt);
    objects_.update(camera, dt);
    if (chargeStarted_ && std::isfinite(seconds) && seconds - *chargeStarted_ >= throwChargeSeconds)
        release(Hand::Throw, camera, doors, boxes, world, seconds);
}
} // namespace viewer
