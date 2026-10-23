#include "blood_particles.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
float BloodParticles::random() {
    seed_ = seed_*1664525u+1013904223u;
    return float(seed_>>8)*(1.0f/16777216);
}
void BloodParticles::spawn(glm::vec3 point, glm::vec3 direction, double seconds, const BloodFloor& floor) {
    for (std::size_t n=0; n<particleCapacity; ++n) {
        auto& p = particles_[particleSlot_];
        particleSlot_ = (particleSlot_+1)%particleCapacity;
        if (p.lifetime>0) continue; // Saturation drops new droplets, never allocates.
        const glm::vec3 scatter(random()-.5f,random()-.5f,random()-.5f);
        p = {point,direction*(1.5f+2*random())+scatter*2.5f+glm::vec3(0,.7f,0),
             seconds,1.5f,-1,.025f+.02f*random(),random()*6.2831853f,floor};
        if (floor.valid) {
            const float separation = glm::dot(point-floor.point,floor.normal);
            const float height = std::max(.002f,separation);
            p.start += floor.normal*(height-separation);
            const float speed = glm::dot(p.velocity,floor.normal);
            const float acceleration = gravity*floor.normal.y;
            p.landing = (speed+std::sqrt(speed*speed+2*acceleration*height))/acceleration;
            p.lifetime = std::min(p.lifetime,p.landing);
        }
        return;
    }
}
void BloodParticles::hit(glm::vec3 point, glm::vec3 direction, double seconds, const BloodFloor& floor,
                         const BloodAttachment& attachment) {
    if (!std::isfinite(seconds) || !std::isfinite(glm::length(point)) || !std::isfinite(glm::length(direction))) return;
    direction = glm::length(direction)>.001f ? glm::normalize(direction) : glm::vec3(0,1,0);
    // Only upward-facing, finite static planes can receive stains.
    auto surface = floor;
    surface.valid = floor.valid && std::isfinite(glm::length(floor.point)) &&
                    std::isfinite(glm::length(floor.normal)) && floor.normal.y>.3f;
    if (surface.valid) surface.normal = glm::normalize(surface.normal);
    update(seconds);
    for (int i=0; i<10; ++i) spawn(point,direction,seconds,surface);
    if (attachment.bone<0) return;
    for (auto& emitter : emitters_) if (emitter.end<=seconds) {
        emitter = {attachment,point,direction,surface,seconds+1.0/60,seconds+1};
        break;
    }
}
void BloodParticles::moveEmitter(std::size_t slot, glm::vec3 point, glm::vec3 direction) {
    auto& emitter = emitters_.at(slot);
    emitter.point = point;
    emitter.direction = glm::length(direction)>.001f ? glm::normalize(direction) : glm::vec3(0,1,0);
}
bool BloodParticles::hasEffects(double seconds) const {
    for (const auto& p : particles_)
        if (p.lifetime>0 && seconds>=p.born && seconds<p.born+p.lifetime) return true;
    for (const auto& s : stains_)
        if (s.size>0 && seconds>=s.born && seconds<s.born+stainDuration) return true;
    return false;
}
void BloodParticles::update(double seconds) {
    if (!std::isfinite(seconds)) return;
    for (auto& p : particles_) {
        if (p.lifetime<=0 || seconds<p.born+p.lifetime) continue;
        if (p.floor.valid && p.landing>=0 && p.landing<=p.lifetime && seconds<p.born+p.landing+stainDuration) {
            const auto point = p.start+p.velocity*p.landing-glm::vec3(0,.5f*gravity*p.landing*p.landing,0);
            stains_[stainSlot_] = {point+p.floor.normal*.008f,p.floor.normal,p.born+p.landing,
                                  p.size*(3+3*random()),p.angle};
            stainSlot_ = (stainSlot_+1)%stainCapacity;
        }
        p.lifetime = 0;
    }
    for (auto& e : emitters_) {
        // Bound catch-up after stalls, and never replay old emission positions.
        const double until = std::min(seconds,e.end);
        if (e.end<=0 || e.next>until) continue;
        e.next = std::max(e.next,seconds-.1);
        for (; e.next<=until; e.next+=1.0/60) spawn(e.point,e.direction,e.next,e.floor);
    }
}
} // namespace viewer
