#include "graphics_view.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace viewer {
GraphicsView::GraphicsView(Rml::ElementDocument& document, const GraphicsSettings& settings)
    : document_(document), settings_(settings) {
    refresh();
    document_.AddEventListener("click", this);
    document_.AddEventListener("change", this);
}
GraphicsView::~GraphicsView() {
    document_.RemoveEventListener("click", this);
    document_.RemoveEventListener("change", this);
}
Rml::Element& GraphicsView::element(const char* id) const {
    auto* result = document_.GetElementById(id);
    if (!result) throw std::runtime_error(std::string("Missing graphics element: ") + id);
    return *result;
}
void GraphicsView::setSettings(const GraphicsSettings& settings) {
    if (settings_.fullscreen == settings.fullscreen && settings_.resolution == settings.resolution &&
        settings_.distantScenery == settings.distantScenery && settings_.shadowCasters == settings.shadowCasters &&
        settings_.skipHighestDetail == settings.skipHighestDetail) return;
    settings_ = settings;
    refresh();
}
std::optional<GraphicsSettings> GraphicsView::takeRequest() { return std::exchange(requested_, {}); }
void GraphicsView::refresh() {
    syncing_ = true;
    const auto text = [&](const char* id, const std::string& value) {
        auto& target = element(id);
        const auto encoded = Rml::StringUtilities::EncodeRml(value);
        if (target.GetInnerRML() != encoded) target.SetInnerRML(encoded);
    };
    const auto slider = [&](const char* id, int value) {
        auto& input = dynamic_cast<Rml::ElementFormControlInput&>(element(id));
        if (std::stof(input.GetValue()) != value) input.SetValue(std::to_string(value));
    };
    element("graphics-windowed").SetClass("selected", !settings_.fullscreen);
    element("graphics-fullscreen").SetClass("selected", settings_.fullscreen);
    slider("graphics-resolution", settings_.resolution);
    const auto size = renderResolutions.at(settings_.resolution);
    text("graphics-resolution-value", std::to_string(size.x) + " x " + std::to_string(size.y));
    slider("graphics-distant", settings_.distantScenery);
    text("graphics-distant-value", settings_.distantScenery == 3 ? "Disabled" :
        std::to_string(static_cast<int>(distantSceneryDistances.at(settings_.distantScenery))) + " m");
    constexpr std::array<const char*, 5> shadows{"None", "Buildings", "+Trees", "+NPCs", "+Objects"};
    constexpr std::array<const char*, 5> descriptions{"No dynamic shadows.", "Buildings only.",
        "Buildings and vegetation.", "Buildings, vegetation, zombies and dogs.",
        "Buildings, vegetation, zombies and dogs, cars and props."};
    const auto shadow = static_cast<int>(settings_.shadowCasters);
    slider("graphics-shadows", shadow);
    text("graphics-shadows-value", shadows.at(shadow));
    text("graphics-shadow-groups", descriptions.at(shadow));
    auto& checkbox = element("graphics-lod");
    if (settings_.skipHighestDetail && !checkbox.HasAttribute("checked")) checkbox.SetAttribute("checked", "checked");
    else if (!settings_.skipHighestDetail && checkbox.HasAttribute("checked")) checkbox.RemoveAttribute("checked");
    syncing_ = false;
}
void GraphicsView::ProcessEvent(Rml::Event& event) {
    if (syncing_) return;
    auto* target = event.GetTargetElement();
    if (!target) return;
    const auto& id = target->GetId();
    if (event.GetType() == "click") {
        if (id == "graphics-windowed") settings_.fullscreen = false;
        else if (id == "graphics-fullscreen") settings_.fullscreen = true;
        else return;
    } else if (event.GetType() == "change") {
        if (id == "graphics-lod") settings_.skipHighestDetail = target->HasAttribute("checked");
        else {
            const auto* input = dynamic_cast<Rml::ElementFormControlInput*>(target);
            if (!input) return;
            const int value = static_cast<int>(std::lround(std::stof(input->GetValue())));
            if (id == "graphics-resolution") settings_.resolution = std::clamp(value, 0, 2);
            else if (id == "graphics-distant") settings_.distantScenery = std::clamp(value, 0, 3);
            else if (id == "graphics-shadows") settings_.shadowCasters = static_cast<ShadowCasters>(std::clamp(value, 0, 4));
            else return;
        }
    } else return;
    requested_ = settings_;
    refresh();
}
}
