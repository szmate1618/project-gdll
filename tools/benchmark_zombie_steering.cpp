#include "zombie_steering_simulation.hpp"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using json=nlohmann::json;
using namespace viewer::steering_benchmark;
using viewer::steering::Mode;
constexpr Mode modes[]={Mode::collision_only,Mode::gradient,Mode::gradient_side,Mode::perpendicular_push,Mode::detour};
struct Options {
    std::string mode="all",scenario="all";
    fs::path output;
    std::size_t population=1000;
    std::uint32_t seed=1515146562;
    unsigned repeats=3,warmups=1;
    SimulationConfig simulation;
};
std::uint64_t integer(const std::string& text,const char* name) {
    std::size_t used=0;
    if (text.empty() || text.front()=='-') throw std::invalid_argument(std::string("Invalid ")+name);
    const auto value=std::stoull(text,&used);
    if (used!=text.size()) throw std::invalid_argument(std::string("Invalid ")+name);
    return value;
}
float real(const std::string& text,const char* name) {
    std::size_t used=0; const auto value=std::stof(text,&used);
    if (used!=text.size() || !std::isfinite(value) || value<=0) throw std::invalid_argument(std::string("Invalid ")+name);
    return value;
}
Options options(int argc,char** argv) {
    Options result;
    for (int index=1;index<argc;++index) {
        const std::string flag=argv[index];
        if (flag=="--help") {
            std::cout<<"benchmark_zombie_steering --output DIR [--mode all|collision_only|gradient|gradient_side|perpendicular_push|detour] "
                "[--scenario all|woodland|head_on|...] [--population 1000] [--seed UINT32] [--repeats 3] [--warmups 1] "
                "[--duration 60] [--timestep .05] [--speed 2] [--neighbors 256]\n";
            std::exit(0);
        }
        if (++index==argc) throw std::invalid_argument("Missing value for "+flag);
        const std::string value=argv[index];
        if (flag=="--output") result.output=value;
        else if (flag=="--mode") result.mode=value;
        else if (flag=="--scenario") result.scenario=value;
        else if (flag=="--population") result.population=integer(value,"population");
        else if (flag=="--seed") {
            const auto seed=integer(value,"seed");
            if (seed>UINT32_MAX) throw std::invalid_argument("seed must fit uint32");
            result.seed=std::uint32_t(seed);
        }
        else if (flag=="--repeats") {
            const auto count=integer(value,"repeats");
            if (!count || count>20) throw std::invalid_argument("repeats must be 1..20");
            result.repeats=unsigned(count);
        }
        else if (flag=="--warmups") {
            const auto count=integer(value,"warmups");
            if (count>5) throw std::invalid_argument("warmups must be 0..5");
            result.warmups=unsigned(count);
        }
        else if (flag=="--duration") result.simulation.duration=real(value,"duration");
        else if (flag=="--timestep") result.simulation.timestep=real(value,"timestep");
        else if (flag=="--speed") result.simulation.speed=real(value,"speed");
        else if (flag=="--neighbors") result.simulation.neighborCapacity=integer(value,"neighbors");
        else throw std::invalid_argument("Unknown option "+flag);
    }
    if (result.output.empty()) throw std::invalid_argument("--output is required (use a new directory)");
    if (!result.simulation.neighborCapacity || result.simulation.neighborCapacity>65536)
        throw std::invalid_argument("neighbors must be 1..65536");
    bool valid=result.mode=="all";
    for (auto mode:modes) valid|=result.mode==viewer::steering::modeName(mode);
    if (!valid) throw std::invalid_argument("Unknown steering mode "+result.mode);
    return result;
}
double percentile(std::vector<double> values,double quantile) {
    if (values.empty()) return 0;
    std::sort(values.begin(),values.end());
    const double position=(values.size()-1)*quantile;
    const auto index=std::size_t(position);
    return values[index]+(values[std::min(index+1,values.size()-1)]-values[index])*(position-index);
}
json distribution(const std::vector<double>& values) {
    if (values.empty()) return nullptr;
    return {{"min",*std::min_element(values.begin(),values.end())},{"median",percentile(values,.5)},
            {"p95",percentile(values,.95)},{"max",*std::max_element(values.begin(),values.end())},
            {"mean",std::accumulate(values.begin(),values.end(),0.0)/values.size()}};
}
json nullable(double value) { return value<1e29?json(value):json(nullptr); }
json validationJson(const Validation& validation) {
    return {{"endpoint_penetrations",validation.endpointPenetrations},{"swept_penetrations",validation.sweptPenetrations},
        {"mesh_violations",validation.meshViolations},{"checked_segments",validation.checkedSegments},
        {"minimum_endpoint_clearance_m",nullable(validation.minimumEndpointClearance)},
        {"minimum_swept_clearance_m",nullable(validation.minimumSweptClearance)},
        {"penetration_reporting_tolerance_m",0}};
}
json summary(const SimulationResult& result,const Scenario& scenario,const SimulationConfig& config) {
    std::vector<double> arrivalTimes,ratios,travelArrived,plannedArrived,stepTimes,fullLoadTimes;
    double travel=0,headings=0;
    std::uint64_t interventions=0,sweptInterventions=0,invalidSweepStarts=0,rejections=0;
    for (std::size_t id=0;id<result.agents.size();++id) {
        const auto& agent=result.agents[id];
        travel+=agent.travel; headings+=agent.headingRadians;
        interventions+=agent.collisionInterventions; sweptInterventions+=agent.sweptTrunkInterventions;
        invalidSweepStarts+=agent.invalidSweepStarts; rejections+=agent.navmeshRejections;
        if (!agent.arrived) continue;
        arrivalTimes.push_back(agent.arrivalSeconds); travelArrived.push_back(agent.travel);
        const auto& path=scenario.requests[id].path;
        double planned=0;
        for (std::size_t vertex=1;vertex<path.size();++vertex)
            planned+=std::hypot(path[vertex].x-path[vertex-1].x,path[vertex].z-path[vertex-1].z);
        plannedArrived.push_back(planned);
        if (planned>0) ratios.push_back(agent.travel/planned);
    }
    for (const auto& step:result.steps) {
        stepTimes.push_back(step.totalMilliseconds);
        if (step.step*double(config.timestep)<5 && step.active==scenario.requests.size())
            fullLoadTimes.push_back(step.totalMilliseconds);
    }
    const double perUpdate=result.updates?1e6/result.updates:0;
    return {{"population",result.agents.size()},{"arrived",result.arrived},
        {"not_arrived",result.agents.size()-result.arrived},{"stalled",result.stalled},
        {"still_moving_nonarrival",result.agents.size()-result.arrived-result.stalled},
        {"updates",result.updates},{"collision_interventions",interventions},{"navmesh_rejections",rejections},
        {"query_overflows",result.queryOverflows},{"maximum_nearby",result.maximumNearby},
        {"swept_trunk_interventions",sweptInterventions},{"invalid_sweep_starts",invalidSweepStarts},
        {"travel_total_m",travel},{"arrived_travel_m",distribution(travelArrived)},
        {"arrived_planned_length_m",distribution(plannedArrived)},{"arrived_detour_ratio",distribution(ratios)},
        {"arrival_seconds",distribution(arrivalTimes)},{"heading_degrees_per_meter",travel?headings*180/3.141592653589793/travel:0},
        {"total_ms",result.totalMilliseconds},{"query_ms",result.queryMilliseconds},
        {"controller_ms",result.controllerMilliseconds},{"movement_ms",result.movementMilliseconds},
        {"swept_trunk_guard_ms",result.sweepMilliseconds},{"navmesh_guard_ms",result.navmeshMilliseconds},{"ns_per_active_update",result.totalMilliseconds*perUpdate},
        {"controller_ns_per_active_update",result.controllerMilliseconds*perUpdate},
        {"step_ms",distribution(stepTimes)},{"first_5s_all_active_step_ms",distribution(fullLoadTimes)},
        {"first_5s_all_active_steps",fullLoadTimes.size()},{"validation",validationJson(result.validation)},
        {"controller_workspace_bytes",result.controllerWorkspaceBytes},{"history_bytes",result.historyBytes},
        {"obstacle_tests",result.steeringStats.obstacleTests},{"velocity_samples",result.steeringStats.velocitySamples},
        {"side_changes",result.steeringStats.sideChanges}};
}
json agentJson(const AgentMeasurement& agent,std::size_t id) {
    json trace=json::array();
    for (const auto point:agent.trace) trace.push_back({point.x,point.y});
    return {{"id",id+1},{"arrived",agent.arrived},{"stalled",agent.stalled},{"arrival_seconds",agent.arrivalSeconds},
        {"travel_m",agent.travel},{"heading_radians",agent.headingRadians},{"final_distance_m",agent.finalDistance},
        {"no_progress_seconds",agent.noProgressSeconds},{"updates",agent.updates},
        {"collision_interventions",agent.collisionInterventions},{"swept_trunk_interventions",agent.sweptTrunkInterventions},
        {"invalid_sweep_starts",agent.invalidSweepStarts},{"navmesh_rejections",agent.navmeshRejections},
        {"trace",std::move(trace)}};
}
json scenarioJson(const Scenario& scenario) {
    json trunks=json::array(),requests=json::array();
    for (const auto& trunk:scenario.trunks)
        trunks.push_back({{"base",{trunk.base.x,trunk.base.y,trunk.base.z}},
                          {"radius",trunk.radius},{"height",trunk.height}});
    for (std::size_t id=0;id<scenario.requests.size();++id) {
        const auto& request=scenario.requests[id]; json path=json::array();
        for (auto point:request.path) path.push_back({point.x,point.z});
        requests.push_back({{"id",id+1},{"start",{request.start.x,request.start.z}},
                            {"goal",{request.goal.x,request.goal.z}},{"path",std::move(path)}});
    }
    return {{"name",scenario.name},{"purpose",scenario.purpose},{"pathfinding_ms",scenario.pathfindingMilliseconds},
        {"geometry",{{"trunks",std::move(trunks)},{"bounds",{scenario.mesh.minimumX(),scenario.mesh.minimumZ(),
            scenario.mesh.maximumX(),scenario.mesh.maximumZ()}},
            {"mesh_vertices",scenario.mesh.vertices().size()},{"mesh_polygons",scenario.mesh.polygons().size()}}},
        {"requests",std::move(requests)},{"modes",json::array()}};
}
json writeTrajectory(const fs::path& directory,const std::string& scene,const std::string& mode,
                     const SimulationResult& result) {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"Trajectory requires IEEE754 float32");
    const std::string filename=scene+"-"+mode+"-positions.bin";
    std::vector<char> bytes(16+result.positions.size()*8);
    const char magic[8]={'G','D','S','T','0','0','1','\0'};
    std::copy(magic,magic+8,bytes.begin());
    std::size_t offset=8;
    const auto word=[&](std::uint32_t value) {
        for (unsigned byte=0;byte<4;++byte) bytes[offset++]=char((value>>(byte*8))&255);
    };
    word(std::uint32_t(result.agents.size())); word(result.frames);
    for (auto point:result.positions) for (auto coordinate:{point.x,point.y}) {
        std::uint32_t value; std::memcpy(&value,&coordinate,sizeof(value)); word(value);
    }
    std::ofstream stream(directory/filename,std::ios::binary);
    stream.write(bytes.data(),std::streamsize(bytes.size()));
    if (!stream) throw std::runtime_error("Failed writing full trajectory "+filename);
    return {{"file",filename},{"population",result.agents.size()},{"frames",result.frames},
        {"format","GDST001: magic8, little-endian uint32 population, uint32 frames, then frame-major float32 X/Z pairs"},
        {"bytes",bytes.size()},{"includes_initial_frame",true}};
}
void publish(const fs::path& output,const json& document) {
    auto temporary=output; temporary+=".tmp";
    { std::ofstream stream(temporary); if (!stream) throw std::runtime_error("Cannot write "+temporary.string());
      stream<<document.dump()<<'\n'; if (!stream) throw std::runtime_error("Failed writing "+temporary.string()); }
    fs::rename(temporary,output);
}
}
int main(int argc,char** argv) try {
    const auto arguments=options(argc,argv);
    if (fs::exists(arguments.output/"summary.json")) throw std::invalid_argument("Output already contains a steering report; choose a new directory");
    fs::create_directories(arguments.output);
    auto scenarios=makeScenarios(arguments.population,arguments.seed);
    bool selected=false;
    for (const auto& scenario:scenarios) selected|=arguments.scenario=="all" || arguments.scenario==scenario.name;
    if (!selected) throw std::invalid_argument("Unknown scenario "+arguments.scenario);
    viewer::steering::Config controllerConfig;
    json document={{"schema",1},{"configuration",{{"seed",arguments.seed},{"timestep_seconds",arguments.simulation.timestep},
        {"speed_m_per_second",arguments.simulation.speed},{"duration_seconds",arguments.simulation.duration},
        {"body_radius_m",controllerConfig.radius},{"body_height_m",1.8},{"skin_m",controllerConfig.skin},{"physical_skin_m",.002},
        {"influence_range_m",controllerConfig.influenceRange},{"gradient_weight",controllerConfig.gradientWeight},
        {"horizon_seconds",controllerConfig.horizon},{"tangent_weight",controllerConfig.tangentWeight},
        {"neighbors_capacity",arguments.simulation.neighborCapacity},{"repeats",arguments.repeats},{"warmups",arguments.warmups},
        {"trace_interval_seconds",arguments.simulation.traceEvery*arguments.simulation.timestep},
        {"heuristic_weight",8},{"trees_in_global_navmesh",false},{"agent_agent_collisions",false},
        {"physical_movement","moveZombieBody + shared exact swept-trunk guard"},{"timing","Update loop includes follower, BVH query/conversion, controller, physical movement, exact swept-trunk guard, navmesh guard, timers and bookkeeping. Planning, setup, output and independent validation excluded."},
        {"stalled_definition","Nonarrival with no 0.05 m improvement in direct goal distance for the final 5 seconds; simulation continues after stalls."},
        {"validation","Full accepted timestep traces; independent double precision circle/segment tests through separate uniform bins. Native mesh segment coverage also checked outside timer."}}},
        {"scenarios",json::array()}};
    std::ofstream steps(arguments.output/"steps.csv");
    if (!steps) throw std::runtime_error("Cannot write steps.csv");
    steps<<"scenario,mode,repeat,step,active,total_ms,query_ms,controller_ms,movement_ms,swept_trunk_guard_ms,navmesh_ms\n"<<std::setprecision(12);
    bool allRunsValid=true;
    for (const auto& scenario:scenarios) {
        if (arguments.scenario!="all" && arguments.scenario!=scenario.name) continue;
        const viewer::CollisionWorld world(scenario.terrain,scenario.trunks);
        auto scene=scenarioJson(scenario);
        for (auto mode:modes) {
            const std::string name=viewer::steering::modeName(mode);
            if (arguments.mode!="all" && arguments.mode!=name) continue;
            json samples=json::array(); SimulationResult final;
            for (unsigned run=0;run<arguments.warmups+arguments.repeats;++run) {
                auto result=simulate(scenario,world,mode,arguments.simulation,controllerConfig);
                const bool invalidStart=std::any_of(result.agents.begin(),result.agents.end(),
                    [](const auto& agent) { return agent.invalidSweepStarts!=0; });
                if (invalidStart || result.queryOverflows || result.validation.endpointPenetrations || result.validation.sweptPenetrations ||
                    result.validation.meshViolations) {
                    allRunsValid=false;
                    std::cerr<<"Validation violation in "<<scenario.name<<'/'<<name<<": "<<validationJson(result.validation)
                        <<"; query overflows "<<result.queryOverflows<<'\n';
                    // Preserve the trace and report before returning a failed run.
                }
                if (run<arguments.warmups) continue;
                const unsigned repeat=run-arguments.warmups;
                samples.push_back(summary(result,scenario,arguments.simulation));
                samples.back()["repeat"]=repeat;
                for (const auto& step:result.steps)
                    steps<<scenario.name<<','<<name<<','<<repeat<<','<<step.step<<','<<step.active<<','
                        <<step.totalMilliseconds<<','<<step.queryMilliseconds<<','<<step.controllerMilliseconds<<','
                        <<step.movementMilliseconds<<','<<step.sweepMilliseconds<<','<<step.navmeshMilliseconds<<'\n';
                std::cout<<scenario.name<<' '<<name<<" repeat "<<repeat<<": "<<result.arrived<<'/'<<result.agents.size()
                    <<" arrived, "<<result.totalMilliseconds<<" ms, "<<result.steeringStats.velocitySamples<<" velocity samples\n"<<std::flush;
                final=std::move(result);
            }
            json agents=json::array();
            for (std::size_t id=0;id<final.agents.size();++id) agents.push_back(agentJson(final.agents[id],id));
            scene["modes"].push_back({{"mode",name},{"summary",summary(final,scenario,arguments.simulation)},
                {"samples",std::move(samples)},{"agents",std::move(agents)},
                {"trajectory",writeTrajectory(arguments.output,scenario.name,name,final)}});
            document["validation_passed"]=allRunsValid;
            auto checkpoint=document; checkpoint["scenarios"].push_back(scene); publish(arguments.output/"summary.json",checkpoint);
        }
        document["scenarios"].push_back(std::move(scene));
        publish(arguments.output/"summary.json",document);
    }
    bool valid=allRunsValid;
    for (const auto& scene:document["scenarios"]) for (const auto& mode:scene["modes"]) {
        const auto& measured=mode["summary"];
        valid&=measured["invalid_sweep_starts"]==0 && measured["query_overflows"]==0 && measured["validation"]["endpoint_penetrations"]==0 &&
            measured["validation"]["swept_penetrations"]==0 && measured["validation"]["mesh_violations"]==0;
    }
    if (!steps) throw std::runtime_error("Failed writing steps.csv");
    return valid?0:2;
} catch (const std::exception& error) { std::cerr<<"Steering benchmark failed: "<<error.what()<<'\n'; return 1; }
