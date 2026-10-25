#include "inventory_view.hpp"
#include "storage_config.hpp"
#include "item_config.hpp"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <iomanip>
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
std::string gridMarkup(const StorageGrid& grid, double cell) {
    std::string result = "<div class='storage-grid' style='width:" + number(grid.width * cell) + "dp'>";
    for (int row = 0; row < grid.height; ++row) {
        result += "<div class='storage-row' style='height:" + number(cell) + "dp'>";
        for (int column = 0; column < grid.width; ++column) result += "<div class='storage-cell'></div>";
        result += "</div>";
    }
    return result + "</div>";
}

// Coordinates refer to the whole inner panel, including space around the main
// grid. Clamp the complete card at edges so its label and timing stay readable.
std::string slotMarkup(const StorageSlot& slot, double panelWidth, double panelHeight, double maxGridHeight = 64) {
    constexpr double width = 108;
    const double cell = std::min({24.0, width / slot.grid.width, maxGridHeight / slot.grid.height});
    const double height = 28 + slot.grid.height * cell + 22;
    const double left = std::clamp((slot.x + 1) * panelWidth / 2 - width / 2, 0.0, panelWidth - width);
    const double top = std::clamp((slot.y + 1) * panelHeight / 2 - height / 2, 0.0, panelHeight - height);
    return "<div class='storage-slot' style='" + position(left, top) + "'>"
        "<div class='storage-label'>" + Rml::StringUtilities::EncodeRml(slot.name) + "</div>" +
        gridMarkup(slot.grid, cell) + "<div class='storage-time'>" + size(slot.grid) + " / " +
        number(slot.grid.accessTime) + "s</div></div>";
}
void set(Rml::ElementDocument& document, const char* id, const std::string& markup) {
    auto* element = document.GetElementById(id);
    if (!element) throw std::runtime_error(std::string("Missing inventory element: ") + id);
    element->SetInnerRML(markup);
}
} // namespace

void populateInventory(Rml::ElementDocument& document, const StorageLoadout& loadout) {
    std::string body;
    for (const auto& equipment : loadout.equipment)
        for (const auto& slot : equipment.slots) body += slotMarkup(slot, 348, 344);
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
        gridMarkup(grid, cell) + "</div>";
    // Shorter external grids leave room for top/bottom compartments around the
    // central grid, while retaining the same panel-relative coordinate frame.
    for (const auto& slot : backpack.slots) contents += slotMarkup(slot, 388, 344, 40);
    set(document, "backpack-storage", contents);
    set(document, "backpack-name", Rml::StringUtilities::EncodeRml(backpack.name));
    set(document, "backpack-size", size(grid));
    set(document, "backpack-access", "TOP " + number(grid.accessTime) + "s / BOTTOM " +
        number(grid.accessTime + (grid.height - 1) * grid.depthTimePerRow) + "s");
    set(document, "backpack-caption", std::to_string(grid.width * grid.height) +
        " internal cells · " + std::to_string(backpack.slots.size()) + " external compartments");
}

void populateActionHotbar(Rml::ElementDocument& document, const InventoryItems& inventory) {
    std::string contents;
    for (std::size_t slot = 0; slot < inventory.hotbar.size(); ++slot) {
        const auto& id = inventory.hotbar[slot];
        contents += "<div class='action-slot " + std::string(id.empty() ? "empty" : "item") +
            (slot == 9 ? " last" : "") + "'><span class='action-number'>" + std::to_string(slot) + "</span>";
        if (id.empty()) contents += "<div class='assignment-name'>Unassigned</div>";
        else {
            const auto& item = inventory.owned.at(id);
            const bool explosive = std::find(item.tags.begin(), item.tags.end(), "explosive") != item.tags.end();
            contents += "<span class='assignment-type'>ITEM</span><div class='assignment-square " +
                std::string(explosive ? "grenade" : "pistol") + "'></div><div class='assignment-name'>" +
                Rml::StringUtilities::EncodeRml(item.name) + "</div>";
        }
        contents += "</div>";
    }
    set(document, "hotbar-slots", contents);
}
} // namespace viewer
