// Real-map, fixed-step zombie-update profiling. Scene loading and route planning
// are excluded. Alternate detailed/control runs and verify identical final poses.
#include "scene.hpp"
#include "zombie_scenario.hpp"
#include "zombie_navigation.hpp"
#include "zombie_update_profile.hpp"
#include "simulation_clock.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        std::size_t activateNearest=0,measuredSteps=300,rounds=6,warmupSteps=60,profileBlockSteps=0;
        while (argc>2) {
            const std::string option=argv[argc-2];
            if (option!="--activate-nearest" && option!="--steps" && option!="--rounds" && option!="--warmup-steps" && option!="--profile-block-steps") break;
            std::size_t used=0;
            const std::string text=argv[argc-1];
            const auto value=std::stoul(text,&used);
            if (used!=text.size() || text.front()=='-' || ((option=="--steps" || option=="--rounds") && !value))
                throw std::runtime_error("Invalid value for "+option);
            if (option=="--activate-nearest") activateNearest=value;
            else if (option=="--steps") measuredSteps=value;
            else if (option=="--warmup-steps") warmupSteps=value;
            else if (option=="--profile-block-steps") profileBlockSteps=value;
            else rounds=value;
            argc-=2;
        }
        const bool collideOverlays=argc>1 && std::string(argv[argc-1])=="--collide-overlays";
        if (collideOverlays) --argc;
        if (argc!=4 && argc!=6 && argc!=7) throw std::runtime_error(
            "Usage: benchmark_zombie_updates MAP SCENARIO OUTPUT_DIR [SPAWN_X SPAWN_Z [full|optimized]] [--activate-nearest N] [--steps N] [--rounds N] [--warmup-steps N] [--profile-block-steps N]");
        multiplayer::Options options;
        options.collideTerrainOverlays=collideOverlays;
        options.map=argv[1];
        if (argc>=6) options.spawn=glm::vec2(std::stof(argv[4]),std::stof(argv[5]));
        const std::string modeName=argc==7?argv[6]:"optimized";
        if (modeName!="full" && modeName!="optimized") throw std::runtime_error("Unknown scenario travel mode");
        const auto config=viewer::loadZombieScenarioConfig(argv[2]);
        std::size_t travelers=0;
        for (const auto& horde : config.hordes)
            if (horde.movement == viewer::ZombieScenarioMovement::travel) travelers+=horde.count;
        if (!travelers) throw std::runtime_error("Movement profiling requires at least one travel group");
        if (!config.seed) throw std::runtime_error("Repeatable profiling requires a scenario seed");
        options.zombieCount=config.count();
        auto navmesh=options.map; navmesh.replace_extension(".navmesh");
        const auto population=[&](const auto& assets, const auto& collision, const auto& model, std::size_t reserved=0) {
            auto layer=viewer::loadZombieScenario(assets,collision,model,navmesh,config,reserved);
            layer.setDistantScenarioTravelEnabled(modeName!="full");
            return layer;
        };
        multiplayer::Scene scene(options,population);
        const auto dormant=scene.zombies.instances.size()-config.count();
        const std::filesystem::path output=argv[3];
        std::filesystem::create_directories(output);
        std::ofstream metadata(output/"metadata.txt");
        metadata<<"map="<<options.map<<"\nscenario="<<argv[2]<<"\npopulation="<<scene.zombies.instances.size()
                <<"\ntravelers="<<travelers<<"\nbehavior_bytes="<<sizeof(viewer::ZombieBehavior)
                <<"\ntraveler_plan_bytes="<<sizeof(viewer::TravelerPlan)
                <<"\nactivate_nearest="<<activateNearest
                <<"\ntrunks="<<scene.collision->stats().trunks<<"\ntriangles="<<scene.collision->stats().triangles
                <<"\nseed="<<*config.seed<<"\nspawn_eye="<<scene.spawnEye.x<<','<<scene.spawnEye.y<<','<<scene.spawnEye.z
                <<"\ncollision_bytes="<<scene.collision->stats().allocatedBytes
                <<"\ncollide_overlays="<<collideOverlays
                <<"\ntravel_mode="<<modeName<<"\ndt=1/60\nwarmup_steps="<<warmupSteps<<"\nmeasured_steps="<<measuredSteps
                <<"\nprofile_block_steps="<<profileBlockSteps
                <<"\nruns="<<rounds<<" (single round is detailed; otherwise alternating detailed/control)\n";
        viewer::zombie_profile::initializeClock();
        metadata << "seconds_per_tick=" << viewer::zombie_profile::detail::secondsPerTick
                 << "\ntimer=" << viewer::zombie_profile::timerName() << '\n';
        std::ofstream routes(output/"routes.csv");
        routes<<std::setprecision(12)<<"id,start_x,start_z,goal_x,goal_z\n";
        for (std::size_t id=0;id<scene.zombies.instances.size();++id) {
            if (const auto* goal=scene.zombies.scenarioDestination(id)) {
                const auto feet=glm::vec3(scene.zombies.instances[id].restRoot[3]);
                routes<<id<<','<<feet.x<<','<<feet.z<<','<<goal->x<<','<<goal->z<<'\n';
            }
        }
        std::vector<viewer::ZombieInstance> reference;
        auto& clock=viewer::SimulationClock::instance();
        const viewer::ZombiePlayer player{scene.spawnEye-glm::vec3(0,1.7f,0),1.7f,1.8f};
        const auto activate=[&] {
            if (!activateNearest) return;
            std::vector<std::pair<float,std::size_t>> nearest;
            for (std::size_t id=0;id<config.count();++id) {
                const auto offset=glm::vec3(scene.zombies.instances[id].restRoot[3])-player.feet;
                nearest.emplace_back(glm::dot(offset,offset),id);
            }
            std::sort(nearest.begin(),nearest.end());
            // Controlled remembered-noise targets exercise direct investigation
            // even outside perception range. Normal sight/attacks still run.
            for (std::size_t i=0;i<std::min(activateNearest,nearest.size());++i) {
                const auto id=nearest[i].second;
                auto& behavior=scene.zombies.behaviors.create(id,0,glm::vec3(scene.zombies.instances[id].restRoot[3]));
                behavior.state=viewer::ZombieState::investigating;
                behavior.target=player.feet;
            }
        };
        constexpr float dt=1.f/60;
        for (std::size_t run=0;run<rounds;++run) {
            clock.reset();
            if (run) scene.zombies=population(options.zombieAssets,*scene.collision,scene.model,dormant);
            activate();
            const bool detailed=rounds==1 || run%2 != (run/2)%2;
            std::cout<<"Run "<<run<<" ("<<(detailed?"detailed":"control")<<")\n"<<std::flush;
            std::ofstream rows(output/("run-"+std::to_string(run)+".csv"));
            if (!rows) throw std::runtime_error("Cannot open profile CSV");
            rows<<std::setprecision(12)<<"frame,detailed,cpu_update_ms,gameplay_behavior_ms,scenario_travel_ms,idle,looking,investigating,chasing,attacking,hiding,midnight,house_investigating,chase_starts";
            for (const auto name:viewer::zombie_profile::names) rows<<','<<name<<"_ms,"<<name<<"_calls,"<<name<<"_dropped";
            rows<<",other_ms,perception_checks,sight_rays,path_requests,validation_reuses,coverage_region_reuses,coverage_region_builds,door_candidates,door_overlaps,crowd_moves,crowd_neighbor_checks,crowd_budget_stops,crowd_empty_sector_skips,walking_region_reuses,simple_traveler_clears,traveler_plans,traveler_fallbacks,traveler_interpolations,traveler_full_ms,traveler_full_coverage_ms,navigation_steering_calls,navigation_mesh_rejections";
            for (const auto name:viewer::zombie_profile::travelerSolverNames) rows<<",traveler_full_"<<name;
            for (const auto name:viewer::zombie_profile::travelerFailureNames) rows<<",traveler_failure_"<<name;
            for (const auto name:viewer::zombie_profile::travelerSceneNames) rows<<",traveler_scene_"<<name;
            rows<<'\n';
            viewer::zombie_profile::Profile aggregate;
            for (std::size_t frame=0;frame<warmupSteps+measuredSteps;++frame) {
                clock.advance(dt);
                viewer::zombie_profile::Profile sample;
                const bool measured=frame>=warmupSteps;
                const bool profiled=measured && detailed && (!profileBlockSteps || ((frame-warmupSteps)/profileBlockSteps)%2!=0);
                const auto begin=std::chrono::steady_clock::now();
                {
                    viewer::zombie_profile::Capture capture(profiled?&sample:nullptr);
                    scene.zombies.updateBehavior(dt,player,*scene.collision,clock.seconds());
                }
                const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                if (!measured) continue;
                rows<<frame<<','<<profiled<<','<<ms<<','<<sample.behaviorSeconds*1000<<','<<sample.scenarioSeconds*1000;
                for (const auto count:sample.behaviorStates) rows<<','<<count;
                rows<<','<<sample.chaseStarts;
                aggregate.behaviorSeconds+=sample.behaviorSeconds;
                aggregate.scenarioSeconds+=sample.scenarioSeconds;
                aggregate.chaseStarts+=sample.chaseStarts;
                for (std::size_t i=0;i<sample.behaviorStates.size();++i)
                    aggregate.behaviorStates[i]+=sample.behaviorStates[i];
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
                    <<','<<scene.zombies.crowdStats().budgetStops<<','<<scene.zombies.crowdStats().emptySectorSkips<<','<<sample.walkingRegionReuses<<','<<sample.simpleTravelerClears<<','<<sample.travelerPlans<<','<<sample.travelerFallbacks<<','<<sample.travelerInterpolations
                    <<','<<sample.travelerFullSeconds*1000<<','<<sample.travelerFullCoverageSeconds*1000
                    <<','<<navigation.steeringCalls<<','<<navigation.meshRejectedCandidates;
                for (std::size_t i=0;i<sample.travelerSolverSteps.size();++i) {
                    rows<<','<<sample.travelerSolverSteps[i];
                    aggregate.travelerSolverSteps[i]+=sample.travelerSolverSteps[i];
                }
                for (std::size_t i=0;i<sample.travelerFailures.size();++i) {
                    rows<<','<<sample.travelerFailures[i];
                    aggregate.travelerFailures[i]+=sample.travelerFailures[i];
                }
                for (std::size_t i=0;i<sample.travelerSceneRejects.size();++i) {
                    rows<<','<<sample.travelerSceneRejects[i];
                    aggregate.travelerSceneRejects[i]+=sample.travelerSceneRejects[i];
                }
                rows<<'\n';
                aggregate.travelerFullSeconds+=sample.travelerFullSeconds;
                aggregate.travelerFullCoverageSeconds+=sample.travelerFullCoverageSeconds;
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
            if (scene.zombies.navigation()->stats().requests!=travelers)
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
        quality<<"id,x,y,z,ground_error,blocking_contacts,navmesh_inside,scenario_active,scenario_finished,walk_seconds\n";
        for (std::size_t id=0;id<scene.zombies.instances.size();++id) {
            if (scene.zombies.instances[id].removed) continue;
            const auto feet=glm::vec3(scene.zombies.instances[id].restRoot[3]);
            const auto hit=scene.collision->walkingGroundAt(feet.x,feet.z,-1e10f,feet.y+.3f);
            const double error=hit?double(feet.y-hit->height):1e10;
            std::size_t blocking=0;
            for (const auto& c:scene.collision->contacts(feet,.3f,1.8f,.002f,.05f))
                if (c.depth>.0001f && !(c.terrainSupport && c.normal.y>=.70710678f) &&
                    !(c.normal.y>.7f && c.point.y<=feet.y+.0021f)) ++blocking;
            const bool finished=scene.zombies.scenarioFinished(id);
            const bool active=scene.zombies.scenarioDestination(id) && !finished;
            const bool inside=scene.zombies.navigation()->segmentClear(feet,feet);
            if (active && !inside) throw std::runtime_error("An active traveler escaped navmesh coverage");
            quality<<id<<','<<feet.x<<','<<feet.y<<','<<feet.z<<','<<error<<','<<blocking<<','
                   <<inside<<','<<active<<','<<finished<<','<<scene.zombies.instances[id].walkSeconds<<'\n';
        }
        std::cout<<"Finished "<<rounds<<" runs with no runtime path searches";
        if (rounds>1) std::cout<<"; identical final poses";
        std::cout<<'\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
