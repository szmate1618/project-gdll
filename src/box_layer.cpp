#include "box_layer.hpp"
#include "collision_world.hpp"
#include "collision_box.hpp"
#include "physics_world.hpp"
#include "physics_impacts.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 p) { return {p.x, p.y, p.z}; }
glm::vec3 vector(b3Vec3 p) { return {p.x, p.y, p.z}; }
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

glm::vec3 colliderEnvelope(const std::vector<PropCollider>& parts) {
    if (parts.empty() || parts.size()>64) throw std::invalid_argument("A prop requires 1–64 collider parts");
    glm::vec3 half(0);
    for (const auto& part : parts) {
        if (!finite(part.center) || !finite(part.halfExtents) || !std::isfinite(glm::length(part.rotation)) ||
            std::abs(glm::length(part.rotation)-1)>.001f ||
            glm::any(glm::lessThanEqual(part.halfExtents,glm::vec3(0))) ||
            (part.shape!=PropColliderShape::Box && part.shape!=PropColliderShape::Sphere) ||
            (part.shape==PropColliderShape::Sphere &&
                (std::abs(part.halfExtents.x-part.halfExtents.y)>1e-6f || std::abs(part.halfExtents.x-part.halfExtents.z)>1e-6f)))
            throw std::invalid_argument("Invalid prop collider part");
        const auto axes=glm::mat3_cast(part.rotation);
        const auto extent=glm::abs(axes[0])*part.halfExtents.x+glm::abs(axes[1])*part.halfExtents.y+glm::abs(axes[2])*part.halfExtents.z;
        half=glm::max(half,glm::abs(part.center)+extent);
    }
    if (glm::any(glm::greaterThan(half,glm::vec3(2)))) throw std::invalid_argument("Prop colliders exceed the 4 metre body limit");
    return half;
}
float colliderVolume(const std::vector<PropCollider>& parts) {
    double volume=0;
    for (const auto& part:parts) {
        const auto h=glm::dvec3(part.halfExtents);
        volume+=part.shape==PropColliderShape::Sphere ? 4.1887902047863905*h.x*h.x*h.x : 8*h.x*h.y*h.z;
    }
    if (!std::isfinite(volume) || volume<=0 || !std::isfinite(static_cast<float>(volume)) || static_cast<float>(volume)<=0)
        throw std::invalid_argument("Invalid prop collider volume");
    return static_cast<float>(volume);
}
void createColliders(b3BodyId body, const std::vector<PropCollider>& parts, float mass,
    b3Filter filter, float friction, void* owner) {
    auto definition=b3DefaultShapeDef();
    definition.density=mass/colliderVolume(parts);
    if (!std::isfinite(definition.density)) throw std::invalid_argument("Invalid prop collider density");
    definition.filter=filter; definition.baseMaterial.friction=friction;
    definition.userData=owner; definition.updateBodyMass=false;
    definition.enableCustomFiltering=true;
    for (const auto& part:parts) {
        definition.baseMaterial.userMaterialId=part.materialId;
        b3ShapeId shape{};
        if (part.shape==PropColliderShape::Sphere) {
            const b3Sphere sphere{vector(part.center),part.halfExtents.x};
            shape=b3CreateSphereShape(body,&definition,&sphere);
        } else {
            const auto h=part.halfExtents;
            const auto q=part.rotation;
            const auto hull=b3MakeTransformedBoxHull(h.x,h.y,h.z,{vector(part.center),{{q.x,q.y,q.z},q.w}});
            shape=b3CreateHullShape(body,&definition,&hull.base);
        }
        if (!b3Shape_IsValid(shape)) throw std::runtime_error("Cannot create prop collider");
    }
}
b3CastOutput rayCollider(const BoxPose& pose, PropColliderShape shape, glm::vec3 origin, glm::vec3 translation) {
    const auto inverse=glm::conjugate(pose.rotation);
    b3RayCastInput ray{};
    ray.origin=vector(inverse*(origin-pose.center)); ray.translation=vector(inverse*translation); ray.maxFraction=1.000001f;
    if (shape==PropColliderShape::Sphere) {
        const b3Sphere sphere{b3Vec3_zero,pose.halfExtents.x};
        return b3RayCastSphere(&sphere,&ray);
    }
    const auto h=pose.halfExtents;
    const auto hull=b3MakeBoxHull(h.x,h.y,h.z);
    return b3RayCastHull(&hull.base,&ray);
}
std::vector<Contact> capsuleContacts(const BoxPose& pose, PropColliderShape shape,
    glm::vec3 feet,float radius,float height,float skin) {
    if (shape==PropColliderShape::Box) return boxContacts(pose,feet,radius,height,skin);
    const float a=feet.y+radius,b=feet.y+height-radius;
    const glm::vec3 closest(feet.x,std::clamp(pose.center.y,std::min(a,b),std::max(a,b)),feet.z);
    const auto delta=closest-pose.center;
    const float distance=glm::length(delta), depth=radius+skin+pose.halfExtents.x-distance;
    if (depth<=0) return {};
    const auto normal=distance>1e-6f ? delta/distance : glm::vec3(1,0,0);
    return {{normal,pose.center+normal*pose.halfExtents.x,depth}};
}
std::optional<Contact> orientedBoxContact(const CollisionBox& box,const BoxPose& pose,PropColliderShape shape,float skin) {
    if (shape==PropColliderShape::Box) return collisionBoxContact(box,{pose.center,pose.halfExtents,pose.rotation},skin);
    const auto inverse=glm::conjugate(box.rotation);
    const auto h=box.halfExtents+glm::vec3(skin);
    const auto hull=b3MakeBoxHull(h.x,h.y,h.z);
    const b3Sphere sphere{vector(inverse*(pose.center-box.center)),pose.halfExtents.x};
    std::array<b3LocalManifoldPoint,8> points{};
    b3LocalManifold manifold{}; manifold.points=points.data(); b3SimplexCache cache{};
    b3CollideHullAndSphere(&manifold,static_cast<int>(points.size()),&hull.base,&sphere,b3Transform_identity,&cache);
    const b3LocalManifoldPoint* deepest=nullptr;
    for (int i=0;i<manifold.pointCount;++i)
        if (points[i].separation<0 && (!deepest || points[i].separation<deepest->separation)) deepest=&points[i];
    if (!deepest) return {};
    return Contact{-(box.rotation*vector(manifold.normal)),box.center+box.rotation*vector(deepest->point),-deepest->separation};
}
void pushContact(b3BodyId body,const Contact& contact,glm::vec3 velocity,float dt) {
    if (std::abs(contact.normal.y)>.6f) return;
    const auto normal=glm::normalize(glm::vec3(contact.normal.x,0,contact.normal.z));
    const auto bodyVelocity=vector(b3Body_GetWorldPointVelocity(body,vector(contact.point)));
    const float approach=glm::dot(velocity-bodyVelocity,-normal);
    if (approach<=0) return;
    const float mass=b3Body_GetMass(body);
    const float strength=std::min(approach*mass,std::min(approach*500.f,900.f)*std::min(dt,.05f));
    b3Body_ApplyLinearImpulse(body,vector(-normal*strength),vector(contact.point),true);
}

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
    struct Entry {
        Impl* owner;
        b3BodyId body;
        glm::vec3 halfExtents;
        std::size_t index;
        glm::vec2 damping;
        BoxPose retiredPose{};
        std::vector<PropCollider> parts;
        float mass = 0;
    };
    std::shared_ptr<PhysicsWorld> world;
    std::vector<std::unique_ptr<Entry>> entries;
    std::uint64_t revision = 1;
    std::uint64_t categories = 0;
    ~Impl() {
        for (const auto& e : entries) {
            if (B3_IS_NON_NULL(e->body)) {
                world->untrackBodyMotion(e->body);
                world->impacts().untrack(e->body);
                b3DestroyBody(e->body);
            }
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
void BoxLayer::wake() { for (const auto& entry : impl_->entries) if (B3_IS_NON_NULL(entry->body)) b3Body_SetAwake(entry->body,true); }
std::shared_ptr<PhysicsWorld> BoxLayer::physicsWorld() const { return impl_->world; }
PhysicsProp BoxLayer::prop(std::size_t index) const { return {impl_->world,impl_->entries.at(index)->body}; }
const std::vector<PropCollider>& BoxLayer::colliders(std::size_t index) const { return impl_->entries.at(index)->parts; }
std::optional<std::size_t> BoxLayer::indexOf(const PhysicsProp& prop) const {
    if (prop.valid())
        for (const auto& entry : impl_->entries)
            if (prop == PhysicsProp(impl_->world,entry->body)) return entry->index;
    return std::nullopt;
}
std::size_t BoxLayer::add(glm::vec3 center, glm::vec3 size, const BoxSpawnMotion& motion) {
    if (!finite(size) || glm::any(glm::lessThan(size,glm::vec3(.01f))))
        throw std::invalid_argument("Invalid box dimensions");
    return addCompound(center,{{PropColliderShape::Box,{},size*.5f,motion.materialId}},motion);
}
std::size_t BoxLayer::addCompound(glm::vec3 center,const std::vector<PropCollider>& parts,const BoxSpawnMotion& motion) {
    const auto half=colliderEnvelope(parts);
    const float volume=colliderVolume(parts);
    if (!finite(center) || !finite(motion.velocity) || !finite(motion.angularVelocity) ||
        !std::isfinite(glm::length(motion.rotation)) || std::abs(glm::length(motion.rotation)-1)>.001f ||
        !std::isfinite(motion.mass) || motion.mass<0 || !std::isfinite(motion.friction) || motion.friction<0 ||
        !std::isfinite(motion.linearDamping) || motion.linearDamping<0 ||
        !std::isfinite(motion.angularDamping) || motion.angularDamping<0)
        throw std::invalid_argument("Invalid prop dimensions, position or motion");
    const auto index=count();
    if (index==impl_->entries.capacity()) impl_->entries.reserve(std::max(std::size_t(8),index*2));
    auto entry=std::make_unique<Impl::Entry>();
    entry->owner=impl_.get(); entry->halfExtents=half; entry->index=index;
    entry->damping={motion.linearDamping,motion.angularDamping}; entry->parts=parts;
    entry->mass=motion.mass>0 ? motion.mass : glm::clamp(volume*70.f,8.f,55.f);
    auto definition=b3DefaultBodyDef();
    definition.type=b3_dynamicBody; definition.position={center.x,center.y,center.z};
    definition.rotation={{motion.rotation.x,motion.rotation.y,motion.rotation.z},motion.rotation.w};
    definition.linearVelocity=vector(motion.velocity); definition.angularVelocity=vector(motion.angularVelocity);
    definition.isBullet=motion.continuous;
    definition.linearDamping=motion.linearDamping; definition.angularDamping=motion.angularDamping;
    const auto body=b3CreateBody(impl_->world->id(),&definition);
    entry->body=body;
    try {
        createColliders(body,parts,entry->mass,motion.filter,motion.friction,entry.get());
        b3Body_ApplyMassFromShapes(body);
        impl_->world->impacts().track(body,motion.acoustics,motion.acousticScale);
    } catch (...) { b3DestroyBody(body); throw; }
    impl_->entries.push_back(std::move(entry));
    impl_->categories|=motion.filter.categoryBits;
    impl_->world->trackBodyMotion(body,impl_->revision);
    ++impl_->revision;
    return index;
}
void BoxLayer::replaceColliders(std::size_t index,const std::vector<PropCollider>& parts) {
    const auto half=colliderEnvelope(parts);
    (void)colliderVolume(parts);
    auto& entry=*impl_->entries.at(index);
    if (!active(index)) throw std::logic_error("Cannot replace retired prop colliders");
    std::vector<b3ShapeId> oldShapes(static_cast<std::size_t>(b3Body_GetShapeCount(entry.body)));
    b3Body_GetShapes(entry.body,oldShapes.data(),static_cast<int>(oldShapes.size()));
    if (oldShapes.empty()) throw std::logic_error("Prop body has no collider shapes");
    const auto filter=b3Shape_GetFilter(oldShapes.front());
    const float friction=b3Shape_GetFriction(oldShapes.front());
    // Build replacements first: malformed parts cannot erase the last good shape.
    createColliders(entry.body,parts,entry.mass,filter,friction,&entry);
    for (const auto shape:oldShapes) b3DestroyShape(shape,false);
    b3Body_ApplyMassFromShapes(entry.body);
    entry.parts=parts; entry.halfExtents=half;
    b3Body_SetAwake(entry.body,true);
    ++impl_->revision;
}
void BoxLayer::remove(std::size_t index) {
    retire(index);
    if (index+1<count()) {
        impl_->entries[index] = std::move(impl_->entries.back());
        impl_->entries[index]->index = index;
    }
    impl_->entries.pop_back();
    ++impl_->revision;
}
void BoxLayer::retire(std::size_t index) {
    auto& entry = *impl_->entries.at(index);
    if (!active(index)) return;
    entry.retiredPose = pose(index);
    impl_->world->untrackBodyMotion(entry.body);
    impl_->world->impacts().untrack(entry.body);
    b3DestroyBody(entry.body);
    entry.body = b3_nullBodyId;
    ++impl_->revision;
}
bool BoxLayer::active(std::size_t index) const {
    return B3_IS_NON_NULL(impl_->entries.at(index)->body);
}
bool BoxLayer::belowHeight(std::size_t index, float cutoffY) const {
    if (!std::isfinite(cutoffY)) throw std::invalid_argument("Box cleanup height must be finite");
    return active(index) && b3Body_ComputeAABB(impl_->entries.at(index)->body).upperBound.y < cutoffY;
}
void BoxLayer::retireBelow(float cutoffY) {
    if (!std::isfinite(cutoffY)) throw std::invalid_argument("Box cleanup height must be finite");
    for (std::size_t i = 0; i < count(); ++i)
        if (belowHeight(i, cutoffY)) retire(i);
}
BoxPose BoxLayer::pose(std::size_t index) const {
    const auto& e = *impl_->entries.at(index);
    if (!active(index)) return {index, e.retiredPose.center, e.halfExtents, e.retiredPose.rotation};
    const auto p = b3Body_GetPosition(e.body);
    const auto q = b3Body_GetRotation(e.body);
    return {index, {p.x, p.y, p.z}, e.halfExtents, {q.s, q.v.x, q.v.y, q.v.z}};
}
void BoxLayer::setPose(std::size_t index, glm::vec3 center, glm::quat q) {
    if (!finite(center) || !std::isfinite(glm::length(q)) || std::abs(glm::length(q) - 1) > .001f)
        throw std::invalid_argument("Invalid replicated box pose");
    if (!active(index)) throw std::logic_error("Cannot move a retired box");
    b3Body_SetTransform(impl_->entries.at(index)->body, {center.x, center.y, center.z}, {{q.x, q.y, q.z}, q.w});
    b3Body_SetLinearVelocity(impl_->entries.at(index)->body, b3Vec3_zero);
    b3Body_SetAngularVelocity(impl_->entries.at(index)->body, b3Vec3_zero);
    ++impl_->revision;
}
glm::vec3 BoxLayer::velocity(std::size_t index) const {
    return active(index) ? vector(b3Body_GetLinearVelocity(impl_->entries.at(index)->body)) : glm::vec3(0);
}
float BoxLayer::mass(std::size_t index) const { return impl_->entries.at(index)->mass; }
BoxPointState BoxLayer::pointState(std::size_t index, glm::vec3 localPoint) const { return prop(index).pointState(localPoint); }
void BoxLayer::applyImpulse(std::size_t index, glm::vec3 impulse, glm::vec3 point) { prop(index).applyImpulse(impulse,point); }
void BoxLayer::dampOnGroundContact(float linearDamping, float angularDamping) {
    std::vector<b3ContactData> contacts;
    for (const auto& entry : impl_->entries) {
        const auto body = entry->body;
        if (B3_IS_NULL(body) || !b3Body_IsAwake(body)) continue;
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
std::optional<BoxRayHit> BoxLayer::raycast(glm::vec3 origin,glm::vec3 direction,float reach) const {
    const float length=glm::length(direction);
    if (!finite(origin) || !finite(direction) || length<1e-6f || !std::isfinite(length) ||
        !std::isfinite(reach) || reach<=0 || reach>1024) return {};
    const auto translation=direction/length*reach, end=origin+translation;
    std::optional<BoxRayHit> closest;
    for (const auto& collider:queryColliders(glm::min(origin,end),glm::max(origin,end),false)) {
        const auto hit=rayCollider(collider.pose,collider.shape,origin,translation);
        if (!hit.hit || (closest && closest->distance<=hit.fraction*reach)) continue;
        const auto point=origin+translation*hit.fraction;
        const auto body=pose(collider.pose.index);
        closest=BoxRayHit{body.index,point,glm::conjugate(body.rotation)*(point-body.center),hit.fraction*reach,
            collider.pose.rotation*vector(hit.normal),collider.materialId};
    }
    return closest;
}
std::vector<BoxPose> BoxLayer::query(glm::vec3 minimum,glm::vec3 maximum) const {
    std::vector<BoxPose> result;
    if (!finite(minimum) || !finite(maximum) || glm::any(glm::greaterThan(minimum,maximum))) return result;
    struct Query { const BoxLayer* layer; std::vector<BoxPose>* result; std::set<std::size_t> seen; } query{this,&result,{}};
    auto filter=b3DefaultQueryFilter(); filter.maskBits=impl_->categories;
    b3World_OverlapAABB(impl_->world->id(),{{minimum.x,minimum.y,minimum.z},{maximum.x,maximum.y,maximum.z}},filter,
        [](b3ShapeId shape,void* context) {
            auto& q=*static_cast<Query*>(context);
            const auto* entry=static_cast<Impl::Entry*>(b3Shape_GetUserData(shape));
            if (entry && entry->owner==q.layer->impl_.get() && q.seen.insert(entry->index).second)
                q.result->push_back(q.layer->pose(entry->index));
            return true;
        },&query);
    return result;
}
std::vector<BoxLayer::ColliderPose> BoxLayer::queryColliders(glm::vec3 minimum,glm::vec3 maximum,bool physicalOnly) const {
    std::vector<ColliderPose> result;
    for (const auto& body:query(minimum,maximum)) {
        const auto& entry=*impl_->entries[body.index];
        if (physicalOnly && impl_->world->isCraftingHeld(entry.body)) continue;
        for (const auto& part:entry.parts) {
            const BoxPose box{body.index,body.center+body.rotation*part.center,part.halfExtents,body.rotation*part.rotation};
            // Box3D already selected conservative broadphase candidates. Let
            // each part's narrow query decide contact: a second exact AABB
            // test can round a reach-boundary ray endpoint outside its face.
            result.push_back({box,part.shape,part.materialId});
        }
    }
    return result;
}
void BoxLayer::appendContacts(glm::vec3 feet,float radius,float height,float skin,std::vector<Contact>& contacts) const {
    const float reach=radius+skin;
    for (const auto& collider:queryColliders(feet-glm::vec3(reach,skin,reach),feet+glm::vec3(reach,height+skin,reach),true)) {
        const auto found=capsuleContacts(collider.pose,collider.shape,feet,radius,height,skin);
        contacts.insert(contacts.end(),found.begin(),found.end());
    }
}
void BoxLayer::appendContacts(const CollisionBox& box,float skin,std::vector<Contact>& contacts) const {
    if (!validCollisionBox(box) || !std::isfinite(skin) || skin<0) return;
    const auto bounds=collisionBoxBounds(box,skin);
    for (const auto& collider:queryColliders(bounds.first,bounds.second,true)) {
        const auto contact=orientedBoxContact(box,collider.pose,collider.shape,skin);
        if (contact) contacts.push_back(*contact);
    }
}
std::optional<GroundHit> BoxLayer::groundAt(float x,float z,float minY,float maxY) const {
    std::optional<GroundHit> result;
    const glm::vec3 origin(x,maxY+.001f,z), translation(0,minY-maxY-.002f,0);
    for (const auto& collider:queryColliders({x,minY,z},{x,maxY,z},true)) {
        const auto hit=rayCollider(collider.pose,collider.shape,origin,translation);
        if (!hit.hit) continue;
        const float y=origin.y+translation.y*hit.fraction;
        const auto normal=collider.pose.rotation*vector(hit.normal);
        if (normal.y>.0001f && y>=minY-.0001f && y<=maxY+.0001f && (!result || y>result->height))
            result=GroundHit{y,normal};
    }
    return result;
}
bool BoxLayer::blocksSight(glm::vec3 origin,glm::vec3 target) const {
    for (const auto& collider:queryColliders(glm::min(origin,target),glm::max(origin,target),false)) {
        const auto& box=collider.pose;
        const auto inverse=glm::conjugate(box.rotation);
        const auto start=inverse*(origin-box.center), end=inverse*(target-box.center);
        const auto inside=[&](glm::vec3 point) {
            return collider.shape==PropColliderShape::Sphere ? glm::length(point)<=box.halfExtents.x :
                glm::all(glm::lessThanEqual(glm::abs(point),box.halfExtents));
        };
        if (inside(start) || inside(end) || rayCollider(box,collider.shape,origin,target-origin).hit) return true;
    }
    return false;
}
void BoxLayer::push(glm::vec3 feet,float radius,float height,glm::vec3 velocity,float dt) {
    if (!finite(feet) || !finite(velocity) || !std::isfinite(dt) || dt<=0) return;
    const float reach=radius+.003f;
    std::map<std::size_t,Contact> deepest;
    for (const auto& collider:queryColliders(feet-glm::vec3(reach,0,reach),feet+glm::vec3(reach,height,reach),true))
        for (const auto& contact:capsuleContacts(collider.pose,collider.shape,feet,radius,height,.003f)) {
            const auto found=deepest.find(collider.pose.index);
            if (found==deepest.end() || found->second.depth<contact.depth) deepest.insert_or_assign(collider.pose.index,contact);
        }
    for (const auto& [index,contact]:deepest) pushContact(impl_->entries[index]->body,contact,velocity,dt);
}
void BoxLayer::push(const CollisionBox& box,glm::vec3 velocity,float dt) {
    if (!validCollisionBox(box) || !finite(velocity) || !std::isfinite(dt) || dt<=0) return;
    const auto bounds=collisionBoxBounds(box,.003f);
    std::map<std::size_t,Contact> deepest;
    for (const auto& collider:queryColliders(bounds.first,bounds.second,true)) {
        const auto contact=orientedBoxContact(box,collider.pose,collider.shape,.003f);
        if (!contact) continue;
        const auto found=deepest.find(collider.pose.index);
        if (found==deepest.end() || found->second.depth<contact->depth) deepest.insert_or_assign(collider.pose.index,*contact);
    }
    for (const auto& [index,contact]:deepest) pushContact(impl_->entries[index]->body,contact,velocity,dt);
}
std::vector<glm::vec3> BoxLayer::debugLines(glm::vec3 minimum,glm::vec3 maximum) const {
    std::vector<glm::vec3> result;
    for (const auto& collider:queryColliders(minimum,maximum,false)) {
        const auto& box=collider.pose;
        if (collider.shape==PropColliderShape::Sphere) {
            constexpr int segments=24;
            for (int axis=0;axis<3;++axis) for (int segment=0;segment<segments;++segment) {
                for (int endpoint=0;endpoint<2;++endpoint) {
                    const float angle=(segment+endpoint)*6.28318530718f/segments;
                    glm::vec3 point(0); point[(axis+1)%3]=std::cos(angle)*box.halfExtents.x;
                    point[(axis+2)%3]=std::sin(angle)*box.halfExtents.x;
                    result.push_back(box.center+box.rotation*point);
                }
            }
            continue;
        }
        const auto corners=collisionBoxCorners({box.center,box.halfExtents,box.rotation});
        for (unsigned i=0;i<corners.size();++i) for (unsigned bit:{1u,2u,4u})
            if (!(i&bit)) result.insert(result.end(),{corners[i],corners[i|bit]});
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
