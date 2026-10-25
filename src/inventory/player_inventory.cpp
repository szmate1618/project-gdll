#include "player_inventory.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace viewer {
PlayerInventory::PlayerInventory(StorageLoadout storage, InventoryContents contents)
    : storage_(std::move(storage)), contents_(std::move(contents)), layout_(buildInventoryLayout(storage_, contents_)) {
    for (const auto& id : hotbar())
        if (!id.empty() && !owns(id) && !(id.compare(0, 6, "skill:") == 0 && contents_.skills.count(id.substr(6))))
            throw std::invalid_argument("Inventory shortcut references unowned item or unknown skill " + id);
}
void PlayerInventory::rebuild() {
    layout_ = buildInventoryLayout(storage_, contents_, held_ ? held_->item : "");
}
bool PlayerInventory::owns(const std::string& id) const { return find(id) != nullptr; }
const Item* PlayerInventory::find(const std::string& id) const {
    const auto found = contents_.owned.find(id);
    return found == contents_.owned.end() ? nullptr : &found->second;
}
bool PlayerInventory::hasTag(std::string_view tag) const {
    return std::any_of(items().begin(), items().end(), [&](const auto& entry) { return entry.second.hasTag(tag); });
}
bool PlayerInventory::hasType(std::string_view type) const {
    return std::any_of(items().begin(), items().end(), [&](const auto& entry) { return entry.second.inventory().type == type; });
}
std::optional<InventoryLocation> PlayerInventory::locationOf(const std::string& id) const {
    if (!owns(id)) return {};
    if (held_ && held_->item == id) return InventoryLocation{InventoryLocationKind::Hand, {}};
    return InventoryLocation{InventoryLocationKind::Storage, contents_.placements.at(id)};
}
std::vector<InventoryMatch> PlayerInventory::findByTag(std::string_view tag) const {
    std::vector<InventoryMatch> result;
    for (const auto& [id, item] : items()) if (item.hasTag(tag)) result.push_back({&item, *locationOf(id)});
    return result;
}
std::vector<InventoryMatch> PlayerInventory::findByType(std::string_view type) const {
    std::vector<InventoryMatch> result;
    for (const auto& [id, item] : items()) if (item.inventory().type == type) result.push_back({&item, *locationOf(id)});
    return result;
}
std::vector<InventoryMatch> PlayerInventory::itemsIn(const std::string& storage) const {
    std::vector<InventoryMatch> result;
    for (const auto& [id, placement] : placements())
        if (placement.storage == storage) result.push_back({find(id), {InventoryLocationKind::Storage, placement}});
    return result;
}
} // namespace viewer
