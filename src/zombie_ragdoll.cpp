#include "zombie_ragdoll.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace viewer {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float fixedStep = 1.0f / 60.0f;

enum Part {
    pelvis,
    torso,
    head,
    upperArmLeft,
    lowerArmLeft,
    upperArmRight,
    lowerArmRight,
    upperLegLeft,
    lowerLegLeft,
    upperLegRight,
    lowerLegRight,
    partCount
};

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

b3Pos toB3Pos(glm::vec3 value) {
    return {value.x, value.y, value.z};
}

b3Vec3 toB3Vec(glm::vec3 value) {
    return {value.x, value.y, value.z};
}

b3Quat toB3(glm::quat value) {
    value = glm::normalize(value);
    return {{value.x, value.y, value.z}, value.w};
}

glm::vec3 fromB3(b3Pos value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

glm::quat fromB3(b3Quat value) {
    return glm::normalize(glm::quat(value.s, value.v.x, value.v.y, value.v.z));
}

glm::vec3 safePerpendicular(glm::vec3 value) {
    value = glm::normalize(value);
    const glm::vec3 helper = std::abs(value.y) < 0.8f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    return glm::normalize(glm::cross(value, helper));
}

// Box3D capsules use their local Y axis as the segment axis.
glm::quat frameFromY(glm::vec3 axis, glm::vec3 xHint) {
    axis = glm::normalize(axis);
    glm::vec3 x = xHint - axis * glm::dot(xHint, axis);
    if (glm::dot(x, x) < 1e-6f) x = safePerpendicular(axis);
    x = glm::normalize(x);
    glm::vec3 z = glm::normalize(glm::cross(x, axis));
    x = glm::normalize(glm::cross(axis, z));
    return glm::normalize(glm::quat_cast(glm::mat3(x, axis, z)));
}

// Box3D uses the local Z axis as a spherical twist or revolute hinge axis.
glm::quat frameFromZ(glm::vec3 axis, glm::vec3 xHint) {
    axis = glm::normalize(axis);
    glm::vec3 x = xHint - axis * glm::dot(xHint, axis);
    if (glm::dot(x, x) < 1e-6f) x = safePerpendicular(axis);
    x = glm::normalize(x);
    const glm::vec3 y = glm::normalize(glm::cross(axis, x));
    x = glm::normalize(glm::cross(y, axis));
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, axis)));
}

