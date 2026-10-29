#include "renderer.hpp"
#include "frame_profile.hpp"
#include "frame_stage_profile.hpp"
#include "file_fingerprint.hpp"
#include "physics_world.hpp"
#include "vehicle_layer.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int width = 1280, height = 720, warmup = 48;

struct View {
    std::string name;
    viewer::Camera camera;
    float forestDistance = 100;
    bool night = false, flashlight = true, flashlightShadows = false, driving = false;
};

struct Window {
    GLFWwindow* handle = nullptr;
    Window() {
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW; hardware GPU/display access is required");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        handle = glfwCreateWindow(width, height, "Renderer optimization benchmark", nullptr, nullptr);
        if (!handle) { glfwTerminate(); throw std::runtime_error("Cannot create an OpenGL 3.3 context"); }
        glfwMakeContextCurrent(handle); glfwSwapInterval(0); viewer::initializeOpenGL();
    }
    ~Window() { glfwDestroyWindow(handle); glfwTerminate(); }
};

void drain(viewer::FrameProfile& profile, std::size_t limit = 0) {
    glFlush();
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (profile.pending() > limit) {
        profile.collect();
        if (Clock::now() > deadline) throw std::runtime_error("GPU timestamp drain timed out");
        if (profile.pending() > limit) glfwWaitEventsTimeout(.0005);
    }
}

void finishWarmup() {
    const auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!fence) throw std::runtime_error("Cannot fence renderer warmup");
    glFlush();
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    for (;;) {
        const auto result = glClientWaitSync(fence, 0, 0);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) break;
        if (result == GL_WAIT_FAILED || Clock::now() > deadline) {
            glDeleteSync(fence);
            throw std::runtime_error("Renderer warmup fence failed/timed out");
        }
        glfwWaitEventsTimeout(.0005);
    }
    glDeleteSync(fence);
}

std::vector<View> mapViews(const viewer::Model& scene, const viewer::CollisionWorld& world,
                           const viewer::ForestLayer& forest, const std::filesystem::path& cameraFile) {
    std::vector<View> views;
    std::ifstream input(cameraFile);
    int index; std::string id, label; glm::vec3 eye; float yaw, pitch;
    while (input >> index >> id >> label >> eye.x >> eye.y >> eye.z >> label >> yaw >> label >> pitch) {
        viewer::Camera camera; camera.setPosition(eye); camera.setOrientation(yaw, pitch);
        views.push_back({id, camera});
    }
    if (views.empty()) throw std::runtime_error("No benchmark cameras");
    const auto firstForestView = views.front();
    const viewer::OldForestVisibility oldForest(forest.patches);
    for (const auto& view : views) {
        const auto p = view.camera.position();
        const auto ground = world.groundAt(p.x, p.z, scene.boundsMin.y - 100, scene.boundsMax.y + 100, true);
        if (!ground) continue;
        viewer::Camera camera; camera.setPosition({p.x, ground->height + 1.7f, p.z}); camera.setOrientation(180, 0);
        if (!oldForest.rangeAt(camera.position())) continue;
        views.push_back({"old-forest-100m", camera, 100});
        views.push_back({"old-forest-unrestricted", camera, 0});
        break;
    }
    // Derive the street view from the published map frame, retaining the
    // existing building-benchmark camera policy.
    for (const auto& draw : scene.draws) {
        if (draw.name.find("building_") != 0 || draw.name.find("_walls") == std::string::npos) continue;
        const auto& mesh = scene.primitives.at(draw.primitive);
        if (mesh.vertices.empty()) continue;
        glm::vec3 lo(1e20f), hi(-1e20f);
        for (const auto& vertex : mesh.vertices) {
            const glm::vec3 p(draw.transform * glm::vec4(vertex.position, 1));
            lo = glm::min(lo, p); hi = glm::max(hi, p);
        }
        const auto center = (lo + hi) * .5f;
        const auto ground = world.groundAt(center.x, hi.z + 20, scene.boundsMin.y - 100, scene.boundsMax.y + 100, true);
        if (!ground || world.insideBuilding({center.x, ground->height, hi.z + 20}, .3f, 1.8f)) continue;
        viewer::Camera camera; camera.setPosition({center.x, ground->height + 1.7f, hi.z + 20}); camera.setOrientation(-90, 0);
        views.push_back({"town", camera});
        break;
    }
    const auto town = std::find_if(views.begin(), views.end(), [](const auto& view) { return view.name == "town"; });
    if (town == views.end()) throw std::runtime_error("Cannot derive benchmark town camera");
    const auto townView = *town;
    for (const auto& base : {firstForestView, townView}) {
        const auto name = base.name == "town" ? "night-town-" : "night-forest-";
        views.push_back({std::string(name) + "off", base.camera, base.forestDistance, true, false});
        views.push_back({std::string(name) + "on", base.camera, base.forestDistance, true, true});
        views.push_back({std::string(name) + "driving-shadows", base.camera, base.forestDistance, true, true, true, true});
    }
    return views;
}

