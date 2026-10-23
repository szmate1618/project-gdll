#include "snapshots.hpp"
#include <algorithm>
#include <array>
#include <unordered_map>
#include <stdexcept>
#include <glm/gtc/quaternion.hpp>

namespace multiplayer {
bool Snapshots::push(Snapshot snapshot, double receivedSeconds) {
    if (!frames_.empty() && snapshot.tick <= frames_.back().tick) return false;
    frames_.push_back(std::move(snapshot));
    if (frames_.size() > 32) frames_.pop_front();
    received_ = receivedSeconds;
    return true;
}
void Snapshots::pushPose(const Scene& scene, const CorpsePose& pose) {
    if (pose.id >= scene.zombies.instances.size() || pose.asset >= scene.zombies.assets.size() ||
        pose.asset != scene.zombies.instances[pose.id].asset || !pose.version ||
        pose.bones.size() != scene.zombies.assets[pose.asset].skeleton.size())
        throw std::runtime_error("Corpse pose does not match session assets");
    if (pose.severedJoint >= 0 && (pose.asset >= scene.zombies.dismembermentAssets.size() ||
        std::size_t(pose.severedJoint) >= scene.zombies.dismembermentAssets[pose.asset].meshes.size()))
        throw std::runtime_error("Corpse separation does not match session assets");
    auto& cached = poses_.at(pose.id);
    if (!cached || cached->version < pose.version) cached = pose;
}
std::vector<viewer::GrenadeExplosion> Snapshots::explosions(double now) const {
    std::vector<viewer::GrenadeExplosion> result;
    if (frames_.empty()) return result;
    for (auto explosion : frames_.back().explosions) {
        explosion.age += static_cast<float>(std::max(0.0, now - received_));
        result.push_back(explosion);
    }
    return result;
}
std::vector<viewer::MuzzleFlashLight> Snapshots::explosionLights(double now) const {
    std::vector<viewer::MuzzleFlashLight> result;
    for (const auto& explosion : explosions(now)) {
        const auto light = viewer::grenadeExplosionLight(explosion);
        if (light.color.r > 0) result.push_back(light);
    }
    return result;
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
    grenades_ = b.grenades;
    for (auto& grenade : grenades_) for (const auto& previous : a.grenades) if (previous.id == grenade.id) {
        grenade.center = glm::mix(previous.center, grenade.center, t);
        grenade.rotation = glm::slerp(previous.rotation, grenade.rotation, t);
        break;
    }
    // Explosion lights use the latest confirmed event. Remove its body on the
    // same frame even if pose interpolation is still behind the detonation.
    const auto& explosions = frames_.back().explosions;
    grenades_.erase(std::remove_if(grenades_.begin(), grenades_.end(), [&](const auto& grenade) {
        return std::any_of(explosions.begin(), explosions.end(), [&](const auto& e) { return e.id == grenade.id; });
    }), grenades_.end());
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
    scene.zombies.setReplicatedTorsoContents(b.torsoContents);
    for (std::size_t id = 0; id < scene.zombies.instances.size(); ++id) {
        auto& instance = scene.zombies.instances[id];
        const auto* z = current[id];
        if (!z) {
            if (!instance.removed) { instance.removed = true; scene.zombies.markRenderChanged(id); }
            continue;
        }
        if (z->asset != instance.asset || z->asset >= scene.zombies.assets.size()) throw std::runtime_error("Zombie asset mismatch");
        instance.removed = false;
        scene.zombies.setTorsoWoundMask(id, z->torsoHoles);
        instance.transform = z->transform; instance.restRoot = z->root;
        instance.phase = z->phase; instance.walkSeconds = z->walkSeconds;
        instance.walking = z->walking; instance.running = z->running; instance.ragdoll = z->dead;
        instance.displayedAnimationFrame = -1;
        const auto* before = previous[id];
        if (before && !before->dead && !z->dead) {
            // Root motion is rigid. Keep the asset normalization (including its
            // translation, reflection or shear) outside quaternion interpolation.
            const auto rotation = glm::slerp(glm::normalize(glm::quat_cast(glm::mat3(before->root))),
                glm::normalize(glm::quat_cast(glm::mat3(z->root))), t);
            auto root = glm::mat4_cast(rotation);
            root[3] = glm::mix(before->root[3], z->root[3], t);
            instance.restRoot = root;
            instance.transform = root * glm::inverse(z->root) * z->transform;
            instance.walkSeconds = glm::mix(before->walkSeconds, z->walkSeconds, t);
        }
        if (z->dead) {
            const auto& pose = poses_[id];
            // Until its first complete pose arrives a corpse stays hidden. Never
            // pair a newer root with an older palette or blend affine bone
            // matrices, which may legitimately contain baked scale and shear.
            if (!pose) instance.removed = true;
            else {
                instance.transform = pose->transform; instance.restRoot = pose->root;
                if (appliedPoseVersions_[id] != pose->version) {
                    instance.severedJoint = pose->severedJoint;
                    scene.zombies.setRagdollPalette(id, pose->bones);
                    appliedPoseVersions_[id] = pose->version;
                }
            }
        }
        scene.zombies.markRenderChanged(id);
    }
    for (const auto& z : b.zombies) if (z.id >= scene.zombies.instances.size()) throw std::runtime_error("Unknown zombie ID");
}
} // namespace multiplayer
