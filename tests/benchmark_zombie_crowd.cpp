#include "renderer.hpp"
#include "frame_profile.hpp"
#include "file_fingerprint.hpp"
#include "simulation_clock.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t population = 100000;
constexpr int width = 1280, height = 720, rounds = 6, frames = 64;
constexpr float halfField = 600, radius = 500, dt = 1.f / 60;
#ifdef VIEWER_BENCHMARK_TREE_HIZ
constexpr int variants = 2;
#else
constexpr int variants = 1;
#endif

void selectVariant(viewer::Renderer& renderer, bool enabled) {
#ifdef VIEWER_BENCHMARK_TREE_HIZ
    renderer.setTreeHiZMode(enabled ? viewer::TreeHiZMode::Cull : viewer::TreeHiZMode::Off);
#else
    (void)renderer;
    if (enabled) throw std::logic_error("This renderer has no Hi-Z prototype");
#endif
}

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end-start).count();
}

viewer::Model openField() {
    viewer::Model scene;
    viewer::Material ground; ground.baseColor = {.18f,.23f,.12f,1};
    scene.materials.push_back(ground);
    viewer::Primitive mesh; mesh.material = 0;
    for (const auto point : {glm::vec3(-halfField,0,-halfField), glm::vec3(-halfField,0,halfField),
                             glm::vec3(halfField,0,halfField), glm::vec3(halfField,0,-halfField)}) {
        viewer::Vertex vertex; vertex.position = point; vertex.normal = {0,1,0};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0,1,2,0,2,3};
    scene.primitives.push_back(std::move(mesh));
    scene.draws.push_back({0,glm::mat4(1),"terrain_benchmark"});
    scene.boundsMin = {-halfField,0,-halfField}; scene.boundsMax = {halfField,0,halfField};
    return scene;
}

struct Window {
    GLFWwindow* handle = nullptr;
    Window() {
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW; a hardware GPU/display is required");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        handle = glfwCreateWindow(width,height,"100,000 zombie rendering benchmark",nullptr,nullptr);
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

void checkPopulation(const viewer::Renderer& renderer, std::size_t wounded) {
    if (renderer.zombieCount() != population || renderer.zombieVisibleCount() != population ||
        renderer.zombieRenderStats().distanceCulled != 0 ||
        renderer.zombieRenderStats().cameraLevels != std::array<std::size_t,4>{wounded,0,0,population-wounded} ||
        (!wounded && renderer.zombieDrawCalls() != 10))
        throw std::runtime_error("Standard benchmark must submit all 100,000 zombies; no distance/frustum shortcut is allowed");
}

void metadata(const std::filesystem::path& output, const std::filesystem::path& assets,
              const viewer::ZombieLayer& zombies, const viewer::Camera& camera, const viewer::Renderer& renderer,
              std::size_t wounded) {
    std::ofstream file(output / "metadata.txt");
    if (!file) throw std::runtime_error("Cannot write benchmark metadata");
    file << "scenario=" << (wounded ? "zombies-open-100k-wounded-v1" : "zombies-open-100k-v1")
         << "\nresolution=" << width << 'x' << height
         << "\nsun_shadow_resolution=" << renderer.sunShadowResolution()
         << "\nfield_m=1200x1200\nplacement_radius_m=" << radius << "\npopulation=" << population
         << "\nassets=" << zombies.assets.size() << "\nwounded_actors=" << wounded
         << "\nwound_mask=" << (wounded ? "all-zero vertex mask; full wound rendering without removed geometry or physics" : "none")
         << "\nrounds=" << rounds << "\nframes_per_round=" << frames
         << "\ncomparison_variants=" << variants
         << "\nrendered=" << renderer.zombieVisibleCount() << "\ndraw_calls=" << renderer.zombieDrawCalls()
         << "\ntriangles=" << renderer.zombieRenderStats().cameraTriangles << "\ncamera_lod_counts=";
    for (std::size_t i=0;i<4;++i) file << (i ? " " : "") << renderer.zombieRenderStats().cameraLevels[i];
    file << "\nshadow_lod_counts=";
    for (std::size_t i=0;i<4;++i) file << (i ? " " : "") << renderer.zombieRenderStats().shadowLevels[i];
    const auto eye = camera.position();
    file << "\neye=" << eye.x << ' ' << eye.y << ' ' << eye.z << "\nyaw=" << camera.yawDegrees()
         << "\npitch=" << camera.pitchDegrees() << "\nanimation=deterministic 60 Hz timeline with stable individual phases"
         << "\ndistance_culling=disabled\nmesh_lod=normal distance policy, three prepared levels required"
         << "\nshadows=normal enabled policy; camera/light visibility independent\nfog=disabled"
         << "\ncpu_simulation=idle zombie behavior update; observer outside activation range"
         << "\nno_audio_player_or_dynamic_physics=true\nvegetation=none"
#ifdef VIEWER_BENCHMARK_TREE_HIZ
         << "\nhiz_note=vegetation-only prototype skips pyramid construction without trees"
#else
         << "\nhiz_note=no Hi-Z prototype in this renderer"
#endif
         << "\ngpu=" << glGetString(GL_RENDERER) << "\nOpenGL=" << glGetString(GL_VERSION) << '\n';
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(assets))
        if (entry.is_regular_file() && (entry.path().extension()==".glb" || entry.path().extension()==".gltf" ||
                                       entry.path().filename()=="lods.json")) files.push_back(entry.path());
    std::sort(files.begin(),files.end());
    for (const auto& path : files) file << "sha256 " << path.filename().string() << ' ' << viewer::fileSha256(path) << '\n';
}
} // namespace

