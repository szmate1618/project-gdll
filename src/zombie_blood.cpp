#include "zombie_layer.hpp"

namespace viewer {
void ZombieLayer::emitBloodParticles(const ZombieShotHit& hit, glm::vec3 point, glm::vec3 direction,
                                     double animationSeconds, const CollisionWorld* occlusion) {
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
    const BloodSpurtStart start{0,point,spray,0,attachment};
    bloodSpurts_.emit(start,animationSeconds);
    if (bloodParticlesEnabled_) startBloodSpurt(start,animationSeconds,occlusion);
}

void ZombieLayer::startBloodSpurt(const BloodSpurtStart& start, double seconds, const CollisionWorld* world) {
    BloodFloor floor;
    if (world) {
        const auto point = start.position;
        if (const auto ground = world->staticGroundAt(point.x,point.z,point.y-20,point.y+.02f))
            floor = {{point.x,ground->height,point.z},ground->normal,true};
    }
    auto attachment = start.attachment;
    if (attachment.zombie>=instances.size() || attachment.bone<0 ||
        instances[attachment.zombie].asset>=assets.size() ||
        std::size_t(attachment.bone)>=assets[instances[attachment.zombie].asset].skeleton.size())
        attachment.bone = -1;
    bloodParticles_.hit(start.position,start.direction,seconds,floor,attachment);
}

void ZombieLayer::updateBloodEffects(double seconds) {
    for (std::size_t i=0; i<BloodParticles::emitterCapacity; ++i) {
        const auto& e = bloodParticles_.emitters()[i];
        if (e.end<=seconds) continue;
        const auto& a = e.attachment;
        if (a.zombie>=instances.size() || a.bone<0) {
            bloodParticles_.stopEmitter(i);
            continue;
        }
        if (instances[a.zombie].removed || !instances[a.zombie].ragdoll ||
            std::size_t(a.bone)>=instances[a.zombie].ragdollBones.size()) {
            // A start can beat the separate corpse-pose lane. Emit at its
            // impact until the first palette arrives; later removal stops it.
            if (e.attachmentResolved) bloodParticles_.stopEmitter(i);
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
