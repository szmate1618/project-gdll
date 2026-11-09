#pragma once

#include "box_layer.hpp"
#include "inventory/inventory_drag.hpp"
#include "weapons.hpp"
#include <cstdint>
#include <set>
#include <map>

namespace viewer {
class Camera;
class DoorLayer;
class CollisionWorld;
class PhysicsScene;
class Canopy;
struct GroundItem {
    std::uint64_t id;
    Item item;
    int quantity;
    std::optional<WeaponItem> weapon;
};
struct GroundItemPose {
    GroundItem entry;
    BoxPose body;
    std::vector<BoxPose> panels{}; // Canopy only; 36 row-major articulated poses.
};
// Shared world ownership of equippable items. Grid coordinates are presentation
// only; ground-query reach is measured in world metres from player feet to the item's bounds.
class GroundItems {
public:
    static constexpr float reach = 2;
    explicit GroundItems(std::shared_ptr<PhysicsWorld> world, bool replica = false);
    ~GroundItems();
    bool pending(std::uint64_t id) const { return replica_ && id >= (std::uint64_t(1) << 63); }
    void setModels(const std::array<Model, weaponIds.size()>& models);
    std::uint64_t add(const Item& item, int quantity, std::optional<WeaponItem> weapon,
        glm::vec3 center, glm::vec3 velocity = {}, glm::quat rotation = {1,0,0,0}, glm::vec3 angularVelocity = {});
    bool drop(PlayerInventory& inventory, InventoryDrag& drag, Weapons& weapons,
        glm::vec3 feet, glm::vec3 forward, const CollisionWorld& collision);
    // For hand-to-storage transfers, the caller may use a validated grip as
    // the reach origin, since a held item can be lifted beyond ground reach.
    bool take(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
        const ItemPlacement& destination, glm::vec3 reachOrigin);
    std::vector<GroundItem> nearby(glm::vec3 feet) const;
    std::vector<GroundItemPose> poses() const;
    bool discard(Weapons& weapons, PlayerInventory& inventory, const Camera& camera,
        const CollisionWorld& collision, glm::vec3 velocity);
    // Quick pickup uses backpack space and leaves the selected equipment alone.
    bool pickUp(Weapons& weapons, PlayerInventory& inventory, const Camera& camera, const CollisionWorld& collision);
    // Equip a nearby or already gripped weapon, dropping the selected weapon.
    // A rejected swap leaves both ownership and the grip untouched.
    bool equip(Weapons& weapons, PlayerInventory& inventory, const Camera& camera,
        const CollisionWorld& collision, glm::vec3 velocity, std::optional<PhysicsProp> held = {});
    void removeBelow(float cutoffY);
    void prepare(PhysicsScene& scene, DoorLayer* doors = nullptr);
    // Replace a replica's complete visible set. Never advances replica physics.
    void synchronize(const std::vector<GroundItemPose>& poses);
    const auto& centers() const { return centers_; }
    std::size_t count() const { return entries_.size(); }
    std::optional<PhysicsProp> prop(std::uint64_t id) const;
    std::optional<std::uint64_t> idOf(const PhysicsProp& prop) const;
    bool erase(std::uint64_t id); // Invalidates any grip on this body.
    void reserve(std::uint64_t id, bool reserved);
private:
    friend class HandInventory;
    bool store(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
        glm::vec3 reachOrigin, bool gripped, bool reserved = false);
    bool takeImpl(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
        const ItemPlacement& destination, glm::vec3 reachOrigin, bool gripped, bool reserved = false);
    bool equipImpl(Weapons& weapons, PlayerInventory& inventory, const Camera& camera,
        const CollisionWorld& collision, glm::vec3 velocity, std::optional<PhysicsProp> held, bool reserved);
    bool inReach(std::size_t index, glm::vec3 feet) const;
    bool grabbed(std::size_t index) const;
    void remove(std::size_t index);
    BoxLayer bodies_;
    std::vector<GroundItem> entries_;
    std::map<std::uint64_t,std::unique_ptr<Canopy>> canopies_;
    std::uint64_t nextId_ = 1;
    bool replica_ = false;
    std::set<std::uint64_t> reserved_; // Physical previews still owned by a hand transaction.
    std::array<glm::vec3, weaponIds.size()> centers_{}, sizes_{};
};
}
