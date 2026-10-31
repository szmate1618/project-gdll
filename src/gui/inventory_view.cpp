#include "inventory_view.hpp"
#include "inventory/storage_config.hpp"
#include "inventory/player_inventory.hpp"
#include "inventory/inventory_placement.hpp"
#include "inventory/inventory_access.hpp"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <functional>
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
std::string gridMarkup(const InventoryContainer& container, double cell, const std::map<std::string, Item>& items, const std::function<int(const std::string&)>& quantity, const std::string& reference, bool ground = false) {
    const auto& grid = container.grid;
    std::string result = "<div class='storage-grid' data-storage=\"" + Rml::StringUtilities::EncodeRml(reference) + "\" style='width:" + number(grid.width * cell) + "dp'>";
    for (int row = 0; row < grid.height; ++row) {
        result += "<div class='storage-row' style='height:" + number(cell) + "dp'>";
        for (int column = 0; column < grid.width; ++column) result += "<div class='storage-cell'></div>";
        result += "</div>";
    }
    for (const auto& rectangle : container.items) {
        const auto& item = items.at(rectangle.item);
        const bool explosive = std::find(item.tags().begin(), item.tags().end(), "explosive") != item.tags().end();
        result += "<div class='storage-item" + std::string(explosive ? " explosive" : "") + "' " + std::string(ground ? "data-ground" : "data-item") + "=\"" + Rml::StringUtilities::EncodeRml(rectangle.item) + "\" style='" +
            position(rectangle.x * cell, rectangle.y * cell) + "width:" + number(rectangle.width * cell) +
            "dp;height:" + number(rectangle.height * cell) + "dp;font-size:" + number(std::min(10.0, cell * .48)) + "dp'>";
        if (rectangle.width > 1 || rectangle.height > 1)
            result += "<div class='storage-item-label'>" + Rml::StringUtilities::EncodeRml(item.inventory().name) + "</div>";
        if (item.inventory().stackable)
            result += "<span class='storage-item-quantity' id='quantity-" + Rml::StringUtilities::EncodeRml(ground ? "ground-" + rectangle.item : item.id()) + "'>" +
                std::to_string(quantity(rectangle.item)) + "</span>";
        result += "</div>";
    }
    return result + "</div>";
}

std::string gridMarkup(const InventoryContainer& container, double cell, const PlayerInventory& inventory, const std::string& reference) {
    return gridMarkup(container, cell, inventory.items(), [&](const auto& id) { return inventory.quantity(id); }, reference);
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
    // Compact tall body grids leave room for two pouch rows below the slings.
    for (const auto& equipment : loadout.equipment)
        for (const auto& slot : equipment.slots)
            body += slotMarkup(slot, layout.at(bodyStorageReference(equipment.id, slot.id)), inventory, 348, 344, bodyStorageReference(equipment.id, slot.id), 48);
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
    const auto range = inventoryAccessRange(grid);
    set(document, "backpack-access", "ACCESS / " + number(range.best) + "–" + number(range.worst) + "s");
    set(document, "backpack-caption", std::to_string(grid.width * grid.height) +
        " internal cells · " + std::to_string(backpack.slots.size()) + " external compartments");
}

void refreshInventoryQuantities(Rml::ElementDocument& document, const PlayerInventory& inventory) {
    for (const auto& [id, item] : inventory.items()) {
        if (!item.inventory().stackable) continue;
        auto* element = document.GetElementById("quantity-" + id);
        if (!element) continue; // The held stack is drawn by the drag ghost.
        const auto quantity = std::to_string(inventory.quantity(id));
        if (element->GetInnerRML() != quantity) element->SetInnerRML(quantity);
    }
}

void populateInventoryDetails(Rml::ElementDocument& document, const PlayerInventory& inventory, const std::string& selected, const GroundItem* ground) {
    const auto set = [&](const char* id, const std::string& value) {
        auto* element = document.GetElementById(id);
        const auto encoded = Rml::StringUtilities::EncodeRml(value);
        if (element->GetInnerRML() != encoded) element->SetInnerRML(encoded);
    };
    if (!ground && (selected.empty() || !inventory.find(selected))) {
        set("inventory-item-name", "Hover over an item");
        for (const auto* id : {"inventory-type", "inventory-quantity", "inventory-size", "inventory-tags", "inventory-current-location", "inventory-access-time"}) set(id, "—");
        return;
    }
    const auto& item = ground ? ground->item : inventory.items().at(selected);
    set("inventory-item-name", item.inventory().name);
    const auto tagged = [&](const char* tag) { return std::find(item.tags().begin(), item.tags().end(), tag) != item.tags().end(); };
    set("inventory-type", tagged("long_gun") ? "Primary weapon" : tagged("handgun") ? "Handgun" : tagged("grenade") ? "Grenade" : item.inventory().type);
    set("inventory-quantity", item.inventory().stackable ?
        std::to_string(ground ? ground->quantity : inventory.quantity(selected)) + " / " + std::to_string(item.inventory().maxStack) : "1");
    std::string tags;
    for (const auto& tag : item.tags()) { if (!tags.empty()) tags += ", "; tags += tag; }
    set("inventory-tags", tags);
    const bool held = !ground && inventory.held() && inventory.held()->item == selected;
    const auto placement = ground ? ItemPlacement{"ground", 0, 0, 0} : held ? ItemPlacement{"", 0, 0, inventory.held()->rotation} : inventory.placements().at(selected);
    const auto rectangle = inventoryRectangle(item, placement);
    set("inventory-size", std::to_string(rectangle.width) + " × " + std::to_string(rectangle.height) + " / " + std::to_string(placement.rotation) + "°");
    set("inventory-current-location", ground ? "GROUND / WITHIN 2 METRES" : held ? "IN HAND" : placement.storage);
    std::ostringstream access;
    if (held || ground) access << "—";
    else {
        const auto time = inventoryAccess(inventory.layout().at(placement.storage), inventoryRectangle(item, placement));
        access << std::fixed << std::setprecision(2) << time.seconds << "s";
    }
    set("inventory-access-time", access.str());
}
InventoryContainer populateGroundInventory(Rml::ElementDocument& document, const std::vector<GroundItem>& entries) {
    InventoryContainer container{StorageGrid{16, 16, 0, {}, {}}, {}};
    std::map<std::string, Item> items;
    std::map<std::string, int> quantities;
    // Stable ID order keeps cells still while bodies settle or change distance.
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    int row = 0, column = 0, rowHeight = 0;
    for (const auto& entry : sorted) {
        const auto id = std::to_string(entry.id);
        const auto& dimensions = entry.item.inventory();
        if (column + dimensions.width > 16) { row += rowHeight; column = rowHeight = 0; }
        container.items.push_back({id, column, row, dimensions.width, dimensions.height});
        column += dimensions.width;
        rowHeight = std::max(rowHeight, dimensions.height);
        items.emplace(id, entry.item);
        quantities.emplace(id, entry.quantity);
    }
    container.grid.height = std::max(16, row + rowHeight + 4);
    auto* host = document.GetElementById("ground-storage");
    const auto markup = gridMarkup(container, 18, items, [&](const auto& id) { return quantities.at(id); }, "ground", true);
    if (host->GetInnerRML() != markup) host->SetInnerRML(markup);
    return container;
}
} // namespace viewer
