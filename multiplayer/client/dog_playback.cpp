#include "dog_playback.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
namespace multiplayer {
void DogPlayback::push(std::uint64_t tick, const std::vector<DogState>& states, double receivedSeconds) {
    if (states.size() > maxDogUpdatesPerSnapshot || !std::isfinite(receivedSeconds)) throw std::runtime_error("Invalid dog updates");
    for (const auto& state : states) {
        if (state.id >= viewer::maxDogs || state.asset >= 3) throw std::runtime_error("Invalid dog identity");
        if (replicas_.size() <= state.id) replicas_.resize(state.id+1);
        auto& replica = replicas_[state.id];
        if (replica.latest && tick <= replica.latest->tick) continue;
        replica.previous = replica.latest;
        replica.latest = Sample{tick,state}; replica.received = receivedSeconds;
    }
}
void DogPlayback::apply(viewer::DogLayer& dogs, double serverTick, double now) const {
    for (std::size_t id = 0; id < dogs.instances.size(); ++id) {
        auto& dog = dogs.instances[id];
        if (id >= replicas_.size() || !replicas_[id].latest || now-replicas_[id].received > 5) { dog.removed = true; continue; }
        const auto& replica = replicas_[id]; const auto& latest = *replica.latest;
        const auto& state = latest.state;
        if (state.asset != dog.asset) throw std::runtime_error("Dog appearance does not match session assets");
        auto feet = state.feet; float yaw = state.yaw;
        double walk = state.walkSeconds, attack = state.attackSeconds;
        // Render 100 ms behind the server. Interpolate known motion, then
        // extrapolate for at most 250 ms. Death and attack epochs never blend.
        const double renderTick = serverTick-6;
        if (replica.previous && !state.dead && !state.attacking && !replica.previous->state.dead && !replica.previous->state.attacking) {
            const auto& previous = *replica.previous;
            const double ticks = double(latest.tick-previous.tick);
            const float fraction = static_cast<float>(std::clamp((renderTick-double(previous.tick))/ticks,0.,1.+std::min(15.,ticks)/ticks));
            auto velocity = (state.feet-previous.state.feet)/static_cast<float>(ticks/60);
            const float speed = glm::length(velocity);
            if (speed > 6) velocity *= 6/speed;
            if (fraction <= 1) feet = glm::mix(previous.state.feet,state.feet,fraction);
            else if (state.walking) feet += velocity*static_cast<float>(std::min(.25,(renderTick-double(latest.tick))/60));
            yaw = previous.state.yaw + std::remainder(state.yaw-previous.state.yaw,6.28318530718f)*std::min(fraction,1.f);
            if (state.walking && state.walkSeconds >= previous.state.walkSeconds)
                walk = previous.state.walkSeconds+(state.walkSeconds-previous.state.walkSeconds)*fraction;
        }
        if (state.attacking) attack += std::clamp((serverTick-double(latest.tick))/60,0.,.25);
        dog.removed = false; dog.health = state.health; dog.dead = state.dead;
        dog.walking = state.walking; dog.running = state.running; dog.attacking = state.attacking;
        dog.phase = state.phase; dog.walkSeconds = walk; dog.attackSeconds = attack;
        dog.root = glm::translate(glm::mat4(1),feet)*glm::rotate(glm::mat4(1),yaw,{0,1,0});
        dog.transform = dog.root;
        if (dog.dead) dog.transform = dog.root*glm::translate(glm::mat4(1),{0,.18f,0})*glm::rotate(glm::mat4(1),1.57079633f,{0,0,1});
    }
}
}
