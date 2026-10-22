#include "potential_surface_simulation.hpp"
#include "potential_surface_guidance_io.hpp"
#include "file_fingerprint.hpp"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using json=nlohmann::json;
using namespace viewer::potential_surface_benchmark;
using viewer::potential_surface::Point;
struct Options {
    fs::path scene,requests,output,guidance;
    unsigned repeats=3,warmups=1;
    Config simulation;
    viewer::potential_surface::Config field;
    bool recordMoves=true,copyAerial=true;
};
Options parse(int argc,char** argv) {
    Options result;
    for (int index=1;index<argc;++index) {
        const std::string flag=argv[index];
        if (flag=="--help") {
            std::cout<<"benchmark_potential_surface --scene paths.json --requests requests.json --output NEWDIR "
                "[--repeats 3] [--warmups 1] [--timestep .05] [--speed 2] "
                "[--weight 16] [--support 3] [--length-scale 1] [--exact-buildings] "
                "[--steering filtered|normalized|tangent] [--guidance FILE] "
                "[--guidance-mode none|hierarchy|pockets|combined] [--no-move-log] [--no-aerial]\n";
            std::exit(0);
        }
        if (flag=="--exact-buildings") { result.field.exactBuildings=true;continue; }
        if (flag=="--no-move-log") { result.recordMoves=false;continue; }
        if (flag=="--no-aerial") { result.copyAerial=false;continue; }
        if (++index==argc) throw std::invalid_argument("Missing value for "+flag);
        const std::string value=argv[index];
        if (flag=="--scene") result.scene=value;
        else if (flag=="--requests") result.requests=value;
        else if (flag=="--output") result.output=value;
        else if (flag=="--guidance") result.guidance=value;
        else if (flag=="--guidance-mode") {
            using viewer::potential_surface::GuidanceMode;
            if (value=="none") result.simulation.guidanceMode=GuidanceMode::none;
            else if (value=="hierarchy") result.simulation.guidanceMode=GuidanceMode::hierarchy;
            else if (value=="pockets") result.simulation.guidanceMode=GuidanceMode::pockets;
            else if (value=="combined") result.simulation.guidanceMode=GuidanceMode::combined;
            else throw std::invalid_argument("Unknown guidance mode "+value);
        }
        else if (flag=="--steering") {
            if (value=="filtered") result.simulation.steering=SteeringMode::filtered;
            else if (value=="normalized") result.simulation.steering=SteeringMode::normalized;
            else if (value=="tangent") result.simulation.steering=SteeringMode::tangent;
            else throw std::invalid_argument("Unknown steering mode "+value);
        }
        else if (flag=="--repeats" || flag=="--warmups") {
            std::size_t used=0;
            if (value.empty() || value.front()=='-') throw std::invalid_argument("Invalid "+flag);
            const auto count=std::stoull(value,&used);
            if (used!=value.size() || count>20 || (flag=="--repeats" && !count)) throw std::invalid_argument("Invalid "+flag);
            if (flag=="--repeats") result.repeats=unsigned(count); else result.warmups=unsigned(count);
        } else if (flag=="--timestep" || flag=="--speed" || flag=="--weight" ||
                   flag=="--support" || flag=="--length-scale") {
            std::size_t used=0;const double number=std::stod(value,&used);
            if (used!=value.size() || !std::isfinite(number) || (flag=="--weight"?number<0:number<=0))
                throw std::invalid_argument("Invalid "+flag);
            if (flag=="--timestep") result.simulation.timestep=number;
            else if (flag=="--speed") result.simulation.speed=number;
            else if (flag=="--weight") result.field.weight=number;
            else if (flag=="--support") result.field.support=number;
            else result.field.lengthScale=number;
        } else throw std::invalid_argument("Unknown option "+flag);
    }
    if (result.scene.empty() || result.requests.empty() || result.output.empty())
        throw std::invalid_argument("--scene, --requests and --output are required");
    if (result.guidance.empty()!=(result.simulation.guidanceMode==viewer::potential_surface::GuidanceMode::none))
        throw std::invalid_argument("--guidance and an enabled --guidance-mode must be supplied together");
    return result;
}
const char* steeringName(SteeringMode mode) {
    switch (mode) {
        case SteeringMode::filtered:return "filtered";
        case SteeringMode::normalized:return "normalized";
        case SteeringMode::tangent:return "tangent";
    }
    throw std::invalid_argument("Invalid steering mode");
}
json read(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read "+path.string());
    json result;stream>>result;return result;
}
Point point(const json& value) {
    if (!value.is_array() || value.size()!=2) throw std::invalid_argument("Coordinates must be X/Z pairs");
    Point result{value[0].get<double>(),value[1].get<double>()};
    if (!std::isfinite(result.x) || !std::isfinite(result.z)) throw std::invalid_argument("Coordinate must be finite");
    return result;
}
double percentile(std::vector<double> values,double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(),values.end());
    const double position=(values.size()-1)*fraction;
    const auto index=std::size_t(position);
    return values[index]+(values[std::min(index+1,values.size()-1)]-values[index])*(position-index);
}
json distribution(const std::vector<double>& values) {
    if (values.empty()) return nullptr;
    return {{"min",*std::min_element(values.begin(),values.end())},{"median",percentile(values,.5)},
        {"p95",percentile(values,.95)},{"max",*std::max_element(values.begin(),values.end())},
        {"mean",std::accumulate(values.begin(),values.end(),0.0)/values.size()}};
}
json guidanceStats(const viewer::potential_surface::GuidanceStats& stats) {
    return {{"queries",stats.queries},{"nodes",stats.nodes},{"candidates",stats.candidates},
        {"bump_contributors",stats.bumpContributors},{"pocket_checks",stats.pocketChecks},
        {"pocket_contributors",stats.pocketContributors},{"virtual_probes",stats.virtualProbes},
        {"escape_activations",stats.escapeActivations},{"escape_updates",stats.escapeUpdates},
        {"target_switches",stats.targetSwitches},{"escape_releases",stats.escapeReleases},
        {"blocked_targets",stats.blockedTargets}};
}
json agentJson(const AgentResult& agent,bool trace) {
    json result={{"id",agent.id},{"start",{agent.start.x,agent.start.z}},{"goal",{agent.goal.x,agent.goal.z}},
        {"status",statusName(agent.status)},{"final",{agent.position.x,agent.position.z}},
        {"arrival_seconds",agent.arrivalSeconds},{"elapsed_seconds",agent.elapsedSeconds},
        {"travel_m",agent.travel},{"initial_distance_m",agent.initialDistance},{"budget_seconds",agent.budgetSeconds},
        {"updates",agent.updates},{"heading_radians",agent.headingRadians},{"clipped_moves",agent.clippedMoves},
        {"invalid_moves",agent.invalidMoves},{"steering_stats",{{"tangent_activations",agent.tangentActivations},
            {"tangent_updates",agent.tangentUpdates},{"tangent_releases",agent.tangentReleases},
            {"retreat_updates",agent.retreatUpdates}}}};
    result["guidance_stats"]=guidanceStats(agent.guidanceStats);
    if (trace) {
        result["trace"]=json::array();
        for (auto position:agent.trace) result["trace"].push_back({position.x,position.z});
    }
    return result;
}
json summary(const Result& result,const Config& config,std::size_t worldBytes,std::size_t guidanceBytes) {
    json statuses={{"arrived",0},{"stalled",0},{"timeout",0},{"invalid_endpoint",0}};
    std::vector<double> arrivedTimes,travel,arrivedRatios,elapsed,stepWall,stepCpu,fixedWall,fixedCpu,earlyWall,earlyCpu,earlyActive;
    std::uint64_t clipped=0,invalid=0,tangentActivations=0,tangentUpdates=0,tangentReleases=0,retreatUpdates=0;
    double totalTravel=0,totalHeading=0;
    for (const auto& agent:result.agents) {
        const auto status=statusName(agent.status);
        statuses[status]=statuses.value(status,0)+1;
        clipped+=agent.clippedMoves;invalid+=agent.invalidMoves;
        tangentActivations+=agent.tangentActivations;tangentUpdates+=agent.tangentUpdates;
        tangentReleases+=agent.tangentReleases;retreatUpdates+=agent.retreatUpdates;
        totalTravel+=agent.travel;totalHeading+=agent.headingRadians;
        travel.push_back(agent.travel);elapsed.push_back(agent.elapsedSeconds);
        if (agent.status==Status::arrived) {
            arrivedTimes.push_back(agent.arrivalSeconds);
            if (agent.initialDistance>0) arrivedRatios.push_back(agent.travel/agent.initialDistance);
        }
    }
    for (const auto& step:result.steps) {
        stepWall.push_back(step.milliseconds);stepCpu.push_back(step.cpuMilliseconds);
        if (step.step*config.timestep<5) {
            earlyWall.push_back(step.milliseconds);earlyCpu.push_back(step.cpuMilliseconds);
            earlyActive.push_back(double(step.active));
        }
        if (step.step*config.timestep<5 && step.active==result.agents.size()) {
            fixedWall.push_back(step.milliseconds);fixedCpu.push_back(step.cpuMilliseconds);
        }
    }
    return {{"population",result.agents.size()},{"statuses",std::move(statuses)},{"updates",result.updates},
        {"steps",result.steps.size()},{"total_ms",result.milliseconds},{"cpu_ms",result.cpuMilliseconds},
        {"cpu_utilization_percent",result.milliseconds?100*result.cpuMilliseconds/result.milliseconds:0},
        {"ns_per_update",result.updates?1e6*result.milliseconds/result.updates:0},
        {"cpu_ns_per_update",result.updates?1e6*result.cpuMilliseconds/result.updates:0},
        {"step_ms",distribution(stepWall)},{"step_cpu_ms",distribution(stepCpu)},
        {"first_5s_step_ms",distribution(earlyWall)},{"first_5s_step_cpu_ms",distribution(earlyCpu)},
        {"first_5s_active",distribution(earlyActive)},{"first_5s_steps",earlyWall.size()},
        {"first_5s_all_active_step_ms",distribution(fixedWall)},
        {"first_5s_all_active_step_cpu_ms",distribution(fixedCpu)},{"first_5s_all_active_steps",fixedWall.size()},
        {"travel_total_m",totalTravel},{"travel_m",distribution(travel)},{"arrival_seconds",distribution(arrivedTimes)},
        {"elapsed_seconds",distribution(elapsed)},{"arrived_detour_ratio",distribution(arrivedRatios)},
        {"heading_degrees_per_meter",totalTravel?totalHeading*180/3.141592653589793/totalTravel:0},
        {"clipped_moves",clipped},{"invalid_moves",invalid},{"moved_segments",result.movedSegments},
        {"steering_stats",{{"tangent_activations",tangentActivations},{"tangent_updates",tangentUpdates},
            {"tangent_releases",tangentReleases},{"retreat_updates",retreatUpdates}}},
        {"validation_segments",result.validationSegments},{"validation_violations",result.validationViolations},
        {"query_stats",{{"field_nodes",result.queries.fieldNodes},{"field_candidates",result.queries.fieldCandidates},
            {"field_contributors",result.queries.fieldContributors},{"collision_nodes",result.queries.collisionNodes},
            {"collision_candidates",result.queries.collisionCandidates}}},
        {"world_bytes",worldBytes},{"guidance_bytes",guidanceBytes},
        {"guidance_stats",guidanceStats(result.guidanceStats)},{"simulation_scratch_bytes",result.scratchBytes}};
}
json signature(const Result& result) {
    json agents=json::array();for (const auto& agent:result.agents) agents.push_back(agentJson(agent,true));
    const auto& q=result.queries;
    return {{"agents",std::move(agents)},{"queries",{q.fieldNodes,q.fieldCandidates,q.fieldContributors,
        q.collisionNodes,q.collisionCandidates}},{"updates",result.updates},
        {"moved_segments",result.movedSegments},{"validation_violations",result.validationViolations}};
}
void publish(const fs::path& output,const json& document) {
    auto temporary=output;temporary+=".tmp";
    { std::ofstream stream(temporary);stream<<document.dump()<<'\n';
      if (!stream) throw std::runtime_error("Cannot write "+temporary.string()); }
    fs::rename(temporary,output);
}
}
int main(int argc,char** argv) try {
    auto options=parse(argc,argv);
    for (const auto* name:{"summary.json","steps.csv","moves.bin"})
        if (fs::exists(options.output/name)) throw std::invalid_argument("Choose a fresh output directory");
    const auto scene=read(options.scene),requestDocument=read(options.requests);
    const auto sceneSha=viewer::fileSha256(options.scene);
    if (requestDocument.at("source_sha256").get<std::string>()!=sceneSha)
        throw std::invalid_argument("Requests were prepared against different scene bytes");
    const auto minimum=point(scene.at("raster").at("minimum")),maximum=point(scene.at("raster").at("maximum"));
    std::vector<viewer::potential_surface::Obstacle> obstacles;
    std::array<std::size_t,3> kinds{};
    obstacles.reserve(scene.at("obstacles").size());
    for (const auto& item:scene.at("obstacles")) {
        if (!item.is_array() || item.size()!=7) throw std::invalid_argument("Obstacle must have seven fields");
        const unsigned kind=item[0].get<unsigned>();if (kind>2) throw std::invalid_argument("Invalid obstacle kind");
        obstacles.push_back({kind,{item[1].get<double>(),item[2].get<double>()},
            {item[3].get<double>(),item[4].get<double>()},item[5].get<double>(),item[6].get<double>()});++kinds[kind];
    }
    const auto& fieldConfig=options.field;
    for (const auto& entry:std::initializer_list<std::pair<const char*,double>>{
            {"radius",fieldConfig.radius},{"skin",fieldConfig.skin},{"building_extra",fieldConfig.buildingExtra}})
        if (requestDocument.contains(entry.first) && requestDocument.at(entry.first).get<double>()!=entry.second)
            throw std::invalid_argument(std::string("Endpoint preparation physical ")+entry.first+" differs from runtime");
    const auto setupBegin=std::chrono::steady_clock::now();
    const viewer::potential_surface::World world(std::move(obstacles),
        {minimum.x,minimum.z,maximum.x,maximum.z},fieldConfig);
    const double worldSetupMilliseconds=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-setupBegin).count();
    LoadedGuidance guidance;
    if (!options.guidance.empty()) {
        guidance=loadGuidance(options.guidance,sceneSha,fieldConfig,options.simulation.guidanceMode);
        options.simulation.guidance=guidance.field.get();
    }
    std::vector<Request> requests;
    for (const auto& item:requestDocument.at("requests")) {
        const auto id=item.at("id").get<std::uint64_t>();if (id>UINT32_MAX) throw std::invalid_argument("Agent ID exceeds uint32");
        requests.push_back({std::uint32_t(id),point(item.at("start")),point(item.at("goal"))});
    }
    if (requests.empty() || requests.size()>100000) throw std::invalid_argument("Request population must be 1..100000");
    fs::create_directories(options.output);
    json raster=scene.at("raster");
    const auto aerial=options.scene.parent_path()/raster.at("file").get<std::string>();
    if (options.copyAerial && fs::exists(aerial)) { fs::copy_file(aerial,options.output/"aerial.ppm");raster["file"]="aerial.ppm"; }
    else raster["file"]=fs::absolute(aerial).string();
    json document={{"schema",1},{"source",fs::absolute(options.scene).string()},{"source_sha256",sceneSha},
        {"requests_file",fs::absolute(options.requests).string()},{"requests_sha256",viewer::fileSha256(options.requests)},
        {"raster",std::move(raster)},{"obstacle_counts",kinds},{"requests",requestDocument.at("requests")},
        {"setup_world_ms",worldSetupMilliseconds},
        {"guidance",{{"file",options.guidance.empty()?json(nullptr):json(fs::absolute(options.guidance).string())},
            {"sha256",guidance.sha256},{"mode",guidanceName(options.simulation.guidanceMode)},
            {"bumps",guidance.field?guidance.field->bumpCount():0},
            {"pockets",guidance.field?guidance.field->pocketCount():0},
            {"memory_bytes",guidance.field?guidance.field->memoryBytes():0},
            {"load_wall_ms",guidance.loadMilliseconds}}},
        {"configuration",{{"seed",requestDocument.value("seed",std::uint64_t(0))},
            {"timestep_seconds",options.simulation.timestep},{"speed_m_per_second",options.simulation.speed},
            {"radius_m",fieldConfig.radius},{"skin_m",fieldConfig.skin},{"building_extra_m",fieldConfig.buildingExtra},
            {"support_m",fieldConfig.support},{"length_scale_m",fieldConfig.lengthScale},{"weight",fieldConfig.weight},
            {"exact_buildings",fieldConfig.exactBuildings},{"steering_mode",steeringName(options.simulation.steering)},
            {"guidance_mode",guidanceName(options.simulation.guidanceMode)},
            {"guidance_representation","Sparse analytical bump parameters and verified polygon pockets with exit attractors; no raster or global runtime path search."},
            {"arrival_radius_m",.15},{"stall_window_seconds",10},{"stall_anchor_displacement_m",.05},
            {"time_budget","max(120 seconds, 3 * initial_distance / speed + 60 seconds) per agent"},
            {"trace_interval_seconds",options.simulation.traceInterval},{"warmups",options.warmups},{"repeats",options.repeats},
            {"global_paths",false},{"navmesh",false},{"agent_agent_collisions",false},
            {"record_moves",options.recordMoves},{"copy_aerial",options.copyAerial},
            {"timing_scope","Only agent update batches: direct goal velocity, analytical potential and selected steering mode, strict clipped movement and outcome bookkeeping. Two wall and two process CPU probes per timestep; no per-agent probes. Setup, native checks, traces, stream I/O and reporting excluded."},
            {"moves_format","PSSMOV01 magic8 then packed little-endian records uint32 id,double ax,az,bx,bz; 36 bytes per actual moved segment"}}},
        {"samples",json::array()},{"moves_file",options.recordMoves?json("moves.bin"):json(nullptr)}};
    std::ofstream steps(options.output/"steps.csv");if (!steps) throw std::runtime_error("Cannot create steps.csv");
    steps<<"repeat,step,active,total_ms,cpu_ms\n"<<std::setprecision(12);
    json reference;bool valid=true,deterministic=true;
    for (unsigned run=0;run<options.warmups+options.repeats;++run) {
        const bool finalRun=run+1==options.warmups+options.repeats;
        std::ofstream moves;
        const bool logMoves=finalRun && options.recordMoves;
        if (logMoves) { moves.open(options.output/"moves.bin",std::ios::binary);moves.write("PSSMOV01",8); }
        auto result=simulate(world,requests,options.simulation,logMoves?&moves:nullptr);
        if (logMoves) { moves.flush();if (!moves) throw std::runtime_error("Cannot finish moves.bin"); }
        auto outcomes=signature(result);
        if (run==0) reference=std::move(outcomes);else deterministic&=outcomes==reference;
        valid&=result.validationViolations==0;
        for (const auto& agent:result.agents) valid&=agent.invalidMoves==0 && agent.status!=Status::invalidEndpoint;
        if (run<options.warmups) continue;
        const auto repeat=run-options.warmups;
        auto measured=summary(result,options.simulation,world.memoryBytes(),guidance.field?guidance.field->memoryBytes():0);
        measured["repeat"]=repeat;
        document["samples"].push_back(measured);document["summary"]=std::move(measured);
        for (const auto& step:result.steps)
            steps<<repeat<<','<<step.step<<','<<step.active<<','<<step.milliseconds<<','<<step.cpuMilliseconds<<'\n';
        document["native_validation_passed"]=valid;document["deterministic"]=deterministic;
        if (finalRun) {
            document["agents"]=json::array();
            for (const auto& agent:result.agents) document["agents"].push_back(agentJson(agent,true));
            document["moves_records"]=result.movedSegments;
        }
        publish(options.output/"summary.json",document);
        std::cout<<"repeat "<<repeat<<": "<<document["summary"]["statuses"]<<", "<<result.milliseconds
            <<" ms wall / "<<result.cpuMilliseconds<<" ms CPU, "<<result.updates<<" updates\n"<<std::flush;
    }
    if (!steps) throw std::runtime_error("Could not finish steps.csv");
    return valid && deterministic?0:2;
} catch (const std::exception& error) { std::cerr<<"Potential-surface benchmark failed: "<<error.what()<<'\n';return 1; }
