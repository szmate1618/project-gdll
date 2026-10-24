#include "box_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 p) { return {p.x, p.y, p.z}; }
glm::vec3 vector(b3Vec3 p) { return {p.x, p.y, p.z}; }
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

} // namespace

std::vector<Contact> boxContacts(const BoxPose& box, glm::vec3 feet, float radius, float height, float skin) {
    const auto inverse = glm::conjugate(box.rotation);
    const b3Capsule capsule{vector(inverse * (feet + glm::vec3(0, radius, 0) - box.center)),
                            vector(inverse * (feet + glm::vec3(0, height - radius, 0) - box.center)), radius + skin};
    const auto h = box.halfExtents;
    const auto hull = b3MakeBoxHull(h.x, h.y, h.z);
    std::array<b3LocalManifoldPoint, 8> points{};
    b3LocalManifold manifold{};
    manifold.points = points.data();
    b3SimplexCache cache{};
    b3CollideHullAndCapsule(&manifold, static_cast<int>(points.size()), &hull.base, &capsule, b3Transform_identity, &cache);
    std::vector<Contact> contacts;
    for (int i = 0; i < manifold.pointCount; ++i)
        if (points[i].separation < 0)
            contacts.push_back({box.rotation * vector(manifold.normal),
                                box.center + box.rotation * vector(points[i].point), -points[i].separation});
    return contacts;
}

glm::mat4 BoxPose::transform() const {
    return glm::translate(glm::mat4(1), center) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1), halfExtents * 2.0f);
}

struct BoxLayer::Impl {
    struct Entry { Impl* owner; b3BodyId body; glm::vec3 halfExtents; std::size_t index; glm::vec2 damping; };
    std::shared_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<Entry>> entries;
    std::uint64_t revision = 1;
    std::uint64_t categories = 0;
    ~Impl() {
        for (const auto& e : entries) {
            world->untrackBodyMotion(e->body);
            b3DestroyBody(e->body);
        }
    }
};

