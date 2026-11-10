#pragma once

#include "ground_items.hpp"
#include "player_interaction.hpp"

namespace viewer {
// A storage transaction remains reserved while its physical body is gripped.
// Closing the drawer does not end that transaction. Releasing the grip does.
// Inventory, world and interaction are borrowed and must outlive this controller.
class HandInventory {
public:
    explicit HandInventory(PlayerInventory& inventory) : inventory_(inventory), drag_(inventory) {}
    bool begin(const std::string& item, bool crafting = false);
    bool finishCraftingPlacement(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons);
    void update(double dt, const Camera& camera, PlayerInteraction& interaction,
                BoxLayer& boxes, GroundItems& ground, Weapons& weapons, const CollisionWorld& collision);
    bool store(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons);
    bool pickUp(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons,
                const Camera& camera, const CollisionWorld& collision);
    bool equip(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons,
               const Camera& camera, const CollisionWorld& collision, glm::vec3 velocity);
    void release(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons);
    bool active() const { return !item_.empty(); }
    double remaining() const { return remaining_; }
    double duration() const { return duration_; }
    std::uint32_t rejectedStores() const { return rejectedStores_; }
private:
    PlayerInventory& inventory_;
    InventoryDrag drag_;
    std::string item_;
    std::optional<std::uint64_t> groundId_;
    double remaining_ = 0, duration_ = 0;
    std::uint32_t rejectedStores_ = 0; // Counts failed returns of held equippable items.
    bool singleNail_ = false, finishWhenRetrieved_ = false;
};
} // namespace viewer
