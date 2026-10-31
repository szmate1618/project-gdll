#include "inventory_interaction.hpp"
#include "inventory_view.hpp"
#include "action_hotbar.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

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
        if (!result.ground) result.ground = element->GetAttribute<unsigned long long>("data-ground", 0);
        const auto reference = element->GetAttribute<Rml::String>("data-storage", "");
        if (reference.empty()) continue;
        const auto found = drag_.layout().find(reference);
        if (reference != "ground" && found == drag_.layout().end()) break;
        const auto& grid = reference == "ground" ? groundLayout_.grid : found->second.grid;
        const auto offset = element->GetAbsoluteOffset(Rml::BoxArea::Content);
        const auto size = element->GetBox().GetSize(Rml::BoxArea::Content);
        result.storage = reference;
        result.grid = element;
        result.left = offset.x; result.top = offset.y;
        result.cellWidth = size.x / grid.width;
        result.cellHeight = size.y / grid.height;
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
    populateActionHotbar(document_, inventory_);
    details();
    document_.UpdateDocument();
}
std::vector<InventoryDragAction> InventoryInteraction::takeActions() {
    return std::exchange(actions_, {});
}
void InventoryInteraction::refreshLayout() {
    if (!drag_.held()) refresh();
}
void InventoryInteraction::setGround(GroundItems& ground, Weapons& weapons, const CollisionWorld& collision) {
    if (ground_ == &ground && weapons_ == &weapons && collision_ == &collision) return;
    ground_ = &ground; weapons_ = &weapons; collision_ = &collision;
    groundSignature_.clear();
}
void InventoryInteraction::refreshGround() {
    nearby_ = ground_ ? ground_->nearby(feet_) : std::vector<GroundItem>{};
    std::string signature = "ground";
    for (const auto& item : nearby_) signature += "/" + std::to_string(item.id) + ":" + std::to_string(item.quantity);
    if (signature != groundSignature_) {
        groundLayout_ = populateGroundInventory(document_, nearby_);
        groundSignature_ = signature;
        document_.UpdateDocument();
    }
}
void InventoryInteraction::details() {
    const auto hovered = hit();
    const auto found = std::find_if(nearby_.begin(), nearby_.end(), [&](const auto& entry) { return entry.id == hovered.ground; });
    populateInventoryDetails(document_, inventory_, hovered.item, found == nearby_.end() ? nullptr : &*found);
}
bool InventoryInteraction::canTake(const Hit& hovered) const {
    if (!groundHeld_ || hovered.storage.empty() || hovered.storage == "ground") return false;
    if (groundHeld_->weapon && weapons_->state(groundHeld_->weapon->slot).owned) return false;
    if (std::none_of(nearby_.begin(),nearby_.end(),[&](const auto& item) { return item.id == groundHeld_->id; })) return false;
    const auto& container = inventory_.layout().at(hovered.storage);
    const auto rectangle = inventoryRectangle(groundHeld_->item,
        {hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_, groundRotation_});
    return inventoryItemAllowed(container.grid, groundHeld_->item) && inventoryRectangleFits(container.grid, rectangle) &&
        std::none_of(container.items.begin(), container.items.end(), [&](const auto& other) { return inventoryRectanglesOverlap(rectangle, other); });
}
void InventoryInteraction::rotate(int turns) {
    if (groundHeld_) {
        const auto rectangle = inventoryRectangle(groundHeld_->item, {"",0,0,groundRotation_});
        const int oldX = anchorX_, oldY = anchorY_;
        if (turns > 0) { anchorX_ = rectangle.height - 1 - oldY; anchorY_ = oldX; }
        else { anchorX_ = oldY; anchorY_ = rectangle.width - 1 - oldX; }
        groundRotation_ = (groundRotation_ + turns * 90 + 360) % 360;
        return;
    }
    if (!drag_.held() || drag_.pending()) return;
    const auto& held = *drag_.held();
    const auto rectangle = inventoryRectangle(inventory_.items().at(held.item), {"", 0, 0, held.rotation});
    const int oldX = anchorX_, oldY = anchorY_;
    if (turns > 0) { anchorX_ = rectangle.height - 1 - oldY; anchorY_ = oldX; }
    else { anchorX_ = oldY; anchorY_ = rectangle.width - 1 - oldX; }
    drag_.rotate(turns);
    actions_.push_back({InventoryDragActionKind::Rotate, held.item, "", 0, 0, turns});
    details();
}
void InventoryInteraction::ghost() {
    progress_->SetProperty("display", "none");
    if (!drag_.held() && !groundHeld_) { ghost_->SetProperty("display", "none"); return; }
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
        if (groundHeld_) check.result = canTake(hovered) || hovered.storage == "ground" ? InventoryDrop::Free : InventoryDrop::Rejected;
        else if (hovered.storage == "ground") check.result = ground_ ? InventoryDrop::Free : InventoryDrop::Rejected;
        else check = drag_.preview(hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_);
    }
    const auto held = groundHeld_ ? HeldInventoryItem{groundHeld_->item.id(), groundRotation_} : *drag_.held();
    const auto& item = groundHeld_ ? groundHeld_->item : inventory_.items().at(held.item);
    const auto rectangle = inventoryRectangle(item, {"", 0, 0, held.rotation});
    ghost_->SetProperty("display", "block");
    ghost_->SetClass("valid", check.result == InventoryDrop::Free || check.result == InventoryDrop::Merge);
    ghost_->SetClass("swap", check.result == InventoryDrop::Swap);
    ghost_->SetClass("placing", bool(drag_.pending()));
    for (const auto& property : {std::pair{Rml::PropertyId::Left, left}, {Rml::PropertyId::Top, top},
            {Rml::PropertyId::Width, rectangle.width * cellWidth_}, {Rml::PropertyId::Height, rectangle.height * cellHeight_}})
        ghost_->SetProperty(property.first, Rml::Property(property.second, Rml::Unit::PX));
    ghost_->SetInnerRML(Rml::StringUtilities::EncodeRml(item.inventory().name) +
        (item.inventory().stackable ? "<span class='storage-item-quantity'>" + std::to_string(groundHeld_ ? groundHeld_->quantity : inventory_.quantity(held.item)) + "</span>" : ""));
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
    if (changed) actions_.push_back({InventoryDragActionKind::Cancel, drag_.held()->item, "", 0, 0, 0});
    drag_.cancel();
    wheel_ = 0;
    groundHeld_.reset();
    if (changed) refresh();
    ghost_->SetProperty("display", "none");
    progress_->SetProperty("display", "none");
}
void InventoryInteraction::process(const std::vector<HudPointerEvent>& events, int width, int height,
    const PresentationViewport& presentation, double seconds, glm::vec3 feet, glm::vec3 forward) {
    feet_ = feet; forward_ = forward;
    const double elapsed = lastSeconds_ && std::isfinite(seconds) && seconds >= *lastSeconds_ ? seconds - *lastSeconds_ : 0;
    if (std::isfinite(seconds)) lastSeconds_ = seconds;
    refreshGround();
    refreshInventoryQuantities(document_, inventory_);
    bool advancePending = bool(drag_.pending());
    const auto updatePointer = [&] {
        const auto point = presentation.scenePointer(normalizedX_, normalizedY_, width, height);
        x_ = point.x; y_ = point.y;
    };
    updatePointer();
    for (const auto& event : events) {
        switch (event.action) {
        case HudPointerAction::Move:
            normalizedX_ = event.x; normalizedY_ = event.y;
            updatePointer();
            break;
        case HudPointerAction::RightPress:
        case HudPointerAction::Cancel: cancel(); advancePending = false; break;
        case HudPointerAction::Wheel:
            if ((drag_.held() || groundHeld_) && !drag_.pending()) {
                wheel_ += event.x;
                while (std::abs(wheel_) >= 1) {
                    const int turn = wheel_ > 0 ? 1 : -1;
                    rotate(turn); wheel_ -= turn;
                }
            } else {
                document_.GetContext()->ProcessMouseMove(static_cast<int>(x_), static_cast<int>(y_), 0);
                document_.GetContext()->ProcessMouseWheel(-event.x, 0);
            }
            break;
        case HudPointerAction::Press: {
            if (drag_.pending()) break;
            const auto hovered = hit();
            if (groundHeld_) {
                if (hovered.storage == "ground") groundHeld_.reset();
                else if (canTake(hovered) && ground_->take(groundHeld_->id, inventory_, *weapons_,
                    {hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_, groundRotation_}, feet_)) {
                    actions_.push_back({InventoryDragActionKind::TakeGround, "", hovered.storage,
                        hovered.x - anchorX_, hovered.y - anchorY_, groundRotation_ / 90, groundHeld_->id});
                    groundHeld_.reset();
                    refresh(); refreshGround();
                }
            } else if (drag_.held()) {
                const auto item = drag_.held()->item;
                if (hovered.storage == "ground") {
                    if (ground_ && ground_->drop(inventory_, drag_, *weapons_, feet_, forward_, *collision_)) {
                        actions_.push_back({InventoryDragActionKind::DropGround, item, "", 0, 0, 0});
                        refresh(); refreshGround();
                    }
                } else {
                    const auto check = drag_.drop(hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_);
                    if (check.result != InventoryDrop::Rejected) {
                        actions_.push_back({InventoryDragActionKind::Drop, item, hovered.storage, hovered.x - anchorX_, hovered.y - anchorY_});
                        if (drag_.pending()) destinationGrid_ = hovered.grid;
                        else if (drag_.held()) anchorX_ = anchorY_ = 0;
                        wheel_ = 0;
                        if (!drag_.pending()) refresh();
                    }
                }
            } else if (hovered.ground) {
                const auto found = std::find_if(nearby_.begin(), nearby_.end(), [&](const auto& entry) { return entry.id == hovered.ground; });
                if (found != nearby_.end() && !ground_->pending(found->id)) {
                    groundHeld_ = *found; groundRotation_ = 0;
                    const auto rectangle = std::find_if(groundLayout_.items.begin(), groundLayout_.items.end(), [&](const auto& entry) { return entry.item == std::to_string(hovered.ground); });
                    anchorX_ = hovered.x - rectangle->x; anchorY_ = hovered.y - rectangle->y;
                    cellWidth_ = hovered.cellWidth; cellHeight_ = hovered.cellHeight; wheel_ = 0;
                }
            } else if (!hovered.item.empty()) {
                const auto placement = inventory_.placements().at(hovered.item);
                anchorX_ = hovered.x - placement.x; anchorY_ = hovered.y - placement.y;
                cellWidth_ = hovered.cellWidth; cellHeight_ = hovered.cellHeight;
                wheel_ = 0;
                if (drag_.pickUp(hovered.item)) {
                    actions_.push_back({InventoryDragActionKind::PickUp, hovered.item, "", 0, 0, 0});
                    refresh();
                }
            }
            break;
        }
        case HudPointerAction::Release: break; // The item follows the cursor between clicks.
        }
    }
    if (advancePending && drag_.advance(elapsed)) {
        if (drag_.held()) anchorX_ = anchorY_ = 0;
        refresh();
    }
    details();
    ghost();
}
} // namespace viewer
