#include "../tools/potential_surface_simulation.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace viewer::potential_surface_benchmark;
using viewer::potential_surface::World;
using viewer::potential_surface::Obstacle;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void unobstructedAndAuditStream() {
    const World world({}, {-20,-20,20,20});
    const std::vector<Request> requests={{31,{-5,0},{5,0}},{72,{0,-4},{0,4}}};
    std::ostringstream bytes(std::ios::binary);
    Config config;
    const auto result=simulate(world,requests,config,&bytes);
    require(result.agents.size()==2 && result.agents[0].status==Status::arrived && result.agents[1].status==Status::arrived,
        "Direct unobstructed travel did not reach the goal");
    require(result.agents[0].arrivalSeconds>=4.9 && result.agents[0].arrivalSeconds<=5.05,
        "Unobstructed velocity exceeded or failed to maintain the common speed");
    require(result.validationViolations==0 && result.validationSegments==result.updates,
        "Every accepted update must receive an untimed strict native geometry check");
    require(result.movedSegments==result.updates && bytes.str().size()==36*result.movedSegments,
        "Audit stream omitted a moved step or used the wrong packed record size");
    const auto data=bytes.str();
    std::uint32_t first=0;for (unsigned index=0;index<4;++index) first|=std::uint32_t(static_cast<unsigned char>(data[index]))<<(index*8);
    require(first==31,"Packed audit ID is not original agent ID in little-endian form");
    auto readDouble=[&](std::size_t offset) {
        std::uint64_t bits=0;for (unsigned index=0;index<8;++index) bits|=std::uint64_t(static_cast<unsigned char>(data[offset+index]))<<(index*8);
        double value;std::memcpy(&value,&bits,sizeof(value));return value;
    };
    require(readDouble(4)==-5 && readDouble(12)==0,"Audit stream did not preserve the exact initial double coordinates");
    require(readDouble(20)>-5 && readDouble(28)==0,"Audit stream did not record actual accepted motion");
    const auto repeat=simulate(world,requests,config);
    require(repeat.updates==result.updates && repeat.queries.fieldNodes==result.queries.fieldNodes,
        "Unchanged requests produced inconsistent work counters");
    for (std::size_t id=0;id<result.agents.size();++id) {
        const auto& agent=result.agents[id];const auto& again=repeat.agents[id];
        require(agent.travel==again.travel && agent.position.x==again.position.x && agent.position.z==again.position.z &&
            agent.headingRadians==again.headingRadians && agent.trace.size()==again.trace.size(),"Repeated simulation changed deterministic movement");
        require(agent.budgetSeconds==120,"Minimum per-agent time budget changed");
        require(agent.invalidMoves==0,"Clear direct motion produced an invalid collision start");
    }
}
void blockedMotionAndInvalidEndpoints() {
    const World world({Obstacle{1,{0,0},{1,0},.35,.35}}, {-10,-10,10,10});
    const auto result=simulate(world,{{5,{-4,0},{4,0}}},{});
    const auto& agent=result.agents[0];
    require(agent.status==Status::stalled,"Exact head-on potential filter must expose a stationary symmetry trap");
    require(agent.position.x<-.65 && agent.position.z==0,"Pure potential filter introduced a sidestep or crossed a trunk");
    require(agent.clippedMoves>0 && result.validationViolations==0,"Hard physical clipping did not preserve clear accepted segments");
    require(agent.elapsedSeconds<agent.budgetSeconds && agent.elapsedSeconds>=10,
        "Stall detection failed to use the ten-second displacement window");
    const auto invalid=simulate(world,{{8,{0,0},{4,0}},{9,{-4,0},{0,0}}},{});
    require(invalid.updates==0 && invalid.steps.empty(),"Invalid endpoints were moved or silently snapped");
    require(invalid.agents[0].status==Status::invalidEndpoint && invalid.agents[1].status==Status::invalidEndpoint,
        "Blocked start and goal must both receive invalid_endpoint outcomes");
    const auto already=simulate(world,{{11,{-4,0},{-4,0}}},{});
    require(already.agents[0].status==Status::arrived && already.agents[0].arrivalSeconds==0 && already.updates==0,
        "Coincident valid endpoint did not arrive immediately");
}
void configurationAndBudgetChecks() {
    const World world({}, {-10000,-10000,10000,10000});
    bool rejected=false;Config tiny;tiny.timestep=1e-100;
    try { (void)simulate(world,{{1,{-1,0},{1,0}}},tiny); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"Extreme timestep must reject before integer conversion");
    rejected=false;
    try { (void)simulate(world,{{1,{-1,0},{1,0}},{1,{0,-1},{0,1}}},{}); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"Duplicate original IDs would corrupt per-agent audit continuity");
    const auto result=simulate(world,{{1,{-100,0},{100,0}}},{});
    require(result.agents[0].budgetSeconds==360,"Distance-dependent time budget formula changed");
    require(result.agents[0].status==Status::arrived,"Long open route should arrive within its individual budget");
}
}
int main() try {
    unobstructedAndAuditStream();blockedMotionAndInvalidEndpoints();configurationAndBudgetChecks();
    std::cout<<"Standalone potential-surface simulation and complete movement audit tests passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