class Ragdoll {
public:
    Ragdoll(b3WorldId world, glm::vec3 feet, float yaw, glm::vec3 impulse, int collisionGroup)
        : world_(world) {
        const auto forward = glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
        const auto rotate = [yaw, feet](glm::vec3 local) {
            return feet + glm::mat3(glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0))) * local;
        };

        // A small local static box is deliberately used instead of importing the
        // complete terrain mesh into Box3D. It is enough to keep each ragdoll on
        // the ground it was standing on and keeps activation cheap.
        auto groundDef = b3DefaultBodyDef();
        groundDef.type = b3_staticBody;
        groundDef.position = toB3Pos({feet.x, feet.y - 0.05f, feet.z});
        const auto ground = b3CreateBody(world_, &groundDef);
        auto groundShape = b3DefaultShapeDef();
        const auto groundBox = b3MakeBoxHull(3.0f, 0.05f, 3.0f);
        b3CreateHullShape(ground, &groundShape, &groundBox.base);

        // The dimensions are deliberately stable and close to the normalized
        // 1.8 m character frame used by zombie_layer.cpp.
        bodies_[pelvis] = addCapsule(rotate({0.0f, 0.71f, 0.0f}), rotate({0.0f, 1.05f, 0.0f}),
                                     0.22f, 0.34f, 12.0f, forward, collisionGroup);
        const auto torsoBottom = rotate({0.0f, 1.08f, 0.0f});
        const auto torsoTop = rotate({0.0f, 1.70f, 0.0f});
        bodies_[torso] = addCapsule(torsoBottom, torsoTop, 0.30f, 0.62f, 25.0f, forward, collisionGroup);
        bodies_[head] = addSphere(rotate({0.0f, 1.98f, 0.0f}), 0.22f, 5.0f, forward, collisionGroup);

        const auto addLimb = [&](Part part, glm::vec3 a, glm::vec3 b, float radius, float mass) {
            bodies_[part] = addCapsule(rotate(a), rotate(b), radius, glm::length(b - a), mass, forward, collisionGroup);
        };
        // The supplied Biped assets use positive X for their "L" bones and keep
        // the upper/lower arms nearly horizontal in the reference pose. Keeping
        // the physical rest pose close to that pose avoids a large correction
        // when the live-skinned arm bones take over.
        addLimb(upperArmLeft, {0.34f, 1.58f, 0.0f}, {0.62f, 1.48f, 0.0f}, 0.105f, 2.2f);
        addLimb(lowerArmLeft, {0.62f, 1.48f, 0.0f}, {0.86f, 1.43f, 0.0f}, 0.09f, 1.6f);
        addLimb(upperArmRight, {-0.34f, 1.58f, 0.0f}, {-0.62f, 1.48f, 0.0f}, 0.105f, 2.2f);
        addLimb(lowerArmRight, {-0.62f, 1.48f, 0.0f}, {-0.86f, 1.43f, 0.0f}, 0.09f, 1.6f);
        addLimb(upperLegLeft, {-0.16f, 0.78f, 0.0f}, {-0.18f, 0.38f, 0.0f}, 0.14f, 7.5f);
        addLimb(lowerLegLeft, {-0.18f, 0.38f, 0.0f}, {-0.18f, 0.08f, 0.0f}, 0.115f, 4.0f);
        addLimb(upperLegRight, {0.16f, 0.78f, 0.0f}, {0.18f, 0.38f, 0.0f}, 0.14f, 7.5f);
        addLimb(lowerLegRight, {0.18f, 0.38f, 0.0f}, {0.18f, 0.08f, 0.0f}, 0.115f, 4.0f);

        addBallJoint(pelvis, torso, rotate({0, 1.08f, 0}), glm::vec3(0, 1, 0), forward, 40, -35, 35);
        addBallJoint(torso, head, rotate({0, 1.70f, 0}), glm::vec3(0, 1, 0), forward, 55, -70, 70);
        addBallJoint(torso, upperArmLeft, rotate({0.34f, 1.58f, 0}), glm::vec3(1, -0.35f, 0), forward, 120, -95, 95);
        addBallJoint(torso, upperArmRight, rotate({-0.34f, 1.58f, 0}), glm::vec3(-1, -0.35f, 0), forward, 120, -95, 95);
        addHingeJoint(upperArmLeft, lowerArmLeft, rotate({0.62f, 1.48f, 0}), glm::vec3(0.24f, -0.05f, 0), forward, -20, 160);
        addHingeJoint(upperArmRight, lowerArmRight, rotate({-0.62f, 1.48f, 0}), glm::vec3(-0.24f, -0.05f, 0), forward, -20, 160);
        addBallJoint(pelvis, upperLegLeft, rotate({-0.16f, 0.78f, 0}), glm::vec3(0, -1, 0), forward, 70, -45, 45);
        addBallJoint(pelvis, upperLegRight, rotate({0.16f, 0.78f, 0}), glm::vec3(0, -1, 0), forward, 70, -45, 45);
        addHingeJoint(upperLegLeft, lowerLegLeft, rotate({-0.18f, 0.38f, 0}), glm::vec3(0, -1, 0), -forward, -15, 165);
        addHingeJoint(upperLegRight, lowerLegRight, rotate({0.18f, 0.38f, 0}), glm::vec3(0, -1, 0), -forward, -15, 165);

        for (int part = 0; part < partCount; ++part)
            restBodies_[static_cast<std::size_t>(part)] = bodyTransform(static_cast<Part>(part));

        // The hit impulse is horizontal/aim-directed. Adding an unconditional
        // upward velocity here made every activation visibly jump.
        b3Body_SetLinearVelocity(bodies_[pelvis], toB3Vec(impulse * 0.7f));
        b3Body_SetAngularVelocity(bodies_[pelvis], toB3Vec(glm::vec3(impulse.z, 0.0f, -impulse.x) * 1.4f + glm::vec3(0, 0, 1.2f)));
    }

    glm::mat4 rootTransform() const {
        const auto position = fromB3(b3Body_GetPosition(bodies_[pelvis]));
        const auto rotation = fromB3(b3Body_GetRotation(bodies_[pelvis]));
        const auto matrix = glm::mat4_cast(rotation);
        // Keep the instance origin at the pelvis' vertical ground offset. The
        // pelvis bone itself carries pitch/roll; rotating this offset as well
        // makes the entire rendered zombie hop whenever the pelvis tips.
        const float yaw = std::atan2(matrix[2][0], matrix[0][0]);
        return glm::translate(glm::mat4(1.0f), position - glm::vec3(0, 0.88f, 0)) *
               glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
    }

    glm::mat4 bodyTransform(Part part) const {
        const auto position = fromB3(b3Body_GetPosition(bodies_[part]));
        const auto rotation = fromB3(b3Body_GetRotation(bodies_[part]));
        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
    }

    const std::array<glm::mat4, partCount>& restBodyTransforms() const { return restBodies_; }

    std::array<glm::mat4, partCount> bodyTransforms() const {
        std::array<glm::mat4, partCount> result{};
        for (int part = 0; part < partCount; ++part) result[static_cast<std::size_t>(part)] = bodyTransform(static_cast<Part>(part));
        return result;
    }

