#pragma once

#include <string>
#include <unordered_map>

namespace Rml { class ElementDocument; }
namespace viewer {
struct HudState;
class PlayerInventory;

// Read-only medical presentation. Simulation and inventory retain ownership.
class HealthView {
public:
    explicit HealthView(Rml::ElementDocument& document) : document_(document) {}
    void update(const HudState& state, const PlayerInventory& inventory);
private:
    void markup(const char* id, const std::string& value);
    Rml::ElementDocument& document_;
    std::unordered_map<std::string, std::string> contents_;
};
}
