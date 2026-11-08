#include "command_wheel.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace viewer {
namespace {
constexpr std::array<const char*, 5> names{"CANCEL", "DANGER", "MOVE THERE", "BACKUP", "MARK LOCATION"};
constexpr std::array<const char*, 5> classes{"", "danger", "move", "backup", "mark"};
constexpr std::array<const char*, 5> pages{"inventory", "skills", "health", "map", "graphics"};

void quad(Rml::Mesh& mesh, Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f c, Rml::Vector2f d,
          Rml::ColourbPremultiplied color) {
    const int first = static_cast<int>(mesh.vertices.size());
    for (const auto point : {a, b, c, d}) mesh.vertices.push_back({point, color, {0, 0}});
    for (const int index : {0, 1, 2, 0, 2, 3}) mesh.indices.push_back(first + index);
}

// Inner actions use 90-degree sectors; the outer ring has five page choices.
// Meshes use framebuffer pixels; RmlUi supplies layout scaling and rendering state.
class CommandSector final : public Rml::Element {
public:
    explicit CommandSector(const Rml::String& tag) : Rml::Element(tag) {}
protected:
    void OnRender() override {
        const auto size = GetBox().GetSize(Rml::BoxArea::Content);
        const auto fill = GetProperty<Rml::Colourb>("sector-fill");
        const auto outline = GetProperty<Rml::Colourb>("sector-outline");
        const auto accent = GetProperty<Rml::Colourb>("sector-accent");
        if (!geometry_ || size != size_ || fill != fill_ || outline != outline_ || accent != accent_) {
            size_ = size; fill_ = fill; outline_ = outline; accent_ = accent;
            const bool page = GetAttribute<bool>("page", false);
            const float scale = std::min(size.x, size.y) /
                (page ? CommandPageGeometry::designSize : CommandSectorGeometry::designSize);
            if (scale <= 0) return;
            const Rml::Vector2f center = size * .5f;
            const float inner = (page ? CommandPageGeometry::innerRadius : CommandSectorGeometry::innerRadius) * scale;
            const float outer = (page ? CommandPageGeometry::outerRadius : CommandSectorGeometry::outerRadius) * scale;
            const float stroke = std::max(.75f, scale);
            constexpr float radians = 3.1415926536f / 180;
            const float start = (GetAttribute<int>("sector", 0) * (page ? 72 : 90) - (page ? 35 : 45)) * radians;
            const float sweep = (page ? 70 : 90) * radians;
            const auto point = [&](float radius, float angle) {
                return center + Rml::Vector2f(std::sin(angle), -std::cos(angle)) * radius;
            };
            Rml::Mesh mesh;
            const auto arc = [&](float inside, float outside, Rml::Colourb color) {
                constexpr int segments = CommandSectorGeometry::segments;
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
            // Only the outer arc carries the selection accent; the interior
            // and radial edges retain the neutral menu highlight.
            if (accent.alpha) arc(outer - std::max(1.f, 2 * scale), outer, accent);
            geometry_ = GetRenderManager()->MakeGeometry(std::move(mesh));
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
    }
private:
    Rml::Geometry geometry_;
    Rml::Vector2f size_;
    Rml::Colourb fill_, outline_, accent_;
};
}

void registerCommandWheelElements() {
    Rml::StyleSheetSpecification::RegisterProperty("sector-fill", "transparent", false).AddParser("color");
    Rml::StyleSheetSpecification::RegisterProperty("sector-outline", "transparent", false).AddParser("color");
    Rml::StyleSheetSpecification::RegisterProperty("sector-accent", "transparent", false).AddParser("color");
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
        const auto page = commandWheelPageSelection(offset);
        for (std::size_t i = 1; i < classes.size(); ++i)
            document_.GetElementById(Rml::String("command-") + classes[i])->SetClass("selected", std::size_t(selected) == i);
        for (std::size_t i = 0; i < pages.size(); ++i)
            document_.GetElementById(Rml::String("command-page-") + pages[i])->SetClass("selected",
                page && static_cast<std::size_t>(*page) == i + 1);
        const Rml::String label = page ? "OPEN" : selected == SignalKind::None ? "CANCEL" : "SEND";
        if (center_->GetInnerRML() != label) center_->SetInnerRML(label);
        const float length = std::hypot(offset.x, offset.y);
        if (!std::isfinite(length)) offset = {};
        else if (length > 0) offset *= (page ? 167.f : 36.f) / length;
        // Indicate direction at the selected ring's inner edge, clear of labels.
        cursor_->SetProperty(Rml::PropertyId::Display,
            Rml::Property(!page && selected == SignalKind::None ? Rml::Style::Display::None : Rml::Style::Display::Block));
        cursor_->SetProperty(Rml::PropertyId::Left, Rml::Property(148.5f + offset.x, Rml::Unit::DP));
        cursor_->SetProperty(Rml::PropertyId::Top, Rml::Property(148.5f + offset.y, Rml::Unit::DP));
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
