#include "item.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace viewer {
Item::Item(std::string id, ItemInventoryConfig inventory, WeaponConfig gameplay, std::vector<std::string> tags)
    : id_(std::move(id)), inventory_(std::move(inventory)), gameplay_(std::move(gameplay)), tags_(std::move(tags)) {
    if (id_.empty() || gameplay_.id != id_ || inventory_.name.empty() || inventory_.type != "weapon" ||
        inventory_.width < 1 || inventory_.width > 16 || inventory_.height < 1 || inventory_.height > 16)
        throw std::invalid_argument("Invalid inventory/gameplay components for item " + id_);
}
bool Item::hasTag(std::string_view tag) const {
    return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}
} // namespace viewer
