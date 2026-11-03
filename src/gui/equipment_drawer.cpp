#include "equipment_drawer.hpp"
#include "hud.hpp"
#include "inventory/player_inventory.hpp"
#include "inventory/inventory_access.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <iomanip>
#include <sstream>
#include <utility>

namespace viewer {
namespace {
std::string storageName(const StorageLoadout& storage, const std::string& reference) {
    for (const auto& equipment : storage.equipment)
        for (const auto& slot : equipment.slots)
            if (bodyStorageReference(equipment.id, slot.id) == reference) return slot.name;
    if (storage.backpack) {
        if (backpackStorageReference(storage.backpack->id) == reference) return storage.backpack->name;
        for (const auto& slot : storage.backpack->slots)
            if (backpackStorageReference(storage.backpack->id, slot.id) == reference) return slot.name;
    }
    return {};
}
}
EquipmentDrawer::EquipmentDrawer(Rml::ElementDocument& document, PlayerInventory& inventory)
    : document_(document), inventory_(inventory), drawer_(document.GetElementById("equipment-drawer")),
      list_(document.GetElementById("equipment-list")), status_(document.GetElementById("equipment-status")) {}
bool EquipmentDrawer::contains(Rml::Element& element) const {
    const auto origin = element.GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = element.GetBox().GetSize(Rml::BoxArea::Border);
    return pointer_.x >= origin.x && pointer_.x < origin.x + size.x &&
        pointer_.y >= origin.y && pointer_.y < origin.y + size.y;
}
void EquipmentDrawer::populate() {
    std::ostringstream html;
    for (const auto& [id, item] : inventory_.items()) {
        const auto found = inventory_.placements().find(id);
        if (found == inventory_.placements().end()) continue;
        const auto& placement = found->second;
        const auto access = inventoryAccess(inventory_.layout().at(placement.storage), inventoryRectangle(item, placement));
        const auto encode = [](const std::string& value) { return Rml::StringUtilities::EncodeRml(value); };
        html << "<div class=\"equipment-row\" item=\"" << encode(id) << "\"><div class=\"equipment-name\">"
             << encode(item.inventory().name);
        if (item.inventory().stackable) html << " × " << inventory_.quantity(id);
        html << "</div><div class=\"equipment-access\">" << std::fixed << std::setprecision(1)
             << access.seconds << "s · " << encode(storageName(inventory_.storage(), placement.storage)) << "</div></div>";
    }
    if (html.str().empty()) html << "<div class=\"equipment-empty\">No equipment</div>";
    const auto next = html.str();
    if (next != signature_) { signature_ = next; list_->SetInnerRML(next); }
}
void EquipmentDrawer::process(const HudState& state, const std::vector<HudPointerEvent>& events,
    int width, int height, const PresentationViewport& presentation) {
    const bool visible = state.objectHandling && state.screen == HudScreen::Gameplay;
    drawer_->SetClass("available", visible);
    if (!visible || state.mouseCaptured) open_ = false;
    if (visible) populate();
    auto* context = document_.GetContext();
    for (const auto& event : events) {
        if (event.action == HudPointerAction::Cancel) {
            if (pressed_ || state.handInventoryActive) actions_.push_back({InventoryDragActionKind::HandRelease, "", ""});
            pressed_ = false; open_ = false; pointer_ = {-1,-1};
        }
        if (!visible || state.mouseCaptured) continue;
        if (event.action == HudPointerAction::Move) {
            pointer_ = presentation.scenePointer(event.x, event.y, width, height);
            context->ProcessMouseMove(int(pointer_.x), int(pointer_.y), 0);
            open_ = contains(*drawer_);
            drawer_->SetClass("expanded", open_);
        }
        if (event.action == HudPointerAction::Wheel && open_)
            list_->SetScrollTop(list_->GetScrollTop() - event.x * 48 * context->GetDensityIndependentPixelRatio());
        if (event.action == HudPointerAction::Press && open_ && contains(*list_)) {
            for (int index = 0; index < list_->GetNumChildren(); ++index) {
                auto* row = list_->GetChild(index);
                if (!row->HasAttribute("item") || !contains(*row)) continue;
                if (!state.handInventoryActive && state.weaponSwitchRemaining <= 0 && state.reloadRemaining <= 0) {
                    actions_.push_back({InventoryDragActionKind::HandPickUp, row->GetAttribute<Rml::String>("item", ""), ""});
                    pressed_ = true;
                }
                break;
            }
        }
        if (event.action == HudPointerAction::Release) {
            actions_.push_back({contains(*drawer_) && open_ ? InventoryDragActionKind::HandStore : InventoryDragActionKind::HandRelease, "", ""});
            pressed_ = false;
        }
    }
    for (int index = 0; index < list_->GetNumChildren(); ++index) {
        auto* row = list_->GetChild(index);
        row->SetClass("hovered", open_ && contains(*list_) && contains(*row));
    }
    drawer_->SetClass("expanded", open_);
    std::ostringstream status;
    if (state.handAccessRemaining > 0) status << "Retrieving · " << std::fixed << std::setprecision(1) << state.handAccessRemaining << "s";
    else status << "Drag out to hold · release here to store";
    if (statusText_ != status.str()) { statusText_ = status.str(); status_->SetInnerRML(statusText_); }
}
std::vector<InventoryDragAction> EquipmentDrawer::takeActions() { return std::exchange(actions_, {}); }
}
