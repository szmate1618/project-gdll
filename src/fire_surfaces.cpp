#include "fire_surfaces.hpp"
#include "physics_world.hpp"
#include "door_layer.hpp"
#include "dog_layer.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
bool triangleRay(glm::vec3 origin, glm::vec3 direction, const StaticCollisionFace& face, float& distance) {
    const glm::dvec3 a(face.vertices[0]), e1 = glm::dvec3(face.vertices[1])-a, e2 = glm::dvec3(face.vertices[2])-a;
    const auto p = glm::cross(glm::dvec3(direction),e2);
    const double determinant = glm::dot(e1,p);
    if (std::abs(determinant)<1e-12) return false;
    const auto t = glm::dvec3(origin)-a;
    const double u = glm::dot(t,p)/determinant;
    const auto q = glm::cross(t,e1);
    const double v = glm::dot(glm::dvec3(direction),q)/determinant;
    const double d = glm::dot(e2,q)/determinant;
    if (u<0 || v<0 || u+v>1 || d<0 || d>distance) return false;
    distance = static_cast<float>(d);
    return true;
}
template<class V> glm::vec3 vector(V p) { return {float(p.x),float(p.y),float(p.z)}; }
}
FireSurfaceQuery::FireSurfaceQuery(const FireScene& scene, glm::vec3 center) : scene_(scene) {
    const glm::vec3 extent(molotovRayReach+.03f);
    faces_ = scene.collision.staticFaces(center-extent,center+extent);
    bool overflow = true;
    for (std::size_t size=32; overflow; size*=2) {
        trunks_.resize(size);
        const auto count = scene.collision.nearbyTrunks(center-glm::vec3(0,extent.y,0),extent.y*2,extent.x,
            trunks_.data(),trunks_.size(),overflow);
        if (!overflow) trunks_.resize(count);
    }
    if (scene.zombies) scene.zombies->visitReplicationNear(center,extent.x,[&](std::size_t id) {
        const auto& actor = scene.zombies->instances[id];
        if (actor.removed || actor.asset>=scene.zombies->assets.size()) return true;
        std::vector<glm::mat4> pose;
        if (actor.asset<scene.zombies->capsuleAssets.size() && !scene.zombies->capsuleAssets[actor.asset].capsules.empty())
            pose = sampleZombiePose(scene.zombies->assets[actor.asset],actor,scene.animationSeconds);
        zombies_.push_back({id,std::move(pose)});
        return true;
    });
}
std::optional<FireContact> FireSurfaceQuery::cast(glm::vec3 origin, glm::vec3 direction, float reach) const {
    float nearest = reach;
    std::optional<FireContact> result;
    for (const auto& face : faces_) if (triangleRay(origin,direction,face,nearest)) {
        auto normal = glm::normalize(glm::cross(face.vertices[1]-face.vertices[0],face.vertices[2]-face.vertices[0]));
        if (glm::dot(normal,direction)>0) normal = -normal;
        result = FireContact{origin+direction*nearest,normal};
    }
    const std::vector<glm::mat4> identity{glm::mat4(1)};
    for (const auto& entry : trunks_) {
        const auto& t = entry.collider;
        const ZombieBoneCapsule capsule{0,t.base+glm::vec3(0,t.radius,0),
            t.base+glm::vec3(0,std::max(t.radius,t.height-t.radius),0),t.radius};
        float distance = 0;
        if (zombieCapsuleRay(origin,direction,glm::mat4(1),capsule,identity,distance) && distance<=nearest) {
            nearest = distance;
            result = FireContact{origin+direction*distance,-direction};
        }
    }
    // The local physics broad phase covers props, parked/moving vehicles and
    // available corpse bodies. Ignore invisible fallback floors and projectiles.
    if (scene_.physics) {
        auto filter = b3DefaultQueryFilter();
        filter.maskBits = interiorBoxCategory | vehicleBodyCategory | ragdollBodyCategory |
            zombieContentsCategory | detachedLimbCategory | playerBodyCategory | zombieAttackBodyCategory;
        const auto delta = direction*nearest;
        const auto hit = b3World_CastRayClosest(scene_.physics->id(),{origin.x,origin.y,origin.z},
            {delta.x,delta.y,delta.z},filter);
        if (hit.hit) {
            nearest *= hit.fraction;
            const auto body = b3Shape_GetBody(hit.shapeId);
            FireContact contact{vector(hit.point),vector(hit.normal),FireAttachment::body};
            contact.body.emplace(scene_.physics,body);
            contact.localPoint = vector(b3Body_GetLocalPoint(body,hit.point));
            result = std::move(contact);
        }
    }
    if (scene_.doors) if (const auto hit = scene_.doors->raycast(origin,direction,nearest)) {
        nearest = hit->distance;
        FireContact contact{hit->point,-direction,FireAttachment::door};
        contact.actor = hit->door; contact.localPoint = hit->localPoint;
        result = std::move(contact);
    }
    for (const auto& cached : zombies_) {
        const auto& actor = scene_.zombies->instances[cached.id];
        if (cached.pose.empty()) {
            const auto hit = scene_.zombies->traceShot(cached.id,actor,origin,direction,scene_.animationSeconds);
            if (!hit || hit->distance>nearest) continue;
            nearest = hit->distance;
            FireContact contact{origin+direction*nearest,-direction,FireAttachment::zombie};
            contact.actor = cached.id; contact.bone = hit->claim.bone; contact.localPoint = hit->claim.point;
            result = std::move(contact);
            continue;
        }
        for (const auto& capsule : scene_.zombies->capsuleAssets[actor.asset].capsules) {
            float distance = 0;
            if (capsule.bone<0 || std::size_t(capsule.bone)>=cached.pose.size() ||
                !zombieCapsuleRay(origin,direction,actor.transform,capsule,cached.pose,distance) || distance>nearest) continue;
            nearest = distance;
            FireContact contact{origin+direction*distance,-direction,FireAttachment::zombie};
            contact.actor = cached.id; contact.bone = capsule.bone;
            contact.localPoint = glm::vec3(glm::inverse(actor.transform*cached.pose[std::size_t(capsule.bone)])*
                glm::vec4(contact.point,1));
            result = std::move(contact);
        }
    }
    if (scene_.dogs) if (const auto hit = scene_.dogs->traceShot(origin,direction,scene_.collision,scene_.animationSeconds)) {
        if (hit->distance<=nearest) {
            FireContact contact{origin+direction*hit->distance,-direction,FireAttachment::dog};
            contact.actor = hit->dog;
            contact.localPoint = glm::vec3(glm::inverse(scene_.dogs->instances[hit->dog].transform)*glm::vec4(contact.point,1));
            result = std::move(contact);
        }
    }
    return result;
}
} // namespace viewer
