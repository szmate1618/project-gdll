#include "zombie_ragdoll.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {

constexpr float pi = 3.14159265358979323846f;

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
    Ragdoll(PhysicsWorld& simulation, glm::vec3 feet, float yaw, glm::vec3 impulse, int collisionGroup,
            const RagdollPose& reference, const RagdollPose& current)
        : simulation_(simulation), world_(simulation.id()), feet_(feet), yaw_(yaw) {
        const auto facing = glm::mat3(glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)));
        const auto forward = facing * glm::vec3(0, 0, 1);
        const auto position = [](const RagdollPose& pose, Part part) {
            return glm::vec3(pose.bones[part][3]);
        };
        auto groundDef = b3DefaultBodyDef();
        groundDef.type = b3_staticBody;
        groundDef.position = toB3Pos({feet.x, feet.y - 0.05f, feet.z});
        ground_ = b3CreateBody(world_, &groundDef);
        auto groundShape = b3DefaultShapeDef();
        groundShape.filter.categoryBits = ragdollFloorCategory;
        groundShape.filter.groupIndex = -collisionGroup;
        groundShape.enableCustomFiltering = true;
        const auto groundBox = b3MakeBoxHull(3.0f, 0.05f, 3.0f);
        b3CreateHullShape(ground_, &groundShape, &groundBox.base);

        const float hipWidth = glm::distance(position(reference, upperLegLeft), position(reference, upperLegRight));
        const float shoulderWidth = glm::distance(position(reference, upperArmLeft), position(reference, upperArmRight));
        const std::array<float, partCount> masses{12, 25, 5, 2.2f, 1.6f, 2.2f, 1.6f, 7.5f, 4, 7.5f, 4};
        const std::array<float, partCount> radiusRatios{0.4f, 0.25f, 0.5f, 0.18f, 0.16f,
                                                      0.18f, 0.16f, 0.19f, 0.16f, 0.19f, 0.16f};
        for (int index = 0; index < partCount; ++index) {
            const auto part = static_cast<Part>(index);
            auto begin = position(reference, part);
            auto end = reference.tips[part];
            if (part == pelvis) {
                begin = position(reference, upperLegLeft);
                end = position(reference, upperLegRight);
            }
            const auto axis = end - begin;
            const float length = glm::length(axis);
            const auto rotation = length > 1e-5f ? frameFromY(axis, forward) : frameFromY({0, 1, 0}, forward);
            referenceBodies_[part] = glm::translate(glm::mat4(1), (begin + end) * 0.5f) * glm::mat4_cast(rotation);
            // Uniform source scale cancels in this delta. Normalize the resulting
            // axes because interpolated animation matrices can contain small shear.
            const auto live = current.bones[part] * glm::inverse(reference.bones[part]) * referenceBodies_[part];
            const auto liveRotation = rigidRotation(live);
            const auto center = glm::vec3(live[3]);
            float radius = std::max(0.005f, length * radiusRatios[part]);
            if (part == torso) radius = std::max(0.005f, shoulderWidth * 0.34f);
            if (part == pelvis) radius = std::max(0.005f, hipWidth * 0.4f);
            radius = std::min(radius, std::max(0.005f, length * 0.5f));
            float halfLength = part == head ? 0.0f : std::max(0.0f, length * 0.5f - radius);

            // Capsule endpoints are inset from the anatomical joints. A radius
            // added beyond the foot used to start the calf below ground and the
            // solver lifted the entire skeleton to resolve that penetration.
            // Also trim a collider if an unusually low animation pose needs it;
            // never move the visible pose upward just to fit a collision shape.
            const float clearance = std::max(0.001f, center.y - feet.y);
            radius = std::min(radius, clearance);
            const float verticalAxis = std::abs((liveRotation * glm::vec3(0, 1, 0)).y);
            if (verticalAxis > 1e-5f)
                halfLength = std::min(halfLength, std::max(0.0f, (clearance - radius) / verticalAxis));
            bodies_[part] = addBody(center, liveRotation, radius, halfLength, masses[part], collisionGroup);
        }

        // Joint rotation frames come from the reference anatomy. Positional
        // anchors use the live joint, since intermediate spine/clavicle bones
        // need not keep their reference offsets when the animation bends them.
        const auto ball = [&](Part parent, Part child, float cone, float twist) {
            auto axis = reference.tips[child] - position(reference, child);
            if (glm::dot(axis, axis) < 1e-8f) axis = glm::vec3(0, 1, 0);
            addBallJoint(parent, child, position(current, child), frameFromZ(axis, forward), cone, -twist, twist);
        };
        const auto hinge = [&](Part parent, Part child, glm::vec3 bendDirection, float lower, float upper) {
            auto direction = glm::normalize(reference.tips[parent] - position(reference, parent));
            auto bend = bendDirection - direction * glm::dot(bendDirection, direction);
            if (glm::dot(bend, bend) < 1e-6f) bend = safePerpendicular(direction);
            const auto frame = frameFromZ(glm::normalize(glm::cross(direction, glm::normalize(bend))), direction);
            addHingeJoint(parent, child, position(current, child), frame, lower, upper);
        };
        ball(pelvis, torso, 40, 35);
        ball(torso, head, 55, 70);
        ball(torso, upperArmLeft, 120, 95);
        ball(torso, upperArmRight, 120, 95);
        hinge(upperArmLeft, lowerArmLeft, forward, -20, 160);
        hinge(upperArmRight, lowerArmRight, forward, -20, 160);
        ball(pelvis, upperLegLeft, 70, 45);
        ball(pelvis, upperLegRight, 70, 45);
        hinge(upperLegLeft, lowerLegLeft, -forward, -15, 165);
        hinge(upperLegRight, lowerLegRight, -forward, -15, 165);

        for (int part = 0; part < partCount; ++part)
            restBodies_[static_cast<std::size_t>(part)] = bodyTransform(static_cast<Part>(part));
        initialPelvis_ = glm::vec3(restBodies_[pelvis][3]);
        b3Body_SetLinearVelocity(bodies_[pelvis], toB3Vec(impulse * 0.7f));
    }

    ~Ragdoll() {
        for (const auto body : bodies_) {
            simulation_.untrackBodyMotion(body);
            if (b3Body_IsValid(body)) b3DestroyBody(body);
        }
        if (b3Body_IsValid(ground_)) b3DestroyBody(ground_);
    }

    // Register only after construction, when this owner's address is stable.
    void trackMotion() {
        for (const auto body : bodies_) simulation_.trackBodyMotion(body, revision_);
    }
    std::uint64_t revision() const { return revision_; }
    bool sleeping() const {
        for (const auto body : bodies_) if (b3Body_IsAwake(body)) return false;
        return true;
    }
    void translate(glm::vec3 displacement) {
        for (const auto body : bodies_) {
            b3Body_SetTransform(body, toB3Pos(fromB3(b3Body_GetPosition(body)) + displacement), b3Body_GetRotation(body));
            b3Body_SetAwake(body, true);
        }
        b3Body_SetTransform(ground_, toB3Pos(fromB3(b3Body_GetPosition(ground_)) + displacement), b3Body_GetRotation(ground_));
        ++revision_;
    }
    void applyImpulse(Part part, glm::vec3 impulse) {
        b3Body_ApplyLinearImpulseToCenter(bodies_[part], toB3Vec(impulse), true);
        ++revision_;
    }

    void addLinearVelocity(Part part, glm::vec3 velocity) {
        const auto body = bodies_[part];
        const auto current = b3Body_GetLinearVelocity(body);
        b3Body_SetLinearVelocity(body, {current.x + velocity.x, current.y + velocity.y, current.z + velocity.z});
        b3Body_SetAwake(body, true);
        ++revision_;
    }

    void addAngularVelocity(Part part, glm::vec3 velocity) {
        const auto body = bodies_[part];
        const auto current = b3Body_GetAngularVelocity(body);
        b3Body_SetAngularVelocity(body, {current.x + velocity.x, current.y + velocity.y, current.z + velocity.z});
        b3Body_SetAwake(body, true);
        ++revision_;
    }

    void sever(Part child) {
        if (child <= pelvis || child >= partCount) throw std::invalid_argument("Invalid severed ragdoll joint");
        auto& joint = joints_[child];
        if (b3Joint_IsValid(joint)) {
            b3DestroyJoint(joint, true);
            joint = b3_nullJointId;
            // Light pieces can leave the local floor before sliding/rolling
            // friction stops them. Drag limits travel without changing launch
            // impulse or adding velocity to the whole detached assembly.
            b3Body_SetLinearDamping(bodies_[child], 1.5f);
            ++revision_;
        }
        for (const auto body : bodies_) b3Body_SetAwake(body, true);
    }


    glm::mat4 rootTransform() const {
        const auto position = fromB3(b3Body_GetPosition(bodies_[pelvis]));
        // Preserve the exact instance frame at handoff; the skinning matrices
        // carry every body rotation. No assumed pelvis height or yaw is needed.
        return glm::translate(glm::mat4(1), feet_ + position - initialPelvis_) *
               glm::rotate(glm::mat4(1), yaw_, glm::vec3(0, 1, 0));
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
    static glm::quat rigidRotation(const glm::mat4& matrix) {
        auto x = glm::normalize(glm::vec3(matrix[0]));
        auto y = glm::vec3(matrix[1]) - x * glm::dot(x, glm::vec3(matrix[1]));
        if (glm::dot(y, y) < 1e-8f) y = safePerpendicular(x);
        y = glm::normalize(y);
        return glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));
    }

    b3Transform localJointFrame(Part part, glm::vec3 pivot, glm::quat referenceRotation) const {
        b3Transform result = b3Transform_identity;
        result.p = b3Body_GetLocalPoint(bodies_[part], toB3Pos(pivot));
        result.q = b3InvMulQuat(toB3(rigidRotation(referenceBodies_[part])), toB3(referenceRotation));
        return result;
    }

    b3BodyId addBody(glm::vec3 center, glm::quat rotation, float radius, float halfLength,
                     float mass, int collisionGroup) {
        auto bodyDef = b3DefaultBodyDef();
        bodyDef.type = b3_dynamicBody;
        bodyDef.position = toB3Pos(center);
        bodyDef.rotation = toB3(rotation);
        bodyDef.linearDamping = 0.025f;
        bodyDef.angularDamping = 0.045f;
        const auto body = b3CreateBody(world_, &bodyDef);
        auto shapeDef = b3DefaultShapeDef();
        shapeDef.filter.categoryBits = ordinaryBodyCategory;
        shapeDef.density = mass / (pi * radius * radius * (2 * halfLength) + (4.0f / 3.0f) * pi * radius * radius * radius);
        shapeDef.baseMaterial.friction = 0.6f;
        // Sliding friction alone cannot stop spheres/capsules rolling. Apply
        // contact resistance so detached pieces settle without damping air spin.
        shapeDef.baseMaterial.rollingResistance = 0.2f;
        shapeDef.filter.groupIndex = collisionGroup;
        if (halfLength > 1e-5f) {
            const b3Capsule capsule{{0, -halfLength, 0}, {0, halfLength, 0}, radius};
            b3CreateCapsuleShape(body, &shapeDef, &capsule);
        } else {
            const b3Sphere sphere{{0, 0, 0}, radius};
            b3CreateSphereShape(body, &shapeDef, &sphere);
        }
        return body;
    }

    void addBallJoint(Part parent, Part child, glm::vec3 pivot, glm::quat referenceFrame,
                      float cone, float lowerTwist, float upperTwist) {
        auto joint = b3DefaultSphericalJointDef();
        joint.base.bodyIdA = bodies_[parent];
        joint.base.bodyIdB = bodies_[child];
        joint.base.localFrameA = localJointFrame(parent, pivot, referenceFrame);
        joint.base.localFrameB = localJointFrame(child, pivot, referenceFrame);
        joint.base.collideConnected = false;
        joint.enableConeLimit = true;
        joint.coneAngle = cone * B3_DEG_TO_RAD;
        joint.enableTwistLimit = true;
        joint.lowerTwistAngle = lowerTwist * B3_DEG_TO_RAD;
        joint.upperTwistAngle = upperTwist * B3_DEG_TO_RAD;
        joints_[child] = b3CreateSphericalJoint(world_, &joint);
    }

    void addHingeJoint(Part parent, Part child, glm::vec3 pivot, glm::quat referenceFrame,
                       float lower, float upper) {
        auto joint = b3DefaultRevoluteJointDef();
        joint.base.bodyIdA = bodies_[parent];
        joint.base.bodyIdB = bodies_[child];
        joint.base.localFrameA = localJointFrame(parent, pivot, referenceFrame);
        joint.base.localFrameB = localJointFrame(child, pivot, referenceFrame);
        joint.base.collideConnected = false;
        joint.enableLimit = true;
        joint.lowerAngle = lower * B3_DEG_TO_RAD;
        joint.upperAngle = upper * B3_DEG_TO_RAD;
        joints_[child] = b3CreateRevoluteJoint(world_, &joint);
    }

    PhysicsWorld& simulation_;
    std::uint64_t revision_ = 1;
    b3WorldId world_ = b3_nullWorldId;
    b3BodyId ground_{};
    std::array<b3BodyId, partCount> bodies_{};
    std::array<b3JointId, partCount> joints_{};
    std::array<glm::mat4, partCount> restBodies_{};
    std::array<glm::mat4, partCount> referenceBodies_{};
    glm::vec3 feet_{0};
    glm::vec3 initialPelvis_{0};
    float yaw_ = 0;
};

} // namespace

