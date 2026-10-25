#include "inventory_interaction.hpp"
#include "inventory_view.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace viewer {
InventoryInteraction::InventoryInteraction(Rml::ElementDocument& document, PlayerInventory& inventory)
    : document_(document), inventory_(inventory), drag_(inventory_) {
    auto element = document_.CreateElement("div");
    element->SetClassNames("inventory-ghost");
    ghost_ = document_.AppendChild(std::move(element));
    ghost_->SetProperty("display", "none");
    auto progress = document_.CreateElement("div");
    progress->SetClassNames("inventory-placement-progress");
    progress->SetInnerRML("<div class='placement-track'><div class='placement-fill'></div></div><div class='placement-label'></div>");
    progress_ = document_.AppendChild(std::move(progress));
    progressFill_ = progress_->GetChild(0)->GetChild(0);
    progressLabel_ = progress_->GetChild(1);
    progress_->SetProperty("display", "none");
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
        result.grid = element;
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
    destinationGrid_ = nullptr;
    populateInventory(document_, inventory_);
    populateInventoryDetails(document_, inventory_, selected_);
    document_.UpdateDocument();
}
void InventoryInteraction::rotate(int turns) {
    if (!drag_.held() || drag_.pending()) return;
    const auto& held = *drag_.held();
    const auto rectangle = inventoryRectangle(inventory_.items().at(held.item), {"", 0, 0, held.rotation});
    const int oldX = anchorX_, oldY = anchorY_;
    if (turns > 0) { anchorX_ = rectangle.height - 1 - oldY; anchorY_ = oldX; }
    else { anchorX_ = oldY; anchorY_ = rectangle.width - 1 - oldX; }
    drag_.rotate(turns);
    populateInventoryDetails(document_, inventory_, selected_);
}
void InventoryInteraction::ghost() {
    progress_->SetProperty("display", "none");
    if (!drag_.held()) { ghost_->SetProperty("display", "none"); return; }
    const auto hovered = hit();
    InventoryDropCheck check;
    float left = x_ - anchorX_ * cellWidth_, top = y_ - anchorY_ * cellHeight_;
    if (drag_.pending() && destinationGrid_) {
        const auto& pending = *drag_.pending();
        const auto offset = destinationGrid_->GetAbsoluteOffset(Rml::BoxArea::Content);
        const auto size = destinationGrid_->GetBox().GetSize(Rml::BoxArea::Content);
        const auto& grid = inventory_.layout().at(pending.destination.storage).grid;
        cellWidth_ = size.x / grid.width; cellHeight_ = size.y / grid.height;
        left = offset.x + pending.destination.x * cellWidth_;
        top = offset.y + pending.destination.y * cellHeight_;
        check = pending.result;
    } else if (!hovered.storage.empty()) {
        cellWidth_ = hovered.cellWidth; cellHeight_ = hovered.cellHeight;
        left = hovered.left + (hovered.x - anchorX_) * cellWidth_;
        top = hovered.top + (hovered.y - anchorY_) * cellHeight_;
        check = drag_.preview(hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_);
    }
    const auto& held = *drag_.held();
    const auto rectangle = inventoryRectangle(inventory_.items().at(held.item), {"", 0, 0, held.rotation});
    ghost_->SetProperty("display", "block");
    ghost_->SetClass("valid", check.result == InventoryDrop::Free);
    ghost_->SetClass("swap", check.result == InventoryDrop::Swap);
    ghost_->SetClass("placing", bool(drag_.pending()));
    for (const auto& property : {std::pair{Rml::PropertyId::Left, left}, {Rml::PropertyId::Top, top},
            {Rml::PropertyId::Width, rectangle.width * cellWidth_}, {Rml::PropertyId::Height, rectangle.height * cellHeight_}})
        ghost_->SetProperty(property.first, Rml::Property(property.second, Rml::Unit::PX));
    ghost_->SetInnerRML(Rml::StringUtilities::EncodeRml(inventory_.items().at(held.item).inventory().name));
    if (drag_.pending()) {
        const float scale = document_.GetContext()->GetDensityIndependentPixelRatio();
        const auto dimensions = document_.GetContext()->GetDimensions();
        const float width = 132 * scale, height = 30 * scale;
        const float barLeft = std::clamp(left, 0.f, std::max(0.f, dimensions.x - width));
        const float below = top + rectangle.height * cellHeight_ + 5 * scale;
        const float barTop = below + height <= dimensions.y ? below : std::max(0.f, top - height - 5 * scale);
        progress_->SetProperty("display", "block");
        progress_->SetProperty(Rml::PropertyId::Left, Rml::Property(barLeft, Rml::Unit::PX));
        progress_->SetProperty(Rml::PropertyId::Top, Rml::Property(barTop, Rml::Unit::PX));
        progressFill_->SetProperty(Rml::PropertyId::Width, Rml::Property(float(drag_.pending()->progress() * 100), Rml::Unit::PERCENT));
        std::ostringstream label;
        label << "PLACING / " << std::fixed << std::setprecision(1)
              << std::max(0.0, drag_.pending()->durationSeconds - drag_.pending()->elapsedSeconds) << "s";
        progressLabel_->SetInnerRML(label.str());
    }
}
void InventoryInteraction::cancel() {
    const bool changed = bool(drag_.held());
    drag_.cancel();
    pressed_ = false; wheel_ = 0;
    if (changed) refresh();
    ghost_->SetProperty("display", "none");
    progress_->SetProperty("display", "none");
}
void InventoryInteraction::process(const std::vector<InventoryPointerEvent>& events, int width, int height, double seconds) {
    const double elapsed = lastSeconds_ && std::isfinite(seconds) && seconds >= *lastSeconds_ ? seconds - *lastSeconds_ : 0;
    if (std::isfinite(seconds)) lastSeconds_ = seconds;
    // Newly requested placements start now, not at the preceding frame's time.
    // Input cancellation wins over a completion in this update.
    bool advancePending = bool(drag_.pending());
    x_ = normalizedX_ * width; y_ = normalizedY_ * height;
    for (const auto& event : events) {
        switch (event.action) {
        case InventoryPointerAction::Move:
            normalizedX_ = event.x; normalizedY_ = event.y;
            x_ = normalizedX_ * width; y_ = normalizedY_ * height;
            break;
        case InventoryPointerAction::Cancel: cancel(); advancePending = false; break;
        case InventoryPointerAction::Wheel:
            if (drag_.held() && !drag_.pending()) {
                wheel_ += event.x;
                while (std::abs(wheel_) >= 1) {
                    const int turn = wheel_ > 0 ? 1 : -1;
                    rotate(turn); wheel_ -= turn;
                }
            }
            break;
        case InventoryPointerAction::Press: {
            if (drag_.pending()) break;
            pressed_ = true;
            const auto hovered = hit();
            if (!drag_.held() && !hovered.item.empty()) {
                const auto placement = inventory_.placements().at(hovered.item);
                anchorX_ = hovered.x - placement.x; anchorY_ = hovered.y - placement.y;
                cellWidth_ = hovered.cellWidth; cellHeight_ = hovered.cellHeight;
                selected_ = hovered.item; wheel_ = 0;
                drag_.pickUp(selected_);
                refresh();
            }
            break;
        }
        case InventoryPointerAction::Release:
            if (pressed_ && drag_.held() && !drag_.pending()) {
                const auto hovered = hit();
                const auto check = drag_.drop(hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_);
                if (check.result != InventoryDrop::Rejected) {
                    if (drag_.pending()) destinationGrid_ = hovered.grid;
                    else if (drag_.held()) { selected_ = drag_.held()->item; anchorX_ = anchorY_ = 0; }
                    wheel_ = 0;
                    if (!drag_.pending()) refresh();
                }
            }
            pressed_ = false;
            break;
        }
    }
    if (advancePending && drag_.advance(elapsed)) {
        if (drag_.held()) { selected_ = drag_.held()->item; anchorX_ = anchorY_ = 0; }
        refresh();
    }
    ghost();
}
} // namespace viewer
