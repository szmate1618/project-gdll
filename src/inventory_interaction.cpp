#include "inventory_interaction.hpp"
#include "inventory_view.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace viewer {
InventoryInteraction::InventoryInteraction(Rml::ElementDocument& document, StorageLoadout storage, InventoryItems& inventory)
    : document_(document), storage_(std::move(storage)), inventory_(inventory), drag_(storage_, inventory_) {
    auto element = document_.CreateElement("div");
    element->SetClassNames("inventory-ghost");
    ghost_ = document_.AppendChild(std::move(element));
    ghost_->SetProperty("display", "none");
    refresh();
}
InventoryInteraction::Hit InventoryInteraction::hit() const {
    Hit result;
    auto* element = document_.GetContext()->GetElementAtPoint({x_, y_}, ghost_);
    for (; element && element != &document_; element = element->GetParentNode()) {
        if (result.item.empty()) result.item = element->GetAttribute<Rml::String>("data-item", "");
        const auto reference = element->GetAttribute<Rml::String>("data-storage", "");
        if (reference.empty()) continue;
        const auto found = drag_.layout().find(reference);
        if (found == drag_.layout().end()) break;
        const auto offset = element->GetAbsoluteOffset(Rml::BoxArea::Content);
        const auto size = element->GetBox().GetSize(Rml::BoxArea::Content);
        result.storage = reference;
        result.left = offset.x; result.top = offset.y;
        result.cellWidth = size.x / found->second.grid.width;
        result.cellHeight = size.y / found->second.grid.height;
        if (result.cellWidth <= 0 || result.cellHeight <= 0) return {};
        result.x = static_cast<int>(std::floor((x_ - offset.x) / result.cellWidth));
        result.y = static_cast<int>(std::floor((y_ - offset.y) / result.cellHeight));
        return result;
    }
    return {};
}
void InventoryInteraction::refresh() {
    populateInventory(document_, storage_, inventory_, drag_.held() ? drag_.held()->item : "");
    details();
    document_.UpdateDocument();
}
void InventoryInteraction::details() {
    const auto set = [&](const char* id, const std::string& value) {
        document_.GetElementById(id)->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
    };
    if (selected_.empty()) return;
    const auto& item = inventory_.owned.at(selected_);
    set("inventory-item-name", item.name);
    const auto tagged = [&](const char* tag) { return std::find(item.tags.begin(), item.tags.end(), tag) != item.tags.end(); };
    set("inventory-type", tagged("long_gun") ? "Primary weapon" : tagged("handgun") ? "Handgun" : tagged("grenade") ? "Grenade" : item.type);
    std::string tags;
    for (const auto& tag : item.tags) { if (!tags.empty()) tags += ", "; tags += tag; }
    set("inventory-tags", tags);
    const bool held = drag_.held() && drag_.held()->item == selected_;
    const auto placement = held ? ItemPlacement{"", 0, 0, drag_.held()->rotation} : inventory_.placements.at(selected_);
    const auto rectangle = inventoryRectangle(item, placement);
    set("inventory-size", std::to_string(rectangle.width) + " × " + std::to_string(rectangle.height) + " / " + std::to_string(placement.rotation) + "°");
    set("inventory-current-location", held ? "IN HAND" : placement.storage);
    std::ostringstream access;
    if (held) access << "—";
    else {
        const auto& grid = drag_.layout().at(placement.storage).grid;
        access << std::fixed << std::setprecision(2) << grid.accessTime + grid.depthTimePerRow * placement.y << "s";
    }
    set("inventory-access-time", access.str());
}
void InventoryInteraction::rotate(int turns) {
    if (!drag_.held()) return;
    const auto& held = *drag_.held();
    const auto rectangle = inventoryRectangle(inventory_.owned.at(held.item), {"", 0, 0, held.rotation});
    const int oldX = anchorX_, oldY = anchorY_;
    if (turns > 0) { anchorX_ = rectangle.height - 1 - oldY; anchorY_ = oldX; }
    else { anchorX_ = oldY; anchorY_ = rectangle.width - 1 - oldX; }
    drag_.rotate(turns);
    details();
}
void InventoryInteraction::ghost() {
    if (!drag_.held()) { ghost_->SetProperty("display", "none"); return; }
    const auto hovered = hit();
    InventoryDropCheck check;
    float left = x_ - anchorX_ * cellWidth_, top = y_ - anchorY_ * cellHeight_;
    if (!hovered.storage.empty()) {
        cellWidth_ = hovered.cellWidth; cellHeight_ = hovered.cellHeight;
        left = hovered.left + (hovered.x - anchorX_) * cellWidth_;
        top = hovered.top + (hovered.y - anchorY_) * cellHeight_;
        check = drag_.preview(hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_);
    }
    const auto& held = *drag_.held();
    const auto rectangle = inventoryRectangle(inventory_.owned.at(held.item), {"", 0, 0, held.rotation});
    ghost_->SetProperty("display", "block");
    ghost_->SetClass("valid", check.result == InventoryDrop::Free);
    ghost_->SetClass("swap", check.result == InventoryDrop::Swap);
    for (const auto& property : {std::pair{Rml::PropertyId::Left, left}, {Rml::PropertyId::Top, top},
            {Rml::PropertyId::Width, rectangle.width * cellWidth_}, {Rml::PropertyId::Height, rectangle.height * cellHeight_}})
        ghost_->SetProperty(property.first, Rml::Property(property.second, Rml::Unit::PX));
    ghost_->SetInnerRML(Rml::StringUtilities::EncodeRml(inventory_.owned.at(held.item).name));
}
void InventoryInteraction::cancel() {
    const bool changed = bool(drag_.held());
    drag_.cancel();
    pressed_ = false; wheel_ = 0;
    if (changed) refresh();
    ghost_->SetProperty("display", "none");
}
void InventoryInteraction::process(const std::vector<InventoryPointerEvent>& events, int width, int height) {
    x_ = normalizedX_ * width; y_ = normalizedY_ * height;
    for (const auto& event : events) {
        switch (event.action) {
        case InventoryPointerAction::Move:
            normalizedX_ = event.x; normalizedY_ = event.y;
            x_ = normalizedX_ * width; y_ = normalizedY_ * height;
            break;
        case InventoryPointerAction::Cancel: cancel(); break;
        case InventoryPointerAction::Wheel:
            if (drag_.held()) {
                wheel_ += event.x;
                while (std::abs(wheel_) >= 1) {
                    const int turn = wheel_ > 0 ? 1 : -1;
                    rotate(turn); wheel_ -= turn;
                }
            }
            break;
        case InventoryPointerAction::Press: {
            pressed_ = true;
            const auto hovered = hit();
            if (!drag_.held() && !hovered.item.empty()) {
                const auto placement = inventory_.placements.at(hovered.item);
                anchorX_ = hovered.x - placement.x; anchorY_ = hovered.y - placement.y;
                cellWidth_ = hovered.cellWidth; cellHeight_ = hovered.cellHeight;
                selected_ = hovered.item; wheel_ = 0;
                drag_.pickUp(selected_);
                refresh();
            }
            break;
        }
        case InventoryPointerAction::Release:
            if (pressed_ && drag_.held()) {
                const auto hovered = hit();
                const auto check = drag_.drop(hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_);
                if (check.result != InventoryDrop::Rejected) {
                    if (drag_.held()) { selected_ = drag_.held()->item; anchorX_ = anchorY_ = 0; }
                    wheel_ = 0;
                    refresh();
                }
            }
            pressed_ = false;
            break;
        }
    }
    ghost();
}
} // namespace viewer
