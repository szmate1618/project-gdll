#pragma once
#include "../map_view.hpp"
#include "map_mark.hpp"
#include <RmlUi/Core.h>
#include <functional>

namespace viewer {
using MapMarkerProjection = std::function<std::optional<MapView::Marker>(glm::dvec2, double)>;
// All vertices are absolute framebuffer pixels. Border players become dots on
// the minimap; persistent marks remain X symbols in both map presentations.
void appendMapMarkers(Rml::Mesh& mesh, Rml::Element& element, const std::vector<MapPlayer>& players,
    const std::vector<MapMark>& marks, const MapMarkerProjection& project,
    double scale, bool minimap, std::uint64_t selected = 0);
}
