#pragma once
#include "hud_pointer.hpp"
#include "sleep_plan.hpp"
#include <glm/vec2.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rml { class Element; class ElementDocument; }
namespace viewer {
struct HudState;
struct PresentationViewport;
class SleepDrawer {
public:
    explicit SleepDrawer(Rml::ElementDocument& document);
    // Returns events not claimed by sleep controls, preserving held world items.
    std::vector<HudPointerEvent> process(const HudState& state, const std::vector<HudPointerEvent>& events,
        int width, int height, const PresentationViewport& presentation);
    std::optional<SleepAction> takeAction();
    void close();
private:
    bool contains(const char* id) const;
    void text(const char* id, const std::string& value);
    void update(const HudState& state);
    bool activate(const std::string& id, const HudState& state);
    Rml::ElementDocument& document_;
    std::unordered_map<std::string, Rml::Element*> elements_;
    std::unordered_map<std::string, std::string> text_;
    glm::vec2 pointer_{-1};
    std::uint16_t minutes_ = minimumSleepMinutes;
    std::uint64_t revision_ = 0;
    bool open_ = false, notice_ = false, available_ = false;
    std::string pressed_;
    std::optional<SleepAction> action_;
};
}
