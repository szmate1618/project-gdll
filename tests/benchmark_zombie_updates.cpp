// Real-map, fixed-step zombie-update profiling. Scene loading and route planning
// are excluded. Alternate detailed/control runs and verify identical final poses.
#include "scene.hpp"
#include "zombie_scenario.hpp"
#include "zombie_navigation.hpp"
#include "zombie_update_profile.hpp"
#include "simulation_clock.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc!=4 && argc!=6 && argc!=7) throw std::runtime_error(
            "Usage: benchmark_zombie_updates MAP SCENARIO OUTPUT_DIR [SPAWN_X SPAWN_Z [full|optimized]]");
        multiplayer::Options options;
        options.map=argv[1];
        if (argc>=6) options.spawn=glm::vec2(std::stof(argv[4]),std::stof(argv[5]));
        const std::string modeName=argc==7?argv[6]:"optimized";
        if (modeName!="full" && modeName!="optimized") throw std::runtime_error("Unknown scenario travel mode");
        const auto config=viewer::loadZombieScenarioConfig(argv[2]);
        if (!config.seed) throw std::runtime_error("Repeatable profiling requires a scenario seed");
        options.zombieCount=config.count();
        auto navmesh=options.map; navmesh.replace_extension(".navmesh");
        const auto population=[&](const auto& assets, const auto& collision, const auto& model) {
            auto layer=viewer::loadZombieScenario(assets,collision,model,navmesh,config);
            layer.setDistantScenarioTravelEnabled(modeName!="full");
            return layer;
        };
        multiplayer::Scene scene(options,population);
        const std::filesystem::path output=argv[3];
        std::filesystem::create_directories(output);
        std::ofstream metadata(output/"metadata.txt");
        metadata<<"map="<<options.map<<"\nscenario="<<argv[2]<<"\npopulation="<<scene.zombies.instances.size()
                <<"\ntrunks="<<scene.collision->stats().trunks<<"\ntriangles="<<scene.collision->stats().triangles
                <<"\nseed="<<*config.seed<<"\nspawn_eye="<<scene.spawnEye.x<<','<<scene.spawnEye.y<<','<<scene.spawnEye.z
                <<"\ntravel_mode="<<modeName<<"\ndt=1/60\nwarmup_steps=60\nmeasured_steps=300\nruns=6 (3 detailed, 3 control)\n";
        viewer::zombie_profile::initializeClock();
        metadata << "seconds_per_tick=" << viewer::zombie_profile::detail::secondsPerTick
                 << "\ntimer=" << viewer::zombie_profile::timerName() << '\n';
        std::vector<viewer::ZombieInstance> reference;
        auto& clock=viewer::SimulationClock::instance();
        const viewer::ZombiePlayer player{scene.spawnEye-glm::vec3(0,1.7f,0),1.7f,1.8f};
        constexpr float dt=1.f/60;
        for (unsigned run=0;run<6;++run) {
            clock.reset();
            if (run) scene.zombies=population(options.zombieAssets,*scene.collision,scene.model);
            const bool detailed=run%2 != (run/2)%2;
            std::cout<<"Run "<<run<<" ("<<(detailed?"detailed":"control")<<")\n"<<std::flush;
            std::ofstream rows(output/("run-"+std::to_string(run)+".csv"));
            if (!rows) throw std::runtime_error("Cannot open profile CSV");
            rows<<std::setprecision(12)<<"frame,detailed,cpu_update_ms";
            for (const auto name:viewer::zombie_profile::names) rows<<','<<name<<"_ms,"<<name<<"_calls,"<<name<<"_dropped";
            rows<<",other_ms,perception_checks,sight_rays,path_requests,validation_reuses,coverage_region_reuses,coverage_region_builds,door_candidates,door_overlaps,crowd_moves,crowd_neighbor_checks,crowd_budget_stops,crowd_empty_sector_skips,walking_region_reuses,simple_traveler_clears,traveler_plans,traveler_fallbacks,traveler_interpolations\n";
            viewer::zombie_profile::Profile aggregate;
            for (unsigned frame=0;frame<360;++frame) {
                clock.advance(dt);
                viewer::zombie_profile::Profile sample;
                const bool measured=frame>=60;
                const auto begin=std::chrono::steady_clock::now();
                {
                    viewer::zombie_profile::Capture capture(measured && detailed?&sample:nullptr);
                    scene.zombies.updateBehavior(dt,player,*scene.collision,clock.seconds());
                }
                const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                if (!measured) continue;
                rows<<frame<<','<<detailed<<','<<ms;
                double stages=0;
                for (std::size_t i=0;i<sample.stages.size();++i) {
                    const auto& stage=sample.stages[i];
                    rows<<','<<stage.seconds*1000<<','<<stage.calls<<','<<stage.dropped;
                    aggregate.stages[i].seconds+=stage.seconds;
                    aggregate.stages[i].calls+=stage.calls;
                    aggregate.stages[i].dropped+=stage.dropped;
                    stages+=stage.seconds;
                }
                const auto& behavior=scene.zombies.behaviorStats();
                const auto& navigation=scene.zombies.navigation()->stats();
                rows<<','<<(sample.seconds-stages)*1000<<','<<behavior.perceptionChecks<<','<<behavior.sightRays
                    <<','<<navigation.requests<<','<<navigation.movementValidationReuses
                    <<','<<navigation.coverageRegionReuses<<','<<navigation.coverageRegionBuilds
                    <<','<<sample.doorCandidates<<','<<sample.doorOverlaps
                    <<','<<scene.zombies.crowdStats().moves<<','<<scene.zombies.crowdStats().neighborChecks
                    <<','<<scene.zombies.crowdStats().budgetStops<<','<<scene.zombies.crowdStats().emptySectorSkips<<','<<sample.walkingRegionReuses<<','<<sample.simpleTravelerClears<<','<<sample.travelerPlans<<','<<sample.travelerFallbacks<<','<<sample.travelerInterpolations<<'\n';
                aggregate.doorCandidates+=sample.doorCandidates;
                aggregate.doorOverlaps+=sample.doorOverlaps;
                aggregate.walkingRegionReuses+=sample.walkingRegionReuses;
                aggregate.simpleTravelerClears+=sample.simpleTravelerClears;
                aggregate.travelerPlans+=sample.travelerPlans;
                aggregate.travelerFallbacks+=sample.travelerFallbacks;
                aggregate.travelerInterpolations+=sample.travelerInterpolations;
                aggregate.seconds+=sample.seconds;
                aggregate.steps+=sample.steps;
            }
            if (scene.zombies.navigation()->stats().requests!=config.count())
                throw std::runtime_error("Unexpected runtime path searches");
            if (run==0) {
                reference=scene.zombies.instances;
                std::ofstream poses(output/"final-poses.bin",std::ios::binary);
                if (!poses) throw std::runtime_error("Cannot write final-pose comparison");
                for (const auto& instance:reference) {
                    poses.write(reinterpret_cast<const char*>(&instance.restRoot),sizeof(instance.restRoot));
                    poses.write(reinterpret_cast<const char*>(&instance.transform),sizeof(instance.transform));
                    poses.write(reinterpret_cast<const char*>(&instance.walkSeconds),sizeof(instance.walkSeconds));
                    poses.put(instance.walking); poses.put(instance.running);
                }
            }
            else for (std::size_t id=0;id<reference.size();++id) {
                const auto& a=reference[id];const auto& b=scene.zombies.instances[id];
                if (a.restRoot!=b.restRoot || a.transform!=b.transform || a.walkSeconds!=b.walkSeconds ||
                    a.walking!=b.walking || a.running!=b.running)
                    throw std::runtime_error("Profiling/control final poses diverged");
            }
            aggregate.print(std::cout);
        }
        // Untimed final quality checks. Keep these after all six timing runs so
        // their additional queries do not warm the measured movement workload.
        std::ofstream quality(output/"quality.csv");
        quality<<"id,x,y,z,ground_error,blocking_contacts,navmesh_inside\n";
        for (std::size_t id=0;id<scene.zombies.instances.size();++id) {
            const auto feet=glm::vec3(scene.zombies.instances[id].restRoot[3]);
            const auto hit=scene.collision->walkingGroundAt(feet.x,feet.z,-1e10f,feet.y+.3f);
            const double error=hit?double(feet.y-hit->height):1e10;
            std::size_t blocking=0;
            for (const auto& c:scene.collision->contacts(feet,.3f,1.8f,.002f,.05f))
                if (c.depth>.0001f && !(c.terrainSupport && c.normal.y>=.70710678f) &&
                    !(c.normal.y>.7f && c.point.y<=feet.y+.0021f)) ++blocking;
            quality<<id<<','<<feet.x<<','<<feet.y<<','<<feet.z<<','<<error<<','<<blocking<<','
                   <<scene.zombies.navigation()->segmentClear(feet,feet)<<'\n';
        }
        std::cout<<"All six runs finished with identical final poses and no runtime path searches\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
