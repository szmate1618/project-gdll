#include "command_wheel.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace viewer {
namespace {
constexpr std::array<const char*, 4> names{"CANCEL", "DANGER", "MOVE THERE", "BACKUP"};
constexpr std::array<const char*, 4> classes{"", "danger", "move", "backup"};

void quad(Rml::Mesh& mesh, Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f c, Rml::Vector2f d,
          Rml::ColourbPremultiplied color) {
    const int first = static_cast<int>(mesh.vertices.size());
    for (const auto point : {a, b, c, d}) mesh.vertices.push_back({point, color, {0, 0}});
    for (const int index : {0, 1, 2, 0, 2, 3}) mesh.indices.push_back(first + index);
}

// Annular 120-degree choices share the input selector's top/right/left ordering.
// Meshes use framebuffer pixels; RmlUi supplies layout scaling and rendering state.
class CommandSector final : public Rml::Element {
public:
    explicit CommandSector(const Rml::String& tag) : Rml::Element(tag) {}
protected:
    void OnRender() override {
        const auto size = GetBox().GetSize(Rml::BoxArea::Content);
        const auto fill = GetProperty<Rml::Colourb>("sector-fill");
        const auto outline = GetProperty<Rml::Colourb>("sector-outline");
        if (!geometry_ || size != size_ || fill != fill_ || outline != outline_) {
            size_ = size; fill_ = fill; outline_ = outline;
            const float scale = std::min(size.x, size.y) / 380;
            if (scale <= 0) return;
            const Rml::Vector2f center = size * .5f;
            const float inner = 52 * scale, outer = 189 * scale;
            const float stroke = std::max(.75f, scale);
            constexpr float radians = 3.1415926536f / 180;
            const float start = (GetAttribute<int>("sector", 0) * 120 - 60) * radians;
            constexpr float sweep = 120 * radians;
            const auto point = [&](float radius, float angle) {
                return center + Rml::Vector2f(std::sin(angle), -std::cos(angle)) * radius;
            };
            Rml::Mesh mesh;
            const auto arc = [&](float inside, float outside, Rml::Colourb color) {
                constexpr int segments = 64;
                for (int i = 0; i < segments; ++i) {
                    const float a = start + sweep * i / segments, b = start + sweep * (i + 1) / segments;
                    quad(mesh, point(inside, a), point(outside, a), point(outside, b), point(inside, b), color.ToPremultiplied());
                }
            };
            arc(inner, outer, fill);
            arc(inner, inner + stroke, outline);
            arc(outer - stroke, outer, outline);
            for (const float angle : {start, start + sweep}) {
                const auto side = Rml::Vector2f(std::cos(angle), std::sin(angle)) * (stroke * .5f);
                quad(mesh, point(inner, angle) - side, point(outer, angle) - side,
                     point(outer, angle) + side, point(inner, angle) + side, outline.ToPremultiplied());
            }
            geometry_ = GetRenderManager()->MakeGeometry(std::move(mesh));
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
    }
private:
    Rml::Geometry geometry_;
    Rml::Vector2f size_;
    Rml::Colourb fill_, outline_;
};
}

void registerCommandWheelElements() {
    Rml::StyleSheetSpecification::RegisterProperty("sector-fill", "transparent", false).AddParser("color");
    Rml::StyleSheetSpecification::RegisterProperty("sector-outline", "transparent", false).AddParser("color");
    // The factory borrows this stateless instancer across successive HUD lifetimes.
    static Rml::ElementInstancerGeneric<CommandSector> instancer;
    Rml::Factory::RegisterElementInstancer("command-sector", &instancer);
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
