#pragma once

#include "minimap.hpp"
#include <optional>
#include <string>
#include <vector>

namespace viewer {
struct MapPlayer {
    glm::dvec2 position{0}; // Runtime X east/Z south, metres.
    double yawDegrees = -90;
    std::string label;
    bool local = false;
};
// Local map presentation state. Screen positions are framebuffer pixels from
// the top left; stored measurement points are runtime X/Z horizontal metres.
class MapView {
public:
    struct ImageRegion { glm::dvec2 offset, size, uvOrigin, uvExtent; };
    struct Marker { glm::dvec2 position, direction; bool atEdge = false; };
    void setSource(const std::optional<MinimapSource>& source);
    void setViewport(glm::dvec2 offset, glm::dvec2 size, double density = 1);
    std::optional<ImageRegion> imageRegion() const;
    std::optional<glm::dvec2> worldAt(glm::dvec2 screen) const;
    glm::dvec2 screenAt(glm::dvec2 world) const; // Requires an available imageRegion().
    std::optional<Marker> marker(glm::dvec2 world, double yawDegrees) const;
    void zoomAt(glm::dvec2 screen, double notches);
    void centerOn(glm::dvec2 world);
    void beginMeasurement(std::optional<glm::dvec2> start = {});
    void clearMeasurement();
    void move(glm::dvec2 screen);
    void press();
    void release();
    void undo(); // Right click: remove a point only while drawing.
    void cancel(); // Close/focus loss: abandon drawing, roll back an endpoint edit.
    const std::vector<glm::dvec2>& points() const { return points_; }
    std::optional<glm::dvec2> preview() const;
    double distance() const;
    bool measuring() const { return measuring_; }
    bool finishing() const;
    bool dragging() const { return dragging_; }
    double zoom() const { return zoom_; }
private:
    glm::dvec2 imageSize() const;
    glm::dvec2 imageOffset() const;
    void clampCenter();
    std::optional<MinimapSource> source_;
    glm::dmat3 worldFromUv_{1};
    glm::dvec2 offset_{0}, size_{1}, center_{.5}, pointer_{-1}, press_{0}, previous_{0};
    double zoom_ = 1, density_ = 1;
    std::vector<glm::dvec2> points_;
    std::optional<std::size_t> editing_;
    glm::dvec2 originalPoint_{0};
    bool measuring_ = false, pressed_ = false, dragging_ = false;
};
} // namespace viewer
