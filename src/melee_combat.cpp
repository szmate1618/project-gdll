#include "melee_combat.hpp"
#include "melee_contact_mesh.hpp"
#include "player_body.hpp"
#include "physics_world.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "zombie_bone_collision.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace viewer {
namespace {
struct ZombieMesh {
    std::size_t id = 0;
    const AnimatedModel* asset = nullptr;
    glm::mat4 transform{1};
    float animationFrame = 0;
    std::size_t firstFrame = 0;
    bool ragdoll = false;
    std::uint64_t poseRevision = 0;
    std::unique_ptr<MeleeContactMesh> surface;
};
constexpr std::array<PlayerBodyPart, 6> arms{PlayerBodyPart::leftUpperArm, PlayerBodyPart::leftLowerArm,
    PlayerBodyPart::leftFist, PlayerBodyPart::rightUpperArm, PlayerBodyPart::rightLowerArm, PlayerBodyPart::rightFist};
}
struct MeleeCombat::Impl {
    struct BoneRig {
        const AnimatedModel* asset = nullptr;
        std::uint64_t generation = 0;
        std::unique_ptr<ZombieBoneContacts> contacts;
    };
    std::shared_ptr<PhysicsWorld> world;
    std::unordered_map<const AnimatedModel*, std::shared_ptr<const ZombieBoneCollisionAsset>> boneAssets;
    std::map<std::size_t, BoneRig> boneRigs;
    std::uint64_t generation = 0;
    std::vector<std::unique_ptr<ZombieMesh>> meshes;
    std::vector<std::size_t> nearby;
    std::vector<b3ContactData> contacts;
};
MeleeCombat::MeleeCombat(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Melee combat requires a shared physics world");
    impl_->world = std::move(world);
}
MeleeCombat::~MeleeCombat() = default;
void MeleeCombat::prepare(PlayerBody& player, ZombieLayer* zombies, double seconds) {
    auto* body = &player;
    prepare(&body, 1, zombies, seconds);
}
void MeleeCombat::prepare(PlayerBody* const* players, std::size_t count, ZombieLayer* zombies, double seconds) {
    auto& d = *impl_;
    auto previous = std::move(d.meshes);
    d.meshes.clear();
    if (!zombies) { d.boneRigs.clear(); return; }
    ++d.generation;
    d.nearby.clear();
    std::vector<std::size_t> nearby;
    for (std::size_t p=0; p<count; ++p) if (players[p]->enabled()) {
        const auto center = glm::vec3(players[p]->transform(PlayerBodyPart::hip)[3]);
        zombies->queryMeleeNear(center, 3, nearby);
        d.nearby.insert(d.nearby.end(), nearby.begin(), nearby.end());
    }
    std::sort(d.nearby.begin(),d.nearby.end());
    d.nearby.erase(std::unique(d.nearby.begin(),d.nearby.end()),d.nearby.end());
    for (const auto id : d.nearby) {
        // Catch up only nearby rigs before positioning their bone surfaces,
        // including when one render frame advances several physics steps.
        if (zombies->instances[id].springRagdoll) zombies->synchronizePhysics(id);
        const auto& instance = zombies->instances[id];
        if (instance.removed || (instance.ragdoll && !instance.springRagdoll) || instance.asset >= zombies->assets.size()) continue;
        const auto& asset = zombies->assets[instance.asset];
        if (ZombieBoneCollisionAsset::supported(asset)) {
            auto& rig = d.boneRigs[id];
            if (rig.asset != &asset) {
                auto& geometry = d.boneAssets[&asset];
                if (!geometry) geometry = std::make_shared<ZombieBoneCollisionAsset>(asset);
                const auto& root = instance.ragdoll ? instance.transform : instance.restTransform;
                rig.contacts = std::make_unique<ZombieBoneContacts>(d.world->id(), geometry, root);
                rig.asset = &asset;
            }
            rig.generation = d.generation;
            rig.contacts->update(asset, instance, seconds);
            continue;
        }
        const bool ragdoll = instance.ragdoll;
        const auto& transform = ragdoll ? instance.transform : instance.restTransform;
        const auto poseRevision = ragdoll ? instance.ragdollPoseVersion : 0;
        const auto* clip = instance.displayedAnimationFrame >= 0 ?
            (instance.displayedAnimationClip == 2 && asset.runClip ? &*asset.runClip :
             instance.displayedAnimationClip == 1 && asset.walkClip ? &*asset.walkClip : nullptr) : zombieAnimationClip(asset, instance);
        const auto count = ragdoll ? 0 : (clip ? clip->frameCount : asset.frameCount);
        const auto firstFrame = ragdoll ? 0 : (clip ? clip->firstFrame : 0);
        const float frame = ragdoll ? 0 : (instance.displayedAnimationFrame >= 0 ? instance.displayedAnimationFrame :
            count ? animatedFrame(clip ? clip->duration : asset.duration, count, clip ? instance.walkSeconds : seconds, instance.phase) : 0);
        const auto first = count ? static_cast<std::size_t>(frame) % count : 0;
        const auto second = count ? (first + 1) % count : 0;
        const float fraction = frame - std::floor(frame);
        const auto origin = glm::vec3(ragdoll ? transform[3] : instance.restRoot[3]);
        bool reused = false;
        for (auto& mesh : previous) {
            if (mesh && mesh->id == id && mesh->asset == &asset && mesh->transform == transform &&
                mesh->animationFrame == frame && mesh->firstFrame == firstFrame &&
                mesh->ragdoll == ragdoll && mesh->poseRevision == poseRevision) {
                d.meshes.push_back(std::move(mesh)); reused = true;
            }
        }
        if (reused) continue; // Preserve contact caches across identical displayed poses.
        std::vector<b3Vec3> vertices;
        std::vector<int32_t> indices;
        for (const auto& draw : asset.model.draws) {
            const auto& primitive = asset.model.primitives.at(draw.primitive);
            const auto base = static_cast<int32_t>(vertices.size());
            const auto* animation = draw.primitive < asset.animation.size() ? &asset.animation[draw.primitive] : nullptr;
            const auto n = primitive.vertices.size();
            for (std::size_t v = 0; v < n; ++v) {
                glm::vec4 p(primitive.vertices[v].position, 1);
                if (ragdoll && animation && !animation->skinning.empty()) {
                    const auto& skin = animation->skinning.at(v);
                    glm::vec4 skinned(0);
                    float weight = 0;
                    for (std::size_t influence = 0; influence < skin.weights.size(); ++influence) {
                        if (skin.weights[influence] == 0) continue;
                        skinned += instance.ragdollBones.at(skin.joints[influence]) * p * skin.weights[influence];
                        weight += skin.weights[influence];
                    }
                    if (weight > 0) p = skinned;
                } else if (animation && count && animation->frames.size() >= (firstFrame + count) * n)
                    p = animation->frames[(firstFrame + first) * n + v].position * (1 - fraction) +
                        animation->frames[(firstFrame + second) * n + v].position * fraction;
                const auto world = glm::vec3(transform * draw.transform * p) - origin;
                vertices.push_back({world.x, world.y, world.z});
            }
            for (const auto v : primitive.indices) indices.push_back(base + static_cast<int32_t>(v));
        }
        if (indices.empty()) continue;
        // Changing a pose updates vertices, bounds, and edge flags on each
        // existing winding; body/shape IDs and triangle topology stay owned.
        const b3Pos meshOrigin{origin.x, origin.y, origin.z};
        for (auto& mesh : previous) {
            if (!mesh || mesh->id != id || mesh->asset != &asset || mesh->ragdoll != ragdoll) continue;
            if (!mesh->surface->update(vertices, meshOrigin)) break;
            mesh->transform = transform; mesh->animationFrame = frame; mesh->firstFrame = firstFrame;
            mesh->poseRevision = poseRevision;
            d.meshes.push_back(std::move(mesh)); reused = true;
        }
        if (reused) {
            // Both windings have identical welding and degeneracy conditions.
            // A partial update must never drop the other side of the surface.
            const auto windings = std::count_if(d.meshes.begin(), d.meshes.end(),
                [&](const auto& mesh) { return mesh->id == id; });
            if (windings == 2) continue;
            d.meshes.erase(std::remove_if(d.meshes.begin(), d.meshes.end(),
                [&](const auto& mesh) { return mesh->id == id; }), d.meshes.end());
        }
        // Two windings give both sides physical contact, including thin clothes.
        for (int side = 0; side < 2; ++side) {
            if (side) for (std::size_t i = 0; i < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
            auto entry = std::make_unique<ZombieMesh>(); entry->id = id;
            entry->asset = &asset; entry->transform = transform;
            entry->animationFrame = frame; entry->firstFrame = firstFrame;
            entry->ragdoll = ragdoll; entry->poseRevision = poseRevision;
            entry->surface = std::make_unique<MeleeContactMesh>(d.world->id(), vertices, indices, meshOrigin);
            d.meshes.push_back(std::move(entry));
        }
    }
    for (auto it = d.boneRigs.begin(); it != d.boneRigs.end();) {
        if (it->second.generation != d.generation) it = d.boneRigs.erase(it);
        else ++it;
    }
}
MeleeContactStats MeleeCombat::contactStats() const {
    MeleeContactStats stats;
    for (const auto& [asset, geometry] : impl_->boneAssets)
        if (geometry) stats.cachedBoneMeshes += geometry->pieces().size();
    for (const auto& [id, rig] : impl_->boneRigs)
        if (rig.contacts) stats.activeBoneBodies += rig.contacts->bodies().size();
    stats.deformingMeshes = impl_->meshes.size();
    return stats;
}
void MeleeCombat::finish(PlayerBody& player, ZombieLayer* zombies, double seconds) {
    auto* body = &player;
    finish(&body, 1, zombies, seconds);
}
void MeleeCombat::finish(PlayerBody* const* players, std::size_t count, ZombieLayer* zombies, double seconds) {
    if (!zombies) return;
    auto& d = *impl_;
    struct Hit { glm::vec3 impulse{0}, point{0}; float weight = 0; };
    std::map<std::size_t, Hit> hits;
    for (std::size_t p=0; p<count; ++p) {
        auto& player = *players[p];
        if (!player.enabled()) continue;
        for (const auto part : arms) {
            if (!player.punching(part)) continue;
            const auto body = player.body(part);
            d.contacts.resize(b3Body_GetContactCapacity(body));
            const int n = b3Body_GetContactData(body, d.contacts.data(), static_cast<int>(d.contacts.size()));
            for (int c = 0; c < n; ++c) {
                const auto& contact = d.contacts[c];
                const bool armIsA = B3_ID_EQUALS(b3Shape_GetBody(contact.shapeIdA), body);
                const auto other = b3Shape_GetBody(armIsA ? contact.shapeIdB : contact.shapeIdA);
                const auto found = std::find_if(d.meshes.begin(), d.meshes.end(), [&](const auto& mesh) { return B3_ID_EQUALS(mesh->surface->body(), other); });
                auto zombie = found != d.meshes.end() ? (*found)->id : zombies->instances.size();
                if (found == d.meshes.end()) {
                    for (const auto& [id, rig] : d.boneRigs)
                        if (rig.contacts->owns(other)) { zombie = id; break; }
                }
                if (zombie == zombies->instances.size()) continue;
                for (int m = 0; m < contact.manifoldCount; ++m) {
                    const auto& manifold = contact.manifolds[m];
                    const auto normal = manifold.normal;
                    for (int p = 0; p < manifold.pointCount; ++p) {
                        const auto& point = manifold.points[p];
                        if (point.totalNormalImpulse <= .001f) continue;
                        player.recordArmContact(part);
                        auto& hit = hits[zombie];
                        hit.impulse += glm::vec3(normal.x, normal.y, normal.z) * (armIsA ? 1.f : -1.f) * point.totalNormalImpulse;
                        const auto center = b3Body_GetWorldCenter(body);
                        const auto anchor = armIsA ? point.anchorA : point.anchorB;
                        hit.point += (glm::vec3(center.x, center.y, center.z) + glm::vec3(anchor.x, anchor.y, anchor.z)) * point.totalNormalImpulse;
                        hit.weight += point.totalNormalImpulse;
                    }
                }
            }
        }
    }
    for (const auto& [id, hit] : hits) zombies->meleeHit(id, hit.impulse, hit.point / hit.weight, seconds);
    // Bone proxies survive the handoff; the next prepare only moves their bodies.
    // Replace non-skinned living fallback surfaces before the next solve.
    d.meshes.erase(std::remove_if(d.meshes.begin(), d.meshes.end(), [&](const auto& mesh) {
        const auto& instance = zombies->instances[mesh->id];
        return instance.removed || (instance.ragdoll && (!instance.springRagdoll || !mesh->ragdoll));
    }), d.meshes.end());
    for (auto it = d.boneRigs.begin(); it != d.boneRigs.end();) {
        const auto& instance = zombies->instances[it->first];
        if (instance.removed || (instance.ragdoll && !instance.springRagdoll)) it = d.boneRigs.erase(it);
        else ++it;
    }
}
void MeleeCombat::punch(PlayerBody& player, glm::vec3 eye, glm::vec3 direction, const CollisionWorld& scene) {
    if (!player.enabled() || !std::isfinite(glm::length(direction)) || glm::length(direction) < .0001f) return;
    direction = glm::normalize(direction);
    // The scene BVH includes distant geometry that has not been uploaded into
    // local Box3D tiles. Binary search its finite visibility segment for aim.
    constexpr float range = 1000;
    float distance = range;
    bool hit = !scene.lineOfSight(eye, eye + direction * range);
    if (hit) {
        float lo = 0, hi = range;
        for (int i = 0; i < 22; ++i) {
            const float middle = (lo + hi) * .5f;
            if (scene.lineOfSight(eye, eye + direction * middle)) lo = middle; else hi = middle;
        }
        distance = (lo + hi) * .5f;
    }
    auto filter = b3DefaultQueryFilter();
    // Spring ragdoll internals exclude this category, so aim uses their bone
    // surfaces too rather than an invisible capsule in front of the mesh.
    filter.categoryBits = playerBodyCategory;
    filter.maskBits = meleeZombieCategory | ordinaryBodyCategory | doorBodyCategory | interiorBoxCategory | vehicleBodyCategory;
    const auto ray = b3World_CastRayClosest(impl_->world->id(), {eye.x, eye.y, eye.z},
        {direction.x * range, direction.y * range, direction.z * range}, filter);
    if (ray.hit && ray.fraction * range < distance) { distance = ray.fraction * range; hit = true; }
    const auto target = eye + direction * distance;
    player.punch(direction, hit ? &target : nullptr);
}
} // namespace viewer
