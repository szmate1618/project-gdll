#include "item.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace viewer {
int itemQuantityScale(const ItemInventoryConfig& inventory) {
    return inventory.quantityUnit == "m" ? 1000 : 1;
}
std::optional<int> itemQuantityFromDisplay(const ItemInventoryConfig& inventory, double amount) {
    if (!std::isfinite(amount) || amount < 0 ||
        (inventory.quantityUnit != "count" && inventory.quantityUnit != "m")) return {};
    const double scaled = amount * itemQuantityScale(inventory);
    if (!std::isfinite(scaled) || scaled > std::numeric_limits<int>::max()) return {};
    const double rounded = std::round(scaled);
    // TOML decimals pass through binary floating point. Tolerate representation
    // error, not an authored fraction of the item's smallest quantum.
    if (std::abs(scaled - rounded) > 1e-6) return {};
    return static_cast<int>(rounded);
}
std::string formatItemQuantity(const ItemInventoryConfig& inventory, int amount) {
    if (amount < 0) throw std::invalid_argument("Cannot format a negative item quantity");
    if (inventory.quantityUnit != "m") return std::to_string(amount);
    auto result = std::to_string(amount / 1000);
    if (const int remainder = amount % 1000) {
        auto decimals = std::to_string(1000 + remainder).substr(1);
        while (decimals.back() == '0') decimals.pop_back();
        result += "." + decimals;
    }
    return result + " m";
}
Item::Item(std::string id, ItemInventoryConfig inventory, WeaponConfig gameplay, std::vector<std::string> tags)
    : id_(std::move(id)), prototype_(id_), inventory_(std::move(inventory)), gameplay_(std::move(gameplay)), tags_(std::move(tags)) {
    const bool weapon = inventory_.type == "weapon" && gameplay_.id == id_ &&
        inventory_.ammoType.empty() && !inventory_.stackable && inventory_.maxStack == 1;
    const bool ammo = inventory_.type == "ammo" && gameplay_.id.empty() &&
        !inventory_.ammoType.empty() && inventory_.stackable && inventory_.maxStack > 0;
    const bool medical = inventory_.type == "medical" && gameplay_.id.empty() &&
        inventory_.ammoType.empty() && inventory_.stackable && inventory_.maxStack > 0;
    const bool equipment = inventory_.type == "equipment" && gameplay_.id.empty() &&
        inventory_.ammoType.empty() && inventory_.maxStack > 0 &&
        (inventory_.stackable || inventory_.maxStack == 1);
    const bool quantity = inventory_.quantityUnit == "count" ||
        (inventory_.quantityUnit == "m" && equipment && inventory_.stackable);
    if (id_.empty() || inventory_.name.empty() || (!weapon && !ammo && !medical && !equipment) ||
        !quantity ||
        inventory_.width < 1 || inventory_.width > 16 || inventory_.height < 1 || inventory_.height > 16)
        throw std::invalid_argument("Invalid inventory/gameplay components for item " + id_);
}
Item Item::renamed(const std::string& id) const {
    auto result = Item(id, inventory_, gameplay_, tags_);
    result.prototype_ = prototype_;
    return result;
}
bool Item::hasTag(std::string_view tag) const {
    return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}
bool itemStacksCompatible(const Item& source, const Item& target) {
    const auto& a = source.inventory();
    const auto& b = target.inventory();
    if (!a.stackable || !b.stackable || a.type != b.type || a.quantityUnit != b.quantityUnit) return false;
    if (a.type == "ammo") return a.ammoType == b.ammoType;
    return !a.acoustics.empty() && a.acoustics == b.acoustics;
}
} // namespace viewer
