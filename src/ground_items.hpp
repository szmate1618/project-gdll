#pragma once

#include "box_layer.hpp"
#include "inventory/inventory_drag.hpp"
#include "weapons.hpp"
#include "grip_preview.hpp"
#include "prop_item_geometry.hpp"
#include <functional>
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
// Shared world ownership of items and acquisition of borrowed liftable props.
// Grid coordinates are presentation only; ground-query reach uses world metres.
class GroundItems {
public:
    static constexpr float reach = 2;
    explicit GroundItems(std::shared_ptr<PhysicsWorld> world, bool replica = false);
    ~GroundItems();
    bool pending(std::uint64_t id) const { return replica_ && id >= (std::uint64_t(1) << 63); }
    void setModels(const std::array<Model, weaponIds.size()>& models);
    // Native authored footprints calibrate inferred world props once per loadout.
    void calibrateStorage(const PlayerInventory& inventory);
    using PropCapture = std::function<std::optional<PropItemGeometry>(const PhysicsProp&)>;
    using PropRemoval = std::function<bool(const PhysicsProp&)>;
    // Sources are borrowed for acquisition; capture is read-only, removal runs
    // only after a complete candidate inventory transaction has succeeded.
    void registerPropSource(const void* owner,PropCapture capture,PropRemoval remove);
    void setWorldBoxes(BoxLayer& boxes);
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
    // G stores ordinary items; a saved grip also enables custom melee equipment.
    bool pickUp(Weapons& weapons, PlayerInventory& inventory, const Camera& camera, const CollisionWorld& collision);
    // H equips weapons/gripped items and stores other liftable objects. A rejected
    // weapon swap leaves both ownership and the grip untouched.
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
    friend class GripPlacement;
    bool storeProp(const PhysicsProp&,PlayerInventory&,Weapons&,bool gripped);
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
    std::array<std::vector<glm::vec3>,weaponIds.size()> storageModelPoints_;
    struct PropSource { const void* owner; PropCapture capture; PropRemoval remove; };
    std::vector<PropSource> propSources_;
    float storageCellsPerMetre_=6.f/1.3f; // Starter broom scale before a native loadout is available.
    bool storageCalibrated_=false;
    std::uint64_t nextItemId_=1;
    std::unique_ptr<NailPlacement> nails_; // Destroy callbacks before entries/canopies/bodies_.
    std::unique_ptr<BindingPlacement> bindings_;
    std::optional<std::vector<glm::vec3>> bindingLinesOverride_;
    std::unique_ptr<GripPlacement> grips_;
    std::optional<std::vector<GripPreviewPose>> gripPreviewsOverride_;
};
}