struct ZombieRagdollWorld::Impl {
    struct Entry {
        std::unique_ptr<Ragdoll> ragdoll;
    };

    // Declared first so entries destroy their bodies before releasing the world.
    std::shared_ptr<PhysicsWorld> simulation;
    std::vector<Entry> entries;
};

ZombieRagdollWorld::ZombieRagdollWorld() : ZombieRagdollWorld(std::make_shared<PhysicsWorld>()) {}
ZombieRagdollWorld::ZombieRagdollWorld(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Ragdolls require a physics world");
    impl_->simulation = std::move(world);
}
ZombieRagdollWorld::~ZombieRagdollWorld() = default;
ZombieRagdollWorld::ZombieRagdollWorld(ZombieRagdollWorld&&) noexcept = default;
ZombieRagdollWorld& ZombieRagdollWorld::operator=(ZombieRagdollWorld&&) noexcept = default;

std::size_t ZombieRagdollWorld::create(glm::vec3 feet, float yaw, glm::vec3 impulse) {
    RagdollPose pose;
    const auto root = glm::translate(glm::mat4(1), feet) * glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0));
    const std::array<glm::vec3, partCount> joints{{
        {0, 0.95f, 0}, {0, 1.05f, 0}, {0, 1.62f, 0},
        {0.2f, 1.5f, 0}, {0.48f, 1.5f, 0}, {-0.2f, 1.5f, 0}, {-0.48f, 1.5f, 0},
        {0.1f, 0.95f, 0}, {0.12f, 0.55f, 0}, {-0.1f, 0.95f, 0}, {-0.12f, 0.55f, 0}
    }};
    const std::array<glm::vec3, partCount> tips{{
        {0, 1.05f, 0}, {0, 1.58f, 0}, {0, 1.8f, 0},
        {0.48f, 1.5f, 0}, {0.75f, 1.5f, 0}, {-0.48f, 1.5f, 0}, {-0.75f, 1.5f, 0},
        {0.12f, 0.55f, 0}, {0.14f, 0.1f, 0}, {-0.12f, 0.55f, 0}, {-0.14f, 0.1f, 0}
    }};
    for (int part = 0; part < partCount; ++part) {
        pose.bones[part] = root * glm::translate(glm::mat4(1), joints[part]);
        pose.tips[part] = glm::vec3(root * glm::vec4(tips[part], 1));
    }
    return create(feet, yaw, impulse, pose, pose);
}

