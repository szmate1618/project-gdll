#include "nail_placement.hpp"
#include "camera.hpp"
#include "ground_items.hpp"
#include "object_interaction.hpp"
#include "physics_world.hpp"
#include "prop_collider.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 value) { return {value.x, value.y, value.z}; }
template<class V> glm::vec3 vector(V value) { return {float(value.x), float(value.y), float(value.z)}; }
b3Quat rotation(glm::quat value) { return {{value.x, value.y, value.z}, value.w}; }
glm::quat rotation(b3Quat value) { return {value.s, value.v.x, value.v.y, value.v.z}; }
bool finite(glm::quat value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

glm::quat aligned(glm::vec3 normal, glm::quat previous) {
    auto right = previous * glm::vec3(1, 0, 0);
    right -= normal * glm::dot(normal, right);
    if (glm::length(right) < .01f) {
        right = std::abs(normal.x) < .9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        right -= normal * glm::dot(normal, right);
    }
    right = glm::normalize(right);
    return glm::normalize(glm::quat_cast(glm::mat3(right, normal, glm::cross(right, normal))));
}

struct SurfaceRay {
    b3BodyId excluded{};
    b3BodyId body{};
    glm::vec3 point{0}, normal{0};
    float fraction = 1;
    std::uint64_t material = 0;
    bool hit = false;
};
float surfaceHit(b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction,
    std::uint64_t material, int, int, void* context) {
    auto& ray = *static_cast<SurfaceRay*>(context);
    const auto body = b3Shape_GetBody(shape);
    if (B3_ID_EQUALS(body, ray.excluded) || b3Shape_IsSensor(shape)) return -1;
    if (!ray.hit || fraction < ray.fraction) {
        ray.body = body; ray.point = vector(point); ray.normal = vector(normal);
        ray.fraction = fraction; ray.material = material; ray.hit = true;
    }
    return ray.fraction;
}

struct Intersections {
    b3BodyId excluded{};
    std::map<std::uint64_t, b3BodyId> bodies;
};
bool intersectedBody(b3ShapeId shape, void* context) {
    auto& intersections = *static_cast<Intersections*>(context);
    const auto body = b3Shape_GetBody(shape);
    if (!B3_ID_EQUALS(body, intersections.excluded) && !b3Shape_IsSensor(shape))
        intersections.bodies.emplace(b3StoreBodyId(body), body);
    return true;
}

// Each authored part is convex. Query its actual eight corners, preserving thin
// cardboard intersecting the head without treating the preview sphere as steel.
std::array<b3Vec3, 8> partPoints(const ItemModelPart& part, glm::quat q) {
    std::array<b3Vec3, 8> result;
    for (int level = 0; level < 2; ++level) {
        const auto footprint = level && part.shape == ItemModelShape::Frustum ? part.topSize : glm::vec2(part.size.x, part.size.z);
        for (int corner = 0; corner < 4; ++corner) {
            const glm::vec3 point((corner & 1 ? .5f : -.5f) * footprint.x,
                (level ? .5f : -.5f) * part.size.y, (corner & 2 ? .5f : -.5f) * footprint.y);
            result[static_cast<std::size_t>(level * 4 + corner)] = vector(q * (part.center + part.rotation*point));
        }
    }
    return result;
}
}

struct NailPlacement::Impl {
    struct Entry {
        PhysicsProp prop;
        b3BodyId body{}, target{};
        glm::vec3 localAnchor{0};
        glm::quat relativeRotation{1, 0, 0, 0};
        bool driven = false;
        std::vector<b3JointId> joints;
        Entry(PhysicsProp nail, b3BodyId targetBody) : prop(std::move(nail)), body(prop.body()), target(targetBody) {}
    };
    GroundItems& items;
    std::shared_ptr<PhysicsWorld> world;
    std::map<std::uint64_t, Entry> entries;

