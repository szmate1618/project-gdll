#include "object_interaction.hpp"
#include "collision_world.hpp"
#include "nail_interaction.hpp"
#include "binding_interaction.hpp"
#include "grip_interaction.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace viewer {
namespace {
constexpr float metersPerPixel = .004f;
constexpr float maxLiftForce = ObjectInteraction::maximumLiftForce;
constexpr float extraDragForce = 240; // N of additional joint budget for unliftable props.
constexpr float radiansPerPixel = .005f;
}
std::optional<std::size_t> ObjectInteraction::heldBox() const {
    return grab_ ? grab_->boxes->indexOf(grab_->prop) : std::nullopt;
}
std::optional<PhysicsProp> ObjectInteraction::heldProp() const {
    return grab_ && grab_->prop.valid() && grab_->joint->valid() ? std::optional<PhysicsProp>(grab_->prop) : std::nullopt;
}
float ObjectInteraction::rayReach(glm::vec3 direction) {
    return std::min(1.5f/std::max(glm::length(glm::vec2(direction.x,direction.z)),1e-6f),
                    2.0f/std::max(std::abs(direction.y),1e-6f));
}
bool ObjectInteraction::press(const Camera& camera, BoxLayer& boxes, const CollisionWorld& world) {
    if (settingGrip()) { dragging_=true; gripRotation_.reset(); grips_->move(gripEditor_,camera); return true; }
    if (!crafting_) grab_.reset();
    const auto hit = raycastPhysicsProp(boxes.physicsWorld(),camera.position(),camera.direction(),rayReach(camera.direction()));
    if (!hit || !world.lineOfSight(camera.position(), hit->point)) return false;
    if (crafting_ && heldProp() == std::optional<PhysicsProp>(hit->prop)) {
        if (nails_ && nails_->pinned(hit->prop)) {
            pendingDrive_ = true;
            dragging_ = false;
            grab_->rayOrigin = camera.position();
            grab_->rayDirection = camera.direction();
            grab_->eyeOffset = nails_->tip(hit->prop) - camera.position();
            return true;
        }
        dragging_ = true;
        grab_->eyeOffset = hit->point - camera.position();
        grab_->drag = {};
        return true;
    }
    if (hit->prop.grabbed()) return false;
    if (crafting_ && grab_) endCraftingDrag();
    grab_.reset(); pendingDrive_ = false;
    const float yaw = glm::radians(camera.yawDegrees());
    const glm::vec3 forward(std::cos(yaw), 0, std::sin(yaw));
    const auto right = glm::cross(forward, glm::vec3(0, 1, 0));
    // Keep the combined lift/drag budget below a heavy prop's weight.
    const float gripForce = std::max(maxLiftForce, std::min(maxLiftForce+extraDragForce,
                                                         hit->prop.mass()*9.8f*.9f));
    grab_ = Grab{&boxes, hit->prop, std::make_unique<PhysicsGrab>(hit->prop,hit->localPoint,gripForce),
                 hit->localPoint, hit->point - camera.position(), right, {}, {}};
    if (crafting_) {
        grab_->joint->setCraftingMode(true);
        grab_->target = hit->point;
        dragging_ = true;
    }
    return true;
}
bool ObjectInteraction::grip(const Camera& camera, BoxLayer& boxes, const PhysicsProp& prop) {
    if (!prop.valid() || prop.grabbed()) return false;
    finishSetGrip();
    if (crafting_ && grab_) endCraftingDrag();
    const float yaw = glm::radians(camera.yawDegrees());
    const auto right = glm::vec3(-std::sin(yaw), 0, std::cos(yaw));
    grab_ = Grab{&boxes, prop, std::make_unique<PhysicsGrab>(prop, glm::vec3(0), maxLiftForce),
        glm::vec3(0), prop.center() - camera.position(), right, {}, {}};
    if (crafting_) {
        grab_->joint->setCraftingMode(true);
        grab_->target = prop.center();
        dragging_ = true;
    }
    return true;
}
void ObjectInteraction::cancel() {
    if (settingGrip()) grips_->cancel(gripEditor_);
    gripEditor_=0; gripRotation_.reset();
    if (grab_) {
        if (bindings_) bindings_->cancel(grab_->prop);
        if (crafting_) grab_->joint->park();
        else grab_->joint->resume();
    }
    grab_.reset(); dragging_ = pendingDrive_ = false;
}
void ObjectInteraction::drop() {
    if (settingGrip()) grips_->cancel(gripEditor_);
    gripEditor_=0; gripRotation_.reset();
    if (grab_) {
        if (bindings_) bindings_->cancel(grab_->prop);
        if (nails_) nails_->detach(grab_->prop);
        grab_->joint->resume();
    }
    grab_.reset(); dragging_ = pendingDrive_ = false;
}
void ObjectInteraction::setCraftingMode(bool enabled, NailInteraction* nails, BindingInteraction* bindings,GripInteraction* grips) {
    if (crafting_ && !enabled) finishSetGrip();
    if (crafting_ && !enabled && grab_) { pendingDrive_ = false; endCraftingDrag(); grab_.reset(); }
    crafting_ = enabled;
    nails_ = nails;
    bindings_ = bindings;
    grips_ = grips;
    if (grab_) {
        grab_->joint->setCraftingMode(enabled);
        grab_->target = grab_->prop.pointState(grab_->localPoint).point;
    }
}
void ObjectInteraction::endCraftingDrag() {
    if (settingGrip()) { dragging_=false; grips_->release(gripEditor_); return; }
    if (!crafting_ || !grab_) return;
    if (pendingDrive_ && nails_) {
        const auto prop = grab_->prop;
        cancel(); // The placement controller releases suspension before fastening.
        nailDriven_ = nails_->drive(prop);
        return;
    }
    if (dragging_ && bindings_) bindings_->finish(grab_->prop);
    dragging_ = false;
    grab_->joint->park();
}
bool ObjectInteraction::takeNailDriven() { return std::exchange(nailDriven_, false); }
bool ObjectInteraction::settingGrip() const { return grips_ && gripEditor_ && grips_->active(gripEditor_); }
bool ObjectInteraction::beginSetGrip(const Camera& camera) {
    if (!crafting_ || !grips_) return false;
    finishSetGrip();
    if (grab_) { endCraftingDrag(); grab_.reset(); }
    gripEditor_=grips_->begin(camera); dragging_=true; pendingDrive_=false;
    return settingGrip();
}
void ObjectInteraction::finishSetGrip() {
    if (settingGrip()) grips_->finish(gripEditor_);
    gripEditor_=0; gripRotation_.reset();
}
void ObjectInteraction::moveMouse(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !grab_) return;
    grab_->drag = glm::clamp(grab_->drag + glm::vec2(dx, dy) * metersPerPixel,
                            glm::vec2(-1.5f, -2), glm::vec2(1.5f, 2));
}
bool ObjectInteraction::beginRotation(const Camera& camera) {
    if (settingGrip()) {
        const float yaw=glm::radians(camera.yawDegrees());
        gripRotation_=Rotation{grips_->rotation(gripEditor_),{-std::sin(yaw),0,std::cos(yaw)}};
        return true;
    }
    if (!heldProp()) return false;
    const float yaw = glm::radians(camera.yawDegrees());
    grab_->rotation = Rotation{grab_->joint->rotation(), {-std::sin(yaw), 0, std::cos(yaw)}};
    grab_->joint->setRotationTarget(grab_->rotation->initial);
    return true;
}
void ObjectInteraction::endRotation() { if (grab_) grab_->rotation.reset(); gripRotation_.reset(); }
bool ObjectInteraction::rotating() const { return (settingGrip() && gripRotation_) || (heldProp() && grab_->rotation.has_value()); }
void ObjectInteraction::rotateMouse(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !rotating()) return;
    if (settingGrip()) {
        auto& rotation=*gripRotation_;
        rotation.yaw+=dx*radiansPerPixel;
        rotation.pitch=std::clamp(rotation.pitch-dy*radiansPerPixel,-1.2f,1.2f);
        grips_->rotate(gripEditor_,glm::angleAxis(rotation.pitch,rotation.right)*
            glm::angleAxis(rotation.yaw,glm::vec3(0,1,0))*rotation.initial);
        return;
    }
    auto& rotation = *grab_->rotation;
    // Match the editor's right-drag orbit: right turns the object right, and
    // dragging down tips its top toward the camera. Keep pitch away from a flip.
    rotation.yaw += dx * radiansPerPixel;
    rotation.pitch = std::clamp(rotation.pitch - dy * radiansPerPixel, -1.2f, 1.2f);
    const auto yaw = glm::angleAxis(rotation.yaw, glm::vec3(0, 1, 0));
    const auto pitch = glm::angleAxis(rotation.pitch, rotation.right);
    grab_->joint->setRotationTarget(pitch * yaw * rotation.initial);
    if (crafting_ && nails_ && nails_->pinned(grab_->prop)) {
        nails_->rotate(grab_->prop, pitch * yaw * rotation.initial);
        grab_->target = nails_->tip(grab_->prop);
    }
}
void ObjectInteraction::update(const Camera& camera, float dt) {
    if (!std::isfinite(dt) || dt <= 0) return;
    if (settingGrip()) { if (dragging_ && !gripRotation_) grips_->move(gripEditor_,camera); return; }
    if (!grab_) return;
    if (!heldProp()) { cancel(); return; }
    if (crafting_) {
        const bool rayMoved = glm::distance(camera.position(), grab_->rayOrigin) > .003f ||
                              glm::distance(camera.direction(), grab_->rayDirection) > .003f;
        if (pendingDrive_ && rayMoved && !grab_->rotation) {
            pendingDrive_ = false;
            dragging_ = true;
        }
        if (dragging_ && !grab_->rotation) {
            if (rayMoved && nails_ && nails_->pinned(grab_->prop)) nails_->detach(grab_->prop);
            if (nails_ && nails_->isNail(grab_->prop) && nails_->snap(grab_->prop, camera)) {
                // The nail's authored local tip is five centimetres below its centre.
                grab_->localPoint = {0, -.05f, 0};
                grab_->joint->setGripPoint(grab_->localPoint);
                grab_->joint->setRotationTarget(grab_->joint->rotation());
                grab_->target = nails_->tip(grab_->prop);
            } else {
                if (nails_ && nails_->pinned(grab_->prop)) nails_->detach(grab_->prop);
                grab_->target = camera.position() + camera.direction() * glm::length(grab_->eyeOffset);
            }
        } else if (nails_ && nails_->pinned(grab_->prop)) grab_->target = nails_->tip(grab_->prop);
        grab_->joint->setTarget(grab_->target);
        if (dragging_ && bindings_ && bindings_->isBinding(grab_->prop)) bindings_->update(grab_->prop,camera);
        grab_->rayOrigin = camera.position();
        grab_->rayDirection = camera.direction();
        return;
    }
    const auto point = grab_->prop.pointState(grab_->localPoint).point;
    const auto target = camera.position() + grab_->eyeOffset + grab_->right * grab_->drag.x + glm::vec3(0, grab_->drag.y, 0);
    if (glm::distance(point, camera.position()) > 5 || glm::distance(point, target) > 3) {
        cancel(); return;
    }
    grab_->joint->setTarget(target);
}
} // namespace viewer
