#include "renderer.hpp"
#include "frame_profile.hpp"
#include "fps_controller.hpp"
#include "undergrowth.hpp"
#include "file_fingerprint.hpp"
#include "gpu_timer.hpp"
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Clock=std::chrono::steady_clock;
constexpr int modes=2;
void selectMode(viewer::Renderer& renderer,int mode) {
    renderer.setSunShadowResolution(mode ? 1024 : 2048);
}
void finishWarmup() {
    const auto fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
    if(!fence)throw std::runtime_error("Cannot fence shadow warmup");
    const auto deadline=Clock::now()+std::chrono::seconds(20);
    while(true) {
        const auto result=glClientWaitSync(fence,0,0);
        if(result==GL_ALREADY_SIGNALED || result==GL_CONDITION_SATISFIED)break;
        if(result==GL_WAIT_FAILED || Clock::now()>deadline) {
            glDeleteSync(fence);throw std::runtime_error("Shadow warmup fence failed/timed out");
        }
        glfwWaitEventsTimeout(.0005);
    }
    glDeleteSync(fence);
}
double ms(Clock::time_point start,Clock::time_point end) {return std::chrono::duration<double,std::milli>(end-start).count();}
struct View {std::string name;viewer::Camera camera;float forestDistance=100;bool night=false;};
void drain(viewer::FrameProfile& profile,std::size_t limit=0) {
    glFlush();const auto deadline=Clock::now()+std::chrono::seconds(10);
    while(profile.pending()>limit) {
        profile.collect();
        if(Clock::now()>deadline)throw std::runtime_error("GPU timestamp drain timed out");
        if(profile.pending()>limit)glfwWaitEventsTimeout(.0005);
    }
}
} // namespace
int main(int argc,char** argv) {
    if(argc!=6) {std::cerr<<"Usage: "<<argv[0]<<" MAP TREES FORESTS CAMERA_FILE OUTPUT_DIRECTORY\n";return 1;}
    try {
        const auto scene=viewer::loadModel(argv[1]);auto trees=viewer::loadTreeLayer(argv[2],argv[1]);
        const viewer::CollisionWorld world(scene);
        const auto forest=viewer::loadForestLayer(argv[3],argv[1],scene,world,trees);
        const viewer::UndergrowthIndex undergrowth(trees);
        std::vector<View> views;std::ifstream input(argv[4]);
        int index;std::string id,label;glm::vec3 eye;float yaw,pitch;
        while(input>>index>>id>>label>>eye.x>>eye.y>>eye.z>>label>>yaw>>label>>pitch) {
            viewer::Camera camera;camera.setPosition(eye);camera.setOrientation(yaw,pitch);views.push_back({id,camera});
        }
        if(views.empty())throw std::runtime_error("No benchmark cameras");
        const viewer::OldForestVisibility oldForest(forest.patches);
        for(const auto& view:views) {
            const auto p=view.camera.position();const auto ground=world.groundAt(p.x,p.z,scene.boundsMin.y-100,scene.boundsMax.y+100,true);
            if(!ground)continue;
            viewer::Camera camera;camera.setPosition({p.x,ground->height+1.7f,p.z});camera.setOrientation(180,0);
            if(!oldForest.rangeAt(camera.position()))continue;
            views.push_back({"old-forest-100m",camera,100});views.push_back({"old-forest-unrestricted",camera,0});break;
        }
        // Add a reproducible street-level camera derived from the map, never a geographic constant.
        for(const auto& draw:scene.draws)if(draw.name.find("building_")==0&&draw.name.find("_walls")!=std::string::npos) {
            const auto& mesh=scene.primitives.at(draw.primitive);if(mesh.vertices.empty())continue;
            glm::vec3 lo(1e20f),hi(-1e20f);
            for(const auto& vertex:mesh.vertices) {
                const glm::vec3 p(draw.transform*glm::vec4(vertex.position,1));lo=glm::min(lo,p);hi=glm::max(hi,p);
            }
            const auto center=(lo+hi)*.5f;
            const auto ground=world.groundAt(center.x,hi.z+20,scene.boundsMin.y-100,scene.boundsMax.y+100,true);
            if(!ground)continue;
            if(world.insideBuilding({center.x,ground->height,hi.z+20},.3f,1.8f))continue;
            viewer::Camera camera;camera.setPosition({center.x,ground->height+1.7f,hi.z+20});camera.setOrientation(-90,0);
            views.push_back({"town",camera});break;
        }
        // Night views are controls: the flashlight keeps its own 1024 map and
        // the sunlight pass is inactive, so its resolution must not change images.
        for(std::size_t i=0;i<std::min(std::size_t{2},views.size());++i) {
            auto view=views[i];view.name+="-flashlight";view.night=true;
            view.camera.setOrientation(view.camera.yawDegrees(),-15);views.push_back(view);
        }
        const std::filesystem::path output(argv[5]);std::filesystem::create_directories(output);
        std::filesystem::copy_file(argv[4],output/"input-cameras.txt",std::filesystem::copy_options::overwrite_existing);
        if(!glfwInit())throw std::runtime_error("Cannot initialize GLFW");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        auto* window=glfwCreateWindow(1280,720,"Shadow resolution benchmark",nullptr,nullptr);if(!window)throw std::runtime_error("Cannot create GPU context");
        glfwMakeContextCurrent(window);glfwSwapInterval(0);viewer::initializeOpenGL();
        std::cout<<"GPU "<<glGetString(GL_RENDERER)<<" | GL "<<glGetString(GL_VERSION)<<'\n'<<std::flush;
        {
            viewer::GpuTimer sunShadowTimer;
            viewer::Renderer renderer(scene,"shaders",&trees,nullptr,&forest);renderer.resize(1280,720);
            renderer.setSunShadowTimer(&sunShadowTimer);
            std::ofstream settings(output/"metadata.txt");
            settings << "scenario=sun-shadow-resolution-v1\nGL=" << glGetString(GL_VERSION) << "\nGPU=" << glGetString(GL_RENDERER)
                << "\nresolution=1280x720\nrounds=6\nframes=64\nmode0=2048 square sun shadow map\nmode1=1024 square sun shadow map"
                << "\nvisible_filtering=authored\ncard_splitting=enabled\nflashlight_resolution=1024 unchanged"
                << "\nsun_shadow_timer=whole sunlight shadow pass; clear plus all caster categories\nwarmup_fence=outside timers"
                << "\ncpu_simulation=fixed player collision and undergrowth; no AI/audio/dynamic physics\n";
            settings << "map_sha256=" << viewer::fileSha256(argv[1]) << "\ntrees_sha256=" << viewer::fileSha256(argv[2])
                     << "\nforests_sha256=" << viewer::fileSha256(argv[3]) << '\n';
            std::ofstream counters(output/"geometry.csv");
            counters << "view,mode,round,trees,tree_calls,tree_triangles,town_calls\n";
            std::ofstream metadata(output/"views.csv");metadata<<"view,name,x,y,z,yaw,pitch,forest_distance,trees,town_draws,night\n";
            std::ofstream shadows(output/"sun-shadows.csv");
            shadows<<"view,mode,round,gpu_ms,cpu_ms,samples\n";
            for(std::size_t v=0;v<views.size();++v) {
                const auto& view=views[v];renderer.setOldForestDistance(view.forestDistance);
                renderer.setNight(view.night);renderer.setFlashlight(view.night);renderer.setFlashlightShadows(view.night);
                viewer::FPSController player;if(!player.spawn(view.camera.position(),world))throw std::runtime_error("Cannot spawn benchmark player");
                const auto initial=player.movementState();viewer::UndergrowthEmitter rustle;
                const auto update=[&](int frame) {
                    const viewer::UndergrowthMotion before{player.feet(),player.bodyHeight(),player.config().radius,player.grounded(),false};
                    viewer::FPSInput movement;movement.forward=((frame/30)%2)?-1.f:1.f;movement.yawDegrees=view.camera.yawDegrees();
                    player.update(1.f/60,movement,world);
                    rustle.update(1.f/60,before,{player.feet(),player.bodyHeight(),player.config().radius,player.grounded(),false},undergrowth);
                };
                for(int mode=0;mode<modes;++mode) {
                    selectMode(renderer,mode);
                    for(int i=0;i<48;++i){renderer.render(view.camera,0);glfwPollEvents();}
                    renderer.writeScreenshot(output/("view-"+std::to_string(v)+"-mode-"+std::to_string(mode)+".ppm"));
                }
                for(int round=0;round<6;++round)for(int variant=0;variant<modes;++variant) {
                    const int mode=(round+variant)%modes;selectMode(renderer,mode);
                    player.restoreMovement(initial);rustle.reset();
                    for(int i=0;i<12;++i){update(i);renderer.render(view.camera,0);}
                    finishWarmup();sunShadowTimer.takeAverageMilliseconds();
                    double shadowCpu=0;
                    viewer::FrameProfile profile(output/("view-"+std::to_string(v)+"-mode-"+std::to_string(mode)+"-round-"+std::to_string(round)+".csv"));
                    // Bound in-flight work. Poll only outside measured CPU/GPU sections;
                    // this is an isolated render-cost benchmark, not an FPS measurement.
                    for(int frame=0;frame<64;++frame) {
                        drain(profile,3);const auto start=Clock::now();update(frame+12);const auto updated=Clock::now();
                        renderer.updateVegetationBlasts({});const auto prepared=Clock::now();
                        profile.beginGpu();const auto renderStart=Clock::now();renderer.render(view.camera,0);const auto rendered=Clock::now();profile.endGpu();
                        shadowCpu+=renderer.sunShadowCpuMilliseconds();
                        profile.record({static_cast<std::uint64_t>(frame),ms(renderStart,rendered),ms(start,updated),ms(updated,prepared),0,0});
                    }
                    drain(profile);viewer::Renderer::checkErrors("Shadow resolution benchmark");
                    {
                        sunShadowTimer.collect();const auto samples=sunShadowTimer.samples();
                        const auto gpu=sunShadowTimer.takeAverageMilliseconds();
                        if((!view.night && (samples!=64 || !gpu)) || (view.night && samples!=0))
                            throw std::runtime_error("Missing or unexpected sunlight shadow timing samples");
                        shadows<<v<<','<<mode<<','<<round<<','<<gpu.value_or(0)<<','<<shadowCpu/64<<','<<samples<<'\n'<<std::flush;
                    }
                    {
                        const auto stats=renderer.treeStats();
                        counters<<v<<','<<mode<<','<<round<<','<<stats.visible<<','<<stats.drawCalls<<','<<stats.triangles<<','<<renderer.sceneStats().drawCalls<<'\n'<<std::flush;
                    }
                    std::cout<<"view="<<v<<" name="<<view.name<<" mode="<<mode<<" round="<<round<<'\n'<<std::flush;
                }
                const auto p=view.camera.position();metadata<<v<<','<<view.name<<','<<p.x<<','<<p.y<<','<<p.z<<','
                    <<view.camera.yawDegrees()<<','<<view.camera.pitchDegrees()<<','<<view.forestDistance<<','
                    <<renderer.treeStats().visible<<','<<renderer.sceneStats().drawCalls<<','<<view.night<<'\n'<<std::flush;
            }
        }
        glfwDestroyWindow(window);glfwTerminate();
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