private:
    b3Transform localJointFrame(b3BodyId body, glm::vec3 pivot, glm::quat rotation) const {
        b3Transform result = b3Transform_identity;
        result.p = b3Body_GetLocalPoint(body, toB3Pos(pivot));
        result.q = b3InvMulQuat(b3Body_GetRotation(body), toB3(rotation));
        return result;
    }

    b3BodyId addCapsule(glm::vec3 p0, glm::vec3 p1, float radius, float length, float mass,
                        glm::vec3 forward, int collisionGroup) {
        auto bodyDef = b3DefaultBodyDef();
        bodyDef.type = b3_dynamicBody;
        bodyDef.position = toB3Pos((p0 + p1) * 0.5f);
        bodyDef.rotation = toB3(frameFromY(p1 - p0, forward));
        bodyDef.linearDamping = 0.025f;
        bodyDef.angularDamping = 0.045f;
        const auto body = b3CreateBody(world_, &bodyDef);
        auto shapeDef = b3DefaultShapeDef();
        shapeDef.density = mass / (pi * radius * radius * length + (4.0f / 3.0f) * pi * radius * radius * radius);
        shapeDef.baseMaterial.friction = 0.6f;
        shapeDef.filter.groupIndex = collisionGroup;
        const b3Capsule capsule{{0, -length * 0.5f, 0}, {0, length * 0.5f, 0}, radius};
        b3CreateCapsuleShape(body, &shapeDef, &capsule);
        return body;
    }

    b3BodyId addSphere(glm::vec3 center, float radius, float mass, glm::vec3 forward, int collisionGroup) {
        auto bodyDef = b3DefaultBodyDef();
        bodyDef.type = b3_dynamicBody;
        bodyDef.position = toB3Pos(center);
        bodyDef.rotation = toB3(frameFromY(glm::vec3(0, 1, 0), forward));
        bodyDef.linearDamping = 0.025f;
        bodyDef.angularDamping = 0.045f;
        const auto body = b3CreateBody(world_, &bodyDef);
        auto shapeDef = b3DefaultShapeDef();
        shapeDef.density = mass / ((4.0f / 3.0f) * pi * radius * radius * radius);
        shapeDef.baseMaterial.friction = 0.6f;
        shapeDef.filter.groupIndex = collisionGroup;
        const b3Sphere sphere{{0, 0, 0}, radius};
        b3CreateSphereShape(body, &shapeDef, &sphere);
        return body;
    }

    void addBallJoint(Part parent, Part child, glm::vec3 pivot, glm::vec3 childAxis,
                      glm::vec3 forward, float cone, float lowerTwist, float upperTwist) {
        const auto frame = frameFromZ(childAxis, forward);
        auto joint = b3DefaultSphericalJointDef();
        joint.base.bodyIdA = bodies_[parent];
        joint.base.bodyIdB = bodies_[child];
        joint.base.localFrameA = localJointFrame(bodies_[parent], pivot, frame);
        joint.base.localFrameB = localJointFrame(bodies_[child], pivot, frame);
        joint.base.collideConnected = false;
        joint.enableConeLimit = true;
        joint.coneAngle = cone * B3_DEG_TO_RAD;
        joint.enableTwistLimit = true;
        joint.lowerTwistAngle = lowerTwist * B3_DEG_TO_RAD;
        joint.upperTwistAngle = upperTwist * B3_DEG_TO_RAD;
        b3CreateSphericalJoint(world_, &joint);
    }

    void addHingeJoint(Part parent, Part child, glm::vec3 pivot, glm::vec3 limbDirection,
                       glm::vec3 bendDirection, float lower, float upper) {
        const auto direction = glm::normalize(limbDirection);
        auto bend = bendDirection - direction * glm::dot(bendDirection, direction);
        if (glm::dot(bend, bend) < 1e-6f) bend = safePerpendicular(direction);
        bend = glm::normalize(bend);
        const auto frame = frameFromZ(glm::normalize(glm::cross(direction, bend)), direction);
        auto joint = b3DefaultRevoluteJointDef();
        joint.base.bodyIdA = bodies_[parent];
        joint.base.bodyIdB = bodies_[child];
        joint.base.localFrameA = localJointFrame(bodies_[parent], pivot, frame);
        joint.base.localFrameB = localJointFrame(bodies_[child], pivot, frame);
        joint.base.collideConnected = false;
        joint.enableLimit = true;
        joint.lowerAngle = lower * B3_DEG_TO_RAD;
        joint.upperAngle = upper * B3_DEG_TO_RAD;
        b3CreateRevoluteJoint(world_, &joint);
    }

    b3WorldId world_ = b3_nullWorldId;
    std::array<b3BodyId, partCount> bodies_{};
    std::array<glm::mat4, partCount> restBodies_{};
};

} // namespace

