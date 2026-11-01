#include "zombie_detached_parts.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
glm::vec3 position(b3BodyId body) {
    const auto p = b3Body_GetPosition(body);
    return {p.x,p.y,p.z};
}
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
} // namespace
ZombieDetachedParts::ZombieDetachedParts(std::shared_ptr<PhysicsWorld> world) : world_(std::move(world)) {}
void ZombieDetachedParts::record(RagdollPart part, const std::array<b3BodyId,2>& bodies,
                                std::size_t count, std::uint64_t& revision) {
    if (part < RagdollPart::upperArmLeft || part >= RagdollPart::count || count == 0 || count > bodies.size()) return;
    entries_.insert_or_assign(b3StoreBodyId(bodies[0]),Entry{part,bodies,count,&revision});
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<b3ShapeId> shapes(b3Body_GetShapeCount(bodies[i]));
        const int n = b3Body_GetShapes(bodies[i],shapes.data(),static_cast<int>(shapes.size()));
        for (int s = 0; s < n; ++s) {
            auto filter = b3Shape_GetFilter(shapes[s]);
            filter.categoryBits |= detachedLimbCategory;
            // Category changes must rebuild Box3D's broad-phase proxy so a
            // newly severed limb is visible to indexed queries immediately.
            b3Shape_SetFilter(shapes[s],filter,true);
        }
    }
}
void ZombieDetachedParts::forget(b3BodyId root) { entries_.erase(b3StoreBodyId(root)); }
std::optional<glm::vec3> ZombieDetachedParts::availablePosition(b3BodyId root) const {
    if (!entries_.count(b3StoreBodyId(root)) || !b3Body_IsValid(root) || world_->isGrabbed(root)) return std::nullopt;
    return position(root);
}
void ZombieDetachedParts::queryNear(glm::vec3 center, float radius, std::size_t limit,
                                   std::vector<DetachedZombiePart>& result) const {
    result.clear();
    if (!limit || !finite(center) || !std::isfinite(radius) || radius < 0) return;
    struct Query {
        const ZombieDetachedParts& owner;
        glm::vec3 center;
        float radius;
        std::size_t limit;
        std::vector<DetachedZombiePart>& out;
    } query{*this,center,radius,limit,result};
    auto filter = b3DefaultQueryFilter(); filter.maskBits = detachedLimbCategory;
    const auto lo = center-glm::vec3(radius), hi = center+glm::vec3(radius);
    b3World_OverlapAABB(world_->id(),{{lo.x,lo.y,lo.z},{hi.x,hi.y,hi.z}},filter,
        [](b3ShapeId shape, void* context) {
            auto& q = *static_cast<Query*>(context);
            const auto body = b3Shape_GetBody(shape);
            const auto found = q.owner.entries_.find(b3StoreBodyId(body));
            if (found == q.owner.entries_.end() || q.owner.world_->isGrabbed(body)) return true;
            const auto p = position(body);
            if (glm::distance(p,q.center) > q.radius) return true;
            for (const auto& prior : q.out) if (B3_ID_EQUALS(prior.body,body)) return true;
            const auto where = std::lower_bound(q.out.begin(),q.out.end(),glm::distance(p,q.center),
                [&](const DetachedZombiePart& part, float distance) { return glm::distance(part.position,q.center) < distance; });
            if (where != q.out.end() || q.out.size() < q.limit) q.out.insert(where,{body,found->second.part,p});
            if (q.out.size() > q.limit) q.out.pop_back();
            return true;
        }, &query);
}
std::unique_ptr<PhysicsAttachment> ZombieDetachedParts::carry(b3BodyId root, glm::vec3 mouth) {
    const auto current = availablePosition(root);
    if (!current || !finite(mouth)) return {};
    auto& entry = entries_.at(b3StoreBodyId(root));
    auto grip = std::make_unique<PhysicsAttachment>(world_,root);
    const auto delta = mouth-*current;
    for (std::size_t i = 0; i < entry.count; ++i) {
        const auto body = entry.bodies[i];
        const auto p = position(body)+delta;
        b3Body_SetTransform(body,{p.x,p.y,p.z},b3Body_GetRotation(body));
        b3Body_SetLinearVelocity(body,{0,0,0});
        b3Body_SetAngularVelocity(body,{0,0,0});
        b3Body_SetAwake(body,true);
    }
    ++*entry.revision;
    grip->setTarget(mouth);
    return grip;
}
} // namespace viewer