BoxLayer::BoxLayer(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Boxes require a physics world");
    impl_->world = std::move(world);
}
BoxLayer::~BoxLayer() = default;
std::size_t BoxLayer::count() const { return impl_->entries.size(); }
std::uint64_t BoxLayer::revision() const { return impl_->revision; }
void BoxLayer::wake() { for (const auto& entry : impl_->entries) b3Body_SetAwake(entry->body,true); }
std::shared_ptr<PhysicsWorld> BoxLayer::physicsWorld() const { return impl_->world; }
PhysicsProp BoxLayer::prop(std::size_t index) const { return {impl_->world,impl_->entries.at(index)->body}; }
std::optional<std::size_t> BoxLayer::indexOf(const PhysicsProp& prop) const {
    if (prop.valid())
        for (const auto& entry : impl_->entries)
            if (prop == PhysicsProp(impl_->world,entry->body)) return entry->index;
    return std::nullopt;
}
std::size_t BoxLayer::add(glm::vec3 center, glm::vec3 size, const BoxSpawnMotion& motion) {
    if (!finite(center) || !finite(size) || glm::any(glm::lessThan(size, glm::vec3(.01f))) ||
        glm::any(glm::greaterThan(size, glm::vec3(4))) || !finite(motion.velocity) || !finite(motion.angularVelocity) ||
        !std::isfinite(glm::length(motion.rotation)) || std::abs(glm::length(motion.rotation)-1)>.001f ||
        !std::isfinite(motion.mass) || motion.mass<0 || !std::isfinite(motion.friction) || motion.friction<0 ||
        !std::isfinite(motion.linearDamping) || motion.linearDamping<0 ||
        !std::isfinite(motion.angularDamping) || motion.angularDamping<0)
        throw std::invalid_argument("Invalid box dimensions, position or motion");
    const auto index = count();
    if (index==impl_->entries.capacity()) impl_->entries.reserve(std::max(std::size_t(8),index*2));
    auto entry = std::make_unique<Impl::Entry>(Impl::Entry{impl_.get(), {}, size*.5f, index,
                                                        {motion.linearDamping,motion.angularDamping}});
    auto definition = b3DefaultBodyDef();
    definition.type = b3_dynamicBody;
    definition.position = {center.x, center.y, center.z};
    definition.rotation = {{motion.rotation.x,motion.rotation.y,motion.rotation.z},motion.rotation.w};
    definition.linearVelocity = vector(motion.velocity);
    definition.angularVelocity = vector(motion.angularVelocity);
    definition.isBullet = motion.continuous;
    definition.linearDamping = motion.linearDamping;
    definition.angularDamping = motion.angularDamping;
    const auto body = b3CreateBody(impl_->world->id(), &definition);
    entry->body = body;
    const auto half = entry->halfExtents;
    const auto hull = b3MakeBoxHull(half.x, half.y, half.z);
    auto shape = b3DefaultShapeDef();
    // Hollow crates/cabinets: 8–55 kg, rather than solid timber density.
    const float volume = size.x*size.y*size.z;
    shape.density = (motion.mass>0 ? motion.mass : glm::clamp(volume*70.0f,8.0f,55.0f))/volume;
    shape.baseMaterial.friction = motion.friction;
    shape.filter = motion.filter;
    shape.userData = entry.get();
    b3CreateHullShape(body, &shape, &hull.base);
    impl_->entries.push_back(std::move(entry));
    impl_->categories |= motion.filter.categoryBits;
    impl_->world->trackBodyMotion(body, impl_->revision);
    ++impl_->revision;
    return index;
}
void BoxLayer::remove(std::size_t index) {
    const auto body = impl_->entries.at(index)->body;
    impl_->world->untrackBodyMotion(body);
    b3DestroyBody(body);
    if (index+1<count()) {
        impl_->entries[index] = std::move(impl_->entries.back());
        impl_->entries[index]->index = index;
    }
    impl_->entries.pop_back();
    ++impl_->revision;
}
BoxPose BoxLayer::pose(std::size_t index) const {
    const auto& e = *impl_->entries.at(index);
    const auto p = b3Body_GetPosition(e.body);
    const auto q = b3Body_GetRotation(e.body);
    return {index, {p.x, p.y, p.z}, e.halfExtents, {q.s, q.v.x, q.v.y, q.v.z}};
}
void BoxLayer::setPose(std::size_t index, glm::vec3 center, glm::quat q) {
    if (!finite(center) || !std::isfinite(glm::length(q)) || std::abs(glm::length(q) - 1) > .001f)
        throw std::invalid_argument("Invalid replicated box pose");
    b3Body_SetTransform(impl_->entries.at(index)->body, {center.x, center.y, center.z}, {{q.x, q.y, q.z}, q.w});
    b3Body_SetLinearVelocity(impl_->entries.at(index)->body, b3Vec3_zero);
    b3Body_SetAngularVelocity(impl_->entries.at(index)->body, b3Vec3_zero);
    ++impl_->revision;
}
glm::vec3 BoxLayer::velocity(std::size_t index) const {
    return vector(b3Body_GetLinearVelocity(impl_->entries.at(index)->body));
}
float BoxLayer::mass(std::size_t index) const { return prop(index).mass(); }
BoxPointState BoxLayer::pointState(std::size_t index, glm::vec3 localPoint) const { return prop(index).pointState(localPoint); }
void BoxLayer::applyImpulse(std::size_t index, glm::vec3 impulse, glm::vec3 point) { prop(index).applyImpulse(impulse,point); }
void BoxLayer::dampOnGroundContact(float linearDamping, float angularDamping) {
    std::vector<b3ContactData> contacts;
    for (const auto& entry : impl_->entries) {
        const auto body = entry->body;
        if (!b3Body_IsAwake(body)) continue;
        const bool held = impl_->world->isGrabbed(body);
        contacts.resize(held ? 0 : b3Body_GetContactCapacity(body));
        const int count = held ? 0 : b3Body_GetContactData(body, contacts.data(), static_cast<int>(contacts.size()));
        bool supported = false;
        for (int i=0; i<count && !supported; ++i) {
            const auto& contact = contacts[i];
            const float sign = B3_ID_EQUALS(b3Shape_GetBody(contact.shapeIdB), body) ? 1.0f : -1.0f;
            for (int j=0; j<contact.manifoldCount && !supported; ++j) {
                const auto& manifold = contact.manifolds[j];
                if (sign*manifold.normal.y < .5f) continue;
                for (int k=0; k<manifold.pointCount; ++k)
                    if (manifold.points[k].totalNormalImpulse > 0 || manifold.points[k].separation <= .005f)
                        supported = true;
            }
        }
        b3Body_SetLinearDamping(body,supported ? linearDamping : entry->damping.x);
        b3Body_SetAngularDamping(body,supported ? angularDamping : entry->damping.y);
    }
}
std::optional<BoxRayHit> BoxLayer::raycast(glm::vec3 origin, glm::vec3 direction, float reach) const {
    const float length = glm::length(direction);
    if (!finite(origin) || !finite(direction) || length < 1e-6f || !std::isfinite(length) ||
        !std::isfinite(reach) || reach <= 0 || reach > 1024) return std::nullopt;
    const auto end = origin + direction / length * reach;
    std::optional<BoxRayHit> closest;
    for (const auto& box : query(glm::min(origin, end), glm::max(origin, end))) {
        const auto inverse = glm::conjugate(box.rotation);
        b3RayCastInput ray{};
        ray.origin = vector(inverse * (origin - box.center));
        ray.translation = vector(inverse * (end - origin)); ray.maxFraction = 1;
        const auto h = box.halfExtents;
        const auto hull = b3MakeBoxHull(h.x, h.y, h.z);
        const auto hit = b3RayCastHull(&hull.base, &ray);
        if (!hit.hit || (closest && closest->distance <= hit.fraction * reach)) continue;
        const auto point = origin + (end - origin) * hit.fraction;
        closest = BoxRayHit{box.index, point, inverse * (point - box.center), hit.fraction * reach};
    }
    return closest;
}
std::vector<BoxPose> BoxLayer::query(glm::vec3 minimum, glm::vec3 maximum) const {
    std::vector<BoxPose> result;
    if (!finite(minimum) || !finite(maximum) || glm::any(glm::greaterThan(minimum, maximum))) return result;
    struct Query { const BoxLayer* layer; std::vector<BoxPose>* result; } query{this, &result};
    auto filter = b3DefaultQueryFilter();
    filter.maskBits = impl_->categories;
    b3World_OverlapAABB(impl_->world->id(), {{minimum.x, minimum.y, minimum.z}, {maximum.x, maximum.y, maximum.z}}, filter,
        [](b3ShapeId shape, void* context) {
            const auto& q = *static_cast<Query*>(context);
            const auto* entry = static_cast<Impl::Entry*>(b3Shape_GetUserData(shape));
            if (entry && entry->owner == q.layer->impl_.get()) q.result->push_back(q.layer->pose(entry->index));
            return true;
        }, &query);
    return result;
}
void BoxLayer::appendContacts(glm::vec3 feet, float radius, float height, float skin, std::vector<Contact>& contacts) const {
    const float reach = radius + skin;
    for (const auto& box : query(feet - glm::vec3(reach, skin, reach), feet + glm::vec3(reach, height + skin, reach))) {
        const auto found = boxContacts(box, feet, radius, height, skin);
        contacts.insert(contacts.end(), found.begin(), found.end());
    }
}
std::optional<GroundHit> BoxLayer::groundAt(float x, float z, float minY, float maxY) const {
    std::optional<GroundHit> result;
    for (const auto& box : query({x, minY, z}, {x, maxY, z})) {
        const auto inverse = glm::conjugate(box.rotation);
        const auto h = box.halfExtents;
        const auto hull = b3MakeBoxHull(h.x, h.y, h.z);
        // Start just above the query ceiling so a face exactly at maxY is hit.
        const glm::vec3 origin(x, maxY + .001f, z);
        const glm::vec3 translation(0, minY - maxY - .002f, 0);
        b3RayCastInput ray{};
        ray.origin = vector(inverse * (origin - box.center));
        ray.translation = vector(inverse * translation);
        ray.maxFraction = 1;
        const auto hit = b3RayCastHull(&hull.base, &ray);
        if (!hit.hit) continue;
        const float y = origin.y + translation.y * hit.fraction;
        const auto normal = box.rotation * vector(hit.normal);
        if (normal.y > .0001f && y >= minY - .0001f && y <= maxY + .0001f && (!result || y > result->height))
            result = GroundHit{y, normal};
    }
    return result;
}
bool BoxLayer::blocksSight(glm::vec3 origin, glm::vec3 target) const {
    for (const auto& box : query(glm::min(origin, target), glm::max(origin, target))) {
        const auto inverse = glm::conjugate(box.rotation);
        const auto start = inverse * (origin - box.center), end = inverse * (target - box.center);
        if (glm::all(glm::lessThanEqual(glm::abs(start), box.halfExtents)) ||
            glm::all(glm::lessThanEqual(glm::abs(end), box.halfExtents))) return true;
        const auto h = box.halfExtents;
        const auto hull = b3MakeBoxHull(h.x, h.y, h.z);
        b3RayCastInput ray{};
        ray.origin = vector(start); ray.translation = vector(end - start); ray.maxFraction = 1;
        if (b3RayCastHull(&hull.base, &ray).hit) return true;
    }
    return false;
}
void BoxLayer::push(glm::vec3 feet, float radius, float height, glm::vec3 velocity, float dt) {
    if (!finite(feet) || !finite(velocity) || !std::isfinite(dt) || dt <= 0) return;
    const float reach = radius + .003f;
    for (const auto& box : query(feet - glm::vec3(reach, 0, reach), feet + glm::vec3(reach, height, reach))) {
        const auto contacts = boxContacts(box, feet, radius, height, .003f);
        if (contacts.empty()) continue;
        const auto& contact = *std::max_element(contacts.begin(), contacts.end(),
            [](const Contact& a, const Contact& b) { return a.depth < b.depth; });
        // Standing/jumping supplies support, never a horizontal push.
        if (std::abs(contact.normal.y) > .6f) continue;
        const auto normal = glm::normalize(glm::vec3(contact.normal.x, 0, contact.normal.z));
        const auto body = impl_->entries[box.index]->body;
        const auto point = contact.point;
        const auto bodyVelocity = vector(b3Body_GetWorldPointVelocity(body, {point.x, point.y, point.z}));
        const float approach = glm::dot(velocity - bodyVelocity, -normal);
        if (approach <= 0) continue;
        const float mass = b3Body_GetMass(body);
        const float strength = std::min(approach * mass, std::min(approach * 500.0f, 900.0f) * std::min(dt, .05f));
        b3Body_ApplyLinearImpulse(body, vector(-normal * strength), {point.x, point.y, point.z}, true);
    }
}
std::vector<glm::vec3> BoxLayer::debugLines(glm::vec3 minimum, glm::vec3 maximum) const {
    std::vector<glm::vec3> result;
    for (const auto& box : query(minimum, maximum)) {
        std::array<glm::vec3, 8> corners;
        for (unsigned i = 0; i < 8; ++i)
            corners[i] = box.center + box.rotation * (box.halfExtents * glm::vec3(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1));
        for (unsigned i = 0; i < 8; ++i) for (unsigned bit : {1u, 2u, 4u})
            if (!(i & bit)) result.insert(result.end(), {corners[i], corners[i | bit]});
    }
    return result;
}
Model BoxLayer::unitCube() {
    Model model;
    Material material;
    material.baseColor = {.48f, .34f, .22f, 1};
    model.materials.push_back(material);
    Primitive cube;
    cube.material = 0;
    for (const auto normal : std::array<glm::vec3, 6>{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}}) {
        const auto u = std::abs(normal.y) > .5f ? glm::vec3(1, 0, 0) : glm::normalize(glm::cross(glm::vec3(0, 1, 0), normal));
        const auto v = glm::cross(normal, u);
        const auto first = static_cast<std::uint32_t>(cube.vertices.size());
        for (const auto corner : std::array<glm::vec2, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}) {
            Vertex vertex;
            vertex.position = (normal + u * corner.x + v * corner.y) * .5f;
            vertex.normal = normal;
            cube.vertices.push_back(vertex);
        }
        for (const auto i : {0u, 1u, 2u, 0u, 2u, 3u}) cube.indices.push_back(first + i);
    }
    model.primitives.push_back(std::move(cube));
    model.boundsMin = glm::vec3(-.5f); model.boundsMax = glm::vec3(.5f);
    return model;
}
} // namespace viewer
