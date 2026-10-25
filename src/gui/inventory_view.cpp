#include "inventory_view.hpp"
#include "inventory/storage_config.hpp"
#include "inventory/player_inventory.hpp"
#include "inventory/inventory_placement.hpp"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <iomanip>
#include <RmlUi/Core.h>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace viewer {
namespace {
std::string number(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}
std::string size(const StorageGrid& grid) {
    return std::to_string(grid.width) + " × " + std::to_string(grid.height);
}
std::string position(double x, double y) {
    return "left:" + number(x) + "dp;top:" + number(y) + "dp;";
}
std::string gridMarkup(const InventoryContainer& container, double cell, const PlayerInventory& inventory, const std::string& reference) {
    const auto& grid = container.grid;
    std::string result = "<div class='storage-grid' data-storage=\"" + Rml::StringUtilities::EncodeRml(reference) + "\" style='width:" + number(grid.width * cell) + "dp'>";
    for (int row = 0; row < grid.height; ++row) {
        result += "<div class='storage-row' style='height:" + number(cell) + "dp'>";
        for (int column = 0; column < grid.width; ++column) result += "<div class='storage-cell'></div>";
        result += "</div>";
    }
    for (const auto& rectangle : container.items) {
        const auto& item = inventory.items().at(rectangle.item);
        const bool explosive = std::find(item.tags().begin(), item.tags().end(), "explosive") != item.tags().end();
        result += "<div class='storage-item" + std::string(explosive ? " explosive" : "") + "' data-item=\"" + Rml::StringUtilities::EncodeRml(item.id()) + "\" style='" +
            position(rectangle.x * cell, rectangle.y * cell) + "width:" + number(rectangle.width * cell) +
            "dp;height:" + number(rectangle.height * cell) + "dp;font-size:" + number(std::min(10.0, cell * .48)) + "dp'>";
        if (rectangle.width > 1 || rectangle.height > 1)
            result += "<div class='storage-item-label'>" + Rml::StringUtilities::EncodeRml(item.inventory().name) + "</div>";
        result += "</div>";
    }
    return result + "</div>";
}

// Coordinates refer to the whole inner panel, including space around the main
// grid. Clamp the complete card at edges so its label and timing stay readable.
std::string slotMarkup(const StorageSlot& slot, const InventoryContainer& container, const PlayerInventory& inventory,
                       double panelWidth, double panelHeight, const std::string& reference, double maxGridHeight = 64) {
    constexpr double width = 108;
    const double cell = std::min({24.0, width / slot.grid.width, maxGridHeight / slot.grid.height});
    const double height = 28 + slot.grid.height * cell + 22 + 14;
    const double left = std::clamp((slot.x + 1) * panelWidth / 2 - width / 2, 0.0, panelWidth - width);
    const double top = std::clamp((slot.y + 1) * panelHeight / 2 - height / 2, 0.0, panelHeight - height);
    std::string names;
    for (const auto& placed : container.items) {
        if (!names.empty()) names += " / ";
        names += Rml::StringUtilities::EncodeRml(inventory.items().at(placed.item).inventory().name);
    }
    return "<div class='storage-slot' style='" + position(left, top) + "'>"
        "<div class='storage-label'>" + Rml::StringUtilities::EncodeRml(slot.name) + "</div>" +
        gridMarkup(container, cell, inventory, reference) +
        "<div class='storage-occupants'>" + names + "</div>" +
        "<div class='storage-time'>" + size(slot.grid) + " / " +
        number(slot.grid.accessTime) + "s</div></div>";
}
void set(Rml::ElementDocument& document, const char* id, const std::string& markup) {
    auto* element = document.GetElementById(id);
    if (!element) throw std::runtime_error(std::string("Missing inventory element: ") + id);
    element->SetInnerRML(markup);
}
} // namespace

