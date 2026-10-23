#include "dropped_weapons.hpp"
#include "camera.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "physics_scene.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
DroppedWeapons::DroppedWeapons(std::shared_ptr<PhysicsWorld> world, const std::array<Model,weaponIds.size()>& models)
    : bodies_(std::move(world)) {
    for (std::size_t i=0; i<models.size(); ++i) {
        centers_[i] = (models[i].boundsMin+models[i].boundsMax)*.5f;
        sizes_[i] = glm::max(models[i].boundsMax-models[i].boundsMin,glm::vec3(.04f));
        if (models[i].primitives.empty()) throw std::invalid_argument("Dropped weapons require model bounds");
    }
}
bool DroppedWeapons::discard(Weapons& inventory, const Camera& camera, const CollisionWorld& world, glm::vec3 playerVelocity) {
    const auto& weapon = inventory.equipped();
    if (!weapon.owned) return false;
    const auto slot = inventory.slot();
    const auto direction = camera.direction();
    const auto center = camera.position()+direction*.75f;
    // Leave enough room for the complete collider before creating a fast body.
    const float radius = glm::length(sizes_[slot])*.5f;
    if (!world.lineOfSight(camera.position(),center+direction*radius)) return false;
    BoxSpawnMotion motion;
    motion.rotation = glm::normalize(glm::quat_cast(glm::mat3(glm::inverse(camera.view()))));
    motion.velocity = playerVelocity+direction*throwSpeed+glm::vec3(0,2,0);
    motion.angularVelocity = {0,spinSpeed,0};
    constexpr std::array<float,weaponIds.size()> masses{1,3,3.5f,5,.4f};
    motion.mass = masses[slot];
    motion.continuous = true;
    const auto index = bodies_.add(center,sizes_[slot],motion);
    try { items_.push_back({slot,weapon.loaded,weapon.readyAt}); }
    catch (...) { bodies_.remove(index); throw; }
    inventory.discard();
    return true;
}
bool DroppedWeapons::pickUp(Weapons& inventory, const Camera& camera, const CollisionWorld& world) {
    const auto hit = bodies_.raycast(camera.position(),camera.direction(),pickupReach);
    if (!hit || !world.lineOfSight(camera.position(),hit->point) || !inventory.pickUp(items_.at(hit->box))) return false;
    bodies_.remove(hit->box);
    if (hit->box+1<items_.size()) items_[hit->box] = items_.back();
    items_.pop_back();
    return true;
}
void DroppedWeapons::prepare(PhysicsScene& scene, DoorLayer& doors) const {
    scene.prepare(bodies_);
    for (std::size_t i=0; i<bodies_.count(); ++i) doors.activateNear(bodies_.pose(i).center,8);
}
std::vector<DroppedWeaponPose> DroppedWeapons::poses() const {
    std::vector<DroppedWeaponPose> result;
    for (std::size_t i=0; i<items_.size(); ++i) {
        const auto body = bodies_.pose(i);
        const auto rotation = glm::mat3_cast(body.rotation);
        const auto half = glm::abs(rotation[0])*body.halfExtents.x+glm::abs(rotation[1])*body.halfExtents.y+
                          glm::abs(rotation[2])*body.halfExtents.z;
        const auto slot = items_[i].slot;
        const auto transform = glm::translate(glm::mat4(1),body.center)*glm::mat4_cast(body.rotation)*
                               glm::translate(glm::mat4(1),-centers_[slot]);
        result.push_back({slot,transform,body.center-half,body.center+half});
    }
    return result;
}
} // namespace viewer
