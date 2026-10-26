#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t population = 100000;
constexpr int width = 1280, height = 720, rounds = 8, warmup = 16, frames = 128;

viewer::AnimatedModel markerAsset() {
    viewer::AnimatedModel asset;
    asset.frameCount = 2;
    asset.duration = 1;
    asset.walkClip = viewer::BakedAnimationClip{"walk", 1.5f, 3, 2, {}};
    asset.runClip = viewer::BakedAnimationClip{"run", 2, 2, 5, {}};
    viewer::Material material;
    material.unlit = true;
    material.doubleSided = true;
    material.baseColor = {.7f, .15f, .1f, 1};
    asset.model.materials.push_back(material);
    viewer::Primitive primitive;
    primitive.material = 0;
    for (const glm::vec2 corner : {glm::vec2(-1, 0), glm::vec2(1, 0),
                                  glm::vec2(1, 2), glm::vec2(-1, 2)}) {
        viewer::Vertex vertex;
        vertex.position = {corner.x * .25f, corner.y, 0};
        vertex.normal = {0, 0, 1};
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    asset.model.primitives.push_back(primitive);
    asset.model.draws.push_back({0, glm::mat4(1), "animated-marker"});
    viewer::AnimatedPrimitive animation;
    for (const float offset : {.75f, 1.1f, .2f, .6f, 1.f, -.4f, .3f})
        for (const auto& vertex : primitive.vertices)
            animation.frames.push_back({glm::vec4(vertex.position + glm::vec3(0, offset, 0), 1),
                                        glm::vec4(vertex.normal, 0)});
    asset.animation.push_back(std::move(animation));
    return asset;
}

void advance(viewer::ZombieLayer& layer, std::uint64_t step) {
    const double seconds = double(step) / 60;
    for (std::size_t id = 0; id < population; ++id) {
        layer.instances[id].walkSeconds = seconds + double(id % 31) * .003;
        layer.markRenderChanged(id, false);
    }
}

void validate(const viewer::Renderer& renderer, const viewer::ZombieLayer& layer,
              bool visible, std::ofstream& checks, int round) {
    if (renderer.zombieVisibleCount() != (visible ? population : 0))
        throw std::runtime_error("Preparation fixture has unexpected camera visibility");
    double checksum = 0;
    for (std::size_t id = 0; id < population; ++id) {
        const auto& instance = layer.instances[id];
        if (visible) {
            const bool running = id % 2 != 0;
            if (instance.displayedAnimationClip != (running ? 2 : 1) ||
                !std::isfinite(instance.displayedAnimationFrame) || instance.displayedAnimationFrame < 0 ||
                instance.displayedAnimationFrame >= (running ? 2 : 3))
                throw std::runtime_error("Invalid published locomotion pose");
            checksum += instance.displayedAnimationFrame * double(id + 1);
        } else if (instance.displayedAnimationFrame >= 0) {
            throw std::runtime_error("Hidden actors must not retain a published camera pose");
        }
    }
    checks << (visible ? "visible" : "hidden") << ',' << round << ','
           << std::setprecision(17) << checksum << '\n';
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: benchmark_zombie_render_preparation SHADER_DIRECTORY OUTPUT_DIRECTORY\n";
        return 1;
    }
    try {
        const std::filesystem::path shaders(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        auto* window = glfwCreateWindow(width, height, "Zombie render preparation benchmark", nullptr, nullptr);
        if (!window) throw std::runtime_error("Cannot create an OpenGL 3.3 context");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
        viewer::initializeOpenGL();
        {
            viewer::ZombieLayer layer;
            layer.assets.push_back(markerAsset());
            layer.instances.resize(population);
            for (std::size_t id = 0; id < population; ++id) {
                auto& instance = layer.instances[id];
                instance.transform = glm::translate(glm::mat4(1),
                    {float(id % 400), 0, float(id / 400)});
                instance.walking = true;
                instance.running = id % 2 != 0;
                instance.phase = float(id % 997) / 997;
            }
            viewer::Camera overview;
            overview.frame({-1, -1, -1}, {400, 4, 250}, float(width) / height);
            auto hidden = overview;
            hidden.setOrientation(overview.yawDegrees() + 180, -overview.pitchDegrees());
            const viewer::Model scene;
            viewer::Renderer renderer(scene, shaders, nullptr, &layer);
            renderer.resize(width, height);
            renderer.setZombieDistanceDetail(false);
            renderer.setZombieDrawDistance(0);
            renderer.setShadows(false);
            renderer.setHaze(false);
            renderer.setGroundFog(false);
            std::ofstream timings(output / "preparation.csv"), checks(output / "poses.csv");
            timings << "case,round,frame,cpu_preparation_ms,instances,bounds\n";
            checks << "case,round,displayed_pose_checksum\n";
            std::ofstream metadata(output / "metadata.txt");
            metadata << "scenario=zombie-render-preparation-v1\npopulation=" << population
                     << "\nresolution=" << width << 'x' << height << "\nrounds=" << rounds
                     << "\nframes_per_round=" << frames << "\nwarmup_frames=" << warmup
                     << "\nassets=synthetic four-vertex marker; idle, walk and run clips"
                     << "\nupdates=all actors, animation-only; unchanged spatial bounds"
                     << "\ntimestep=fixed 1/60 s per preparation update"
                     << "\nmeasured=Renderer::updateZombies only; caller updates and rendering excluded"
                     << "\nGPU=" << glGetString(GL_RENDERER) << "\nOpenGL=" << glGetString(GL_VERSION) << '\n';
            for (int round = 0; round < rounds; ++round) {
                for (int order = 0; order < 2; ++order) {
                    const bool visible = (round + order) % 2 == 0;
                    const auto& camera = visible ? overview : hidden;
                    renderer.render(camera, 0);
                    // Isolate this CPU stage from queued rendering. This diagnostic
                    // completion wait is outside every measurement; the timed loop
                    // submits no GL commands for these unwounded living instances.
                    glFinish();
                    for (int frame = 0; frame < warmup + frames; ++frame) {
                        advance(layer, std::uint64_t(frame + 1));
                        const auto start = Clock::now();
                        renderer.updateZombies(layer);
                        const auto end = Clock::now();
                        const auto& stats = renderer.zombieUpdateStats();
                        if (stats.instances != population || stats.bounds != 0 || stats.full)
                            throw std::runtime_error("Preparation must update exactly the changed animation-only actors");
                        if (frame >= warmup)
                            timings << (visible ? "visible" : "hidden") << ',' << round << ',' << frame - warmup << ','
                                    << std::setprecision(9) << std::chrono::duration<double, std::milli>(end - start).count()
                                    << ',' << stats.instances << ',' << stats.bounds << '\n';
                    }
                    renderer.render(camera, 10.125);
                    validate(renderer, layer, visible, checks, round);
                    if (round == rounds - 1 && visible)
                        renderer.writeScreenshot(output / "visible.ppm");
                    glFinish();
                    viewer::Renderer::checkErrors("zombie preparation benchmark");
                    std::cout << "round=" << round << " case=" << (visible ? "visible" : "hidden") << '\n' << std::flush;
                }
            }
        }
        glfwDestroyWindow(window);
        glfwTerminate();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        glfwTerminate();
        return 1;
    }
}
