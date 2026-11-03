#include "hand_inventory.hpp"
#include "collision_world.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
bool HandInventory::begin(const std::string& item) {
    if (inventory_.held()) return false;
    const auto placement = inventory_.placements().find(item);
    if (placement == inventory_.placements().end()) return false;
    const auto access = inventoryAccess(inventory_.layout().at(placement->second.storage),
        inventoryRectangle(*inventory_.find(item), placement->second));
    if (!drag_.pickUp(item)) return false;
    item_ = item;
    remaining_ = duration_ = access.seconds;
    return true;
}
void HandInventory::release(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons) {
    if (!active()) { interaction.cancel(); return; }
    interaction.cancel();
    if (groundId_) {
        // A removed/broken body cannot consume the reserved inventory item.
        ground.reserve(*groundId_, false);
        if (ground.prop(*groundId_)) {
            const auto item = drag_.held()->item;
            for (std::size_t slot = 0; slot < weaponIds.size(); ++slot)
                if (item == weaponIds[slot]) weapons.discard(slot);
            drag_.release();
        } else drag_.cancel();
    } else drag_.cancel();
    item_.clear(); groundId_.reset(); remaining_ = duration_ = 0;
    weapons.syncAmmo(inventory_);
}
void HandInventory::update(double dt, const Camera& camera, PlayerInteraction& interaction,
    BoxLayer& boxes, GroundItems& ground, Weapons& weapons, const CollisionWorld& collision) {
    if (!active() || !std::isfinite(dt) || dt < 0) return;
    if (groundId_) {
        const auto prop = ground.prop(*groundId_);
        if (!prop || !(interaction.heldProp() == prop)) release(interaction, ground, weapons);
        return;
    }
    remaining_ = std::max(0.0, remaining_ - dt);
    if (remaining_ > 0) return;
    const auto center = camera.position() + camera.direction() * .7f;
    if (!collision.lineOfSight(camera.position(), center + camera.direction() * .2f)) {
        release(interaction, ground, weapons); return;
    }
    const auto& item = *inventory_.find(drag_.held()->item);
    std::optional<WeaponItem> weapon;
    for (std::size_t slot = 0; slot < weaponIds.size(); ++slot)
        if (item.id() == weaponIds[slot]) {
            const auto& state = weapons.state(slot);
            weapon = WeaponItem{slot, state.loaded, state.readyAt};
        }
    groundId_ = ground.add(item, inventory_.quantity(item.id()), weapon, center);
    ground.reserve(*groundId_, true);
    if (!interaction.grip(camera, boxes, *ground.prop(*groundId_))) {
        ground.erase(*groundId_); groundId_.reset();
        release(interaction, ground, weapons);
    }
}
bool HandInventory::store(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons) {
    if (active() && groundId_ && !(interaction.heldProp() == ground.prop(*groundId_)))
        release(interaction, ground, weapons);
    if (active()) {
        interaction.cancel();
        if (groundId_) ground.erase(*groundId_);
        item_.clear(); groundId_.reset(); remaining_ = duration_ = 0;
        drag_.cancel(); // Restores the exact reserved position and orientation.
        weapons.syncAmmo(inventory_);
        return true;
    }
    const auto prop = interaction.heldProp();
    const auto id = prop ? ground.idOf(*prop) : std::nullopt;
    if (!id || !inventory_.storage().backpack) { interaction.cancel(); return false; }
    const auto reference = backpackStorageReference(inventory_.storage().backpack->id);
    const auto& grid = inventory_.layout().at(reference).grid;
    // The existing grip establishes reach even when cursor motion has lifted
    // the item beyond the ground query radius measured from the player feet.
    const auto reachOrigin = prop->center();
    interaction.cancel();
    for (int rotation : {0,90}) for (int y = 0; y < grid.height; ++y) for (int x = 0; x < grid.width; ++x)
        if (ground.take(*id, inventory_, weapons, {reference,x,y,rotation}, reachOrigin)) return true;
    return false; // The released physical item stays on the ground when full.
}
} // namespace viewer
