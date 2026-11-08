#pragma once
#include "settings.hpp"
#include <RmlUi/Core/EventListener.h>
#include <optional>
namespace Rml { class Element; class ElementDocument; }
namespace viewer {
// Presentation and pending edits only; the application applies GL/window changes.
class SettingsView final : public Rml::EventListener {
public:
    SettingsView(Rml::ElementDocument& document, const Settings& settings);
    ~SettingsView();
    void setSettings(const Settings& settings);
    std::optional<Settings> takeRequest();
    void ProcessEvent(Rml::Event& event) override;
private:
    Rml::Element& element(const char* id) const;
    void refresh();
    Rml::ElementDocument& document_;
    Settings settings_;
    std::optional<Settings> requested_;
    bool audioTab_ = false;
    bool syncing_ = false;
};
}