struct ZombieRagdollWorld::Impl {
    struct Entry {
        std::unique_ptr<Ragdoll> ragdoll;
    };

    b3WorldId world = b3_nullWorldId;
    std::vector<Entry> entries;
    float accumulator = 0.0f;
    int nextCollisionGroup = -1;
};

ZombieRagdollWorld::ZombieRagdollWorld() : impl_(std::make_unique<Impl>()) {
    auto definition = b3DefaultWorldDef();
    definition.gravity = {0.0f, -9.8f, 0.0f};
    definition.enableSleep = true;
    definition.enableContinuous = true;
    impl_->world = b3CreateWorld(&definition);
    if (!b3World_IsValid(impl_->world)) throw std::runtime_error("Unable to create Box3D ragdoll world");
}

ZombieRagdollWorld::~ZombieRagdollWorld() {
    if (impl_ && b3World_IsValid(impl_->world)) b3DestroyWorld(impl_->world);
}

ZombieRagdollWorld::ZombieRagdollWorld(ZombieRagdollWorld&&) noexcept = default;

ZombieRagdollWorld& ZombieRagdollWorld::operator=(ZombieRagdollWorld&& other) noexcept {
    if (this == &other) return *this;
    if (impl_ && b3World_IsValid(impl_->world)) b3DestroyWorld(impl_->world);
    impl_ = std::move(other.impl_);
    return *this;
}

std::size_t ZombieRagdollWorld::create(glm::vec3 feet, float yaw, glm::vec3 impulse) {
    if (!impl_ || !std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        !std::isfinite(yaw) || !std::isfinite(impulse.x) || !std::isfinite(impulse.y) || !std::isfinite(impulse.z)) {
        throw std::invalid_argument("Ragdoll creation requires finite transforms and impulse");
    }
    const auto group = impl_->nextCollisionGroup--;
    impl_->entries.push_back({std::make_unique<Ragdoll>(impl_->world, feet, yaw, impulse, group)});
    return impl_->entries.size() - 1;
}

void ZombieRagdollWorld::step(float seconds) {
    if (!impl_ || !std::isfinite(seconds) || seconds <= 0.0f) return;
    impl_->accumulator = std::min(impl_->accumulator + seconds, 0.2f);
    int steps = 0;
    while (impl_->accumulator >= fixedStep && steps++ < 8) {
        b3World_Step(impl_->world, fixedStep, 4);
        impl_->accumulator -= fixedStep;
    }
}

glm::mat4 ZombieRagdollWorld::rootTransform(std::size_t handle) const {
    if (!impl_ || handle >= impl_->entries.size()) return glm::mat4(1.0f);
    return impl_->entries[handle].ragdoll->rootTransform();
}

std::array<glm::mat4, ragdollPartCount> ZombieRagdollWorld::bodyTransforms(std::size_t handle) const {
    if (!impl_ || handle >= impl_->entries.size()) return {};
    return impl_->entries[handle].ragdoll->bodyTransforms();
}

std::array<glm::mat4, ragdollPartCount> ZombieRagdollWorld::restBodyTransforms(std::size_t handle) const {
    if (!impl_ || handle >= impl_->entries.size()) return {};
    return impl_->entries[handle].ragdoll->restBodyTransforms();
}

std::size_t ZombieRagdollWorld::count() const {
    return impl_ ? impl_->entries.size() : 0;
}

} // namespace viewer
