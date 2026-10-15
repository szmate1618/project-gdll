#include "snapshots.hpp"
#include <algorithm>
#include <array>
#include <unordered_map>
#include <stdexcept>

namespace multiplayer {
bool Snapshots::push(Snapshot snapshot, double receivedSeconds) {
    if (!frames_.empty() && snapshot.tick <= frames_.back().tick) return false;
    // Materialize before acknowledging: skipped display frames must still retain
    // palettes that subsequent snapshots are allowed to omit.
    for (auto& z : snapshot.zombies) if (z.dead) {
        auto& cached = palettes_.at(z.id);
        if (!z.bones.empty()) cached = z.bones;
        else z.bones = cached;
    }
    frames_.push_back(std::move(snapshot));
    if (frames_.size() > 32) frames_.pop_front();
    received_ = receivedSeconds;
    return true;
}
void Snapshots::apply(Scene& scene, double now) {
    if (frames_.empty()) return;
    const double latestTick = static_cast<double>(frames_.back().tick);
    // Render six ticks behind incoming state; freeze at the latest state on starvation.
    renderTick_ = std::max(renderTick_, std::clamp(latestTick + (now - received_) * 60 - 6,
        static_cast<double>(frames_.front().tick), latestTick));
    while (frames_.size() > 2 && frames_[1].tick <= renderTick_) frames_.pop_front();
    const auto& a = frames_.front();
    const auto& b = frames_.size() > 1 ? frames_[1] : a;
    const float t = b.tick == a.tick ? 1.0f : static_cast<float>(std::clamp(
        (renderTick_ - static_cast<double>(a.tick)) / static_cast<double>(b.tick - a.tick), 0.0, 1.0));
    players_ = b.players;
    for (auto& p : players_) for (const auto& before : a.players) if (before.id == p.id) {
        p.movement.feet = glm::mix(before.movement.feet, p.movement.feet, t); break;
    }
    std::unordered_map<std::uint32_t, float> oldAngles;
    for (const auto& d : a.doors) oldAngles.emplace(d.id, d.angle);
    for (const auto& d : b.doors) {
        if (d.id >= scene.doors->count()) throw std::runtime_error("Server supplied unknown door ID");
        float angle = d.angle;
        const auto before = oldAngles.find(d.id);
        if (before != oldAngles.end()) angle = glm::mix(before->second, angle, t);
        scene.doors->setAngle(d.id, angle);
    }
    // Box IDs are dense and never recycled during a session, including late joins.
    std::array<const BoxState*, maxBoxes> oldBoxes{};
    for (const auto& box : a.boxes) oldBoxes.at(box.id) = &box;
    for (const auto& box : b.boxes) {
        if (box.id > scene.boxes->count()) throw std::runtime_error("Server supplied non-dense box IDs");
        if (box.id == scene.boxes->count()) scene.boxes->add(box.center, box.halfExtents * 2.0f);
        auto center = box.center; auto rotation = box.rotation;
        if (const auto* before = oldBoxes.at(box.id)) {
            center = glm::mix(before->center, center, t); rotation = glm::slerp(before->rotation, rotation, t);
        }
        scene.boxes->setPose(box.id, center, rotation);
    }
    std::array<const ZombieState*, maxZombies> previous{}, current{};
    for (const auto& z : a.zombies) previous.at(z.id) = &z;
    for (const auto& z : b.zombies) current.at(z.id) = &z;
    for (std::size_t id = 0; id < scene.zombies.instances.size(); ++id) {
        auto& instance = scene.zombies.instances[id];
        const auto* z = current[id];
        if (!z) {
            if (!instance.removed) { instance.removed = true; scene.zombies.markRenderChanged(id); }
            continue;
        }
        if (z->asset != instance.asset || z->asset >= scene.zombies.assets.size()) throw std::runtime_error("Zombie asset mismatch");
        instance.removed = false;
        instance.transform = z->transform; instance.restRoot = z->root;
        instance.phase = z->phase; instance.walkSeconds = z->walkSeconds;
        instance.walking = z->walking; instance.running = z->running; instance.ragdoll = z->dead;
        instance.displayedAnimationFrame = -1;
        const auto* before = previous[id];
        if (before && before->dead == z->dead) {
            for (int c = 0; c < 4; ++c) instance.transform[c] = glm::mix(before->transform[c], z->transform[c], t);
            instance.walkSeconds = glm::mix(before->walkSeconds, z->walkSeconds, t);
        }
        if (z->dead) {
            auto bones = z->bones;
            if (before && before->bones.size() == bones.size())
                for (std::size_t i = 0; i < bones.size(); ++i)
                    for (int c = 0; c < 4; ++c) bones[i][c] = glm::mix(before->bones[i][c], bones[i][c], t);
            scene.zombies.setRagdollPalette(id, std::move(bones));
        }
        scene.zombies.markRenderChanged(id);
    }
    for (const auto& z : b.zombies) if (z.id >= scene.zombies.instances.size()) throw std::runtime_error("Unknown zombie ID");
}
} // namespace multiplayer