int positive(const char* text) {
    const auto result = std::stoi(text);
    if (result < 1) throw std::runtime_error("Rounds and frames must be positive");
    return result;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 7 || argc > 11) {
        std::cerr << "Usage: benchmark_render_wins MAP TREES FORESTS CAMERAS SHADERS OUTPUT [ROUNDS=1] [FRAMES=96] [CASE_FILTER=all] [--profile]\n";
        return 1;
    }
    try {
        const bool profileStages = argc == 11;
        if (profileStages && std::string(argv[10]) != "--profile")
            throw std::runtime_error("The final optional argument must be --profile");
        const int rounds = argc > 7 ? positive(argv[7]) : 1, frames = argc > 8 ? positive(argv[8]) : 96;
        const std::string filter = argc > 9 ? argv[9] : "all";
        const std::filesystem::path shaders(argv[5]), output(argv[6]);
        std::filesystem::create_directories(output);
        const auto scene = viewer::loadModel(argv[1]);
        auto trees = viewer::loadTreeLayer(argv[2], argv[1]);
        const viewer::CollisionWorld world(scene);
        const auto forest = viewer::loadForestLayer(argv[3], argv[1], scene, world, trees);
        auto views = mapViews(scene, world, forest, argv[4]);
        views.erase(std::remove_if(views.begin(), views.end(), [&](const auto& view) {
            return filter != "all" && view.name.find(filter) == std::string::npos;
        }), views.end());
        if (views.empty()) throw std::runtime_error("CASE_FILTER selected no views");
        std::filesystem::copy_file(argv[4], output / "input-cameras.txt", std::filesystem::copy_options::overwrite_existing);
        Window window;
        std::cout << "GPU " << glGetString(GL_RENDERER) << " | GL " << glGetString(GL_VERSION) << '\n' << std::flush;
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::VehicleLayer emptyCars(physics);
        viewer::Renderer renderer(scene, shaders, &trees, nullptr, &forest);
        renderer.resize(width, height);
        std::ofstream settings(output / "metadata.txt");
        settings << "scenario=render-wins-map-v1\nGPU=" << glGetString(GL_RENDERER) << "\nGL=" << glGetString(GL_VERSION)
                 << "\nresolution=" << width << 'x' << height << "\nrounds=" << rounds << "\nframes=" << frames
                 << "\nwarmup=" << warmup << "\nprofile_stages=" << profileStages
                 << "\nshader_directory=" << shaders.generic_string()
                 << "\nfiltering=nearest texels and nearest mip levels\ncard_splitting=enabled\nsun_resolution=" << renderer.sunShadowResolution()
                 << "\nworkload=static map/camera; no AI/audio/player updates/dynamic physics/presentation"
                 << "\nclock=deterministic 60 Hz animation input; no simulation steps"
                 << "\ntiming=asynchronous GPU timestamps; max four frames in flight; waits outside CPU timers"
                 << "\ndriving=prototype vehicle below camera; occupancy suppresses held flashlight; headlights retained"
                 << "\nmap_sha256=" << viewer::fileSha256(argv[1]) << "\ntrees_sha256=" << viewer::fileSha256(argv[2])
                 << "\nforests_sha256=" << viewer::fileSha256(argv[3]) << '\n';
        for (const auto* file : {"basic.vert", "basic.frag"})
            settings << "sha256 " << file << ' ' << viewer::fileSha256(shaders / file) << '\n';
        std::ofstream counters(output / "geometry.csv");
        counters << "view,round,trees,tree_calls,tree_triangles,town_visible,town_calls,forest_calls,vehicle_calls\n";
        std::ofstream stageOutput;
        if (profileStages) {
            stageOutput.open(output / "opaque-sort-stages.csv");
            if (!stageOutput) throw std::runtime_error("Cannot write opaque-sort stage timings");
            stageOutput << "view,round,frames,opaque_sort_ms,sort_calls\n" << std::setprecision(9);
        }
        std::ofstream metadata(output / "views.csv");
        metadata << "view,x,y,z,yaw,pitch,forest_distance,night,flashlight,flashlight_shadows,driving\n";
        for (const auto& view : views) {
            // Each occupied fixture remains alive until this view completes.
            viewer::VehicleLayer cars(physics);
            if (view.driving) cars.add(view.camera.position() - glm::vec3(0, .9f, 0), glm::radians(-view.camera.yawDegrees() - 90));
            renderer.updateVehicles(view.driving ? cars : emptyCars, view.driving ? std::optional<std::size_t>(0) : std::nullopt);
            renderer.setOldForestDistance(view.forestDistance); renderer.setNight(view.night);
            renderer.setFlashlight(view.flashlight); renderer.setFlashlightShadows(view.flashlightShadows);
            const auto p = view.camera.position();
            metadata << view.name << ',' << p.x << ',' << p.y << ',' << p.z << ',' << view.camera.yawDegrees() << ','
                     << view.camera.pitchDegrees() << ',' << view.forestDistance << ',' << view.night << ',' << view.flashlight
                     << ',' << view.flashlightShadows << ',' << view.driving << '\n' << std::flush;
            for (int round = 0; round < rounds; ++round) {
                for (int frame = 0; frame < warmup; ++frame) { renderer.render(view.camera, frame / 60.0); glfwPollEvents(); }
                finishWarmup(); // Untimed warmup must not spill into measured submission.
                viewer::FrameProfile profile(output / (view.name + "-round-" + std::to_string(round) + ".csv"));
                viewer::frame_stage_profile::Profile stages;
                for (int frame = 0; frame < frames; ++frame) {
                    drain(profile, 3);
                    profile.beginGpu();
                    const auto start = Clock::now();
                    {
                        viewer::frame_stage_profile::Capture capture(profileStages ? &stages : nullptr);
                        renderer.render(view.camera, (frame + warmup) / 60.0);
                    }
                    const auto end = Clock::now();
                    profile.endGpu();
                    profile.record({static_cast<std::uint64_t>(frame), std::chrono::duration<double, std::milli>(end - start).count(), 0, 0, 0, 0});
                }
                drain(profile);
                if (profileStages) {
                    const auto& sort = stages.stages[std::size_t(viewer::frame_stage_profile::Stage::townSort)];
                    if (sort.calls != static_cast<std::uint64_t>(frames))
                        throw std::runtime_error("Missing measured opaque-sort samples");
                    stageOutput << view.name << ',' << round << ',' << frames << ','
                                << sort.seconds * 1000 / frames << ',' << sort.calls << '\n' << std::flush;
                }
                const auto stats = renderer.treeStats();
                counters << view.name << ',' << round << ',' << stats.visible << ',' << stats.drawCalls << ',' << stats.triangles << ','
                         << renderer.sceneStats().visible << ',' << renderer.sceneStats().drawCalls << ',' << renderer.forestStats().drawCalls << ','
                         << renderer.vehicleStats().drawCalls << '\n' << std::flush;
                viewer::Renderer::checkErrors("Renderer optimization benchmark");
                std::cout << "view=" << view.name << " round=" << round << " trees=" << stats.visible
                          << " town_draws=" << renderer.sceneStats().drawCalls << '\n' << std::flush;
            }
            renderer.render(view.camera, 0);
            renderer.writeScreenshot(output / (view.name + ".ppm"));
            // Clear the borrowed pointer before destroying this view's car.
            renderer.updateVehicles(emptyCars);
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
