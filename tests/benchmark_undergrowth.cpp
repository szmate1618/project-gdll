#include "undergrowth_fixture.hpp"
#include "forest_layer.hpp"
#include "collision_world.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace {
using Clock=std::chrono::steady_clock;
constexpr int ticks=12000, rounds=7;
double run(const viewer::UndergrowthIndex& index,const std::vector<glm::vec3>& roots,int players,bool moving,bool running) {
    std::array<viewer::UndergrowthEmitter,8> emitters;
    std::array<viewer::UndergrowthMotion,8> before;
    for(int i=0;i<players;++i) before[i]=undergrowth_fixture::motion(roots[i%roots.size()]+glm::vec3(1.5f,0,0),running);
    std::uint64_t events=0;
    Clock::time_point start;
    for(int tick=-600;tick<ticks;++tick) {
        if(tick==0) start=Clock::now();
        for(int i=0;i<players;++i) {
            auto after=before[i];
            if(moving) {
                const float angle=(tick+601)*(running?6.f:3.f)/90.f;
                after.feet=roots[i%roots.size()]+glm::vec3(1.5f*std::cos(angle),0,1.5f*std::sin(angle));
            }
            if(emitters[i].update(1.f/60,before[i],after,index)) ++events;
            before[i]=after;
        }
    }
    // Keep event generation observable, without doing IO inside timers.
    if(events>100000) throw std::runtime_error("Unbounded undergrowth event count");
    return std::chrono::duration<double,std::micro>(Clock::now()-start).count()/ticks;
}
}
int main(int argc,char** argv) {
    try {
        auto source=undergrowth_fixture::trees();
        if(argc==4) {
            const auto model=viewer::loadModel(argv[1]); const viewer::CollisionWorld world(model);
            source=viewer::loadTreeLayer(argv[2],argv[1]);
            viewer::loadForestLayer(argv[3],argv[1],model,world,source);
        } else {
            source.instances.clear();
            for(int x=-100;x<100;++x) for(int z=-100;z<100;++z) {
                viewer::TreeInstance shrub; shrub.undergrowth=viewer::UndergrowthKind::dry;
                shrub.transform=glm::scale(glm::translate(glm::mat4(1),{x*8.f,0,z*8.f}),glm::vec3(2));
                source.instances.push_back(shrub);
            }
        }
        std::vector<glm::vec3> roots;
        for(const auto& tree:source.instances)
            if(tree.undergrowth!=viewer::UndergrowthKind::none && roots.size()<8) roots.push_back(glm::vec3(tree.transform[3]));
        if(roots.empty()) throw std::runtime_error("Benchmark needs shrubs");
        const auto start=Clock::now(); const viewer::UndergrowthIndex index(source),empty;
        std::cout<<"shrubs,"<<index.size()<<",index_build_ms,"
                 <<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<'\n';
        std::cout<<"players,mode,disabled_us_per_tick,enabled_us_per_tick\n";
        for(const int players:{1,8}) for(const int mode:{0,1,2}) {
            std::vector<double> off,on;
            for(int round=0;round<rounds;++round) {
                if(round%2) { on.push_back(run(index,roots,players,mode>0,mode==2));off.push_back(run(empty,roots,players,mode>0,mode==2)); }
                else { off.push_back(run(empty,roots,players,mode>0,mode==2));on.push_back(run(index,roots,players,mode>0,mode==2)); }
            }
            std::sort(off.begin(),off.end());std::sort(on.begin(),on.end());
            std::cout<<players<<','<<(mode==0?"idle":mode==1?"walk":"run")<<','<<off[rounds/2]<<','<<on[rounds/2]<<'\n';
        }
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
