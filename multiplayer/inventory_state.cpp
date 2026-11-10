#include "inventory_state.hpp"
#include "canopy.hpp"
#include <stdexcept>
#include <set>

namespace multiplayer {
namespace {
bool sameRotation(glm::quat a,glm::quat b) {
    a=glm::normalize(a); b=glm::normalize(b);
    // q and -q describe the same frame; restoration may renormalize by an ULP.
    return glm::length(a-b)<1e-6f || glm::length(a+b)<1e-6f;
}
bool sameCrafting(const viewer::Item& item,const viewer::CraftedItemData& data) {
    if (!item.custom()) return false;
    const auto a=viewer::captureCraftedItem(item);
    if (a.id!=data.id || a.prototype!=data.prototype || a.name!=data.name || a.acoustics!=data.acoustics ||
        a.width!=data.width || a.height!=data.height || a.tags!=data.tags || a.model.massKg!=data.model.massKg ||
        a.model.parts.size()!=data.model.parts.size() || a.model.mesh.size()!=data.model.mesh.size() || a.grips.size()!=data.grips.size()) return false;
    for (std::size_t i=0;i<a.model.parts.size();++i) {
        const auto& p=a.model.parts[i]; const auto& q=data.model.parts[i];
        if (p.name!=q.name || p.material!=q.material || p.shape!=q.shape || p.center!=q.center || p.size!=q.size ||
            p.color!=q.color || p.topSize!=q.topSize || !sameRotation(p.rotation,q.rotation)) return false;
    }
    for (std::size_t i=0;i<a.grips.size();++i)
        if (a.grips[i].position!=data.grips[i].position || !sameRotation(a.grips[i].rotation,data.grips[i].rotation)) return false;
    for (std::size_t i=0;i<a.model.mesh.size();++i)
        if (a.model.mesh[i].position!=data.model.mesh[i].position ||
            glm::distance(glm::normalize(a.model.mesh[i].normal),glm::normalize(data.model.mesh[i].normal))>1e-6f ||
            a.model.mesh[i].color!=data.model.mesh[i].color) return false;
    return true;
}
viewer::Item restore(const viewer::PlayerInventory& catalog,const std::string& id,const std::string& prototype,
    const std::optional<viewer::CraftedItemData>& crafted) {
    if (crafted) {
        if (crafted->id!=id || crafted->prototype!=prototype) throw std::runtime_error("Custom item identity disagrees with transfer");
        return viewer::restoreCraftedItem(*crafted);
    }
    const auto* definition=catalog.find(prototype);
    if (!definition) throw std::runtime_error("Unknown item prototype");
    return definition->renamed(id);
}
}
InventoryState captureInventory(const viewer::PlayerInventory& inventory) {
    InventoryState state;
    state.hotbar = inventory.hotbar();
    for (const auto& [id, item] : inventory.items()) {
        state.items.push_back({id, item.prototype(), static_cast<std::uint32_t>(inventory.quantity(id)), inventory.placements().at(id)});
        if (item.custom()) state.items.back().crafted=viewer::captureCraftedItem(item);
    }
    return state;
}
bool applyInventory(viewer::PlayerInventory& inventory, const viewer::PlayerInventory& catalog, const InventoryState& state, bool* layoutChanged) {
    bool changed = inventory.items().size() != state.items.size() || inventory.hotbar() != state.hotbar;
    for (const auto& entry : state.items) {
        const auto found = inventory.placements().find(entry.id);
        changed |= found == inventory.placements().end() || found->second.storage != entry.placement.storage ||
            found->second.x != entry.placement.x || found->second.y != entry.placement.y || found->second.rotation != entry.placement.rotation;
        if (entry.crafted) {
            const auto* current=inventory.find(entry.id);
            changed |= !current || !sameCrafting(*current,*entry.crafted);
        } else if (const auto* current=inventory.find(entry.id)) changed |= current->custom();
    }
    viewer::InventoryContents contents;
    contents.skills = catalog.skills();
    contents.hotbar = state.hotbar;
    for (const auto& entry : state.items) {
        if (contents.owned.count(entry.id)) return false;
        std::optional<viewer::Item> item;
        try { item=restore(catalog,entry.id,entry.prototype,entry.crafted); }
        catch (const std::exception&) { return false; }
        if (entry.quantity>static_cast<std::uint32_t>(item->inventory().maxStack) || (entry.crafted && entry.quantity!=0)) return false;
        contents.owned.emplace(entry.id,*item);
        contents.placements.emplace(entry.id, entry.placement);
        if (item->inventory().stackable) contents.quantities.emplace(entry.id, static_cast<int>(entry.quantity));
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
        if (entry.item.custom()) result.back().crafted=viewer::captureCraftedItem(entry.item);
        for (const auto& panel : pose.panels) result.back().panels.push_back({panel.center,panel.rotation});
    }
    return result;
}
void applyGround(viewer::GroundItems& ground, const viewer::PlayerInventory& catalog, const std::vector<GroundItemState>& state) {
    std::vector<viewer::GroundItemPose> poses;
    std::set<std::uint64_t> ids;
    for (const auto& entry : state) {
        const auto item=restore(catalog,entry.item,entry.prototype,entry.crafted);
        const auto* prototype=catalog.find(entry.prototype);
        if (!ids.insert(entry.id).second || entry.quantity > static_cast<std::uint32_t>(item.inventory().maxStack) ||
            (entry.slot == 255) != (item.inventory().type!="weapon") ||
            (item.hasTag("canopy") ? entry.panels.size()!=viewer::Canopy::panelCount : !entry.panels.empty()) ||
            (entry.crafted && entry.quantity!=0) ||
            (entry.slot != 255 && (!prototype || entry.slot>=viewer::weaponIds.size() || prototype->id() != viewer::weaponIds.at(entry.slot) ||
                entry.loaded > static_cast<std::uint32_t>(prototype->gameplay().magazineCapacity))))
            throw std::runtime_error("Invalid ground item definition");
        std::optional<viewer::WeaponItem> weapon;
        if (entry.slot != 255) weapon = viewer::WeaponItem{entry.slot, static_cast<int>(entry.loaded), entry.readyAt};
        poses.push_back({{entry.id, item, static_cast<int>(entry.quantity), weapon},
            {poses.size(), entry.center, entry.halfExtents, entry.rotation}});
        for (std::size_t p=0; p<entry.panels.size(); ++p)
            poses.back().panels.push_back({p,entry.panels[p].center,{.25f,viewer::Canopy::thickness*.5f,.25f},entry.panels[p].rotation});
        if (!entry.panels.empty() && (glm::distance(entry.panels[viewer::Canopy::rootPanel].center,entry.center)>.001f ||
            std::abs(glm::dot(entry.panels[viewer::Canopy::rootPanel].rotation,entry.rotation))<.99999f))
            throw std::runtime_error("Canopy root disagrees with its panel snapshot");
    }
    ground.synchronize(poses);
}
}
