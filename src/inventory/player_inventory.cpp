#include "player_inventory.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace viewer {
PlayerInventory::PlayerInventory(StorageLoadout storage, InventoryContents contents)
    : storage_(std::move(storage)), contents_(std::move(contents)), layout_(buildInventoryLayout(storage_, contents_)), configuredHotbar_(contents_.hotbar) {
    for (const auto& [id, item] : items()) {
        const auto count = contents_.quantities.find(id);
        if (item.inventory().stackable && (count == contents_.quantities.end() || count->second < 0 ||
            count->second > item.inventory().maxStack))
            throw std::invalid_argument("Missing or invalid ammo quantity for " + id);
    }
    for (const auto& [id, count] : contents_.quantities) {
        (void)count;
        if (!owns(id) || !find(id)->inventory().stackable)
            throw std::invalid_argument("Quantity references non-stack item " + id);
    }
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
int PlayerInventory::quantity(const std::string& id) const {
    const auto found = contents_.quantities.find(id);
    return found == contents_.quantities.end() ? 0 : found->second;
}
bool PlayerInventory::consume(const std::string& id, int amount) {
    const auto found = contents_.quantities.find(id);
    if (amount < 0 || found == contents_.quantities.end() || amount > found->second) return false;
    found->second -= amount;
    return true;
}
bool PlayerInventory::setQuantity(const std::string& id, int amount) {
    const auto* item = find(id);
    if (!item || !item->inventory().stackable || amount < 0 || amount > item->inventory().maxStack) return false;
    contents_.quantities.at(id) = amount;
    return true;
}
int PlayerInventory::ammoCount(std::string_view ammoType) const {
    int result = 0;
    for (const auto& [id, item] : items())
        if (item.inventory().type == "ammo" && item.inventory().ammoType == ammoType) {
            const int count = quantity(id);
            if (count > std::numeric_limits<int>::max() - result) return std::numeric_limits<int>::max();
            result += count;
        }
    return result;
}
bool PlayerInventory::synchronizePlacements(const std::map<std::string, ItemPlacement>& placements) {
    if (held_ || placements.size() != items().size()) return false;
    auto candidate = contents_;
    candidate.placements = placements;
    InventoryLayout layout;
    try { layout = buildInventoryLayout(storage_, candidate); }
    catch (const std::runtime_error&) { return false; }
    contents_.placements = std::move(candidate.placements);
    layout_ = std::move(layout);
    return true;
}
bool PlayerInventory::synchronizeContents(InventoryContents contents) {
    if (held_) return false;
    try {
        PlayerInventory candidate(storage_, std::move(contents));
        contents_ = std::move(candidate.contents_);
        layout_ = std::move(candidate.layout_);
        return true;
    } catch (const std::exception&) { return false; }
}
bool PlayerInventory::insert(const Item& item, int quantity, const ItemPlacement& placement) {
    if (held_ || owns(item.id())) return false;
    auto candidate = contents_;
    candidate.owned.emplace(item.id(), item);
    candidate.placements.emplace(item.id(), placement);
    for (std::size_t i = 0; i < candidate.hotbar.size(); ++i)
        if (candidate.hotbar[i].empty() && configuredHotbar_[i] == item.id()) candidate.hotbar[i] = item.id();
    if (item.inventory().stackable) candidate.quantities.emplace(item.id(), quantity);
    return synchronizeContents(std::move(candidate));
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
