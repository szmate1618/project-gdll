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
    if (!id) { interaction.cancel(); return false; }
    // The existing grip establishes reach even when cursor motion has lifted
    // the item beyond the ground query radius measured from the player feet.
    const auto reachOrigin = prop->center();
    if (ground.store(*id, inventory_, weapons, reachOrigin, true)) {
        interaction.cancel();
        return true;
    }
    ++rejectedStores_;
    interaction.cancel();
    return false;
}
bool HandInventory::pickUp(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons,
    const Camera& camera, const CollisionWorld& collision) {
    if (active()) {
        const auto prop = interaction.heldProp();
        if (!groundId_ || !prop || !(ground.prop(*groundId_) == prop)) return false;
        auto candidateInventory = inventory_;
        auto candidateWeapons = weapons;
        InventoryDrag candidateDrag(candidateInventory);
        if (!candidateDrag.release()) return false;
        const auto slot = std::find(weaponIds.begin(), weaponIds.end(), item_);
        if (slot != weaponIds.end()) candidateWeapons.discard(std::size_t(slot - weaponIds.begin()));
        if (!ground.store(*groundId_, candidateInventory, candidateWeapons, prop->center(), true, true)) {
            ++rejectedStores_;
            return false;
        }
        candidateWeapons.equip(weapons.slot());
        drag_.release();
        inventory_ = std::move(candidateInventory);
        weapons = std::move(candidateWeapons);
        interaction.cancel();
        item_.clear(); groundId_.reset(); remaining_ = duration_ = 0;
        return true;
    }
    if (interaction.grabbing()) {
        const auto prop = interaction.heldProp();
        const auto id = prop ? ground.idOf(*prop) : std::nullopt;
        if (!id) return false;
        if (!ground.store(*id, inventory_, weapons, prop->center(), true)) {
            ++rejectedStores_;
            return false;
        }
        interaction.cancel();
        return true;
    }
    return ground.pickUp(weapons, inventory_, camera, collision);
}
bool HandInventory::equip(PlayerInteraction& interaction, GroundItems& ground, Weapons& weapons,
    const Camera& camera, const CollisionWorld& collision, glm::vec3 velocity) {
    const auto prop = interaction.heldProp();
    if (!active()) {
        if (interaction.grabbing() && !prop) return false;
        if (!ground.equip(weapons, inventory_, camera, collision, velocity, prop)) return false;
        interaction.cancel();
        return true;
    }
    if (!groundId_ || !prop || !(ground.prop(*groundId_) == prop)) return false;
    const auto slot = std::find(weaponIds.begin(), weaponIds.end(), item_);
    if (slot == weaponIds.end()) return false;
    const auto target = std::size_t(slot - weaponIds.begin());
    // Preview ownership stays reserved until a complete equip/swap can succeed.
    // Release only the copied inventory's item while checking the transaction.
    auto candidateInventory = inventory_;
    auto candidateWeapons = weapons;
    InventoryDrag candidateDrag(candidateInventory);
    if (!candidateDrag.release() || !candidateWeapons.discard(target)) return false;
    if (weapons.slot() == target) candidateWeapons.equip(handlingSlot);
    if (!ground.equipImpl(candidateWeapons, candidateInventory, camera, collision, velocity, prop, true)) return false;
    drag_.release();
    inventory_ = std::move(candidateInventory);
    weapons = std::move(candidateWeapons);
    interaction.cancel();
    item_.clear(); groundId_.reset(); remaining_ = duration_ = 0;
    return true;
}
} // namespace viewer
