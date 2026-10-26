// Untimed real-map trajectory validation, separate from performance runs.
#include "scene.hpp"
#include "zombie_scenario.hpp"
#include "zombie_navigation.hpp"
#include "simulation_clock.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=7 && argc!=8) throw std::runtime_error(
            "Usage: validate_zombie_travel MAP SCENARIO OUT full|optimized SPAWN_X SPAWN_Z [FINAL_POSES]");
        const std::string mode=argv[4];
        if(mode!="full" && mode!="optimized") throw std::runtime_error("Unknown traveler mode");
        multiplayer::Options options;options.map=argv[1];options.spawn=glm::vec2(std::stof(argv[5]),std::stof(argv[6]));
        const auto config=viewer::loadZombieScenarioConfig(argv[2]);
        for (const auto& horde : config.hordes)
            if (horde.movement != viewer::ZombieScenarioMovement::travel)
                throw std::runtime_error("Travel validation requires movement = 'travel' for every group");
        if(!config.seed) throw std::runtime_error("Validation requires a seeded scenario");
        options.zombieCount=config.count();auto mesh=options.map;mesh.replace_extension(".navmesh");
        const auto population=[&](const auto& assets,const auto& world,const auto& model) {
            auto layer=viewer::loadZombieScenario(assets,world,model,mesh,config);
            layer.setDistantScenarioTravelEnabled(mode!="full");return layer;
        };
        multiplayer::Scene scene(options,population);
        const auto requests=scene.zombies.navigation()->stats().requests;
        const bool poses=argc==8;
        if(poses) {
            std::ifstream input(argv[7],std::ios::binary);
            for(auto& instance:scene.zombies.instances) {
                input.read(reinterpret_cast<char*>(&instance.restRoot),sizeof(instance.restRoot));
                input.ignore(sizeof(glm::mat4)+sizeof(double)+2);
                if(!input) throw std::runtime_error("Invalid explicit-field final-pose capture");
                instance.transform=instance.restTransform=instance.restRoot*instance.chaseNormalization;
            }
            scene.zombies.rebuildSpatialIndex();
        }
        const std::filesystem::path output=argv[3];std::filesystem::create_directories(output);
        std::ofstream violations(output/"violations.csv");
        violations<<"frame,id,physical_depth,skin_depth,navmesh_inside,step,crowd_pairs\n";
        std::size_t physical=0,skin=0,outside=0,overspeed=0,pairs=0;
        float maximumDepth=0,maximumGroundError=0;
        std::vector<glm::vec3> before(scene.zombies.instances.size());std::vector<std::size_t> near;
        const viewer::ZombiePlayer player{scene.spawnEye-glm::vec3(0,1.7f,0),1.7f,1.8f};
        auto& clock=viewer::SimulationClock::instance();clock.reset();constexpr float dt=1.f/60;
        const unsigned frames=poses?1:600;
        const auto blocking=[](const viewer::Contact& c,glm::vec3 feet) {
            return c.depth>.0001f && !(c.terrainSupport && c.normal.y>=.70710678f) &&
                !(c.normal.y>.7f && c.point.y<=feet.y+.0021f);
        };
        for(unsigned frame=0;frame<frames;++frame) {
            if(!poses) {
                for(std::size_t id=0;id<before.size();++id) before[id]=glm::vec3(scene.zombies.instances[id].restRoot[3]);
                clock.advance(dt);scene.zombies.updateBehavior(dt,player,*scene.collision,clock.seconds());
            }
            for(std::size_t id=0;id<scene.zombies.instances.size();++id) {
                const auto feet=glm::vec3(scene.zombies.instances[id].restRoot[3]);
                float padded=0,depth=0;
                for(const auto& c:scene.collision->contacts(feet,.3f,1.8f,.002f,.05f))
                    if(blocking(c,feet)) padded=std::max(padded,c.depth);
                if(padded>0) for(const auto& c:scene.collision->contacts(feet,.3f,1.8f,0,.05f))
                    if(blocking(c,feet)) depth=std::max(depth,c.depth);
                const bool inside=scene.zombies.navigation()->segmentClear(feet,feet);
                const float step=poses?0:glm::length(glm::vec2(feet.x-before[id].x,feet.z-before[id].z));
                // One float coordinate ULP is already ~0.25mm at 3km. Bound
                // quantization from both endpoints instead of assuming origin precision.
                const float scale=std::max({1.f,std::abs(feet.x),std::abs(feet.z),std::abs(before[id].x),std::abs(before[id].z)});
                const float precision=std::max(.0001f,2*std::numeric_limits<float>::epsilon()*scale);
                const bool fast=step>(scene.zombies.scenarioDestination(id)?1.2f:3.f)*dt+precision;
                std::size_t overlaps=0;
                scene.zombies.queryLivingNear(feet,2,near);
                for(auto other:near) if(other>id) {
                    const auto p=glm::vec3(scene.zombies.instances[other].restRoot[3]);
                    const glm::vec2 d(p.x-feet.x,p.z-feet.z);
                    if(std::abs(p.y-feet.y)<1.79f && glm::dot(d,d)<.5998f*.5998f) ++overlaps;
                }
                const auto hit=scene.collision->walkingGroundAt(feet.x,feet.z,-1e10f,feet.y+.3f);
                if(hit) maximumGroundError=std::max(maximumGroundError,std::abs(feet.y-hit->height));
                physical+=depth>0;skin+=padded>0;outside+=!inside;overspeed+=fast;pairs+=overlaps;
                maximumDepth=std::max(maximumDepth,depth);
                if(depth>0 || padded>0 || !inside || fast || overlaps)
                    violations<<frame<<','<<id<<','<<depth<<','<<padded<<','<<inside<<','<<step<<','<<overlaps<<'\n';
            }
        }
        std::ofstream summary(output/"summary.txt");
        summary<<"frames="<<frames<<"\nactors="<<scene.zombies.instances.size()<<"\nphysical_samples="<<physical
               <<"\nskin_samples="<<skin<<"\nmax_physical_depth="<<maximumDepth<<"\noutside_navmesh="<<outside
               <<"\noverspeed="<<overspeed<<"\ncrowd_overlap_pairs="<<pairs<<"\nmax_ground_error="<<maximumGroundError<<'\n';
        if(scene.zombies.navigation()->stats().requests!=requests) throw std::runtime_error("Validation requested runtime paths");
        std::cout<<"Validated "<<frames<<" frames; physical="<<physical<<" skin="<<skin<<" outside="<<outside
                 <<" overspeed="<<overspeed<<" crowd="<<pairs<<" max_depth="<<maximumDepth<<'\n';
        return physical || outside || overspeed || pairs?1:0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
