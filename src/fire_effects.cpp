#include "fire_effects.hpp"
#include "fire_surfaces.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "dog_layer.hpp"
#include "door_layer.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
std::array<glm::vec3,molotovRayCount> molotovRayDirections(glm::vec3 direction, std::uint32_t seed) {
    const auto length = glm::length(direction);
    direction = std::isfinite(length) && length>.001f ? direction/length : glm::vec3(0,-1,0);
    const auto right = glm::normalize(glm::cross(direction,std::abs(direction.y)>.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0)));
    const auto up = glm::cross(right,direction);
    const auto random = [&] { seed = seed*1664525u+1013904223u; return float(seed>>8)/16777216.f; };
    std::array<glm::vec3,molotovRayCount> result;
    for (auto& ray : result) {
        // Uniform solid angle in a 60-degree full cone, not a flat disk.
        const float cosine = 1-random()*(1-std::cos(glm::radians(30.f)));
        const float sine = std::sqrt(std::max(0.f,1-cosine*cosine));
        const float angle = random()*6.28318530718f;
        ray = direction*cosine+(right*std::cos(angle)+up*std::sin(angle))*sine;
    }
    return result;
}
void FireEffects::ignite(const MolotovImpact& impact, double seconds, const FireRaycast& raycast) {
    if (!raycast || !std::isfinite(seconds) || !std::isfinite(glm::length(impact.position)) ||
        !std::isfinite(glm::length(impact.direction)) || glm::length(impact.direction)<.001f) return;
    const auto incoming = glm::normalize(impact.direction);
    // Contact events lie on the surface; start 2 cm back to avoid inside hits.
    const auto origin = impact.position-incoming*.02f;
    std::uint32_t ray = 0;
    for (const auto direction : molotovRayDirections(incoming,impact.id)) {
        const auto hit = raycast(origin,direction,molotovRayReach);
        ++ray;
        if (!hit || !std::isfinite(glm::length(hit->point)) ||
            glm::distance(origin,hit->point)>molotovRayReach+.0001f) continue;
        patches_[slot_] = {*hit,seconds,impact.id*1664525u+ray*1013904223u,true};
        slot_ = (slot_+1)%capacity;
    }
}
void FireEffects::receive(const std::vector<MolotovImpact>& impacts, double snapshotSeconds,
                          double presentationSeconds, double now, const FireScene& scene) {
    std::array<std::uint32_t,historyCapacity> retained{};
    std::size_t count = 0;
    for (const auto& impact : impacts) {
        const double age = presentationSeconds-(snapshotSeconds-impact.age);
        if (!impact.id || age<0 || age>=molotovFireSeconds || count==retained.size()) continue;
        retained[count++] = impact.id;
        if (std::find(seen_.begin(),seen_.end(),impact.id)!=seen_.end()) continue;
        FireSurfaceQuery query(scene,impact.position);
        ignite(impact,now-age,[&](auto origin,auto direction,float reach) { return query.cast(origin,direction,reach); });
    }
    seen_ = retained;
    update(now,scene);
}
bool FireEffects::hasEffects(double seconds) const {
    for (const auto& patch : patches_)
        if (patch.active && seconds>=patch.born && seconds<patch.born+molotovFireSeconds) return true;
    return false;
}
void FireEffects::update(double seconds, const FireScene& scene) {
    for (auto& patch : patches_) {
        if (!patch.active) continue;
        if (seconds>=patch.born+molotovFireSeconds) { patch = {}; continue; }
        auto& c = patch.contact;
        switch (c.attachment) {
        case FireAttachment::world: break;
        case FireAttachment::body:
            if (c.body && c.body->valid()) c.point = c.body->pointState(c.localPoint).point;
            else patch = {};
            break;
        case FireAttachment::door:
            if (scene.doors && c.actor<scene.doors->count()) c.point = scene.doors->pointState(c.actor,c.localPoint).point;
            else patch = {};
            break;
        case FireAttachment::zombie: {
            if (!scene.zombies || c.actor>=scene.zombies->instances.size()) { patch = {}; break; }
            const auto& actor = scene.zombies->instances[c.actor];
            if (actor.removed || actor.asset>=scene.zombies->assets.size()) { patch = {}; break; }
            auto frame = actor.restRoot;
            if (c.bone>=0) {
                const auto pose = sampleZombiePose(scene.zombies->assets[actor.asset],actor,scene.animationSeconds);
                if (std::size_t(c.bone)>=pose.size()) { patch = {}; break; }
                frame = actor.transform*pose[std::size_t(c.bone)];
            }
            c.point = glm::vec3(frame*glm::vec4(c.localPoint,1));
            break;
        }
        case FireAttachment::dog:
            if (scene.dogs && c.actor<scene.dogs->instances.size() && !scene.dogs->instances[c.actor].removed)
                c.point = glm::vec3(scene.dogs->instances[c.actor].transform*glm::vec4(c.localPoint,1));
            else patch = {};
            break;
        }
    }
}
} // namespace viewer
