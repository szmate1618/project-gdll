#pragma once

#include "box_layer.hpp"
#include "inventory/inventory_drag.hpp"
#include "weapons.hpp"
#include "grip_preview.hpp"
#include <cstdint>
#include <set>
#include <map>

namespace viewer {
class Camera;
class DoorLayer;
class CollisionWorld;
class PhysicsScene;
class Canopy;
class NailPlacement;
class BindingPlacement;
class GripPlacement;
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
    bool equipCrafted(Weapons&,PlayerInventory&,const Camera&,const CollisionWorld&,
        std::optional<PhysicsProp> held={},bool reserved=false);
    void removeBelow(float cutoffY);
    void prepare(PhysicsScene& scene, DoorLayer* doors = nullptr);
    // Replace a replica's complete visible set. Never advances replica physics.
    void synchronize(const std::vector<GroundItemPose>& poses);
    const auto& centers() const { return centers_; }
    std::size_t count() const { return entries_.size(); }
    std::optional<PhysicsProp> prop(std::uint64_t id) const;
    std::optional<std::uint64_t> idOf(const PhysicsProp& prop) const;
    const GroundItem* find(const PhysicsProp& prop) const;
    void setNailPreviewSphere(const PhysicsProp& prop, bool enabled);
    NailPlacement& nails(); // Lazy authoritative placement controller; owned before its bodies.
    BindingPlacement& bindings();
    const std::vector<glm::vec3>& bindingLines() const;
    void setBindingLines(std::vector<glm::vec3> lines); // Accepted replica snapshot or preview fixture.
    // An explicit directory selects durable definition storage on first use.
    GripPlacement& grips(BoxLayer* editingBoxes=nullptr,const std::filesystem::path& saveDirectory={});
    const std::vector<GripPreviewPose>& gripPreviews() const;
    void setGripPreviews(std::vector<GripPreviewPose>);
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
    std::unique_ptr<NailPlacement> nails_; // Destroy callbacks before entries/canopies/bodies_.
    std::unique_ptr<BindingPlacement> bindings_;
    std::optional<std::vector<glm::vec3>> bindingLinesOverride_;
    std::unique_ptr<GripPlacement> grips_;
    std::optional<std::vector<GripPreviewPose>> gripPreviewsOverride_;
};
}