    Impl(GroundItems& ground, std::shared_ptr<PhysicsWorld> simulation) : items(ground), world(std::move(simulation)) {
        if (!world) throw std::invalid_argument("Nail placement requires a physics world");
        world->registerStepCallbacks(this, [this] { update(); }, [this] { update(); });
        try { world->registerGrabCallback(this, [this](b3BodyId body) { detach(body); }); }
        catch (...) { world->unregisterStepCallbacks(this); throw; }
    }
    ~Impl() {
        world->unregisterGrabCallback(this);
        world->unregisterStepCallbacks(this);
        while (!entries.empty()) detach(entries.begin()->second.body);
    }
    static std::size_t jointCount(const Entry& entry) {
        return static_cast<std::size_t>(std::count_if(entry.joints.begin(), entry.joints.end(),
            [](b3JointId joint) { return b3Joint_IsValid(joint); }));
    }
    static bool pinned(const Entry& entry) {
        return entry.prop.valid() && (entry.driven ? jointCount(entry) > 0 : b3Body_IsValid(entry.target));
    }
    Entry* find(const PhysicsProp& prop) {
        if (!prop.valid() || prop.world_ != world) return nullptr;
        const auto found = entries.find(b3StoreBodyId(prop.body()));
        return found == entries.end() ? nullptr : &found->second;
    }
    void detach(b3BodyId body) {
        const auto found = entries.find(b3StoreBodyId(body));
        if (found == entries.end()) return;
        auto& entry = found->second;
        for (const auto joint : entry.joints) if (b3Joint_IsValid(joint)) b3DestroyJoint(joint, true);
        world->resumeCraftingProp(entry.body);
        if (entry.prop.valid()) items.setNailPreviewSphere(entry.prop, false);
        entries.erase(found);
    }
    void follow(Entry& entry) {
        // The preview owns its suspension independently of whichever hand most
        // recently selected it. Cancelling that hand cannot resume this body.
        world->parkCraftingProp(entry.body);
        const auto point = vector(b3Body_GetWorldPoint(entry.target, vector(entry.localAnchor)));
        const auto q = glm::normalize(rotation(b3Body_GetRotation(entry.target)) * entry.relativeRotation);
        const auto center = point - q * NailPlacement::localTip();
        b3Body_SetTransform(entry.body, vector(center), rotation(q));
        b3Body_SetLinearVelocity(entry.body, b3Vec3_zero);
        b3Body_SetAngularVelocity(entry.body, b3Vec3_zero);
    }
    void update() {
        for (auto it = entries.begin(); it != entries.end();) {
            auto& entry = it->second;
            if (!pinned(entry)) {
                const auto body = entry.body;
                ++it;
                detach(body);
            } else {
                if (!entry.driven) follow(entry);
                ++it;
            }
        }
    }
};

NailPlacement::NailPlacement(GroundItems& items, std::shared_ptr<PhysicsWorld> world)
    : impl_(std::make_unique<Impl>(items, std::move(world))) {}
