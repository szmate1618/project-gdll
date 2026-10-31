#include "inventory_state.hpp"
#include <stdexcept>
#include <set>

namespace multiplayer {
InventoryState captureInventory(const viewer::PlayerInventory& inventory) {
    InventoryState state;
    state.hotbar = inventory.hotbar();
    for (const auto& [id, item] : inventory.items())
        state.items.push_back({id, item.prototype(), static_cast<std::uint32_t>(inventory.quantity(id)), inventory.placements().at(id)});
    return state;
}
bool applyInventory(viewer::PlayerInventory& inventory, const viewer::PlayerInventory& catalog, const InventoryState& state, bool* layoutChanged) {
    bool changed = inventory.items().size() != state.items.size() || inventory.hotbar() != state.hotbar;
    for (const auto& entry : state.items) {
        const auto found = inventory.placements().find(entry.id);
        changed |= found == inventory.placements().end() || found->second.storage != entry.placement.storage ||
            found->second.x != entry.placement.x || found->second.y != entry.placement.y || found->second.rotation != entry.placement.rotation;
    }
    viewer::InventoryContents contents;
    contents.skills = catalog.skills();
    contents.hotbar = state.hotbar;
    for (const auto& entry : state.items) {
        const auto* prototype = catalog.find(entry.prototype);
        if (!prototype || entry.quantity > static_cast<std::uint32_t>(prototype->inventory().maxStack) || contents.owned.count(entry.id)) return false;
        try { contents.owned.emplace(entry.id, prototype->renamed(entry.id)); }
        catch (const std::exception&) { return false; }
        contents.placements.emplace(entry.id, entry.placement);
        if (prototype->inventory().stackable) contents.quantities.emplace(entry.id, static_cast<int>(entry.quantity));
    }
    if (!inventory.synchronizeContents(std::move(contents))) return false;
    if (layoutChanged) *layoutChanged = changed;
    return true;
}
std::vector<GroundItemState> captureGround(const viewer::GroundItems& ground, glm::vec3 observer) {
    std::vector<GroundItemState> result;
    for (const auto& pose : ground.poses()) {
        if (glm::distance(pose.body.center, observer) > 80 || result.size() == 512) continue;
        const auto& entry = pose.entry;
        result.push_back({entry.id, entry.item.id(), entry.item.prototype(), static_cast<std::uint32_t>(entry.quantity),
            entry.weapon ? static_cast<std::uint32_t>(entry.weapon->loaded) : 0,
            entry.weapon ? static_cast<float>(entry.weapon->readyAt) : 0,
            entry.weapon ? static_cast<std::uint8_t>(entry.weapon->slot) : std::uint8_t(255),
            pose.body.center, pose.body.halfExtents, pose.body.rotation});
    }
    return result;
}
void applyGround(viewer::GroundItems& ground, const viewer::PlayerInventory& catalog, const std::vector<GroundItemState>& state) {
    std::vector<viewer::GroundItemPose> poses;
    std::set<std::uint64_t> ids;
    for (const auto& entry : state) {
        const auto* prototype = catalog.find(entry.prototype);
        if (!prototype || !ids.insert(entry.id).second || entry.quantity > static_cast<std::uint32_t>(prototype->inventory().maxStack) ||
            (entry.slot == 255) != prototype->inventory().stackable ||
            (entry.slot != 255 && (prototype->id() != viewer::weaponIds.at(entry.slot) || entry.loaded > static_cast<std::uint32_t>(prototype->gameplay().magazineCapacity))))
            throw std::runtime_error("Invalid ground item definition");
        std::optional<viewer::WeaponItem> weapon;
        if (entry.slot != 255) weapon = viewer::WeaponItem{entry.slot, static_cast<int>(entry.loaded), entry.readyAt};
        poses.push_back({{entry.id, prototype->renamed(entry.item), static_cast<int>(entry.quantity), weapon},
            {poses.size(), entry.center, entry.halfExtents, entry.rotation}});
    }
    ground.synchronize(poses);
}
}
