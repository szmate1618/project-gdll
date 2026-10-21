#include "navmesh_fixture.hpp"
#include "../tools/zombie_navmesh_benchmark_inputs.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace viewer::navmesh_path;
using namespace viewer::navmesh_benchmark;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void deterministicSampling() {
    const auto mesh=test_navmesh::convexSquares(8,8,{{2,2},{3,2},{4,4},{5,4},{6,4},{1,6}});
    const auto first=randomInputs(mesh,1000,0);
    const auto repeated=randomInputs(mesh,1000,0);
    const auto prefix=randomInputs(mesh,10,0);
    const auto other=randomInputs(mesh,1000,1);
    require(first.inputs.size()==1000 && first.startAttempts>=1000 && first.goalAttempts>=1000,
            "wrong random request count or attempt accounting");
    require(first.startAttempts>1000 || first.goalAttempts>1000,"hole rejection fixture exercised no rejection");
    require(first.startAttempts==repeated.startAttempts && first.goalAttempts==repeated.goalAttempts,
            "fixed seed changed rejection accounting");
    bool different=false;
    const std::set<std::pair<unsigned,unsigned>> holes{{2,2},{3,2},{4,4},{5,4},{6,4},{1,6}};
    for (std::size_t index=0;index<first.inputs.size();++index) {
        const auto& input=first.inputs[index];
        require(input.start==repeated.inputs[index].start && input.goal==repeated.inputs[index].goal,
                "fixed seed changed exact endpoint coordinates");
        if (index<prefix.inputs.size())
            require(input.start==prefix.inputs[index].start && input.goal==prefix.inputs[index].goal,
                    "random request prefix depends on batch size");
        different|=!(input.start==other.inputs[index].start && input.goal==other.inputs[index].goal);
        for (const auto point : {input.start,input.goal}) {
            require(point.x>=0 && point.x<=8 && point.z>=0 && point.z<=8,
                    "random endpoint outside map bounds");
            require(point.x==double(float(point.x)) && point.z==double(float(point.z)),
                    "random endpoint was not rounded to float32");
            for (auto [x,z]:holes)
                require(!(point.x>x && point.x<x+1 && point.z>z && point.z<z+1),
                        "random endpoint inside an independently specified blocked square");
            require(mesh.locate(point)!=noPolygon,"random endpoint is not walkable after rounding");
        }
    }
    require(different,"different seeds produced identical random requests");
    require(randomInputs(mesh,0,0).inputs.empty(),"zero request generation should be empty");
}
void disconnectedPairsAreRetained() {
    const auto mesh=test_navmesh::convexSquares(3,1,{{1,0}});
    const auto generated=randomInputs(mesh,200,1234);
    bool crossedComponents=false;
    for (const auto& input:generated.inputs)
        crossedComponents|=(input.start.x<1 && input.goal.x>2) || (input.start.x>2 && input.goal.x<1);
    require(crossedComponents,"disconnected random request pairs were filtered out");
    require(generated.startAttempts+generated.goalAttempts>400,"blocked gap never exercised rejection");
    bool rejected=false;
    try { (void)randomInputs(Mesh{},1,0); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"empty navmesh accepted for random sampling");
}
}
int main() try {
    deterministicSampling(); disconnectedPairsAreRetained();
    std::cout<<"Deterministic walkable random navmesh benchmark input tests passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
