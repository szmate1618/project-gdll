#include "item.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <set>

namespace viewer {
namespace {
bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool validRotation(glm::quat q) {
    return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::abs(glm::length(q)-1)<.001f;
}
void validateModel(ItemModelConfig& model) {
    if (model.parts.empty() || model.parts.size()>64 || model.mesh.size()>4095 || model.mesh.size()%3 ||
        !std::isfinite(model.massKg) || model.massKg<=0 || model.massKg>1000)
        throw std::invalid_argument("Invalid custom model");
    glm::vec3 minimum(std::numeric_limits<float>::infinity()), maximum(-minimum);
    std::set<std::string> names;
    for (auto& part:model.parts) {
        if (part.name.empty() || part.name.size()>128 || !names.insert(part.name).second || part.material.empty() || part.material.size()>64 ||
            !finite(part.center) || glm::any(glm::greaterThan(glm::abs(part.center),glm::vec3(4))) ||
            !finite(part.size) || glm::any(glm::lessThanEqual(part.size,glm::vec3(0))) || glm::any(glm::greaterThan(part.size,glm::vec3(4))) ||
            !finite(part.color) || glm::any(glm::lessThan(part.color,glm::vec3(0))) || glm::any(glm::greaterThan(part.color,glm::vec3(1))) ||
            !validRotation(part.rotation) || (part.shape!=ItemModelShape::Box && part.shape!=ItemModelShape::Frustum))
            throw std::invalid_argument("Invalid custom model part");
        if (!std::isfinite(part.topSize.x) || !std::isfinite(part.topSize.y) ||
            glm::any(glm::lessThanEqual(part.topSize,glm::vec2(0))) || glm::any(glm::greaterThan(part.topSize,glm::vec2(4))))
            throw std::invalid_argument("Invalid custom part footprint");
        part.rotation=glm::normalize(part.rotation);
        const auto top=part.shape==ItemModelShape::Frustum ? part.topSize : glm::vec2(part.size.x,part.size.z);
        for (int level=0;level<2;++level) for (int corner=0;corner<4;++corner) {
            const auto footprint=level ? top : glm::vec2(part.size.x,part.size.z);
            const auto point=part.center+part.rotation*glm::vec3((corner&1 ? .5f : -.5f)*footprint.x,
                (level ? .5f : -.5f)*part.size.y,(corner&2 ? .5f : -.5f)*footprint.y);
            minimum=glm::min(minimum,point); maximum=glm::max(maximum,point);
        }
    }
    for (auto& vertex:model.mesh) {
        if (!finite(vertex.position) || glm::any(glm::greaterThan(glm::abs(vertex.position),glm::vec3(4))) ||
            !finite(vertex.normal) || std::abs(glm::length(vertex.normal)-1)>.001f || !finite(vertex.color) ||
            glm::any(glm::lessThan(vertex.color,glm::vec3(0))) || glm::any(glm::greaterThan(vertex.color,glm::vec3(1))))
            throw std::invalid_argument("Invalid custom mesh vertex");
        vertex.normal=glm::normalize(vertex.normal);
        minimum=glm::min(minimum,vertex.position); maximum=glm::max(maximum,vertex.position);
    }
    for (std::size_t first=0;first<model.mesh.size();first+=3)
        if (glm::length(glm::cross(model.mesh[first+1].position-model.mesh[first].position,
            model.mesh[first+2].position-model.mesh[first].position))<=1e-12f)
            throw std::invalid_argument("Degenerate custom mesh triangle");
    if (glm::any(glm::greaterThan(maximum-minimum,glm::vec3(4))))
        throw std::invalid_argument("Custom model exceeds the 4 metre body limit");
}
}
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
    result.grips_ = grips_;
    result.custom_ = custom_;
    return result;
}
Item Item::withFootprint(int width,int height) const {
    if (width<1 || width>16 || height<1 || height>16) throw std::invalid_argument("Invalid item footprint");
    auto result=*this;
    result.inventory_.width=width; result.inventory_.height=height;
    return result;
}
Item Item::withModel(ItemModelConfig model) const {
    validateModel(model);
    auto inventory=inventory_;
    inventory.type="equipment"; inventory.ammoType.clear(); inventory.stackable=false; inventory.maxStack=1;
    inventory.quantityUnit="count"; inventory.model=std::move(model);
    auto tags=tags_;
    tags.erase(std::remove(tags.begin(),tags.end(),"canopy"),tags.end());
    auto result=Item(id_,std::move(inventory),WeaponConfig{},std::move(tags));
    result.prototype_=prototype_; result.grips_=grips_; result.custom_=true;
    return result;
}
Item Item::withCrafting(ItemModelConfig model, std::vector<ItemGrip> grips) const {
    if (grips.empty() || grips.size()>8) throw std::invalid_argument("Invalid crafted grip count");
    for (auto& grip:grips) {
        if (!finite(grip.position) || glm::any(glm::greaterThan(glm::abs(grip.position),glm::vec3(4))) || !validRotation(grip.rotation))
            throw std::invalid_argument("Invalid crafted grip");
        grip.rotation=glm::normalize(grip.rotation);
    }
    auto result=withModel(std::move(model));
    auto tags=tags_;
    tags.erase(std::remove_if(tags.begin(),tags.end(),[](const std::string& tag) {
        return tag=="canopy" || tag=="nail" || tag=="bind" || tag=="stake" || tag=="throwable";
    }),tags.end());
    for (const auto* tag:{"crafted","melee","crafting"})
        if (std::find(tags.begin(),tags.end(),tag)==tags.end()) tags.emplace_back(tag);
    if (tags.size()>32) throw std::invalid_argument("Too many crafted item tags");
    result.tags_=std::move(tags);
    result.grips_=std::move(grips);
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
