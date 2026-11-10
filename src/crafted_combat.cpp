#include "crafted_combat.hpp"
#include "crafted_melee_geometry.hpp"
#include "collision_world.hpp"
#include "dog_hit_query.hpp"
#include "zombie_melee_query.hpp"
#include "zombie_layer.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
enum class ContactType { World, Zombie, Dog };
struct CraftedContact {
    ContactType type;
    float fraction;
    glm::vec3 point,direction;
    std::size_t id=0;
    std::optional<ZombieHitClaim> body;
};
glm::vec3 direction(const MeleeSweep& sweep,glm::vec3 fallback) {
    glm::vec3 movement(0);
    for (int point=0;point<sweep.count;++point) movement+=sweep.to[point]-sweep.from[point];
    return glm::length(movement)>1e-6f ? glm::normalize(movement) : fallback;
}
}
void CraftedCombat::update(SwordMelee& swing,const Item& item,const Camera& camera,double seconds,
        double animationSeconds,const CollisionWorld& world,ZombieLayer* zombies,DogLayer* dogs) {
    if (!std::isfinite(seconds) || !std::isfinite(animationSeconds) || item.grips().empty() ||
        item.inventory().model.parts.empty()) return;
    if (!std::isfinite(swing.startedAt())) { startedAt_=swing.startedAt(); return; }
    if (startedAt_!=swing.startedAt()) {
        startedAt_=swing.startedAt(); sampled_=swordWindupSeconds; stopped_=false;
    }
    if (stopped_) return;
    const float end=std::clamp(static_cast<float>(seconds-startedAt_),swordWindupSeconds,swordWindupSeconds+swordCutSeconds);
    if (end<=sampled_) return;
    const auto parts=craftedMeleeParts(item);
    float reach=1.2f;
    for (const auto& part:parts) for (const auto point:part) reach=std::max(reach,glm::length(point)+1.2f);
    std::vector<std::size_t> candidates;
    if (zombies) zombies->queryLivingNear(camera.position(),reach,candidates);
    ZombieMeleeQuery zombieContacts(zombies,candidates,animationSeconds);
    DogMeleeQuery dogContacts(dogs,camera.position(),reach,animationSeconds);
    std::vector<MeleeCollider> solids;
    const auto minimum=camera.position()-glm::vec3(reach),maximum=camera.position()+glm::vec3(reach);
    for (const auto& face:world.staticFaces(minimum,maximum))
        solids.push_back(meleeCollider(face.vertices.data(),3,0,glm::dmat4(1)));
    for (const auto& [id,trunk]:world.staticTrunks(minimum,maximum)) {
        (void)id;
        const std::array<glm::vec3,2> points{{trunk.base+glm::vec3(0,trunk.radius,0),
            trunk.base+glm::vec3(0,trunk.height-trunk.radius,0)}};
        solids.push_back(meleeCollider(points.data(),2,trunk.radius,glm::dmat4(1)));
    }
    const auto eyeToWorld=glm::inverse(camera.view());
    const auto frame=[&](float elapsed) {
        auto pose=swing.pose(startedAt_); pose.elapsed=elapsed;
        return eyeToWorld*swordTransform(pose);
    };
    auto previous=frame(sampled_);
    while (sampled_<end) {
        const float next=std::min(end,sampled_+1.f/360);
        const auto current=frame(next);
        std::optional<CraftedContact> first;
        const auto accept=[&](CraftedContact candidate) {
            if (!first || candidate.fraction<first->fraction ||
                (candidate.fraction==first->fraction && candidate.type<first->type)) first=std::move(candidate);
        };
        for (const auto& part:parts) {
            const auto shape=meleeSweep(part.data(),static_cast<int>(part.size()),previous,current);
            const auto motion=direction(shape,camera.direction());
            for (const auto& solid:solids) if (const auto hit=castMeleeShape(shape,solid))
                accept({ContactType::World,hit->fraction,hit->point,motion,0,{}});
            for (const auto& hit:zombieContacts.cast(shape,false)) {
                if (!world.lineOfSight(camera.position(),hit.hit.point) ||
                    !world.lineOfSight(hit.hit.point-motion*.02f,hit.hit.point)) continue;
                accept({ContactType::Zombie,hit.hit.fraction,hit.hit.point,motion,hit.id,hit.body});
            }
            for (const auto& hit:dogContacts.cast(shape)) {
                if (!world.lineOfSight(camera.position(),hit.hit.point) ||
                    !world.lineOfSight(hit.hit.point-motion*.02f,hit.hit.point)) continue;
                accept({ContactType::Dog,hit.hit.fraction,hit.hit.point,motion,hit.dog,{}});
            }
        }
        if (first) {
            const auto& hit=*first;
            if (hit.type==ContactType::Zombie && hit.body && !zombies->instances[hit.id].ragdoll)
                zombies->applyReportedShot(*hit.body,hit.point-hit.direction*.02f,hit.direction,animationSeconds,world,ShotCaliber::high);
            else if (hit.type==ContactType::Dog) dogs->damage(hit.id,.2f);
            sampled_=glm::mix(sampled_,next,hit.fraction);
            swing.stopAt(sampled_); stopped_=true;
            return;
        }
        previous=current; sampled_=next;
    }
}
}
