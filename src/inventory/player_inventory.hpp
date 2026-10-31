#pragma once

#include "item.hpp"
#include "inventory_placement.hpp"
#include <array>
#include <optional>

namespace viewer {
struct ItemPlacement {
    std::string storage; // Equipped compartment reference.
    int x = 0, y = 0; // Cells from top left.
    int rotation = 0; // Clockwise degrees: 0, 90, 180 or 270.
};
struct InventoryContents {
    std::map<std::string, Item> owned;
    std::map<std::string, ItemPlacement> placements;
    std::array<std::string, 10> hotbar; // Owned item IDs or skill:<registered ID>.
    std::map<std::string, std::string> skills; // Registered action labels; no storage or combat state.
    std::map<std::string, int> quantities; // Ammo stack ID -> rounds; empty stacks remain owned and placed.
};
struct HeldInventoryItem {
    std::string item;
    int rotation = 0;
};
enum class InventoryLocationKind { Storage, Hand };
struct InventoryLocation {
    InventoryLocationKind kind;
    std::optional<ItemPlacement> placement; // Present only for Storage.
};
struct InventoryMatch {
    const Item* item; // Borrowed; valid while this player inventory lives.
    InventoryLocation location;
};

// Owns the player's storage and item locations independently of the GUI.
// Quantity changes are bounded by stack definitions. InventoryDrag owns local
// placement transactions; complete authoritative layouts synchronize when idle.
class PlayerInventory {
public:
    PlayerInventory(StorageLoadout storage, InventoryContents contents);
    bool owns(const std::string& id) const;
    const Item* find(const std::string& id) const;
    int quantity(const std::string& id) const;
    bool setQuantity(const std::string& id, int amount);
    bool consume(const std::string& id, int amount);
    int ammoCount(std::string_view ammoType) const;
    // Apply a complete authoritative layout between local drag transactions.
    // Invalid snapshots leave both placement and quantity state untouched.
    bool synchronizePlacements(const std::map<std::string, ItemPlacement>& placements);
    bool synchronizeContents(InventoryContents contents);
    bool insert(const Item& item, int quantity, const ItemPlacement& placement);
    bool hasTag(std::string_view tag) const;
    bool hasType(std::string_view type) const;
    std::optional<InventoryLocation> locationOf(const std::string& id) const;
    std::vector<InventoryMatch> findByTag(std::string_view tag) const;
    std::vector<InventoryMatch> findByType(std::string_view type) const;
    std::vector<InventoryMatch> itemsIn(const std::string& storage) const;
    const auto& items() const { return contents_.owned; }
    const auto& placements() const { return contents_.placements; }
    const auto& hotbar() const { return contents_.hotbar; }
    const auto& skills() const { return contents_.skills; }
    const StorageLoadout& storage() const { return storage_; }
    const InventoryLayout& layout() const { return layout_; }
    const auto& held() const { return held_; }
private:
    friend class InventoryDrag;
    void rebuild();
    StorageLoadout storage_;
    InventoryContents contents_;
    InventoryLayout layout_;
    std::array<std::string, 10> configuredHotbar_;
    std::optional<HeldInventoryItem> held_;
};
} // namespace viewer
