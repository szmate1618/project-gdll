#include "ground_items.hpp"
#include "inventory/item_footprint.hpp"
#include "item_model_geometry.hpp"
#include "object_interaction.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace viewer {
void GroundItems::registerPropSource(const void* owner,PropCapture capture,PropRemoval remove) {
    if (!owner || !capture || !remove) throw std::invalid_argument("Invalid world item source");
    for (auto& source:propSources_) if (source.owner==owner) {
        source.capture=std::move(capture); source.remove=std::move(remove); return;
    }
    propSources_.push_back({owner,std::move(capture),std::move(remove)});
}
void GroundItems::setWorldBoxes(BoxLayer& boxes) {
    registerPropSource(&boxes,[&boxes](const PhysicsProp& prop) {
        return captureBoxPropItem(boxes,prop,"Crate");
    },[&boxes](const PhysicsProp& prop) {
        const auto index=boxes.indexOf(prop);
        if (!index || !boxes.active(*index)) return false;
        boxes.retire(*index); return true;
    });
}
void GroundItems::calibrateStorage(const PlayerInventory& inventory) {
    std::vector<ItemFootprintReference> references;
    for (const auto& [id,item]:inventory.items()) {
        (void)id;
        if (item.custom()) continue;
        auto points=itemModelFootprintPoints(item.inventory().model);
        if (points.empty()) {
            const auto weapon=std::find(weaponIds.begin(),weaponIds.end(),item.prototype());
            if (weapon!=weaponIds.end()) points=storageModelPoints_[static_cast<std::size_t>(weapon-weaponIds.begin())];
        }
        if (!points.empty()) references.push_back({std::move(points),item.inventory().width,item.inventory().height});
    }
    const float scale=calibrateItemFootprint(references);
    if (std::isfinite(scale) && scale>0) storageCellsPerMetre_=scale;
    storageCalibrated_=true;
}
bool GroundItems::storeProp(const PhysicsProp& prop,PlayerInventory& inventory,Weapons& weapons,bool gripped) {
    if (!prop.valid() || inventory.held() || weapons.reloading() || (!gripped && prop.grabbed())) return false;
    if (const auto id=idOf(prop)) return store(*id,inventory,weapons,prop.center(),gripped);
    // Replicas never manufacture a second item while awaiting the authority.
    if (replica_ || !inventory.storage().backpack) return false;
    if (!storageCalibrated_) calibrateStorage(inventory);
    for (const auto& source:propSources_) {
        const auto geometry=source.capture(prop);
        if (!geometry) continue;
        if (!std::isfinite(geometry->model.massKg) || geometry->model.massKg<=0 ||
            geometry->model.massKg>ObjectInteraction::maximumLiftMass) return false;
        std::optional<Item> item;
        try {
            auto model=geometry->model;
            const auto bounds=itemModelBounds(model);
            const auto center=(bounds.minimum+bounds.maximum)*.5f;
            for (auto& part:model.parts) part.center-=center;
            for (auto& vertex:model.mesh) vertex.position-=center;
            const auto footprint=estimateItemFootprint(itemModelFootprintPoints(model),storageCellsPerMetre_);
            ItemInventoryConfig config;
            config.name=geometry->name.empty() ? "Object" : geometry->name;
            config.type="equipment"; config.width=footprint.width; config.height=footprint.height;
            config.acoustics=geometry->acoustics.empty() ? "crate" : geometry->acoustics;
            auto tags=geometry->tags;
            if (std::find(tags.begin(),tags.end(),"crafting")==tags.end()) tags.emplace_back("crafting");
            std::string id;
            do { id="world_prop_"+std::to_string(nextItemId_++); }
            while (inventory.owns(id) || std::any_of(entries_.begin(),entries_.end(),[&](const auto& entry) { return entry.item.id()==id; }));
            item=Item(id,std::move(config),WeaponConfig{},std::move(tags)).withModel(std::move(model));
        } catch (const std::invalid_argument&) { return false; }
          catch (const std::overflow_error&) { return false; }
        const auto reference=backpackStorageReference(inventory.storage().backpack->id);
        const auto& grid=inventory.layout().at(reference).grid;
        for (const int angle:{0,90}) for (int y=0;y<grid.height;++y) for (int x=0;x<grid.width;++x) {
            auto candidate=inventory;
            if (!candidate.insert(*item,0,{reference,x,y,angle})) continue;
            // The source owner validates the generation again before retiring it.
            // No inventory/world ownership is published on a rejected removal.
            if (!source.remove(prop)) return false;
            inventory=std::move(candidate); weapons.syncAmmo(inventory); return true;
        }
        return false;
    }
    return false;
}
}
