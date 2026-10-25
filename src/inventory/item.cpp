#include "item.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace viewer {
Item::Item(std::string id, ItemInventoryConfig inventory, WeaponConfig gameplay, std::vector<std::string> tags)
    : id_(std::move(id)), inventory_(std::move(inventory)), gameplay_(std::move(gameplay)), tags_(std::move(tags)) {
    const bool weapon = inventory_.type == "weapon" && gameplay_.id == id_ &&
        inventory_.ammoType.empty() && !inventory_.stackable && inventory_.maxStack == 1;
    const bool ammo = inventory_.type == "ammo" && gameplay_.id.empty() &&
        !inventory_.ammoType.empty() && inventory_.stackable && inventory_.maxStack > 0;
    if (id_.empty() || inventory_.name.empty() || (!weapon && !ammo) ||
        inventory_.width < 1 || inventory_.width > 16 || inventory_.height < 1 || inventory_.height > 16)
        throw std::invalid_argument("Invalid inventory/gameplay components for item " + id_);
}
bool Item::hasTag(std::string_view tag) const {
    return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}
} // namespace viewer