std::size_t ZombieRagdollWorld::create(glm::vec3 feet, float yaw, glm::vec3 impulse,
                                      const RagdollPose& referencePose, const RagdollPose& currentPose) {
    const auto finite = [](glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!impl_ || !finite(feet) || !std::isfinite(yaw) || !finite(impulse))
        throw std::invalid_argument("Ragdoll creation requires finite transforms and impulse");
    for (const auto* pose : {&referencePose, &currentPose}) {
        for (std::size_t part = 0; part < ragdollPartCount; ++part) {
            if (!finite(pose->tips[part])) throw std::invalid_argument("Ragdoll endpoints must be finite");
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(pose->bones[part][column][row]))
                        throw std::invalid_argument("Ragdoll bones must be finite");
            const float determinant = glm::determinant(pose->bones[part]);
            if (!std::isfinite(determinant) || determinant == 0)
                throw std::invalid_argument("Ragdoll bones must have invertible transforms");
        }
    }
    // Validate dimensions before creating any Box3D resources. In particular,
    // hinge construction needs a nonzero anatomical segment direction.
    for (std::size_t part = torso; part < ragdollPartCount; ++part) {
        const float length = glm::distance(glm::vec3(referencePose.bones[part][3]), referencePose.tips[part]);
        if (!std::isfinite(length) || length < 1e-5f)
            throw std::invalid_argument("Ragdoll reference segments must have positive length");
    }
    const float hipWidth = glm::distance(glm::vec3(referencePose.bones[upperLegLeft][3]),
                                        glm::vec3(referencePose.bones[upperLegRight][3]));
    if (!std::isfinite(hipWidth) || hipWidth < 1e-5f)
        throw std::invalid_argument("Ragdoll reference hips must be separated");
    const auto group = impl_->simulation->allocateCollisionGroup();
    auto ragdoll = std::make_unique<Ragdoll>(*impl_->simulation, feet, yaw, impulse, group, referencePose, currentPose);
    ragdoll->trackMotion();
    impl_->entries.push_back({std::move(ragdoll)});
    return impl_->entries.size() - 1;
}

