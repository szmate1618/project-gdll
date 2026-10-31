#pragma once

#include "box_layer.hpp"
#include "inventory/inventory_drag.hpp"
#include "weapons.hpp"
#include <cstdint>

namespace viewer {
class Camera;
class DoorLayer;
class CollisionWorld;
class PhysicsScene;
struct GroundItem {
    std::uint64_t id;
    Item item;
    int quantity;
    std::optional<WeaponItem> weapon;
};
struct GroundItemPose {
    GroundItem entry;
    BoxPose body;
};
// Shared world ownership of equippable items. Grid coordinates are presentation
// only; reach is measured in world metres from player feet to the item's bounds.
class GroundItems {
public:
    static constexpr float reach = 2;
    explicit GroundItems(std::shared_ptr<PhysicsWorld> world, bool replica = false);
    bool pending(std::uint64_t id) const { return replica_ && id >= (std::uint64_t(1) << 63); }
    void setModels(const std::array<Model, weaponIds.size()>& models);
    std::uint64_t add(const Item& item, int quantity, std::optional<WeaponItem> weapon,
        glm::vec3 center, glm::vec3 velocity = {}, glm::quat rotation = {1,0,0,0}, glm::vec3 angularVelocity = {});
    bool drop(PlayerInventory& inventory, InventoryDrag& drag, Weapons& weapons,
        glm::vec3 feet, glm::vec3 forward, const CollisionWorld& collision);
    bool take(std::uint64_t id, PlayerInventory& inventory, Weapons& weapons,
        const ItemPlacement& destination, glm::vec3 feet);
    std::vector<GroundItem> nearby(glm::vec3 feet) const;
    std::vector<GroundItemPose> poses() const;
    bool discard(Weapons& weapons, PlayerInventory& inventory, const Camera& camera,
        const CollisionWorld& collision, glm::vec3 velocity);
    bool pickUp(Weapons& weapons, PlayerInventory& inventory, const Camera& camera, const CollisionWorld& collision);
    void removeBelow(float cutoffY);
    void prepare(PhysicsScene& scene, DoorLayer* doors = nullptr);
    // Replace a replica's complete visible set. Never advances replica physics.
    void synchronize(const std::vector<GroundItemPose>& poses);
    const auto& centers() const { return centers_; }
    std::size_t count() const { return entries_.size(); }
private:
    bool inReach(std::size_t index, glm::vec3 feet) const;
    void remove(std::size_t index);
    BoxLayer bodies_;
    std::vector<GroundItem> entries_;
    std::uint64_t nextId_ = 1;
    bool replica_ = false;
    std::array<glm::vec3, weaponIds.size()> centers_{}, sizes_{};
};
}
