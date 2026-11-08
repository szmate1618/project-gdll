#pragma once
#include "hud_pointer.hpp"
#include <optional>
#include <glm/vec2.hpp>
#include <vector>
#include <string>

namespace Rml { class ElementDocument; }
namespace viewer {
struct PresentationViewport;
enum class MissionMenuKey { Previous, Next, Activate };
enum class MissionMenuAction { Resume, Abandon };
class MissionMenu {
public:
    explicit MissionMenu(Rml::ElementDocument& document) : document_(document) {}
    void open();
    void key(MissionMenuKey key);
    void process(const std::vector<HudPointerEvent>& events, int width, int height,
                 const PresentationViewport& presentation);
    bool cancelConfirmation(); // Escape first returns from confirmation to the menu.
    std::optional<MissionMenuAction> takeAction();
private:
    void activate(const std::string& id);
    void updateFocus();
    Rml::ElementDocument& document_;
    glm::vec2 pointer_{-1};
    std::string pressed_;
    bool confirming_ = false;
    int selected_ = 0;
    std::optional<MissionMenuAction> action_;
};
}
