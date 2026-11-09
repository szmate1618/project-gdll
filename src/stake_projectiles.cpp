#include "stake_projectiles.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "zombie_hit_capsules.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <map>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 v) { return {v.x,v.y,v.z}; }
glm::vec3 vector(b3Vec3 v) { return {v.x,v.y,v.z}; }
b3Quat rotation(glm::quat q) { return {{q.x,q.y,q.z},q.w}; }
glm::quat along(glm::vec3 velocity, glm::quat previous) {
    const auto forward = glm::normalize(velocity);
    auto up = previous * glm::vec3(0,1,0);
    if (std::abs(glm::dot(up,forward)) > .98f)
        up = std::abs(forward.y) < .98f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    const auto right = glm::normalize(glm::cross(forward,up));
    return glm::normalize(glm::quat_cast(glm::mat3(right,glm::cross(right,forward),-forward)));
}
bool rayTriangle(glm::dvec3 start, glm::dvec3 direction, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, double& distance) {
    const auto edge = b-a, other = c-a;
    const auto cross = glm::cross(direction,other);
    const double determinant = glm::dot(edge,cross);
    if (std::abs(determinant) < 1e-12) return false;
    const auto offset = start-a;
    const double u = glm::dot(offset,cross)/determinant;
    const auto vCross = glm::cross(offset,edge);
    const double v = glm::dot(direction,vCross)/determinant;
    distance = glm::dot(other,vCross)/determinant;
    return u >= 0 && v >= 0 && u+v <= 1 && distance >= 0;
}
glm::mat4 transform(glm::vec3 center, glm::quat q) {
    return glm::translate(glm::mat4(1),center) * glm::mat4_cast(q);
}
std::optional<glm::mat4> boneFrame(ZombieLayer* zombies, std::size_t id, int bone, double seconds) {
    if (!zombies || id >= zombies->instances.size()) return {};
    const auto& instance = zombies->instances[id];
    if (instance.removed || instance.asset >= zombies->assets.size()) return {};
    if (bone < 0) return instance.restRoot;
    const auto& asset = zombies->assets[instance.asset];
    if (std::size_t(bone) >= asset.skeleton.size()) return {};
    if (instance.hasPhysicalPose()) {
        if (instance.ragdollBones.size() != asset.skeleton.size()) return {};
        return instance.transform * instance.ragdollBones[bone] * glm::inverse(asset.skeleton[bone].inverseBind);
    }
    const auto pose = sampleZombiePose(asset,instance,seconds);
    return instance.transform * pose.at(bone);
}
}

struct StakeProjectiles::Impl {
    struct Entry {
        PhysicsProp prop;
        b3BodyId anchor{}; // Destroying the anchor also destroys its weld.
        b3Filter originalFilter{};
        std::optional<std::size_t> zombie;
        int bone = -1;
        glm::mat4 localPose{1};
        explicit Entry(PhysicsProp body) : prop(std::move(body)) {}
        ~Entry() { if (b3Body_IsValid(anchor)) b3DestroyBody(anchor); }
        bool pinned() const { return b3Body_IsValid(anchor); }
        void release() {
            if (!pinned()) return;
            b3DestroyBody(anchor); anchor = b3_nullBodyId;
            if (prop.valid()) {
                const auto body = prop.body();
                b3ShapeId shape{};
                if (b3Body_GetShapes(body,&shape,1) == 1) b3Shape_SetFilter(shape,originalFilter,true);
                // Pull the point out of the penetration before the hand moves it.
                const auto q = b3Body_GetRotation(body);
                const auto p = vector(b3Body_GetPosition(body)) + glm::quat(q.s,q.v.x,q.v.y,q.v.z) * glm::vec3(0,0,.12f);
                b3Body_SetTransform(body,vector(p),q);
                b3Body_SetAwake(body,true);
            }
            zombie.reset();
        }
    };
    GroundItems& items;
    std::shared_ptr<PhysicsWorld> world;
    const CollisionWorld& collision;
    ZombieLayer* zombies;
    double seconds = 0;
    std::map<std::uint64_t,std::unique_ptr<Entry>> entries;

