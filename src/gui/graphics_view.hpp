#pragma once
#include "graphics_settings.hpp"
#include <RmlUi/Core/EventListener.h>
#include <optional>
namespace Rml { class Element; class ElementDocument; }
namespace viewer {
// Presentation and pending edits only; the application applies GL/window changes.
class GraphicsView final : public Rml::EventListener {
public:
    GraphicsView(Rml::ElementDocument& document, const GraphicsSettings& settings);
    ~GraphicsView();
    void setSettings(const GraphicsSettings& settings);
    std::optional<GraphicsSettings> takeRequest();
    void ProcessEvent(Rml::Event& event) override;
private:
    Rml::Element& element(const char* id) const;
    void refresh();
    Rml::ElementDocument& document_;
    GraphicsSettings settings_;
    std::optional<GraphicsSettings> requested_;
    bool syncing_ = false;
};
}
