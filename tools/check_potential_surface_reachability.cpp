#include "file_fingerprint.hpp"
#include "potential_surface.hpp"
#include "zombie_navmesh_search.hpp"
#include "json.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs=std::filesystem;
namespace nav=viewer::navmesh_path;
namespace physical=viewer::potential_surface;
using Json=nlohmann::json;

struct Options { fs::path mesh,output;std::vector<fs::path> summaries; };
Options options(int argc,char** argv) {
    Options result;
    for (int i=1;i<argc;++i) {
        const std::string flag=argv[i];
        if (flag=="--help") {
            std::cout<<"check_potential_surface_reachability --navmesh FILE --summary FILE [--summary FILE ...] --output NEWFILE\n";
            std::exit(0);
        }
        if (++i==argc) throw std::invalid_argument("Missing value for "+flag);
        if (flag=="--navmesh") result.mesh=argv[i];
        else if (flag=="--summary") result.summaries.emplace_back(argv[i]);
        else if (flag=="--output") result.output=argv[i];
        else throw std::invalid_argument("Unknown argument "+flag);
    }
    if (result.mesh.empty() || result.output.empty() || result.summaries.empty())
        throw std::invalid_argument("--navmesh, at least one --summary and --output are required");
    if (fs::exists(result.output)) throw std::invalid_argument("Output exists; choose a new filename");
    return result;
}
Json read(const fs::path& file) {
    std::ifstream stream(file);
    if (!stream) throw std::runtime_error("Cannot open "+file.string());
    Json value;stream>>value;return value;
}
nav::Point point(const Json& value) {
    if (!value.is_array() || value.size()!=2) throw std::invalid_argument("Expected exact X/Z coordinate pair");
    nav::Point result{value[0].get<double>(),value[1].get<double>()};
    if (!std::isfinite(result.x) || !std::isfinite(result.z)) throw std::invalid_argument("Nonfinite coordinate");
    return result;
}
Json coordinate(nav::Point p) { return {p.x,p.z}; }
physical::Point bodyPoint(nav::Point p) { return {p.x,p.z}; }
double distance(nav::Point a,nav::Point b) { return std::hypot(a.x-b.x,a.z-b.z); }
Json optionalId(std::uint32_t value) { return value==nav::noPolygon?Json(nullptr):Json(value); }

std::unique_ptr<physical::World> physicalWorld(const fs::path& scene,const std::string& expectedSha) {
    const auto before=viewer::fileSha256(scene);
    if (before!=expectedSha) throw std::runtime_error("Summary physical-scene fingerprint mismatch");
    const auto document=read(scene);
    const auto low=point(document.at("raster").at("minimum"));
    const auto high=point(document.at("raster").at("maximum"));
    std::vector<physical::Obstacle> obstacles;
    obstacles.reserve(document.at("obstacles").size());
    for (const auto& row:document.at("obstacles")) {
        if (!row.is_array() || row.size()!=7) throw std::invalid_argument("Obstacle requires seven fields");
        obstacles.push_back({row[0].get<unsigned>(),{row[1].get<double>(),row[2].get<double>()},
            {row[3].get<double>(),row[4].get<double>()},row[5].get<double>(),row[6].get<double>()});
    }
    auto result=std::make_unique<physical::World>(std::move(obstacles),
        physical::Bounds{low.x,low.z,high.x,high.z});
    if (viewer::fileSha256(scene)!=before) throw std::runtime_error("Physical scene changed during loading");
    return result;
}
Json group() {
    return {{"agents",0},{"endpoint_coverage",Json::object()},
        {"component_relation",Json::object()},{"search_status",Json::object()},
        {"route_witness",Json::object()}};
}
void increment(Json& target,const std::string& key) { target[key]=target.value(key,std::uint64_t(0))+1; }
void count(Json& target,const Json& agent) {
    target["agents"]=target["agents"].get<std::uint64_t>()+1;
    increment(target["endpoint_coverage"],agent.at("endpoint_coverage").get<std::string>());
    increment(target["component_relation"],agent.at("component_relation").get<std::string>());
    increment(target["search_status"],agent.at("search_status").get<std::string>());
    increment(target["route_witness"],agent.at("route_witness").get<std::string>());
}

