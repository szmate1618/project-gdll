#include "zombie_steering.hpp"
#include "DetourAlloc.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace {
std::atomic<std::size_t> allocations{0},detourAllocations{0};
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void* detourAllocate(std::size_t size,dtAllocHint) { ++detourAllocations; return std::malloc(size ? size : 1); }
void detourFree(void* memory) { std::free(memory); }
bool near(float a,float b,float tolerance=2e-4f) { return std::abs(a-b)<=tolerance; }
// Independent scalar calculation in double precision for central differences.
double referencePotential(double x,double z,const viewer::steering::Obstacle* obstacles,
                          std::size_t count,const viewer::steering::Config& config) {
    double total=0;
    for (std::size_t i=0;i<count;++i) {
        const double dx=x-obstacles[i].center.x,dz=z-obstacles[i].center.y;
        const double distance=std::hypot(dx,dz);
        const double scale=double(obstacles[i].radius)+config.radius+config.skin;
        const double range=scale+config.influenceRange;
        if (distance>=range) continue;
        const double taper=1-std::pow(distance/range,2);
        total+=std::pow(taper,3)/(1+std::pow(distance/scale,2));
    }
    return total;
}
}
void* operator new(std::size_t size) {
    ++allocations;
    if (void* memory=std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

int main() try {
    using namespace viewer::steering;
    dtAllocSetCustom(detourAllocate,detourFree);
    Config config;
    Controller controller(config,8);
    const Obstacle obstacles[]={{4,{0,0},.4f},{9,{1.4f,.8f},.2f},{12,{-1.1f,1.2f},.6f}};
    for (const glm::vec2 p:std::array<glm::vec2,5>{{{-.8f,.3f},{.4f,-.6f},{2.5f,1.7f},{-.1f,2.3f},{4.7f,4.9f}}}) {
        constexpr double step=1e-5;
        const auto actual=gradient(p,obstacles,3,config);
        const auto dx=(referencePotential(p.x+step,p.y,obstacles,3,config)-
                       referencePotential(p.x-step,p.y,obstacles,3,config))/(2*step);
        const auto dz=(referencePotential(p.x,p.y+step,obstacles,3,config)-
                       referencePotential(p.x,p.y-step,obstacles,3,config))/(2*step);
        require(near(actual.x,float(dx)) && near(actual.y,float(dz)),"analytical product gradient differs from independent numerical derivative");
        require(near(potential(p,obstacles,3,config),float(referencePotential(p.x,p.y,obstacles,3,config))),"potential mismatch");
    }
    const float edge=obstacles[0].radius+config.radius+config.skin+config.influenceRange;
    require(potential({edge,0},obstacles,1,config)==0 &&
            glm::length(gradient({edge,0},obstacles,1,config))==0,"compact field boundary is not flat");
    require(glm::length(gradient({edge-1e-3f,0},obstacles,1,config))<1e-5f,"gradient jumps at influence boundary");
    State state;
    Stats stats;
    const auto uphill=controller.steer(Mode::gradient,{-1,0},{1,0},.02f,obstacles,1,state,stats);
    const auto downhill=controller.steer(Mode::gradient,{-1,0},{-1,0},.02f,obstacles,1,state,stats);
    require(uphill.x>0 && uphill.x<.8f && uphill.y==0,"head-on analytic filter must slow without inventing a sidestep");
    require(downhill==glm::vec2(-1,0),"one-sided filter slowed downhill travel");
    const auto side=controller.steer(Mode::gradient_side,{-1,0},{1,0},.02f,obstacles,1,state,stats);
    require(side.y>0 && state.obstacle==4 && state.side==1,"head-on passing tie is not stable left");
    const auto originalChoices=stats.sideChanges;
    for (int i=0;i<20;++i) {
        const auto value=controller.steer(Mode::gradient_side,{-1,float(i%2 ? -1 : 1)*1e-5f},{1,0},.02f,obstacles,1,state,stats);
        require(value.y>0 && state.side==1,"tiny offsets switched persistent passing side");
    }
    require(stats.sideChanges==originalChoices,"persistent pass reselected side every frame");
    State pushState;
    const auto push=controller.steer(Mode::perpendicular_push,{-1,0},{1,0},.02f,obstacles,1,pushState,stats);
    require(near(push.x,std::sqrt(.5f)) && near(push.y,std::sqrt(.5f)),"one-to-one perpendicular push changed head-on displacement ratio");
    State offAxis;
    const auto pushAway=controller.steer(Mode::perpendicular_push,{-1,-.4f},{1,0},.02f,obstacles,1,offAxis,stats);
    require(pushAway.y<0,"perpendicular push did not move away from offset tree");
    State passed;
    passed.obstacle=4;passed.side=1;
    controller.steer(Mode::gradient_side,{1,1},{1,0},.02f,obstacles,1,passed,stats);
    require(passed.obstacle==UINT32_MAX && passed.side==0,"passing state retained a trunk behind us");
    const std::array<Mode,5> modes={Mode::collision_only,Mode::gradient,Mode::gradient_side,Mode::perpendicular_push,Mode::detour};
    const auto before=allocations.load(),detourBefore=detourAllocations.load();
    for (auto mode:modes) {
        State local;
        Stats samples;
        for (int i=0;i<200;++i) {
            const glm::vec2 desired{1.2f,.15f};
            const auto value=controller.steer(mode,{-1.5f,-.3f},desired,.02f,obstacles,3,local,samples);
            require(std::isfinite(value.x) && std::isfinite(value.y) &&
                    glm::length(value)<=glm::length(desired)+1e-5f,"invalid or over-speed steering velocity");
            local.velocity=value;
        }
        require(controller.steer(mode,{0,0},{0,0},.02f,obstacles,3,local,samples)==glm::vec2(0),"stopped agent acquired velocity");
        require(controller.steer(mode,{0,0},{1,0},.02f,nullptr,0,local,samples)==glm::vec2(1,0),"empty field changed desired velocity");
        if (mode==Mode::detour) require(samples.velocitySamples>0,"Detour sampler was not used");
    }
    require(allocations.load()==before && detourAllocations.load()==detourBefore,"steering allocated in the update loop");
    require(controller.workspaceBytes()>8*sizeof(Obstacle),"steering workspace not reported");
    bool overflow=false;
    try { controller.steer(Mode::detour,{0,0},{1,0},.02f,obstacles,9,state,stats); }
    catch (const std::length_error&) { overflow=true; }
    require(overflow,"overflow silently dropped obstacles");
    std::cout<<"steering analytical derivative, persistent side, exact push, Detour and no-allocation regressions passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