NailPlacement::~NailPlacement() = default;
bool NailPlacement::isNail(const PhysicsProp& nail) const {
    const auto* item = nail.valid() ? impl_->items.find(nail) : nullptr;
    return item && item->item.hasTag("nail");
}
bool NailPlacement::snap(const PhysicsProp& nail, const Camera& camera) {
    if (!isNail(nail)) return false;
    if (const auto* entry = impl_->find(nail)) return !entry->driven && Impl::pinned(*entry);
    SurfaceRay ray;
    ray.excluded = nail.body();
    const auto direction = camera.direction();
    const auto origin = camera.position();
    auto filter = b3DefaultQueryFilter();
    filter.maskBits &= ~ragdollFloorCategory;
    b3World_CastRay(impl_->world->id(), vector(origin), vector(direction * ObjectInteraction::rayReach(direction)), filter, surfaceHit, &ray);
    if (!ray.hit || !nailableMaterial(ray.material) || glm::length(ray.normal) < .1f) return false;
    const auto q = aligned(glm::normalize(ray.normal), rotation(b3Body_GetRotation(nail.body())));
    auto [found, inserted] = impl_->entries.emplace(b3StoreBodyId(nail.body()), Impl::Entry(nail, ray.body));
    (void)inserted;
    auto& entry = found->second;
    entry.localAnchor = vector(b3Body_GetLocalPoint(ray.body, vector(ray.point)));
    entry.relativeRotation = glm::conjugate(rotation(b3Body_GetRotation(ray.body))) * q;
    try {
        impl_->items.setNailPreviewSphere(nail, true);
        impl_->world->parkCraftingProp(nail.body());
        impl_->follow(entry);
    } catch (...) { impl_->detach(nail.body()); throw; }
    return true;
}
bool NailPlacement::pinned(const PhysicsProp& nail) const {
    const auto* entry = impl_->find(nail);
    return entry && Impl::pinned(*entry);
}
bool NailPlacement::driven(const PhysicsProp& nail) const {
    const auto* entry = impl_->find(nail);
    return entry && entry->driven && Impl::pinned(*entry);
}
glm::vec3 NailPlacement::tip(const PhysicsProp& nail) const { return nail.pointState(localTip()).point; }
void NailPlacement::rotate(const PhysicsProp& nail, glm::quat q) {
    if (!finite(q) || glm::length(q) < .001f) throw std::invalid_argument("Nail rotation must be finite and nonzero");
    auto* entry = impl_->find(nail);
    if (!entry || entry->driven || !Impl::pinned(*entry)) return;
    entry->relativeRotation = glm::conjugate(rotation(b3Body_GetRotation(entry->target))) * glm::normalize(q);
    impl_->follow(*entry);
}
bool NailPlacement::drive(const PhysicsProp& nail) {
    auto* entry = impl_->find(nail);
    if (!entry || entry->driven || !Impl::pinned(*entry)) return false;
    const auto* item = impl_->items.find(nail);
    if (!item || item->item.inventory().model.parts.empty()) return false;
    impl_->follow(*entry);
    impl_->world->resumeCraftingProp(entry->body);
    if (impl_->world->isCraftingHeld(entry->body)) return false; // The hand must finish its active pose grip first.
    const auto q = rotation(b3Body_GetRotation(entry->body));
    const auto center = nail.center() + q * glm::vec3(0, -driveDistance, 0);
    b3Body_SetTransform(entry->body, vector(center), rotation(q));
    b3Body_SetLinearVelocity(entry->body, b3Vec3_zero);
    b3Body_SetAngularVelocity(entry->body, b3Vec3_zero);
    Intersections intersections;
    intersections.excluded = entry->body;
    auto filter = b3DefaultQueryFilter();
    filter.maskBits &= ~ragdollFloorCategory;
    for (const auto& part : item->item.inventory().model.parts) {
        const auto points = partPoints(part, q);
        const b3ShapeProxy proxy{points.data(), static_cast<int>(points.size()), 0};
        b3World_OverlapShape(impl_->world->id(), vector(center), &proxy, filter, intersectedBody, &intersections);
    }
    // Each attachment rotates around the nail's length axis. All anchors share
    // a world frame, so connection creation cannot pull penetrated objects.
    const auto axis = q * glm::vec3(0, -1, 0);
    const auto right = q * glm::vec3(1, 0, 0);
    const auto frame = glm::normalize(glm::quat_cast(glm::mat3(right, glm::cross(axis, right), axis)));
    entry->joints.reserve(intersections.bodies.size());
    for (const auto& [key, target] : intersections.bodies) {
        (void)key;
        auto joint = b3DefaultRevoluteJointDef();
        joint.base.bodyIdA = target;
        joint.base.bodyIdB = entry->body;
        joint.base.localFrameA.p = b3Body_GetLocalPoint(target, vector(center));
        joint.base.localFrameB.p = b3Vec3_zero;
        joint.base.localFrameA.q = rotation(glm::conjugate(rotation(b3Body_GetRotation(target))) * frame);
        joint.base.localFrameB.q = rotation(glm::conjugate(q) * frame);
        joint.base.collideConnected = false;
        const auto id = b3CreateRevoluteJoint(impl_->world->id(), &joint);
        if (!b3Joint_IsValid(id)) throw std::runtime_error("Cannot create nail hinge");
        entry->joints.push_back(id);
    }
    entry->driven = true;
    return true;
}
void NailPlacement::detach(const PhysicsProp& nail) { if (nail.valid()) impl_->detach(nail.body()); }
std::size_t NailPlacement::jointCount(const PhysicsProp& nail) const {
    const auto* entry = impl_->find(nail);
    return entry ? Impl::jointCount(*entry) : 0;
}
}
