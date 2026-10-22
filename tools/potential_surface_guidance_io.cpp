#include "potential_surface_guidance_io.hpp"
#include "file_fingerprint.hpp"
#include "json.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace viewer::potential_surface_benchmark {
namespace {
using namespace potential_surface;
using Json=nlohmann::json;
Point point(const Json& value) {
    if (!value.is_array() || value.size()!=2) throw std::invalid_argument("Guidance requires X/Z pairs");
    Point p{value[0].get<double>(),value[1].get<double>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.z)) throw std::invalid_argument("Nonfinite guidance coordinate");
    return p;
}
std::vector<Point> ring(const Json& values) {
    std::vector<Point> result;
    result.reserve(values.size());
    for (const auto& value:values) result.push_back(point(value));
    if (result.size()<3) throw std::invalid_argument("Pocket ring requires at least three points");
    return result;
}
}
const char* guidanceName(GuidanceMode mode) {
    switch (mode) {
        case GuidanceMode::none:return "none";
        case GuidanceMode::hierarchy:return "hierarchy";
        case GuidanceMode::pockets:return "pockets";
        case GuidanceMode::combined:return "combined";
    }
    throw std::invalid_argument("Unknown guidance mode");
}
LoadedGuidance loadGuidance(const std::filesystem::path& path,const std::string& sceneSha,
                           const Config& physical,GuidanceMode mode) {
    const auto started=std::chrono::steady_clock::now();
    LoadedGuidance result;
    result.sha256=viewer::fileSha256(path);
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read guidance "+path.string());
    Json document;stream>>document;
    if (document.at("schema").get<unsigned>()!=1 || document.at("source_sha256").get<std::string>()!=sceneSha)
        throw std::invalid_argument("Guidance schema or source fingerprint differs from the scene");
    const auto& padding=document.at("physical_padding");
    if (padding.at("radius_m").get<double>()!=physical.radius ||
        padding.at("skin_m").get<double>()!=physical.skin ||
        padding.at("building_extra_m").get<double>()!=physical.buildingExtra)
        throw std::invalid_argument("Guidance physical padding differs from runtime");
    if (mode==GuidanceMode::pockets || mode==GuidanceMode::combined) {
        const auto& settings=document.at("parameters").at("pockets");
        for (const auto& expected:std::initializer_list<std::pair<const char*,double>>{
                {"exit_target_distance_m",2},{"exit_release_outset_m",1},
                {"pocket_avoidance_support_m",12},{"pocket_avoidance_length_m",2},
                {"pocket_avoidance_weight",1}})
            if (settings.at(expected.first).get<double>()!=expected.second)
                throw std::invalid_argument("Guidance pocket equation parameters differ from runtime");
    }
    std::vector<CoarseBump> bumps;
    std::vector<Pocket> pockets;
    if (mode==GuidanceMode::hierarchy || mode==GuidanceMode::combined) {
        for (const auto& value:document.at("bumps")) {
            const auto& box=value.at("box");
            if (!box.is_array() || box.size()!=4) throw std::invalid_argument("Guidance box requires four bounds");
            bumps.push_back({point(value.at("center")),point(value.at("radii")),value.at("weight").get<double>(),
                {box[0].get<double>(),box[1].get<double>(),box[2].get<double>(),box[3].get<double>()},
                value.at("level").get<std::uint32_t>()});
        }
    }
    if (mode==GuidanceMode::pockets || mode==GuidanceMode::combined) {
        for (const auto& value:document.at("pockets")) {
            Pocket pocket;
            pocket.outer=ring(value.at("outer"));
            for (const auto& hole:value.at("holes")) pocket.holes.push_back(ring(hole));
            const auto& mouth=value.at("mouth");
            if (!mouth.is_array() || mouth.size()!=2) throw std::invalid_argument("Pocket mouth requires a segment");
            pocket.mouth={point(mouth[0]),point(mouth[1])};
            pocket.outward=point(value.at("outward"));
            pocket.depth=value.at("depth").get<double>();pocket.width=value.at("width").get<double>();
            pockets.push_back(std::move(pocket));
        }
    }
    result.field=std::make_unique<Guidance>(std::move(bumps),std::move(pockets));
    if (viewer::fileSha256(path)!=result.sha256) throw std::runtime_error("Guidance changed while loading");
    result.loadMilliseconds=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-started).count();
    return result;
}
}
