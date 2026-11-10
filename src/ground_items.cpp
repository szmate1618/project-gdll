#include "ground_items.hpp"
#include "canopy.hpp"
#include "camera.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_scene.hpp"
#include "item_model_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace viewer {
namespace {
bool insertGroundItem(const GroundItem& entry, PlayerInventory& inventory, Weapons& weapons,
    const ItemPlacement& destination) {
    if (entry.weapon && weapons.state(entry.weapon->slot).owned) return false;
    std::string itemId = entry.item.id();
    if (inventory.owns(itemId)) {
        if (!entry.item.inventory().stackable) return false;
        itemId = "ground_" + std::to_string(entry.id);
        while (inventory.owns(itemId)) itemId += "_";
    }
    if (!inventory.insert(entry.item.renamed(itemId), entry.quantity, destination)) return false;
    if (entry.weapon) {
        const auto previous = weapons.slot();
        if (!weapons.pickUp(*entry.weapon)) return false;
        weapons.equip(previous);
    }
    weapons.syncAmmo(inventory);
    return true;
}
}
GroundItems::GroundItems(std::shared_ptr<PhysicsWorld> world, bool replica)
    : bodies_(std::move(world)), nextId_(replica ? std::uint64_t(1) << 63 : 1), replica_(replica) {
    sizes_.fill(glm::vec3(.15f, .1f, .3f));
    sizes_[stakeSlot] = {.06f, .04f, 1.5f};
}
GroundItems::~GroundItems() = default;
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
    motion.acoustics = item.inventory().acoustics.empty() ? "crate" : item.inventory().acoustics;
    constexpr std::array<float,weaponIds.size()> masses{1,3,3.5f,5,.4f,0,1.2f};
    motion.mass = weapon ? masses.at(weapon->slot) : .3f;
    motion.friction = 1;
    motion.continuous = true;
    auto size = weapon ? sizes_.at(weapon->slot) : glm::vec3(.16f,.08f,.12f);
    if (!item.inventory().model.parts.empty()) {
        const auto bounds = itemModelBounds(item.inventory().model);
        // Box3D's stable prop bodies require at least 1 cm per axis. The mesh
        // keeps its authored dimensions, including millimetre-wide nails.
        size = glm::max(bounds.maximum - bounds.minimum, glm::vec3(.01f));
        motion.mass = item.inventory().model.massKg;
    } else if (item.hasTag("stake")) { size = sizes_[stakeSlot]; motion.mass = 1.2f; }
    if (item.hasTag("canopy")) {
        auto cloth = Canopy::motion(bodies_.physicsWorld(),replica_);
        cloth.velocity = velocity; cloth.angularVelocity = angularVelocity; cloth.rotation = rotation;
        motion = cloth;
        center += rotation*Canopy::panelOffset(Canopy::rootPanel);
        size = {Canopy::panelSize,Canopy::thickness,Canopy::panelSize};
    }
    const auto index = bodies_.add(center,size,motion);
    const auto id = nextId_++;
    try {
        if (item.hasTag("canopy")) canopies_.emplace(id,std::make_unique<Canopy>(bodies_.prop(index),replica_));
        entries_.push_back({id,item,quantity,weapon});
    } catch (...) { canopies_.erase(id); bodies_.remove(index); throw; }
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
    const auto canopy = canopies_.find(entries_[index].id);
    // Test distance to the oriented collider, including vertical separation.
    const auto withinReach = [&](const BoxPose& pose) {
        const auto local = glm::inverse(pose.rotation)*(feet-pose.center);
        return glm::length(local-glm::clamp(local,-pose.halfExtents,pose.halfExtents))<=reach;
    };
    if (canopy==canopies_.end()) return withinReach(bodies_.pose(index));
    for (const auto& pose : canopy->second->poses()) if (withinReach(pose)) return true;
    return false;
}
bool GroundItems::grabbed(std::size_t index) const {
    const auto found=canopies_.find(entries_[index].id);
    return found==canopies_.end() ? bodies_.prop(index).grabbed() : found->second->grabbed();
}
std::vector<GroundItem> GroundItems::nearby(glm::vec3 feet) const {
    std::vector<GroundItem> result;
    for (std::size_t i = 0; i < entries_.size(); ++i) if (inReach(i, feet)) result.push_back(entries_[i]);
    return result;
}
bool GroundItems::take(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
    const ItemPlacement& destination, glm::vec3 reachOrigin) {
    return takeImpl(id, inventory, weapons, destination, reachOrigin, false);
}
bool GroundItems::takeImpl(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
    const ItemPlacement& destination, glm::vec3 reachOrigin, bool gripped, bool reserved) {
    if (pending(id) || (reserved_.count(id) && !reserved)) return false;
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.id == id; });
    if (found == entries_.end() || inventory.held() || weapons.reloading()) return false;
    const auto index = std::size_t(found - entries_.begin());
    if (grabbed(index) && !gripped) return false;
    if (!inReach(index, reachOrigin) || (found->weapon && weapons.state(found->weapon->slot).owned)) return false;
    auto candidateInventory = inventory;
    auto candidateWeapons = weapons;
    if (!insertGroundItem(*found, candidateInventory, candidateWeapons, destination)) return false;
    inventory = std::move(candidateInventory);
    weapons = std::move(candidateWeapons);
    remove(index);
    return true;
}
void GroundItems::remove(std::size_t index) {
    reserved_.erase(entries_[index].id);
    canopies_.erase(entries_[index].id);
    bodies_.remove(index);
    if (index + 1 < entries_.size()) entries_[index] = std::move(entries_.back());
    entries_.pop_back();
}
void GroundItems::reserve(std::uint64_t id, bool reserved) {
    if (reserved) reserved_.insert(id); else reserved_.erase(id);
}
std::optional<PhysicsProp> GroundItems::prop(std::uint64_t id) const {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].id == id) return bodies_.prop(i);
    return std::nullopt;
}
std::optional<std::uint64_t> GroundItems::idOf(const PhysicsProp& prop) const {
    const auto index = bodies_.indexOf(prop);
    if (index) return entries_[*index].id;
    for (const auto& [id,canopy] : canopies_) if (canopy->owns(prop)) return id;
    return {};
}
bool GroundItems::erase(std::uint64_t id) {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].id == id) { remove(i); return true; }
    return false;
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
    const auto hit = raycastPhysicsProp(bodies_.physicsWorld(),camera.position(),camera.direction(),reach);
    if (!hit || !collision.lineOfSight(camera.position(), hit->point)) return false;
    const auto id = idOf(hit->prop);
    return id && store(*id, inventory, weapons,
                 camera.position() - glm::vec3(0,1.65f,0), false);
}
bool GroundItems::store(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
    glm::vec3 reachOrigin, bool gripped, bool reserved) {
    if (!inventory.storage().backpack) return false;
    const auto reference = backpackStorageReference(inventory.storage().backpack->id);
    const auto& grid = inventory.layout().at(reference).grid;
    for (int rotation : {0,90}) for (int y = 0; y < grid.height; ++y) for (int x = 0; x < grid.width; ++x)
        if (takeImpl(id, inventory, weapons, {reference,x,y,rotation}, reachOrigin, gripped, reserved)) return true;
    return false;
}
bool GroundItems::equip(Weapons& weapons, PlayerInventory& inventory, const Camera& camera,
    const CollisionWorld& collision, glm::vec3 velocity, std::optional<PhysicsProp> held) {
    return equipImpl(weapons, inventory, camera, collision, velocity, held, false);
}
bool GroundItems::equipImpl(Weapons& weapons, PlayerInventory& inventory, const Camera& camera,
    const CollisionWorld& collision, glm::vec3 velocity, std::optional<PhysicsProp> held, bool reserved) {
    if (inventory.held() || weapons.reloading()) return false;
    std::size_t index;
    if (held) {
        const auto found = bodies_.indexOf(*held);
        if (!found || glm::distance(held->center(), camera.position()) > 5) return false;
        index = *found;
    } else {
        const auto hit = bodies_.raycast(camera.position(), camera.direction(), reach);
        if (!hit || !collision.lineOfSight(camera.position(), hit->point)) return false;
        index = hit->box;
        if (!inReach(index, camera.position() - glm::vec3(0,1.65f,0))) return false;
    }
    const auto entry = entries_[index];
    if (!entry.weapon || pending(entry.id) || (reserved_.count(entry.id) && !reserved) ||
        (bodies_.prop(index).grabbed() && !held)) return false;
    auto candidateInventory = inventory;
    auto candidateWeapons = weapons;
    std::optional<GroundItem> dropped;
    const auto center = camera.position() + camera.direction() * .75f;
    if (weapons.slot() < weaponIds.size()) {
        const auto slot = weapons.slot();
        const auto* item = inventory.find(weaponIds[slot]);
        if (!item || !collision.lineOfSight(camera.position(), center + camera.direction() * glm::length(sizes_[slot]) * .5f)) return false;
        const auto state = weapons.state(slot);
        dropped = GroundItem{0, *item, inventory.quantity(item->id()), WeaponItem{slot,state.loaded,state.readyAt}};
        InventoryDrag outgoing(candidateInventory);
        if (!outgoing.pickUp(item->id()) || !outgoing.release() || !candidateWeapons.discard(slot)) return false;
    }
    const auto acquire = [&] {
        for (const auto& [reference, container] : candidateInventory.layout()) {
            for (int rotation : {0,90}) for (int y = 0; y < container.grid.height; ++y) for (int x = 0; x < container.grid.width; ++x) {
                auto attemptInventory = candidateInventory;
                auto attemptWeapons = candidateWeapons;
                if (!insertGroundItem(entry, attemptInventory, attemptWeapons, {reference,x,y,rotation})) continue;
                candidateInventory = std::move(attemptInventory);
                candidateWeapons = std::move(attemptWeapons);
                return true;
            }
        }
        return false;
    };
    if (!acquire() || !candidateWeapons.equip(entry.weapon->slot)) return false;
    if (dropped) add(dropped->item, dropped->quantity, dropped->weapon, center, velocity,
        glm::normalize(glm::quat_cast(glm::mat3(glm::inverse(camera.view())))));
    inventory = std::move(candidateInventory);
    weapons = std::move(candidateWeapons);
    remove(index);
    return true;
}
void GroundItems::removeBelow(float cutoffY) {
    for (std::size_t i = 0; i < entries_.size();)
        if (bodies_.belowHeight(i, cutoffY)) remove(i); else ++i;
}
void GroundItems::prepare(PhysicsScene& scene, DoorLayer* doors) {
    scene.prepare(bodies_);
    bodies_.dampOnGroundContact(12, 14);
    for (auto& [id,canopy] : canopies_) { (void)id; canopy->prepare(scene); }
    if (doors) for (std::size_t i=0; i<entries_.size(); ++i) doors->activateNear(bodies_.pose(i).center,8);
}
std::vector<GroundItemPose> GroundItems::poses() const {
    std::vector<GroundItemPose> result;
    for (std::size_t i=0; i<entries_.size(); ++i) {
        const auto canopy=canopies_.find(entries_[i].id);
        result.push_back({entries_[i],bodies_.pose(i),canopy==canopies_.end() ? std::vector<BoxPose>{} : canopy->second->poses()});
    }
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
            auto motion=pose.entry.item.hasTag("canopy") ? Canopy::motion(bodies_.physicsWorld(),true) : BoxSpawnMotion{};
            motion.rotation=pose.body.rotation;
            if (!pose.entry.item.inventory().model.parts.empty()) motion.mass=pose.entry.item.inventory().model.massKg;
            motion.acoustics=pose.entry.item.inventory().acoustics.empty() ? "crate" : pose.entry.item.inventory().acoustics;
            bodies_.add(pose.body.center,pose.body.halfExtents*2.f,motion);
            if (pose.entry.item.hasTag("canopy"))
                canopies_.emplace(pose.entry.id,std::make_unique<Canopy>(bodies_.prop(index),true));
            entries_.push_back(pose.entry);
        } else entries_[index]=pose.entry;
        bodies_.setPose(index,pose.body.center,pose.body.rotation);
        if (const auto canopy=canopies_.find(pose.entry.id); canopy!=canopies_.end()) canopy->second->synchronize(pose.panels);
        nextId_=std::max(nextId_,pose.entry.id+1);
    }
}
}
