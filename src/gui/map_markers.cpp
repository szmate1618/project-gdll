#include "map_markers.hpp"
#include <glm/gtc/constants.hpp>

namespace viewer {
void appendMapMarkers(Rml::Mesh& mesh, Rml::Element& element, const std::vector<MapPlayer>& players,
        const std::vector<MapMark>& marks, const MapMarkerProjection& project,
        double scale, bool minimap, std::uint64_t selected) {
    const auto outline = Rml::Colourb(35,37,39).ToPremultiplied();
    const auto triangle = [&](glm::dvec2 a, glm::dvec2 b, glm::dvec2 c, Rml::ColourbPremultiplied color) {
        const int first = static_cast<int>(mesh.vertices.size());
        for (const auto p : {a,b,c}) mesh.vertices.push_back({{float(p.x),float(p.y)},color,{0,0}});
        for (int i = 0; i < 3; ++i) mesh.indices.push_back(first+i);
    };
    const auto stroke = [&](glm::dvec2 a, glm::dvec2 b, double width, Rml::ColourbPremultiplied color) {
        const auto d = glm::normalize(b-a), n = glm::dvec2(-d.y,d.x)*width*.5;
        triangle(a-n,b-n,b+n,color); triangle(a-n,b+n,a+n,color);
    };
    for (const auto& player : players) {
        const auto marker = project(player.position, player.yawDegrees);
        if (!marker) continue;
        const auto color = element.GetProperty<Rml::Colourb>(player.local ? "map-self-color" : "map-player-color").ToPremultiplied();
        const auto p = marker->position, d = marker->direction, n = glm::dvec2(-d.y,d.x);
        if (minimap && marker->atEdge) {
            for (int i = 0; i < 16; ++i) {
                const double a = i*glm::two_pi<double>()/16, b = (i+1)*glm::two_pi<double>()/16;
                const glm::dvec2 da(std::cos(a),std::sin(a)), db(std::cos(b),std::sin(b));
                triangle(p,p+da*4.5*scale,p+db*4.5*scale,outline);
                triangle(p,p+da*3.*scale,p+db*3.*scale,color);
            }
        } else {
            const double radius = (minimap ? .6 : 1)*scale;
            triangle(p+d*11.*radius,p-d*7.*radius+n*7.*radius,p-d*7.*radius-n*7.*radius,outline);
            triangle(p+d*8.*radius,p-d*5.*radius+n*4.5*radius,p-d*5.*radius-n*4.5*radius,color);
            if (!minimap && marker->atEdge)
                triangle(p+d*4.*scale,p-d*3.*scale+n*2.*scale,p-d*3.*scale-n*2.*scale,outline);
        }
    }
    for (const auto& mark : marks) {
        const auto marker = project({mark.position.x, mark.position.z}, -90);
        if (!marker) continue;
        const auto p = marker->position;
        const auto color = element.GetProperty<Rml::Colourb>(mark.id == selected ? "map-selected-mark-color" : "map-mark-color").ToPremultiplied();
        const double radius = (minimap ? 4 : 7)*scale;
        for (const auto d : {glm::dvec2(radius,radius), glm::dvec2(radius,-radius)}) {
            stroke(p-d,p+d,5*scale,outline); stroke(p-d,p+d,2*scale,color);
        }
    }
}
}