Json check(const nav::Mesh& mesh,nav::Search& search,const physical::World& world,
           const fs::path& summary,const Json& document,const std::string& fingerprint) {
    const auto& config=document.at("configuration");
    if (config.at("radius_m").get<double>()!=world.config().radius ||
        config.at("skin_m").get<double>()!=world.config().skin ||
        config.at("building_extra_m").get<double>()!=world.config().buildingExtra)
        throw std::invalid_argument("Summary physical padding differs from the original PSS model");
    Json groups=Json::object();
    for (const auto* status:{"arrived","stalled","timeout","invalid_endpoint"}) groups[status]=group();
    Json result={{"summary",fs::absolute(summary).string()},{"summary_sha256",fingerprint},
        {"cohort",summary.parent_path().parent_path().filename().string()},
        {"configuration",config},{"physical_scene",document.at("source")},
        {"physical_scene_sha256",document.at("source_sha256")},
        {"mesh_source_matches_physical_scene",mesh.sourceSha256()==document.at("source_sha256").get<std::string>()},
        {"all",group()},{"by_steering_status",groups},{"agents",Json::array()}};
    std::set<std::uint64_t> ids;
    for (const auto& actual:document.at("agents")) {
        const auto id=actual.at("id").get<std::uint64_t>();
        if (!ids.insert(id).second) throw std::invalid_argument("Duplicate original agent ID");
        const auto start=point(actual.at("start")),goal=point(actual.at("goal"));
        const auto final=point(actual.at("final"));
        const double travel=actual.at("travel_m").get<double>();
        if (!std::isfinite(travel) || travel<0) throw std::invalid_argument("Invalid recorded travel distance");
        const auto first=mesh.locate(start),last=mesh.locate(goal);
        const bool startCovered=first!=nav::noPolygon,goalCovered=last!=nav::noPolygon;
        const auto firstComponent=startCovered?mesh.component(first):nav::noPolygon;
        const auto lastComponent=goalCovered?mesh.component(last):nav::noPolygon;
        const std::string coverage=startCovered?(goalCovered?"both_covered":"goal_not_covered"):
            (goalCovered?"start_not_covered":"both_not_covered");
        const std::string relation=(!startCovered || !goalCovered)?"unclassified_endpoint_not_covered":
            (firstComponent==lastComponent?"same_component":"disconnected_in_this_navmesh");
        const std::string actualStatus=actual.at("status").get<std::string>();
        Json agent={{"id",id},{"steering_status",actualStatus},{"start",coordinate(start)},
            {"goal",coordinate(goal)},{"final",coordinate(final)},
            {"initial_direct_m",distance(start,goal)},{"actual_travel_m",travel},
            {"remaining_m",distance(final,goal)},{"start_covered",startCovered},{"goal_covered",goalCovered},
            {"start_polygon",optionalId(first)},{"goal_polygon",optionalId(last)},
            {"start_component",optionalId(firstComponent)},{"goal_component",optionalId(lastComponent)},
            {"endpoint_coverage",coverage},{"component_relation",relation},
            {"physical_start_clear",world.clear(bodyPoint(start))},{"physical_goal_clear",world.clear(bodyPoint(goal))},
            {"search_status","not_run"},{"route_witness","not_found"},
            {"navmesh_route_length_m",nullptr},{"route_points",Json::array()}};
        if (relation=="same_component") {
            const auto route=search.find(start,goal);
            agent["search_status"]=nav::statusName(route.status);
            agent["search_stats"]={{"stored_nodes",route.stats.storedNodes},
                {"expanded_nodes",route.stats.expandedNodes},{"peak_queue",route.stats.peakQueue},
                {"corridor_polygons",route.stats.corridorPolygons}};
            agent["route_witness"]=route.status==nav::Status::searchLimit?"search_limit_no_witness":"search_failed_no_witness";
            if (route.status==nav::Status::found) {
                const bool endpoints=route.size>0 && route.points[0]==start && route.points[route.size-1]==goal;
                bool nativeValid=endpoints,finiteCoordinates=true;
                bool physicalValid=nativeValid && world.clear(bodyPoint(start)) && world.clear(bodyPoint(goal));
                double routeLength=0;
                std::uint64_t invalidNative=0,invalidPhysical=0;
                for (std::size_t i=0;i<route.size;++i) {
                    finiteCoordinates&=std::isfinite(route.points[i].x) && std::isfinite(route.points[i].z);
                    agent["route_points"].push_back(coordinate(route.points[i]));
                    if (!i) continue;
                    routeLength+=distance(route.points[i-1],route.points[i]);
                    invalidNative+=!mesh.segmentClear(route.points[i-1],route.points[i]);
                    invalidPhysical+=!world.segmentClear(bodyPoint(route.points[i-1]),bodyPoint(route.points[i]));
                }
                nativeValid&=finiteCoordinates && !invalidNative;
                physicalValid&=finiteCoordinates && !invalidPhysical;
                agent["route_endpoints_preserved"]=endpoints;
                agent["route_coordinates_finite"]=finiteCoordinates;
                agent["invalid_navmesh_segments"]=invalidNative;
                agent["invalid_physical_segments"]=invalidPhysical;
                agent["navmesh_route_length_m"]=std::isfinite(routeLength)?Json(routeLength):Json(nullptr);
                agent["navmesh_route_valid"]=nativeValid;
                agent["physical_route_valid"]=physicalValid;
                agent["route_witness"]=!nativeValid?"invalid_navmesh_route":
                    (physicalValid?"validated_physical_route":"route_invalid_for_original_physics");
            }
        } else if (relation=="disconnected_in_this_navmesh") {
            agent["search_status"]="not_run_disconnected_components";
            agent["route_witness"]="disconnected_in_this_navmesh";
        } else {
            agent["search_status"]="not_run_uncovered_endpoint";
            agent["route_witness"]="unclassified_endpoint_not_covered";
        }
        if (!result["by_steering_status"].contains(actualStatus)) result["by_steering_status"][actualStatus]=group();
        count(result["all"],agent);
        count(result["by_steering_status"][actualStatus],agent);
        result["agents"].push_back(std::move(agent));
    }
    if (viewer::fileSha256(summary)!=fingerprint) throw std::runtime_error("Summary changed during checking");
    return result;
}
}