void ZombieRagdollWorld::step(float seconds) {
    if (impl_) impl_->simulation->step(seconds);
}

bool ZombieRagdollWorld::valid(std::size_t handle) const {
    return impl_ && handle < impl_->entries.size() && impl_->entries[handle].ragdoll;
}
std::uint64_t ZombieRagdollWorld::poseRevision(std::size_t handle) const {
    return valid(handle) ? impl_->entries[handle].ragdoll->revision() : 0;
}
bool ZombieRagdollWorld::sleeping(std::size_t handle) const {
    return valid(handle) && impl_->entries[handle].ragdoll->sleeping();
}
void ZombieRagdollWorld::translate(std::size_t handle, glm::vec3 displacement) {
    if (!valid(handle)) throw std::out_of_range("Invalid ragdoll handle");
    if (!std::isfinite(displacement.x) || !std::isfinite(displacement.y) || !std::isfinite(displacement.z))
        throw std::invalid_argument("Ragdoll translation must be finite");
    impl_->entries[handle].ragdoll->translate(displacement);
}
void ZombieRagdollWorld::applyImpulse(std::size_t handle, RagdollPart part, glm::vec3 impulse) {
    if (!valid(handle)) throw std::out_of_range("Invalid ragdoll handle");
    if (static_cast<std::size_t>(part) >= ragdollPartCount || !std::isfinite(impulse.x) ||
        !std::isfinite(impulse.y) || !std::isfinite(impulse.z))
        throw std::invalid_argument("Ragdoll impulse requires a valid part and finite vector");
    impl_->entries[handle].ragdoll->applyImpulse(static_cast<Part>(part), impulse);
}
void ZombieRagdollWorld::sever(std::size_t handle, RagdollPart child) {
    if (!valid(handle)) throw std::out_of_range("Invalid ragdoll handle");
    if (child == RagdollPart::pelvis || std::size_t(child) >= ragdollPartCount)
        throw std::invalid_argument("Invalid severed ragdoll joint");
    impl_->entries[handle].ragdoll->sever(static_cast<Part>(child));
}
void ZombieRagdollWorld::addAngularVelocity(std::size_t handle, RagdollPart part, glm::vec3 velocity) {
    if (!valid(handle)) throw std::out_of_range("Invalid ragdoll handle");
    if (std::size_t(part) >= ragdollPartCount || !std::isfinite(velocity.x) ||
        !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
        throw std::invalid_argument("Angular velocity requires a valid part and finite vector");
    impl_->entries[handle].ragdoll->addAngularVelocity(static_cast<Part>(part), velocity);
}
void ZombieRagdollWorld::addLinearVelocity(std::size_t handle, RagdollPart part, glm::vec3 velocity) {
    if (!valid(handle)) throw std::out_of_range("Invalid ragdoll handle");
    if (std::size_t(part) >= ragdollPartCount || !std::isfinite(velocity.x) ||
        !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
        throw std::invalid_argument("Linear velocity requires a valid part and finite vector");
    impl_->entries[handle].ragdoll->addLinearVelocity(static_cast<Part>(part), velocity);
}
void ZombieRagdollWorld::destroy(std::size_t handle) {
    if (valid(handle)) impl_->entries[handle].ragdoll.reset();
}


glm::mat4 ZombieRagdollWorld::rootTransform(std::size_t handle) const {
    if (!valid(handle)) return glm::mat4(1.0f);
    return impl_->entries[handle].ragdoll->rootTransform();
}

std::array<glm::mat4, ragdollPartCount> ZombieRagdollWorld::bodyTransforms(std::size_t handle) const {
    if (!valid(handle)) return {};
    return impl_->entries[handle].ragdoll->bodyTransforms();
}

std::array<glm::mat4, ragdollPartCount> ZombieRagdollWorld::restBodyTransforms(std::size_t handle) const {
    if (!valid(handle)) return {};
    return impl_->entries[handle].ragdoll->restBodyTransforms();
}

std::size_t ZombieRagdollWorld::count() const {
    if (!impl_) return 0;
    return static_cast<std::size_t>(std::count_if(impl_->entries.begin(), impl_->entries.end(),
        [](const auto& entry) { return bool(entry.ragdoll); }));
}

} // namespace viewer
