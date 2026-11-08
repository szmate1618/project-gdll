#pragma once

#include "inventory/inventory_action.hpp"
#include <RmlUi/Core/EventListener.h>
#include <vector>
#include <string>
#include <unordered_map>

namespace Rml { class ElementDocument; }
namespace viewer {
struct HudState;
class PlayerInventory;

// Medical presentation and queued treatment requests. Simulation and inventory retain ownership.
class HealthView final : public Rml::EventListener {
public:
    explicit HealthView(Rml::ElementDocument& document);
    ~HealthView();
    void ProcessEvent(Rml::Event& event) override;
    std::vector<InventoryDragAction> takeActions();
    void update(const HudState& state, const PlayerInventory& inventory);
private:
    void markup(const char* id, const std::string& value);
    Rml::ElementDocument& document_;
    std::unordered_map<std::string, std::string> contents_;
    std::string bandageItem_;
    std::vector<InventoryDragAction> actions_;
};
}
