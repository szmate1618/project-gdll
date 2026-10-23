#include "zombie_layer.hpp"

namespace viewer {
void ZombieLayer::emitBloodParticles(const ZombieShotHit& hit, glm::vec3 point, glm::vec3 direction,
                                     double animationSeconds, const CollisionWorld* occlusion) {
    BloodFloor floor;
    if (occlusion) {
        if (const auto ground = occlusion->staticGroundAt(point.x,point.z,point.y-20,point.y+.02f))
            floor = {{point.x,ground->height,point.z},ground->normal,true};
    }
    const auto& instance = instances[hit.zombie];
    auto spray = -direction;
    BloodAttachment attachment;
    if (hit.joint>=0) {
        const auto& asset = assets[instance.asset];
        const auto bone = dismembermentAssets[instance.asset].joints[std::size_t(hit.joint)].bone;
        const auto frame = instance.transform*instance.ragdollBones[std::size_t(bone)]*
                           glm::inverse(asset.skeleton[std::size_t(bone)].inverseBind);
        spray = point-glm::vec3(instance.restRoot*glm::vec4(0,.9f,0,1));
        if (glm::length(spray)<.001f) spray = -direction;
        attachment = {hit.zombie,bone,glm::vec3(glm::inverse(frame)*glm::vec4(point,1)),
                      glm::inverse(glm::mat3(frame))*spray};
    }
    bloodParticles_.hit(point,spray,animationSeconds,floor,attachment);
}

void ZombieLayer::updateBloodEffects(double seconds) {
    for (std::size_t i=0; i<BloodParticles::emitterCapacity; ++i) {
        const auto& e = bloodParticles_.emitters()[i];
        if (e.end<=seconds) continue;
        const auto& a = e.attachment;
        if (a.zombie>=instances.size() || instances[a.zombie].removed || !instances[a.zombie].ragdoll) {
            bloodParticles_.stopEmitter(i);
            continue;
        }
        const auto& instance = instances[a.zombie];
        const auto& asset = assets[instance.asset];
        const auto frame = instance.transform*instance.ragdollBones[std::size_t(a.bone)]*
                           glm::inverse(asset.skeleton[std::size_t(a.bone)].inverseBind);
        bloodParticles_.moveEmitter(i,glm::vec3(frame*glm::vec4(a.point,1)),glm::mat3(frame)*a.direction);
    }
    bloodParticles_.update(seconds);
}
} // namespace viewer
