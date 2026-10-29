#include "gui/map_view.hpp"
#include "gui/hud.hpp"
#include "gui/hud_render_interface.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace viewer {
namespace {
std::string distanceLabel(double meters) {
    std::ostringstream text;
    if (meters < 1000) text << std::lround(meters) << " m";
    else text << std::fixed << std::setprecision(2) << meters / 1000 << " km";
    return text.str();
}
void text(Rml::Element& element, const std::string& value) {
    const auto encoded = Rml::StringUtilities::EncodeRml(value);
    if (element.GetInnerRML() != encoded) element.SetInnerRML(encoded);
}
bool inside(glm::dvec2 point, glm::dvec2 low, glm::dvec2 high) {
    return point.x >= low.x && point.y >= low.y && point.x <= high.x && point.y <= high.y;
}
bool hit(Rml::Element& element, glm::dvec2 point) {
    const auto offset = element.GetAbsoluteOffset(Rml::BoxArea::Border), size = element.GetBox().GetSize(Rml::BoxArea::Border);
    return inside(point, {offset.x, offset.y}, {offset.x + size.x, offset.y + size.y});
}
class MapElement final : public Rml::Element {
public:
    explicit MapElement(const Rml::String& tag) : Rml::Element(tag) {}
    MapView* view = nullptr;
    HudRenderInterface* renderer = nullptr;
    const std::vector<MapPlayer>* players = nullptr;
protected:
    void OnRender() override {
        if (!view || !renderer) return;
        const auto region = view->imageRegion();
        if (!region) return;
        const auto rml = [](glm::dvec2 p) { return Rml::Vector2f(float(p.x), float(p.y)); };
        renderer->drawSatelliteMap(rml(region->offset), rml(region->size), glm::vec2(region->uvOrigin), glm::vec2(region->uvExtent));
        const double scale = GetContext()->GetDensityIndependentPixelRatio();
        const auto low = region->offset, high = low + region->size;
        Rml::Mesh mesh;
        const auto route = GetProperty<Rml::Colourb>("map-line-color").ToPremultiplied();
        const auto outline = Rml::Colourb(35, 37, 39).ToPremultiplied();
        const auto triangle = [&](glm::dvec2 a, glm::dvec2 b, glm::dvec2 c, Rml::ColourbPremultiplied color) {
            const int first = static_cast<int>(mesh.vertices.size());
            for (const auto p : {a, b, c}) mesh.vertices.push_back({rml(p), color, {0, 0}});
            for (int i = 0; i < 3; ++i) mesh.indices.push_back(first + i);
        };
        const auto stroke = [&](glm::dvec2 a, glm::dvec2 b, double width, Rml::ColourbPremultiplied color) {
            const double length = glm::length(b - a);
            if (length < .001) return;
            const auto d = (b - a) / length, n = glm::dvec2(-d.y, d.x) * width * .5;
            triangle(a-n, b-n, b+n, color); triangle(a-n, b+n, a+n, color);
        };
        const auto line = [&](glm::dvec2 a, glm::dvec2 b, bool dashed) {
            // Clip before tessellating: zoomed/offscreen route points must not
            // draw over the surrounding HUD or create huge dashed meshes.
            const auto d = b-a;
            double start = 0, end = 1;
            const double inset = 3 * scale;
            for (int axis = 0; axis < 2; ++axis) {
                if (std::abs(d[axis]) < 1e-12) { if (a[axis] < low[axis]+inset || a[axis] > high[axis]-inset) return; }
                else {
                    const double t0 = (low[axis]+inset-a[axis])/d[axis], t1 = (high[axis]-inset-a[axis])/d[axis];
                    start = std::max(start, std::min(t0,t1)); end = std::min(end, std::max(t0,t1));
                }
            }
            if (start >= end) return;
            b = a+d*end; a += d*start;
            const double length = glm::length(b-a);
            const auto direction = (b-a)/length;
            for (double t = 0; t < length; t += dashed ? 12*scale : length) {
                const auto begin = a+direction*t, finish = a+direction*std::min(length, dashed ? t+7*scale : length);
                stroke(begin, finish, 5*scale, outline); stroke(begin, finish, 2.5*scale, route);
            }
        };
        const auto& points = view->points();
        for (std::size_t i = 1; i < points.size(); ++i) line(view->screenAt(points[i-1]), view->screenAt(points[i]), false);
        if (const auto preview = view->preview()) line(view->screenAt(points.back()), view->screenAt(*preview), true);
        if (players) for (const auto& player : *players) {
            const auto marker = view->marker(player.position, player.yawDegrees);
            if (!marker) continue;
            const auto p = marker->position, d = marker->direction, n = glm::dvec2(-d.y, d.x);
            const auto color = GetProperty<Rml::Colourb>(player.local ? "map-self-color" : "map-player-color").ToPremultiplied();
            triangle(p+d*11.*scale, p-d*7.*scale+n*7.*scale, p-d*7.*scale-n*7.*scale, outline);
            triangle(p+d*8.*scale, p-d*5.*scale+n*4.5*scale, p-d*5.*scale-n*4.5*scale, color);
            if (marker->atEdge) triangle(p+d*4.*scale, p-d*3.*scale+n*2.*scale, p-d*3.*scale-n*2.*scale, outline);
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto p = view->screenAt(points[i]);
            const bool last = view->measuring() && i+1 == points.size() && points.size() >= 2;
            const double radius = (last ? (view->finishing() ? 9 : 7) : 5)*scale;
            if (!inside(p, low+radius, high-radius)) continue;
            for (int step = 0; step < 20; ++step) {
                const double a = step*glm::two_pi<double>()/20, b = (step+1)*glm::two_pi<double>()/20;
                const glm::dvec2 da(std::cos(a),std::sin(a)), db(std::cos(b),std::sin(b));
                triangle(p, p+da*radius, p+db*radius, outline);
                triangle(p, p+da*(radius-2*scale), p+db*(radius-2*scale), route);
            }
        }
        if (!mesh.indices.empty()) {
            auto geometry = GetRenderManager()->MakeGeometry(std::move(mesh));
            geometry.Render({0, 0});
        }
    }
};
}
void registerMapViewElement() {
    for (const auto* name : {"map-line-color", "map-self-color", "map-player-color"})
        Rml::StyleSheetSpecification::RegisterProperty(name, "white", false).AddParser("color");
    static Rml::ElementInstancerGeneric<MapElement> instancer;
    Rml::Factory::RegisterElementInstancer("satellite-map", &instancer);
}
MapViewUi::MapViewUi(Rml::ElementDocument& document, HudRenderInterface& renderer) : document_(document) {
    auto* map = dynamic_cast<MapElement*>(document.GetElementById("satellite-overview"));
    total_ = document.GetElementById("map-total"); hint_ = document.GetElementById("map-hint");
    if (!map || !total_ || !hint_) throw std::runtime_error("Incomplete map view layout");
    map_ = map; map->view = &view_; map->renderer = &renderer; map->players = &players_;
}
void MapViewUi::setSource(const std::optional<MinimapSource>& source) { view_.setSource(source); }
void MapViewUi::process(const std::vector<HudPointerEvent>& events, int width, int height,
                       const PresentationViewport& presentation, const HudState& state) {
    const auto offset = map_->GetAbsoluteOffset(Rml::BoxArea::Content), size = map_->GetBox().GetSize(Rml::BoxArea::Content);
    const double density = document_.GetContext()->GetDensityIndependentPixelRatio();
    view_.setViewport({offset.x,offset.y}, {size.x,size.y}, density);
    const auto pointer = [&] { return glm::dvec2(presentation.scenePointer(normalizedPointer_.x,normalizedPointer_.y,width,height)); };
    view_.move(pointer());
    players_ = state.mapPlayers;
    if (state.showLocalMapPlayer) players_.push_back({{state.position.x,state.position.z},state.yawDegrees,"YOU",true});
    constexpr std::array<const char*,4> controls{"map-measure", "map-clear", "map-center", "map-from-me"};
    for (const auto& event : events) {
        switch (event.action) {
        case HudPointerAction::Move: normalizedPointer_ = {event.x,event.y}; view_.move(pointer()); break;
        case HudPointerAction::Press:
            pressedControl_.clear();
            for (const auto* id : controls) if (hit(*document_.GetElementById(id),pointer())) { pressedControl_ = id; break; }
            if (pressedControl_.empty()) view_.press();
            break;
        case HudPointerAction::Release:
            if (pressedControl_.empty()) view_.release();
            else if (hit(*document_.GetElementById(pressedControl_),pointer())) {
                if (pressedControl_ == "map-measure") view_.beginMeasurement();
                else if (pressedControl_ == "map-clear") view_.clearMeasurement();
                else if (state.showLocalMapPlayer) {
                    const glm::dvec2 position{state.position.x,state.position.z};
                    view_.centerOn(position);
                    if (pressedControl_ == "map-from-me") view_.beginMeasurement(position);
                }
            }
            pressedControl_.clear(); break;
        case HudPointerAction::RightPress:
            if (hit(*map_,pointer())) view_.undo();
            pressedControl_.clear(); break;
        case HudPointerAction::Cancel: cancel(); break;
        case HudPointerAction::Wheel: view_.zoomAt(pointer(),event.x); break;
        }
    }
    text(*document_.GetElementById("map-measure"), view_.points().empty() ? "MEASURE" : "NEW MEASUREMENT");
    document_.GetElementById("map-measure")->SetClass("active", view_.measuring());
    text(*total_, view_.points().empty() ? "HORIZONTAL DISTANCE" : "TOTAL: " + distanceLabel(view_.distance()));
    std::string previewTotal;
    if (const auto preview = view_.preview())
        previewTotal = "WITH PREVIEW: " + distanceLabel(view_.distance()+glm::length(*preview-view_.points().back()));
    text(*document_.GetElementById("map-preview-total"),previewTotal);
    text(*hint_, view_.finishing() ? "Click the last point to finish" : view_.measuring() ?
        "Click: add point   |   Last point: finish   |   Right-click: undo   |   Drag: pan   |   Wheel: zoom" :
        "Drag: pan   |   Wheel: zoom   |   Measure: draw a line   |   Drag a finished point to adjust");
    updateLabels();
    // A single small tooltip identifies players without filling the map with names.
    auto* tooltip = document_.GetElementById("map-player-label");
    std::string hovered;
    for (const auto& player : players_) if (const auto marker = view_.marker(player.position,player.yawDegrees))
        if (glm::length(marker->position-pointer()) < 15*density) hovered = player.label + (marker->atEdge ? " (OFFSCREEN)" : "");
    text(*tooltip, hovered);
}
void MapViewUi::updateLabels() {
    const auto region = view_.imageRegion();
    const auto offset = map_->GetAbsoluteOffset(Rml::BoxArea::Content);
    const double density = document_.GetContext()->GetDensityIndependentPixelRatio();
    std::size_t used = 0;
    std::vector<glm::dvec4> occupied;
    const auto label = [&](glm::dvec2 p, const std::string& value, double width) {
        if (!region) return;
        const double w = width*density, h = 18*density;
        const auto low = region->offset, high = low+region->size;
        if (!inside(p, low+glm::dvec2(w*.5,0), high-glm::dvec2(w*.5,h))) return;
        const glm::dvec4 rect{p.x-w*.5,p.y,p.x+w*.5,p.y+h};
        for (const auto& other : occupied) if (rect.x < other.z && rect.z > other.x && rect.y < other.w && rect.w > other.y) return;
        occupied.push_back(rect);
        if (used == labels_.size()) {
            auto element = document_.CreateElement("div"); element->SetClassNames("map-distance-label");
            labels_.push_back(map_->AppendChild(std::move(element)));
        }
        auto& element = *labels_[used++];
        text(element,value); element.SetProperty("display","block");
        element.SetProperty(Rml::PropertyId::Left,Rml::Property(float(rect.x-offset.x),Rml::Unit::PX));
        element.SetProperty(Rml::PropertyId::Top,Rml::Property(float(rect.y-offset.y),Rml::Unit::PX));
        element.SetProperty(Rml::PropertyId::Width,Rml::Property(float(w),Rml::Unit::PX));
    };
    const auto& points = view_.points();
    // Point identities get priority over optional segment lengths.
    for (std::size_t i = 0; i < points.size(); ++i) label(view_.screenAt(points[i])+glm::dvec2(0,10*density),std::to_string(i+1),24);
    for (std::size_t i = 1; i < points.size(); ++i)
        label((view_.screenAt(points[i-1])+view_.screenAt(points[i]))*.5-glm::dvec2(0,22*density),distanceLabel(glm::length(points[i]-points[i-1])),76);
    for (; used < labels_.size(); ++used) labels_[used]->SetProperty("display","none");
}
} // namespace viewer
