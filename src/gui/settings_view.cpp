#include "settings_view.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace viewer {
SettingsView::SettingsView(Rml::ElementDocument& document, const Settings& settings)
    : document_(document), settings_(settings) {
    refresh();
    document_.AddEventListener("click", this);
    document_.AddEventListener("change", this);
}
SettingsView::~SettingsView() {
    document_.RemoveEventListener("click", this);
    document_.RemoveEventListener("change", this);
}
Rml::Element& SettingsView::element(const char* id) const {
    auto* result = document_.GetElementById(id);
    if (!result) throw std::runtime_error(std::string("Missing settings element: ") + id);
    return *result;
}
void SettingsView::setSettings(const Settings& settings) {
    if (settings_.graphics.fullscreen == settings.graphics.fullscreen && settings_.graphics.resolution == settings.graphics.resolution &&
        settings_.graphics.distantScenery == settings.graphics.distantScenery && settings_.graphics.shadowCasters == settings.graphics.shadowCasters &&
        settings_.graphics.skipHighestDetail == settings.graphics.skipHighestDetail &&
        settings_.audio.volume == settings.audio.volume) return;
    settings_ = settings;
    refresh();
}
std::optional<Settings> SettingsView::takeRequest() { return std::exchange(requested_, {}); }
void SettingsView::refresh() {
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
    element("settings-graphics-tab").SetClass("selected", !audioTab_);
    element("settings-audio-tab").SetClass("selected", audioTab_);
    element("graphics-content").SetProperty("display", audioTab_ ? "none" : "block");
    element("audio-content").SetProperty("display", audioTab_ ? "block" : "none");
    slider("audio-volume", static_cast<int>(std::lround(settings_.audio.volume * 100)));
    text("audio-volume-value", std::to_string(static_cast<int>(std::lround(settings_.audio.volume * 100))) + "%");
    element("graphics-windowed").SetClass("selected", !settings_.graphics.fullscreen);
    element("graphics-fullscreen").SetClass("selected", settings_.graphics.fullscreen);
    slider("graphics-resolution", settings_.graphics.resolution);
    const auto size = renderResolutions.at(settings_.graphics.resolution);
    text("graphics-resolution-value", std::to_string(size.x) + " x " + std::to_string(size.y));
    slider("graphics-distant", settings_.graphics.distantScenery);
    text("graphics-distant-value", settings_.graphics.distantScenery == 3 ? "Disabled" :
        std::to_string(static_cast<int>(distantSceneryDistances.at(settings_.graphics.distantScenery))) + " m");
    constexpr std::array<const char*, 5> shadows{"None", "Buildings", "+Trees", "+NPCs", "+Objects"};
    constexpr std::array<const char*, 5> descriptions{"No dynamic shadows.", "Buildings only.",
        "Buildings and vegetation.", "Buildings, vegetation, zombies and dogs.",
        "Buildings, vegetation, zombies and dogs, cars and props."};
    const auto shadow = static_cast<int>(settings_.graphics.shadowCasters);
    slider("graphics-shadows", shadow);
    text("graphics-shadows-value", shadows.at(shadow));
    text("graphics-shadow-groups", descriptions.at(shadow));
    auto& checkbox = element("graphics-lod");
    if (settings_.graphics.skipHighestDetail && !checkbox.HasAttribute("checked")) checkbox.SetAttribute("checked", "checked");
    else if (!settings_.graphics.skipHighestDetail && checkbox.HasAttribute("checked")) checkbox.RemoveAttribute("checked");
    syncing_ = false;
}
void SettingsView::ProcessEvent(Rml::Event& event) {
    if (syncing_) return;
    auto* target = event.GetTargetElement();
    if (!target) return;
    const auto& id = target->GetId();
    if (event.GetType() == "click") {
        if (id == "settings-graphics-tab" || id == "settings-audio-tab") {
            audioTab_ = id == "settings-audio-tab";
            refresh();
            return;
        }
        if (id == "graphics-windowed") settings_.graphics.fullscreen = false;
        else if (id == "graphics-fullscreen") settings_.graphics.fullscreen = true;
        else if (id == "graphics-reset") settings_.graphics = GraphicsSettings{};
        else if (id == "audio-reset") settings_.audio = AudioSettings{};
        else return;
    } else if (event.GetType() == "change") {
        if (id == "graphics-lod") settings_.graphics.skipHighestDetail = target->HasAttribute("checked");
        else {
            const auto* input = dynamic_cast<Rml::ElementFormControlInput*>(target);
            if (!input) return;
            const int value = static_cast<int>(std::lround(std::stof(input->GetValue())));
            if (id == "audio-volume") settings_.audio.volume = std::clamp(value, 0, 100) / 100.f;
            else if (id == "graphics-resolution") settings_.graphics.resolution = std::clamp(value, 0, 2);
            else if (id == "graphics-distant") settings_.graphics.distantScenery = std::clamp(value, 0, 3);
            else if (id == "graphics-shadows") settings_.graphics.shadowCasters = static_cast<ShadowCasters>(std::clamp(value, 0, 4));
            else return;
        }
    } else return;
    requested_ = settings_;
    refresh();
}
}
