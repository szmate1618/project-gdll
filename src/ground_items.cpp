#include "ground_items.hpp"
#include "camera.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_scene.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace viewer {
GroundItems::GroundItems(std::shared_ptr<PhysicsWorld> world, bool replica)
    : bodies_(std::move(world)), nextId_(replica ? std::uint64_t(1) << 63 : 1), replica_(replica) {
    sizes_.fill(glm::vec3(.15f, .1f, .3f));
}
void GroundItems::setModels(const std::array<Model, weaponIds.size()>& models) {
    for (std::size_t i = 0; i < models.size(); ++i) {
        centers_[i] = (models[i].boundsMin + models[i].boundsMax) * .5f;
        sizes_[i] = glm::max(models[i].boundsMax - models[i].boundsMin, glm::vec3(.04f));
    }
}
std::uint64_t GroundItems::add(const Item& item, int quantity, std::optional<WeaponItem> weapon,
    glm::vec3 center, glm::vec3 velocity, glm::quat rotation, glm::vec3 angularVelocity) {
    BoxSpawnMotion motion;
    motion.velocity = velocity;
    motion.rotation = rotation;
    motion.angularVelocity = angularVelocity;
    constexpr std::array<float,weaponIds.size()> masses{1,3,3.5f,5,.4f};
    motion.mass = weapon ? masses.at(weapon->slot) : .3f;
    motion.friction = 1;
    motion.continuous = true;
    const auto index = bodies_.add(center, weapon ? sizes_.at(weapon->slot) : glm::vec3(.16f, .08f, .12f), motion);
    const auto id = nextId_++;
    try { entries_.push_back({id, item, quantity, weapon}); }
    catch (...) { bodies_.remove(index); throw; }
    return id;
}
bool GroundItems::drop(PlayerInventory& inventory, InventoryDrag& drag, Weapons& weapons,
    glm::vec3 feet, glm::vec3 forward, const CollisionWorld& collision) {
    if (!drag.held() || drag.pending() || weapons.reloading()) return false;
    forward.y = 0;
    const float length = glm::length(forward);
    forward = length > .001f ? forward / length : glm::vec3(0, 0, -1);
    const auto center = feet + glm::vec3(0, .65f, 0) + forward * .55f;
    if (!collision.lineOfSight(feet + glm::vec3(0, .65f, 0), center + forward * .3f)) return false;
    const auto& item = inventory.items().at(drag.held()->item);
    std::optional<WeaponItem> weapon;
    for (std::size_t i = 0; i < weaponIds.size(); ++i)
        if (item.id() == weaponIds[i]) {
            const auto& state = weapons.state(i);
            if (!state.owned) return false;
            weapon = WeaponItem{i, state.loaded, state.readyAt};
        }
    add(item, inventory.quantity(item.id()), weapon, center);
    if (weapon) weapons.discard(weapon->slot);
    drag.release();
    weapons.syncAmmo(inventory);
    return true;
}
bool GroundItems::inReach(std::size_t index, glm::vec3 feet) const {
    const auto pose = bodies_.pose(index);
    // Test distance to the oriented collider, including vertical separation.
    const auto local = glm::inverse(pose.rotation) * (feet - pose.center);
    return glm::length(local - glm::clamp(local, -pose.halfExtents, pose.halfExtents)) <= reach;
}
std::vector<GroundItem> GroundItems::nearby(glm::vec3 feet) const {
    std::vector<GroundItem> result;
    for (std::size_t i = 0; i < entries_.size(); ++i) if (inReach(i, feet)) result.push_back(entries_[i]);
    return result;
}
bool GroundItems::take(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
    const ItemPlacement& destination, glm::vec3 feet) {
    if (pending(id)) return false; // Wait for the server to assign the physical drop its stable ID.
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.id == id; });
    if (found == entries_.end() || inventory.held() || weapons.reloading()) return false;
    const auto index = std::size_t(found - entries_.begin());
    if (!inReach(index, feet) || (found->weapon && weapons.state(found->weapon->slot).owned)) return false;
    std::string itemId = found->item.id();
    if (inventory.owns(itemId)) {
        if (!found->item.inventory().stackable) return false;
        itemId = "ground_" + std::to_string(id);
        while (inventory.owns(itemId)) itemId += "_";
    }
    const auto item = found->item.renamed(itemId);
    if (!inventory.insert(item, found->quantity, destination)) return false;
    if (found->weapon) {
        const auto previous = weapons.slot();
        weapons.pickUp(*found->weapon);
        weapons.equip(previous);
    }
    remove(index);
    weapons.syncAmmo(inventory);
    return true;
}
void GroundItems::remove(std::size_t index) {
    bodies_.remove(index);
    if (index + 1 < entries_.size()) entries_[index] = std::move(entries_.back());
    entries_.pop_back();
}
bool GroundItems::discard(Weapons& weapons, PlayerInventory& inventory, const Camera& camera,
    const CollisionWorld& collision, glm::vec3 velocity) {
    if (weapons.slot() >= weaponIds.size() || weapons.reloading() || inventory.held()) return false;
    const auto slot = weapons.slot();
    const auto* item = inventory.find(weaponIds[slot]);
    const auto center = camera.position() + camera.direction() * .75f;
    if (!item || !collision.lineOfSight(camera.position(), center + camera.direction() * glm::length(sizes_[slot]) * .5f)) return false;
    InventoryDrag drag(inventory);
    if (!drag.pickUp(item->id())) return false;
    const auto& weapon = weapons.state(slot);
    add(*item, 0, WeaponItem{slot, weapon.loaded, weapon.readyAt}, center, velocity + camera.direction() * 12.5f + glm::vec3(0,2,0),
        glm::normalize(glm::quat_cast(glm::mat3(glm::inverse(camera.view())))), {0,14,0});
    weapons.discard(slot);
    drag.release();
    return true;
}
bool GroundItems::pickUp(Weapons& weapons, PlayerInventory& inventory, const Camera& camera, const CollisionWorld& collision) {
    const auto hit = bodies_.raycast(camera.position(), camera.direction(), reach);
    if (!hit || !collision.lineOfSight(camera.position(), hit->point)) return false;
    const auto entry = entries_.at(hit->box);
    for (const auto& [reference, container] : inventory.layout())
        for (int rotation : {0,90}) for (int y = 0; y < container.grid.height; ++y) for (int x = 0; x < container.grid.width; ++x)
            if (take(entry.id, inventory, weapons, {reference,x,y,rotation}, camera.position() - glm::vec3(0,1.65f,0))) {
                if (entry.weapon) weapons.equip(entry.weapon->slot);
                return true;
            }
    return false;
}
void GroundItems::removeBelow(float cutoffY) {
    for (std::size_t i = 0; i < entries_.size();)
        if (bodies_.belowHeight(i, cutoffY)) remove(i); else ++i;
}
void GroundItems::prepare(PhysicsScene& scene, DoorLayer* doors) {
    scene.prepare(bodies_);
    bodies_.dampOnGroundContact(12, 14);
    if (doors) for (std::size_t i=0; i<entries_.size(); ++i) doors->activateNear(bodies_.pose(i).center,8);
}
std::vector<GroundItemPose> GroundItems::poses() const {
    std::vector<GroundItemPose> result;
    for (std::size_t i = 0; i < entries_.size(); ++i) result.push_back({entries_[i], bodies_.pose(i)});
    return result;
}
void GroundItems::synchronize(const std::vector<GroundItemPose>& poses) {
    std::map<std::uint64_t,const GroundItemPose*> incoming;
    for (const auto& pose : poses) incoming.emplace(pose.entry.id,&pose);
    for (std::size_t i=0; i<entries_.size();)
        if (!incoming.count(entries_[i].id)) remove(i); else ++i;
    std::map<std::uint64_t,std::size_t> slots;
    for (std::size_t i=0; i<entries_.size(); ++i) slots.emplace(entries_[i].id,i);
    for (const auto& pose : poses) {
        const auto found=slots.find(pose.entry.id);
        const auto index=found==slots.end() ? entries_.size() : found->second;
        if (found==slots.end()) {
            bodies_.add(pose.body.center,pose.body.halfExtents*2.f);
            entries_.push_back(pose.entry);
        } else entries_[index]=pose.entry;
        bodies_.setPose(index,pose.body.center,pose.body.rotation);
        nextId_=std::max(nextId_,pose.entry.id+1);
    }
}
}