void populateInventory(Rml::ElementDocument& document, const PlayerInventory& inventory) {
    const auto& loadout = inventory.storage();
    const auto& layout = inventory.layout();
    std::string body;
    for (const auto& equipment : loadout.equipment)
        for (const auto& slot : equipment.slots)
            body += slotMarkup(slot, layout.at(bodyStorageReference(equipment.id, slot.id)), inventory, 348, 344, bodyStorageReference(equipment.id, slot.id));
    if (body.empty()) body = "<div class='storage-empty'>NO BODY STORAGE EQUIPPED</div>";
    set(document, "body-storage-slots", body);
    set(document, "body-storage-count", std::to_string(loadout.equipment.size()) + " EQUIPPED");

    if (!loadout.backpack) {
        set(document, "backpack-name", "BACKPACK");
        set(document, "backpack-size", "NONE");
        set(document, "backpack-storage", "<div class='storage-empty'>NO BACKPACK EQUIPPED</div>");
        set(document, "backpack-access", "");
        set(document, "backpack-caption", "");
        return;
    }
    const auto& backpack = *loadout.backpack;
    const auto& grid = backpack.grid;
    const double cell = std::min({36.0, 148.0 / grid.width, 160.0 / grid.height});
    const double top = (344 - grid.height * cell) / 2;
    std::string contents = "<div class='main-storage' style='" +
        position((388 - grid.width * cell) / 2, top) + "'>" +
        gridMarkup(layout.at(backpackStorageReference(backpack.id)), cell, inventory, backpackStorageReference(backpack.id)) + "</div>";
    // Shorter external grids leave room for top/bottom compartments around the
    // central grid, while retaining the same panel-relative coordinate frame.
    for (const auto& slot : backpack.slots)
        contents += slotMarkup(slot, layout.at(backpackStorageReference(backpack.id, slot.id)), inventory, 388, 344, backpackStorageReference(backpack.id, slot.id), 26);
    set(document, "backpack-storage", contents);
    set(document, "backpack-name", Rml::StringUtilities::EncodeRml(backpack.name));
    set(document, "backpack-size", size(grid));
    set(document, "backpack-access", "TOP " + number(grid.accessTime) + "s / BOTTOM " +
        number(grid.accessTime + (grid.height - 1) * grid.depthTimePerRow) + "s");
    set(document, "backpack-caption", std::to_string(grid.width * grid.height) +
        " internal cells · " + std::to_string(backpack.slots.size()) + " external compartments");
}

void populateInventoryDetails(Rml::ElementDocument& document, const PlayerInventory& inventory, const std::string& selected) {
    const auto set = [&](const char* id, const std::string& value) {
        document.GetElementById(id)->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
    };
    if (selected.empty()) return;
    const auto& item = inventory.items().at(selected);
    set("inventory-item-name", item.inventory().name);
    const auto tagged = [&](const char* tag) { return std::find(item.tags().begin(), item.tags().end(), tag) != item.tags().end(); };
    set("inventory-type", tagged("long_gun") ? "Primary weapon" : tagged("handgun") ? "Handgun" : tagged("grenade") ? "Grenade" : item.inventory().type);
    std::string tags;
    for (const auto& tag : item.tags()) { if (!tags.empty()) tags += ", "; tags += tag; }
    set("inventory-tags", tags);
    const bool held = inventory.held() && inventory.held()->item == selected;
    const auto placement = held ? ItemPlacement{"", 0, 0, inventory.held()->rotation} : inventory.placements().at(selected);
    const auto rectangle = inventoryRectangle(item, placement);
    set("inventory-size", std::to_string(rectangle.width) + " × " + std::to_string(rectangle.height) + " / " + std::to_string(placement.rotation) + "°");
    set("inventory-current-location", held ? "IN HAND" : placement.storage);
    std::ostringstream access;
    if (held) access << "—";
    else {
        const auto& grid = inventory.layout().at(placement.storage).grid;
        access << std::fixed << std::setprecision(2) << grid.accessTime + grid.depthTimePerRow * placement.y << "s";
    }
    set("inventory-access-time", access.str());
}
} // namespace viewer