int main(int argc, char** argv) {
    constexpr auto usage = "Usage: benchmark_zombie_crowd ZOMBIE_MODEL_DIRECTORY OUTPUT_DIRECTORY"
                           " [--wounded N] [--shadow-resolution N]\n";
    if (argc<3) {
        std::cerr << usage;
        return 1;
    }
    try {
        std::size_t wounded = 0;
        std::optional<int> shadowResolution;
        for (int argument=3; argument<argc; argument+=2) {
            if (argument+1 >= argc) throw std::runtime_error(usage);
            const std::string_view option(argv[argument]), input(argv[argument+1]);
            std::size_t value = 0;
            const auto parsed = std::from_chars(input.data(),input.data()+input.size(),value);
            if (parsed.ec != std::errc{} || parsed.ptr != input.data()+input.size())
                throw std::runtime_error("Benchmark options require nonnegative integer values");
            if (option == "--wounded" && value <= population) wounded = value;
            else if (option == "--shadow-resolution" && value > 0 && value <= std::size_t(std::numeric_limits<int>::max()))
                shadowResolution = static_cast<int>(value);
            else throw std::runtime_error(usage);
        }
        const std::filesystem::path assets(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);
        const auto scene = openField(); const viewer::CollisionWorld world(scene);
        auto zombies = viewer::loadZombieLayer(assets,world,scene,{0,0},population,radius);
        if (zombies.assets.size()!=10 || zombies.lodAssets.size()!=10 ||
            std::any_of(zombies.lodAssets.begin(),zombies.lodAssets.end(),[](const auto& levels){return levels.size()!=3;}))
            throw std::runtime_error("Standard case requires all ten character variants and three prepared LODs; see assets/zombies/README.md");
        // A nonempty zero mask exercises wound rendering without shooting,
        // physics or removed surfaces. Wounded actors retain the normal full
        // mesh policy; the remaining overview population stays at LOD3.
        std::vector<std::vector<std::uint8_t>> masks(zombies.assets.size());
        if (wounded) {
            for (std::size_t asset=0; asset<zombies.assets.size(); ++asset) {
                std::size_t vertices = 0;
                for (const auto& primitive : zombies.assets[asset].model.primitives) vertices += primitive.vertices.size();
                masks[asset].resize((vertices+7)/8);
            }
            for (std::size_t id=0; id<wounded; ++id) zombies.setWoundMask(id,masks[zombies.instances[id].asset]);
        }
        viewer::Camera camera; camera.frame(zombies.boundsMin,zombies.boundsMax,float(width)/height);
        Window window;
        std::cout << "GPU " << glGetString(GL_RENDERER) << "\nScenario "
                  << (wounded ? "zombies-open-100k-wounded-v1" : "zombies-open-100k-v1") << '\n' << std::flush;
        viewer::Renderer renderer(scene,"shaders",nullptr,&zombies);
        renderer.resize(width,height); renderer.setZombieDrawDistance(0);
        if (shadowResolution) renderer.setSunShadowResolution(*shadowResolution);
        renderer.setHaze(false); renderer.setGroundFog(false);
        renderer.render(camera,.5); checkPopulation(renderer,wounded);
        metadata(output,assets,zombies,camera,renderer,wounded);
        for (int variant=0;variant<variants;++variant) {
            const bool enabled = variant!=0;
            selectVariant(renderer,enabled);
            renderer.render(camera,.5); checkPopulation(renderer,wounded);
            renderer.writeScreenshot(output / (enabled ? "tree-hiz.ppm" : "baseline.ppm"));
        }
        const viewer::ZombiePlayer observer{{0,0,halfField+100}};
        auto& clock = viewer::SimulationClock::instance();
        const auto update = [&] {
            clock.advance(dt);
            zombies.updateBehavior(dt,observer,world,clock.seconds());
            if (!zombies.behaviors.entries().empty()) throw std::runtime_error("Idle overview unexpectedly activated zombie AI");
        };
        for (int round=0;round<rounds;++round) for (int variant=0;variant<variants;++variant) {
            const bool enabled = variants>1 && (round+variant)%2 != 0;
            const std::string name = enabled ? "tree-hiz" : "baseline";
            selectVariant(renderer,enabled);
            clock.reset();
            for (int i=0;i<32;++i) { update(); renderer.updateZombies(zombies); renderer.render(camera,clock.seconds()); }
            viewer::FrameProfile profile(output/(name+"-round-"+std::to_string(round)+".csv"));
            for (int frame=0;frame<frames;++frame) {
                drain(profile,3);
                const auto start=Clock::now(); update(); const auto updated=Clock::now();
                renderer.updateZombies(zombies); const auto prepared=Clock::now();
                profile.beginGpu(); const auto renderStart=Clock::now(); renderer.render(camera,clock.seconds());
                const auto rendered=Clock::now(); profile.endGpu();
                profile.record({static_cast<std::uint64_t>(frame),milliseconds(renderStart,rendered),
                    milliseconds(start,updated),milliseconds(updated,prepared),0,0});
                checkPopulation(renderer,wounded);
            }
            drain(profile); viewer::Renderer::checkErrors("100,000 zombie benchmark");
            std::cout << "round=" << round << " variant=" << name << " rendered=" << renderer.zombieVisibleCount()
                      << " draws=" << renderer.zombieDrawCalls() << " triangles=" << renderer.zombieRenderStats().cameraTriangles
                      << '\n' << std::flush;
        }
        std::cout << "Results: " << output << "; summarize with tools/summarize_render_benchmark.py\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