    Impl(GroundItems& items, std::shared_ptr<PhysicsWorld> world, const CollisionWorld& collision, ZombieLayer* zombies)
        : items(items), world(std::move(world)), collision(collision), zombies(zombies) {
        this->world->registerStepCallbacks(this,[this] { prepare(); },[this] { finish(); });
        try {
            this->world->registerGrabCallback(this,[this](b3BodyId body) {
                for (auto& [id,e] : entries) {
                    (void)id;
                    if (e->prop.valid() && B3_ID_EQUALS(e->prop.body(),body)) e->release();
                }
            });
        } catch (...) { this->world->unregisterStepCallbacks(this); throw; }
    }
    ~Impl() {
        world->unregisterGrabCallback(this);
        world->unregisterStepCallbacks(this);
        for (auto& [id,e] : entries) { (void)id; e->release(); }
    }
    void place(Entry& e, glm::vec3 center, glm::quat q) {
        b3Body_SetTransform(e.prop.body(),vector(center),rotation(q));
        b3Body_SetLinearVelocity(e.prop.body(),b3Vec3_zero);
        b3Body_SetAngularVelocity(e.prop.body(),b3Vec3_zero);
        if (e.pinned()) b3Body_SetTransform(e.anchor,vector(center),rotation(q));
    }
    void follow(Entry& e) {
        if (!e.zombie) return;
        const auto frame = boneFrame(zombies,*e.zombie,e.bone,seconds);
        if (!frame) { e.release(); return; }
        const auto pose = *frame * e.localPose;
        const auto q = along(-glm::vec3(pose[2]),glm::normalize(glm::quat_cast(glm::mat3(pose))));
        place(e,glm::vec3(pose[3]),q);
    }
    void pin(Entry& e, glm::vec3 center, glm::quat q, std::optional<ZombieHitClaim> hit) {
        place(e,center,q);
        if (hit) {
            e.zombie = hit->zombie; e.bone = hit->bone;
            const auto frame = boneFrame(zombies,*e.zombie,e.bone,seconds);
            if (!frame) { e.zombie.reset(); return; }
            e.localPose = glm::inverse(*frame) * transform(center,q);
        }
        auto def = b3DefaultBodyDef(); def.position = vector(center); def.rotation = rotation(q);
        e.anchor = b3CreateBody(world->id(),&def);
        auto joint = b3DefaultWeldJointDef();
        joint.base.bodyIdA = e.anchor; joint.base.bodyIdB = e.prop.body();
        b3CreateWeldJoint(world->id(),&joint);
        b3ShapeId shape{};
        if (b3Body_GetShapes(e.prop.body(),&shape,1) == 1) {
            e.originalFilter = b3Shape_GetFilter(shape);
            auto filter = e.originalFilter; filter.maskBits = embeddedPropQueryCategory;
            // Remains ray-queryable for G/H and free-hand retrieval. Suppress
            // contacts with the penetrated bone while the embedding owns it.
            b3Shape_SetFilter(shape,filter,true);
        }
    }
    std::optional<ZombieShotTrace> traceZombie(std::size_t id, glm::vec3 start, glm::vec3 end) {
        const auto& instance = zombies->instances[id];
        if (!instance.ragdoll) return zombies->traceMeleeBody(id,start,end,seconds);
        if (instance.asset >= zombies->capsuleAssets.size()) return {};
        const auto& asset = zombies->assets[instance.asset];
        if (instance.ragdollBones.size() != asset.skeleton.size()) return {};
        std::vector<glm::mat4> pose;
        for (std::size_t b = 0; b < asset.skeleton.size(); ++b)
            pose.push_back(instance.ragdollBones[b] * glm::inverse(asset.skeleton[b].inverseBind));
        const float length = glm::distance(start,end);
        const auto direction = (end-start)/length;
        std::optional<ZombieShotTrace> nearest;
        for (const auto& capsule : zombies->capsuleAssets[instance.asset].capsules) {
            float distance = 0;
            if (!zombieCapsuleRay(start,direction,instance.transform,capsule,pose,distance) || distance > length ||
                (nearest && distance >= nearest->distance)) continue;
            ZombieHitClaim claim; claim.zombie = static_cast<std::uint32_t>(id);
            claim.bone = static_cast<std::int16_t>(capsule.bone);
            const auto frame = boneFrame(zombies,id,capsule.bone,seconds);
            if (!frame) continue;
            claim.point = glm::vec3(glm::inverse(*frame) * glm::vec4(start+direction*distance,1));
            nearest = ZombieShotTrace{claim,distance};
        }
        return nearest;
    }
    void fly(Entry& e, const BoxPose& pose) {
        const auto body = e.prop.body();
        const auto velocity = vector(b3Body_GetLinearVelocity(body));
        if (glm::length(velocity) < 1.f) return;
        const auto q = along(velocity,pose.rotation);
        b3Body_SetTransform(body,vector(pose.center),rotation(q));
        b3Body_SetAngularVelocity(body,b3Vec3_zero);
        const glm::vec3 tip(0,0,-pose.halfExtents.z);
        const auto start = pose.center + pose.rotation*tip;
        constexpr float dt = PhysicsWorld::fixedStepSeconds;
        const auto nextVelocity = velocity + glm::vec3(0,-9.8f*dt,0);
        const auto nextRotation = along(nextVelocity,q);
        const auto end = pose.center + nextVelocity*dt + nextRotation*tip + glm::normalize(velocity)*.035f;
        const auto delta = end-start;
        const float length = glm::length(delta);
        if (length < 1e-6f) return;
        const auto direction = delta/length;
        float nearest = length;
        bool ground = false;
        // Every static face occludes the zombie sweep. Only upward ground/floor
        // faces embed; side impacts keep ordinary rigid-body collision.
        for (const auto& face : collision.staticFaces(glm::min(start,end)-glm::vec3(.04f),glm::max(start,end)+glm::vec3(.04f))) {
            const auto a = glm::dvec3(face.vertices[0]), b = glm::dvec3(face.vertices[1]), c = glm::dvec3(face.vertices[2]);
            double distance = 0;
            if (!rayTriangle(glm::dvec3(start),glm::dvec3(direction),a,b,c,distance) || distance < 0 || distance > nearest) continue;
            nearest = float(distance);
            auto normal = glm::normalize(glm::cross(b-a,c-a));
            if (normal.y < 0) normal = -normal;
            ground = normal.y > .4 && direction.y < -.05f;
        }
        std::optional<ZombieHitClaim> hit;
        if (zombies) zombies->visitReplicationNear((start+end)*.5f,length*.5f+2.5f,[&](std::size_t id) {
            const auto contact = traceZombie(id,start,end);
            if (contact && contact->distance < nearest) {
                nearest = contact->distance; hit = contact->claim; ground = false;
            }
            return true;
        });
        if (!hit && !ground) return;
        const auto point = start+direction*nearest;
        if (hit && !zombies->instances[hit->zombie].ragdoll)
            zombies->applyReportedShot(*hit,start,glm::normalize(velocity),seconds,collision,ShotCaliber::high);
        pin(e,point + glm::normalize(velocity)*.08f - q*tip,q,hit);
    }
    void prepare() {
        for (auto it = entries.begin(); it != entries.end();)
            if (!it->second->prop.valid()) it = entries.erase(it); else ++it;
        for (const auto& item : items.poses()) {
            if (!item.entry.weapon || item.entry.weapon->slot != stakeSlot) continue;
            auto& entry = entries[item.entry.id];
            if (!entry) entry = std::make_unique<Entry>(*items.prop(item.entry.id));
            auto& e = *entry;
            if (e.prop.grabbed()) { e.release(); continue; }
            if (e.pinned()) { follow(e); continue; }
            fly(e,item.body);
        }
    }
    void finish() {
        for (auto& [id,e] : entries) {
            (void)id;
            if (!e->prop.valid() || !e->pinned()) continue;
            if (e->zombie) {
                zombies->synchronizePhysics(*e->zombie);
                follow(*e);
            }
        }
    }
};
StakeProjectiles::StakeProjectiles(GroundItems& items, std::shared_ptr<PhysicsWorld> world,
        const CollisionWorld& collision, ZombieLayer* zombies)
    : impl_(std::make_unique<Impl>(items,std::move(world),collision,zombies)) {}
StakeProjectiles::~StakeProjectiles() = default;
void StakeProjectiles::setAnimationSeconds(double seconds) { impl_->seconds = seconds; }
bool StakeProjectiles::embedded(std::uint64_t id) const {
    const auto found = impl_->entries.find(id);
    return found != impl_->entries.end() && found->second->prop.valid() && found->second->pinned();
}
}
