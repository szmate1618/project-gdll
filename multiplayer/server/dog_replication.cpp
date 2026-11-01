#include "dog_replication.hpp"
#include <algorithm>
#include <cmath>
namespace multiplayer {
void DogReplication::append(const viewer::DogLayer& dogs, glm::vec3 observer, std::uint64_t tick, std::vector<DogState>& samples) {
    samples.clear();
    sent_.resize(dogs.instances.size());
    interests_.clear(); pending_.clear();
    for (std::size_t id = 0; id < dogs.instances.size(); ++id) {
        const auto& dog = dogs.instances[id];
        const float distance = glm::length(glm::vec3(dog.root[3])-observer);
        if (dog.removed || distance > 240) { sent_[id].initialized = sent_[id].interested = sent_[id].setupSent = false; continue; }
        sent_[id].interested = true;
        interests_.push_back({id,distance});
    }
    std::sort(interests_.begin(),interests_.end(),[](const auto& a,const auto& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    for (std::size_t rank = 0; rank < interests_.size(); ++rank) {
        const auto [id,distance] = interests_[rank];
        const auto& dog = dogs.instances[id]; const auto& sent = sent_[id];
        const std::uint64_t period = rank < 128 && distance <= 30 ? 3 : rank < 512 && distance <= 100 ? 12 : 60;
        const auto mode = static_cast<std::uint8_t>(dog.walking | (dog.running<<1) | (dog.attacking<<2) | (dog.dead<<3));
        if (sent.initialized && sent.tick == tick) continue;
        const bool urgent = !sent.initialized || sent.mode != mode || sent.health != dog.health;
        if (urgent || tick-sent.tick >= period) pending_.push_back({id,urgent ? 0 : sent.tick+period,rank,mode});
    }
    // Unsent identities/changed states first, then oldest deadlines. A crowded
    // near band cannot indefinitely starve the remaining stable dog IDs.
    std::sort(pending_.begin(),pending_.end(),[&](const auto& a,const auto& b) {
        if (a.deadline != b.deadline) return a.deadline < b.deadline;
        if (!a.deadline && sent_[a.id].tick != sent_[b.id].tick) return sent_[a.id].tick < sent_[b.id].tick;
        return a.rank < b.rank;
    });
    for (const auto& pending : pending_) {
        if (samples.size() == maxDogUpdatesPerSnapshot) break;
        const auto id = pending.id; const auto& dog = dogs.instances[id];
        samples.push_back({static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(dog.asset),glm::vec3(dog.root[3]),
            std::atan2(dog.root[2].x,dog.root[2].z),dog.phase,static_cast<float>(dog.walkSeconds),
            static_cast<float>(dog.attackSeconds),dog.health,dog.walking,dog.running,dog.attacking,dog.dead});
        auto& sent = sent_[id];
        sent.tick = tick; sent.mode = pending.mode; sent.health = dog.health; sent.initialized = true;
    }
}
std::optional<DogFrame> DogReplication::nextFrame(const viewer::DogLayer& dogs, std::uint64_t tick) const {
    DogFrame frame; frame.tick = tick;
    for (std::size_t id = 0; id < sent_.size(); ++id) {
        if (!sent_[id].interested || sent_[id].setupSent) continue;
        const auto& dog = dogs.instances.at(id);
        frame.dogs.push_back({static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(dog.asset),glm::vec3(dog.root[3]),
            std::atan2(dog.root[2].x,dog.root[2].z),dog.phase,static_cast<float>(dog.walkSeconds),
            static_cast<float>(dog.attackSeconds),dog.health,dog.walking,dog.running,dog.attacking,dog.dead});
        if (frame.dogs.size() == maxDogUpdatesPerSnapshot) break;
    }
    return frame.dogs.empty() ? std::nullopt : std::optional<DogFrame>(std::move(frame));
}
void DogReplication::frameSent(const DogFrame& frame) {
    for (const auto& dog : frame.dogs) sent_.at(dog.id).setupSent = true;
}

}
