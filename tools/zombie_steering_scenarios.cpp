#include "zombie_steering_scenarios.hpp"
#include "zombie_navmesh_paths.hpp"
#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>

namespace viewer::steering_benchmark {
namespace {
using navmesh_path::Point;
struct Random {
    std::mt19937 engine;
    explicit Random(std::uint32_t seed):engine(seed) {}
    float between(float low,float high) {
        return float(double(low)+(double(high)-low)*(double(engine())/4294967296.0));
    }
};
Model terrain(float lowX,float lowZ,float highX,float highZ) {
    Model result;
    Primitive primitive;
    for (const auto point : {glm::vec3(lowX,0,lowZ),glm::vec3(lowX,0,highZ),
                             glm::vec3(highX,0,highZ),glm::vec3(highX,0,lowZ)}) {
        Vertex vertex; vertex.position=point; primitive.vertices.push_back(vertex);
    }
    primitive.indices={0,1,2,0,2,3};
    result.primitives.push_back(std::move(primitive));
    result.draws.push_back({0,glm::mat4(1),"terrain"});
    return result;
}
Scenario rectangle(std::string name,std::string purpose,float lowX=-6,float lowZ=-5,
                   float highX=6,float highZ=5) {
    Scenario result;
    result.name=std::move(name); result.purpose=std::move(purpose);
    result.terrain=terrain(lowX-1,lowZ-1,highX+1,highZ+1);
    result.mesh=navmesh_path::Mesh({{lowX,lowZ},{highX,lowZ},{highX,highZ},{lowX,highZ}},
        {{0,4}},{{0,navmesh_path::noPolygon},{1,navmesh_path::noPolygon},
                {2,navmesh_path::noPolygon},{3,navmesh_path::noPolygon}});
    result.requests.push_back({{-5,0,0},{5,0},{}});
    return result;
}
void tree(Scenario& scenario,float x,float z,float radius=.3f) {
    scenario.trunks.push_back({{x,0,z},radius,5});
}
bool endpointClear(const Scenario& scenario,Point point) {
    for (const auto& trunk:scenario.trunks)
        if (std::hypot(point.x-trunk.base.x,point.z-trunk.base.z)<trunk.radius+.36) return false;
    return scenario.mesh.locate(point)!=navmesh_path::noPolygon;
}
void plan(Scenario& scenario) {
    navmesh_path::PathManager manager(scenario.mesh,std::uint32_t(scenario.requests.size()),
                                     navmesh_path::Search::defaultNodeLimit,8);
    const auto start=std::chrono::steady_clock::now();
    for (std::size_t id=0;id<scenario.requests.size();++id) {
        auto& request=scenario.requests[id];
        const auto path=manager.request(id+1,{request.start.x,request.start.z},request.goal);
        if (!path) throw std::runtime_error("Synthetic tree-free path unexpectedly failed: "+scenario.name);
        const auto view=manager.view(path);
        request.path.assign(view.begin(),view.end());
        if (!endpointClear(scenario,{request.start.x,request.start.z}) || !endpointClear(scenario,request.goal))
            throw std::runtime_error("Synthetic endpoint collides with a trunk: "+scenario.name);
    }
    scenario.pathfindingMilliseconds=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
}
}
std::vector<Scenario> makeScenarios(std::size_t population,std::uint32_t seed) {
    if (!population || population>100000) throw std::invalid_argument("population must be 1..100000");
    std::vector<Scenario> result;
    result.push_back(rectangle("empty","Unobstructed reference; all controllers should preserve the route."));
    result.push_back(rectangle("head_on","Exact head-on approach exposes symmetry and deadlocks."));
    tree(result.back(),0,0,.35f);
    result.push_back(rectangle("offset_tree","Slightly offset approach permits physical sliding."));
    tree(result.back(),0,.22f,.35f);
    result.push_back(rectangle("passable_gap","0.9 m free trunk gap admits the 0.6 m diameter capsule."));
    tree(result.back(),0,-.75f); tree(result.back(),0,.75f);
    result.push_back(rectangle("impassable_gap_aroundable","0.4 m gap blocks passage between trees; detour around the pair is possible."));
    tree(result.back(),0,-.5f); tree(result.back(),0,.5f);
    result.push_back(rectangle("sealed_gap","Tree blocks the entire 1.4 m navmesh corridor; nonarrival is expected.",-6,-.7f,6,.7f));
    tree(result.back(),0,0,.45f);
    result.push_back(rectangle("overlapping_bumps","Three overlapping influence fields with asymmetric physical gaps."));
    tree(result.back(),-.35f,-.55f,.22f); tree(result.back(),-.35f,.55f,.22f); tree(result.back(),.7f,0,.22f);
    result.push_back(rectangle("u_trap","U-shaped overlapping trunk barrier requires retreat or a wide detour."));
    for (int step=0;step<=5;++step) {
        tree(result.back(),-2+step*.4f,-1.2f,.23f);
        tree(result.back(),-2+step*.4f,1.2f,.23f);
    }
    for (int step=1;step<6;++step) tree(result.back(),0,-1.2f+step*.4f,.23f);
    result.push_back(rectangle("dense_cluster","Random local thicket; endpoints stay outside the cluster."));
    Random random(seed);
    for (unsigned index=0;index<22;++index)
        tree(result.back(),random.between(-1.5f,1.5f),random.between(-2,2),random.between(.18f,.32f));
    auto woodland=rectangle("woodland","Independent agents cross one shared 100 x 100 m woodland; no agent-agent avoidance.",-50,-50,50,50);
    woodland.requests.clear();
    for (unsigned index=0;index<500;++index)
        tree(woodland,random.between(-48,48),random.between(-48,48),random.between(.15f,.4f));
    for (std::size_t index=0;index<population;++index) {
        bool accepted=false;
        for (unsigned attempt=0;attempt<100000;++attempt) {
            const Point start{random.between(-37,37),random.between(-37,37)};
            const double angle=random.between(0,6.283185307179586f),length=random.between(15,30);
            const Point goal{float(start.x+std::cos(angle)*length),float(start.z+std::sin(angle)*length)};
            if (goal.x < -47 || goal.x > 47 || goal.z < -47 || goal.z > 47 ||
                !endpointClear(woodland,start) || !endpointClear(woodland,goal)) continue;
            woodland.requests.push_back({{float(start.x),0,float(start.z)},goal,{}});
            accepted=true; break;
        }
        if (!accepted) throw std::runtime_error("Unable to generate clear woodland endpoints");
    }
    result.push_back(std::move(woodland));
    for (auto& scenario:result) plan(scenario);
    return result;
}
}