int main(int argc,char** argv) try {
    const auto args=options(argc,argv);
    const auto meshHash=viewer::fileSha256(args.mesh);
    const auto mesh=nav::Mesh::load(args.mesh.string());
    nav::Search search(mesh,nav::Search::defaultNodeLimit,8);
    std::unique_ptr<physical::World> world;
    std::string sourceSha;
    Json report={{"schema",1},{"navmesh",{{"file",fs::absolute(args.mesh).string()},
        {"sha256",meshHash},{"source_sha256",mesh.sourceSha256()},
        {"vertices",mesh.vertices().size()},{"polygons",mesh.polygons().size()},
        {"components",mesh.componentCount()},{"radius_m",mesh.radius()},
        {"clearance_m",mesh.clearance()},{"building_extra_m",mesh.buildingExtra()},
        {"numeric_margin_m",mesh.numericMargin()},
        {"minimum",{mesh.minimumX(),mesh.minimumZ()}},{"maximum",{mesh.maximumX(),mesh.maximumZ()}}}},
        {"search",{{"stored_polygon_limit",search.nodeLimit()},{"heuristic_weight",8},
            {"reopen_closed",false},{"greedy",false},{"route_endpoints","original_start_to_original_goal"}}},
        {"interpretation","Component labels classify this cached mesh only. Uncovered endpoints are not snapped and stay unclassified. Same-component search limits do not imply unreachable. Different components in a conservative bake do not prove original physical impossibility. Only exported routes independently accepted by the original PSS physical model are positive route witnesses; paths are approximate, not guaranteed shortest."},
        {"summaries",Json::array()}};
    for (const auto& summary:args.summaries) {
        const auto hash=viewer::fileSha256(summary);
        const auto document=read(summary);
        const auto sha=document.at("source_sha256").get<std::string>();
        if (!world) {
            sourceSha=sha;
            world=physicalWorld(document.at("source").get<std::string>(),sourceSha);
            report["original_physics"]={{"source",document.at("source")},{"source_sha256",sourceSha},
                {"radius_m",world->config().radius},{"skin_m",world->config().skin},
                {"building_extra_m",world->config().buildingExtra},
                {"model","Original oriented rectangles for buildings/fences, exact circles for trunks, strict inset map bounds. Closed contact forbidden; no snapping or radius shrink."}};
        } else if (sha!=sourceSha || viewer::fileSha256(document.at("source").get<std::string>())!=sourceSha)
            throw std::runtime_error("All summaries must use the same unchanged original physical scene");
        report["summaries"].push_back(check(mesh,search,*world,summary,document,hash));
        const auto& result=report["summaries"].back();
        std::cout<<result["cohort"]<<": "<<result["all"].dump()<<'\n'<<std::flush;
    }
    if (viewer::fileSha256(args.mesh)!=meshHash) throw std::runtime_error("Navmesh changed during checking");
    if (!args.output.parent_path().empty()) fs::create_directories(args.output.parent_path());
    auto temporary=args.output;temporary+=".tmp";
    { std::ofstream stream(temporary);stream<<report.dump(2)<<'\n';
      if (!stream) throw std::runtime_error("Cannot write diagnostic output"); }
    fs::rename(temporary,args.output);
    std::cout<<args.output<<'\n';
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
