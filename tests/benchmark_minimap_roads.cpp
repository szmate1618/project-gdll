#include "gui/hud.hpp"
#include "inventory/inventory_config.hpp"
#include "minimap.hpp"
#include "minimap_roads.hpp"
#include "model.hpp"
#include "opengl.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int frames = 512, warmup = 512, rounds = 4;
viewer::HudState stateAt(int frame) {
    const float phase = float(frame % frames) / frames * 6.283185307f;
    viewer::HudState state;
    state.position = {100 * std::sin(phase), 0, 100 * std::cos(phase)};
    state.equipment = "Pistol"; state.slot = 0; state.loaded = 12; state.reserve = 120;
    return state;
}
double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}
double collect(GLuint query) {
    GLint ready = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!ready) {
        glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (Clock::now() > deadline) throw std::runtime_error("HUD GPU query timeout");
        if (!ready) glfwWaitEventsTimeout(.0005);
    }
    GLuint64 time = 0; glGetQueryObjectui64v(query, GL_QUERY_RESULT, &time);
    return double(time) / 1e6;
}
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) { std::cerr << "Usage: benchmark_minimap_roads MAP OUTPUT_DIRECTORY [HUD_DIRECTORY]\n"; return 1; }
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(1280, 720, "Minimap road benchmark", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window); glfwSwapInterval(0);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        const auto model = viewer::loadModel(argv[1]);
        const auto source = viewer::findMinimapSource(model);
        if (!source) throw std::runtime_error("Benchmark map has no usable satellite terrain");
        std::filesystem::create_directories(argv[2]);
        const std::filesystem::path output(argv[2]);
        const std::filesystem::path hudDirectory = argc == 4 ? argv[3] : VIEWER_HUD_DIR;
        std::ofstream metadata(output / "metadata.txt"), csv(output / "frames.csv"), report(output / "report.txt");
        if (!metadata || !csv || !report) throw std::runtime_error("Cannot write benchmark output");
        metadata << "GPU: " << glGetString(GL_RENDERER) << "\nOpenGL: " << glGetString(GL_VERSION)
                 << "\nMap: " << argv[1] << "\nFramebuffer: 1280x720 RGBA8; minimap: 108x108, 300 m\n"
                 << "HUD only: no 3D scene, AI, physics, audio, presentation or VSync.\n"
                 << "Fixed 1/60 s timeline; repeated 512-frame circle of radius 100 m around map origin.\n"
                 << "Alternating baseline/roads order; 4 rounds each, 512 warmup and 512 measured frames.\n"
                 << "CPU: Hud::draw; GPU: HUD pass only; query collection/waits outside CPU timing.\n"
                 << "HUD directory: " << hudDirectory << '\n';
        {
            const auto start = Clock::now();
            const auto geometry = viewer::prepareMinimapRoads(model);
            metadata << "Road preparation CPU ms: " << milliseconds(start) << "\nRoad triangles: " << geometry.vertices.size() / 3
                     << "\nRoad cells: " << geometry.batches.size() << "\nRoad VBO bytes: "
                     << geometry.vertices.size() * sizeof(glm::vec2) << '\n';
        }
        const auto& image = model.images.at(model.textures.at(source->texture).image);
        GLuint satellite, color, framebuffer;
        glGenTextures(1, &satellite); glBindTexture(GL_TEXTURE_2D, satellite);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1280, 720, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Incomplete benchmark framebuffer");
        csv << "round,roads,frame,cpu_ms,gpu_ms\n";
        std::vector<double> cpu[2], gpu[2];
        for (int round = 0; round < rounds; ++round) for (int step = 0; step < 2; ++step) {
            const int roads = (round + step) % 2;
            auto hudInventory = viewer::loadPlayerInventory(std::filesystem::path(hudDirectory).parent_path());
            viewer::Hud hud(hudDirectory, hudInventory);
            hud.setMinimap(*source, satellite);
            if (roads) hud.setMinimapRoads(model);
            for (int frame = 0; frame < warmup; ++frame) { glClear(GL_COLOR_BUFFER_BIT); hud.draw(stateAt(frame), 1280, 720); }
            glFinish(); // Warmup only, outside all measured intervals.
            std::vector<GLuint> queries(frames);
            std::vector<double> measuredCpu(frames), measuredGpu(frames);
            glGenQueries(frames, queries.data());
            int collected = 0;
            for (int frame = 0; frame < frames; ++frame) {
                const auto state = stateAt(frame);
                glClear(GL_COLOR_BUFFER_BIT);
                glBeginQuery(GL_TIME_ELAPSED, queries[frame]);
                const auto start = Clock::now();
                hud.draw(state, 1280, 720);
                measuredCpu[frame] = milliseconds(start);
                glEndQuery(GL_TIME_ELAPSED);
                glFlush();
                if (frame - collected >= 63) { measuredGpu[collected] = collect(queries[collected]); ++collected; }
            }
            for (; collected < frames; ++collected) measuredGpu[collected] = collect(queries[collected]);
            glDeleteQueries(frames, queries.data());
            for (int frame = 0; frame < frames; ++frame)
                csv << round << ',' << roads << ',' << frame << ',' << measuredCpu[frame] << ',' << measuredGpu[frame] << '\n';
            cpu[roads].insert(cpu[roads].end(), measuredCpu.begin(), measuredCpu.end());
            gpu[roads].insert(gpu[roads].end(), measuredGpu.begin(), measuredGpu.end());
            std::cout << "Round " << round << (roads ? " roads" : " baseline") << " CPU " << median(measuredCpu)
                      << " ms, GPU " << median(measuredGpu) << " ms\n" << std::flush;
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Benchmark OpenGL error");
        }
        report << std::fixed << std::setprecision(6)
               << "HUD-only medians (milliseconds), 2048 samples per mode\n"
               << "Baseline CPU: " << median(cpu[0]) << "; roads CPU: " << median(cpu[1])
               << "; delta: " << median(cpu[1]) - median(cpu[0]) << '\n'
               << "Baseline GPU: " << median(gpu[0]) << "; roads GPU: " << median(gpu[1])
               << "; delta: " << median(gpu[1]) - median(gpu[0]) << '\n';
        glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &color); glDeleteTextures(1, &satellite);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
