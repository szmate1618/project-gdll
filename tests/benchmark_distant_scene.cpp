#include "renderer.hpp"
#include "frame_profile.hpp"
#include "file_fingerprint.hpp"
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int width = 1280, height = 720, warmup = 32;
struct Window {
    GLFWwindow* handle = nullptr;
    Window() {
        if (!glfwInit()) throw std::runtime_error("Hardware GPU/display access required");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        handle = glfwCreateWindow(width,height,"Distant scenery benchmark",nullptr,nullptr);
        if (!handle) { glfwTerminate(); throw std::runtime_error("Cannot create OpenGL 3.3 context"); }
        glfwMakeContextCurrent(handle); glfwSwapInterval(0); viewer::initializeOpenGL();
    }
    ~Window() { glfwDestroyWindow(handle); glfwTerminate(); }
};
void drain(viewer::FrameProfile& profile, std::size_t limit = 0) {
    glFlush(); const auto deadline = Clock::now()+std::chrono::seconds(30);
    while (profile.pending() > limit) {
        profile.collect();
        if (Clock::now() > deadline) throw std::runtime_error("GPU timestamps timed out");
        if (profile.pending() > limit) glfwWaitEventsTimeout(.0005);
    }
}
struct View { std::string name; viewer::Camera camera; float forestDistance = 100; bool clear = false; };
std::vector<View> views(const char* path, const viewer::Model& scene, const viewer::CollisionWorld& world,
                        const viewer::ForestLayer& forest) {
    std::ifstream file(path); int index; std::string name, label; glm::vec3 eye; float yaw, pitch;
    std::vector<View> result;
    while (file >> index >> name >> label >> eye.x >> eye.y >> eye.z >> label >> yaw >> label >> pitch) {
        viewer::Camera camera; camera.setPosition(eye); camera.setOrientation(yaw,pitch);
        result.push_back({name,camera});
    }
    if (result.empty()) throw std::runtime_error("No benchmark cameras");
    const viewer::OldForestVisibility oldForest(forest.patches);
    for (const auto& base : result) {
        const auto p = base.camera.position();
        const auto ground = world.groundAt(p.x,p.z,scene.boundsMin.y-100,scene.boundsMax.y+100,true);
        if (!ground) continue;
        auto camera = base.camera; camera.setPosition({p.x,ground->height+1.7f,p.z}); camera.setOrientation(180,0);
        if (!oldForest.rangeAt(camera.position())) continue;
        result.push_back({"old-forest-100m",camera,100});
        result.push_back({"old-forest-unrestricted",camera,0}); break;
    }
    for (const auto& draw : scene.draws) {
        if (draw.name.find("building_") != 0 || draw.name.find("_walls") == std::string::npos) continue;
        const auto& primitive = scene.primitives[draw.primitive];
        glm::vec3 lo(1e20f),hi(-1e20f);
        for (const auto& vertex : primitive.vertices) {
            const auto p = glm::vec3(draw.transform*glm::vec4(vertex.position,1)); lo=glm::min(lo,p); hi=glm::max(hi,p);
        }
        const auto center = (lo+hi)*.5f;
        const auto ground = world.groundAt(center.x,hi.z+20,scene.boundsMin.y-100,scene.boundsMax.y+100,true);
        if (!ground || world.insideBuilding({center.x,ground->height,hi.z+20},.3f,1.8f)) continue;
        viewer::Camera camera; camera.setPosition({center.x,ground->height+1.7f,hi.z+20}); camera.setOrientation(-90,0);
        result.push_back({"town",camera}); break;
    }
    viewer::Camera overview;
    const auto center = (scene.boundsMin+scene.boundsMax)*.5f;
    overview.setPosition({center.x,scene.boundsMax.y+40,center.z}); overview.setOrientation(-135,-8);
    result.push_back({"distant-overview-clear",overview,0,true});
    return result;
}
viewer::Camera pose(const View& view, int motion, int frame) {
    auto camera = view.camera;
    const float seconds = frame/60.0f;
    if (motion) camera.setOrientation(camera.yawDegrees()+(motion==1 ? 40 : 180)*seconds,camera.pitchDegrees());
    if (motion==1) camera.setPosition(camera.position()+view.camera.direction()*(1.5f*seconds));
    return camera;
}
}
int main(int argc, char** argv) {
    if (argc < 7 || argc > 10) {
        std::cerr << "Usage: benchmark_distant_scene MAP TREES FORESTS CAMERAS SHADERS OUTPUT [ROUNDS=3] [FRAMES=128] [FILTER=all]\n";
        return 1;
    }
    try {
        const int rounds = argc>7 ? std::stoi(argv[7]) : 3, frames = argc>8 ? std::stoi(argv[8]) : 128;
        if (rounds<1 || frames<4) throw std::runtime_error("Need positive rounds and at least four frames");
        const std::string filter = argc>9 ? argv[9] : "all";
        const std::filesystem::path shaders(argv[5]), output(argv[6]); std::filesystem::create_directories(output);
        const auto scene = viewer::loadModel(argv[1]);
        auto trees = viewer::loadTreeLayer(argv[2],argv[1]);
        const viewer::CollisionWorld world(scene);
        const auto forest = viewer::loadForestLayer(argv[3],argv[1],scene,world,trees);
        const auto cameras = views(argv[4],scene,world,forest);
        Window window; viewer::Renderer renderer(scene,shaders,&trees,nullptr,&forest); renderer.resize(width,height);
        std::ofstream metadata(output/"metadata.txt");
        metadata << "scenario=distant-scene-v1\nGPU=" << glGetString(GL_RENDERER) << "\nGL=" << glGetString(GL_VERSION)
                 << "\nresolution=1280x720\ncutoff_m=1000\nrefresh_hz=15\nangular_tiles=256px\nrounds=" << rounds
                 << "\nframes=" << frames << "\nwarmup=" << warmup
                 << "\nclock=fixed 1/60 second per frame, including motion and refresh; no wall-clock simulation"
                 << "\nmotions=static; walk-turn 1.5m/s and 40deg/s; fast-turn 180deg/s"
                 << "\nworkload=map/vegetation and sunlight shadows; no AI, audio, physics steps, HUD or presentation"
                 << "\ntiming=asynchronous GPU timestamps; max four frames in flight; waits/readbacks outside timers"
                 << "\nfiltering=nearest vegetation and cached texels; normal LoD; shadows enabled\n";
        for (int i=1;i<=4;++i) metadata << "sha256 " << argv[i] << ' ' << viewer::fileSha256(argv[i]) << '\n';
        for (const auto* name : {"basic.vert","basic.frag","distant_cache.vert","distant_cache.frag"})
            metadata << "shader_sha256 " << name << ' ' << viewer::fileSha256(shaders/name) << '\n';
        std::filesystem::copy_file(argv[4],output/"input-cameras.txt",std::filesystem::copy_options::overwrite_existing);
        std::ofstream settings(output/"views.csv"), counters(output/"cache-frames.csv");
        settings << "view,x,y,z,yaw,pitch,forest_distance,clear_atmosphere\n";
        counters << "view,motion,round,mode,frame,refresh,visible_tiles,patches,cache_draws,near_town_draws,near_tree_draws,near_forest_draws\n";
        bool selected = false;
        for (const auto& view : cameras) {
            if (filter!="all" && view.name.find(filter)==std::string::npos) continue;
            selected = true;
            const auto eye = view.camera.position();
            settings << view.name << ',' << eye.x << ',' << eye.y << ',' << eye.z << ',' << view.camera.yawDegrees()
                     << ',' << view.camera.pitchDegrees() << ',' << view.forestDistance << ',' << view.clear << '\n';
            renderer.setOldForestDistance(view.forestDistance);
            renderer.setHaze(!view.clear); renderer.setGroundFog(!view.clear);
            for (int motion=0;motion<3;++motion) {
                const std::string motionName = motion==0 ? "static" : motion==1 ? "walk-turn" : "fast-turn";
                for (int round=0;round<rounds;++round) for (int variant=0;variant<2;++variant) {
                    const bool cached = (round+variant)%2 != 0;
                    const std::string mode = cached ? "cached" : "baseline";
                    const auto label = view.name+"-"+motionName+"-"+mode;
                    renderer.setDistantSceneCache(cached);
                    for (int frame=-warmup;frame<0;++frame)
                        renderer.render(pose(view,motion,frame),(frame+warmup)/60.0,{},(frame+warmup)/60.0);
                    // A completed fence keeps warmup outside measured submission.
                    const auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0); glFlush();
                    const auto deadline=Clock::now()+std::chrono::seconds(30);
                    while (glClientWaitSync(fence,0,0)==GL_TIMEOUT_EXPIRED) {
                        if (Clock::now()>deadline) throw std::runtime_error("Warmup fence timed out");
                        glfwWaitEventsTimeout(.0005);
                    }
                    glDeleteSync(fence);
                    {
                        viewer::FrameProfile profile(output/(label+"-round-"+std::to_string(round)+".csv"));
                        for (int frame=0;frame<frames;++frame) {
                            drain(profile,3);
                            profile.beginGpu(); const auto start=Clock::now();
                            renderer.render(pose(view,motion,frame),(frame+warmup)/60.0,{},(frame+warmup)/60.0);
                            const auto end=Clock::now(); profile.endGpu();
                            profile.record({static_cast<std::uint64_t>(frame),std::chrono::duration<double,std::milli>(end-start).count(),0,0,0,0});
                            const auto& s=renderer.distantCacheStats();
                            counters << view.name << ',' << motionName << ',' << round << ',' << mode << ',' << frame << ','
                                     << s.refreshed << ',' << s.tiles << ',' << s.patches << ',' << s.drawCalls << ','
                                     << renderer.sceneStats().drawCalls << ',' << renderer.treeStats().drawCalls << ','
                                     << renderer.forestStats().drawCalls << '\n';
                        }
                        drain(profile);
                    }
                    viewer::Renderer::checkErrors("distant scenery benchmark");
                    std::cout << label << " round=" << round << '\n' << std::flush;
                    if (round==0) renderer.writeScreenshot(output/(label+".ppm"));
                }
            }
        }
        if (!selected) throw std::runtime_error("Filter selected no views");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
