#include "command_wheel.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace viewer {
namespace {
constexpr std::array<const char*, 4> names{"CANCEL", "DANGER", "MOVE THERE", "BACKUP"};
constexpr std::array<const char*, 4> classes{"", "danger", "move", "backup"};
}
CommandWheelView::CommandWheelView(Rml::ElementDocument& document) : document_(document) {
    wheel_ = document.GetElementById("command-wheel");
    cursor_ = document.GetElementById("command-cursor");
    center_ = document.GetElementById("command-center-label");
    markers_ = document.GetElementById("signal-markers");
    if (!wheel_ || !cursor_ || !center_ || !markers_) throw std::runtime_error("Incomplete command wheel layout");
}
void CommandWheelView::update(bool open, glm::vec2 offset, const std::vector<HudSignal>& signals) {
    document_.SetClass("commanding", open);
    if (open) {
        const auto selected = signalWheelSelection(offset);
        for (std::size_t i = 1; i < classes.size(); ++i)
            document_.GetElementById(Rml::String("command-") + classes[i])->SetClass("selected", std::size_t(selected) == i);
        const Rml::String label = selected == SignalKind::None ? "CANCEL" : "SEND";
        if (center_->GetInnerRML() != label) center_->SetInnerRML(label);
        const float length = std::hypot(offset.x, offset.y);
        if (!std::isfinite(length)) offset = {};
        else if (length > 0) offset *= 45 / length;
        // Indicate direction on the center ring without covering command labels.
        cursor_->SetProperty(Rml::PropertyId::Display,
            Rml::Property(selected == SignalKind::None ? Rml::Style::Display::None : Rml::Style::Display::Block));
        cursor_->SetProperty(Rml::PropertyId::Left, Rml::Property(187 + offset.x, Rml::Unit::DP));
        cursor_->SetProperty(Rml::PropertyId::Top, Rml::Property(187 + offset.y, Rml::Unit::DP));
    }
    // Reuse marker elements across frames; positions may change every camera update.
    while (markers.size() < signals.size()) {
        auto marker = document_.CreateElement("div");
        markers.push_back(markers_->AppendChild(std::move(marker)));
    }
    for (std::size_t i = 0; i < markers.size(); ++i) {
        auto& marker = *markers[i];
        marker.SetProperty(Rml::PropertyId::Display,
            Rml::Property(i < signals.size() ? Rml::Style::Display::Block : Rml::Style::Display::None));
        if (i >= signals.size()) continue;
        const auto& signal = signals[i];
        const auto kind = std::size_t(signal.kind);
        if (kind == 0 || kind >= names.size()) continue;
        const auto classNames = Rml::String("signal-marker ") + classes[kind] + (signal.atEdge ? " edge" : "");
        if (marker.GetClassNames() != classNames) {
            marker.SetClassNames(classNames);
            marker.SetInnerRML(names[kind]);
        }
        marker.SetProperty(Rml::PropertyId::Left, Rml::Property(signal.position.x * 100, Rml::Unit::PERCENT));
        marker.SetProperty(Rml::PropertyId::Top, Rml::Property(signal.position.y * 100, Rml::Unit::PERCENT));
    }
}
} // namespace viewer
