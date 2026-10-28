// CPU-only visibility maintenance/query benchmark. No assets, GL or AI work.
// Each case restarts four rounds of 30 warmup + 120 measured fixed-60-Hz
// frames. Initialization, pose arithmetic and result checks are untimed.
// Optional --profile adds coarse query phases; compare default runs for cost.
#include "zombie_visibility.hpp"
#include "frame_stage_profile.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using viewer::VisibilityBounds;
constexpr int rounds = 4, warmup = 30, frames = 120;
constexpr float timestep = 1.f / 60;

struct Case {
    const char* name;
    std::size_t population, moving;
    bool dense = false, separated = false;
};

VisibilityBounds body(glm::vec3 center) {
    constexpr glm::vec3 half(.4f, .9f, .4f);
    return {center-half, center+half};
}

std::vector<glm::vec3> positions(const Case& test) {
    std::vector<glm::vec3> result;
    result.reserve(test.population);
    for (std::size_t id = 0; id < test.population; ++id) {
        if (test.separated) {
            result.push_back({float(id%32)*1000000+.5f, .9f, float(id/32)*1000000+.5f});
        } else if (test.dense) {
            result.push_back({(float(id%317)-158)*3+.5f, .9f, (float(id/317)-158)*3+.5f});
        } else {
            // 33,120 occupied fine cells, with two actors in some cells.
            const auto cell = id%33120;
            const float duplicateOffset = id >= 33120 ? 3.f : 0.f;
            result.push_back({(float(cell%184)-92)*20+10+duplicateOffset, .9f,
                              (float(cell/184)-90)*20+10+duplicateOffset});
        }
    }
    return result;
}

bool overlaps(const VisibilityBounds& a, const VisibilityBounds& b) {
    for (int axis = 0; axis < 3; ++axis)
        if (a.maximum[axis] < b.minimum[axis] || a.minimum[axis] > b.maximum[axis]) return false;
    return true;
}

void verify(const std::vector<VisibilityBounds>& bounds, const viewer::Frustum& camera,
            const viewer::Frustum& sun, const viewer::Frustum& flashlight,
            const VisibilityBounds* region, const std::vector<viewer::ZombieVisibilityResult>& actual) {
    std::size_t cursor = 0;
    for (std::size_t id = 0; id < bounds.size(); ++id) {
        const auto& bound = bounds[id];
        const bool inCamera = (!region || overlaps(bound, *region)) && camera.intersects(bound.minimum, bound.maximum);
        const bool inSun = sun.intersects(bound.minimum, bound.maximum);
        const bool inFlashlight = flashlight.intersects(bound.minimum, bound.maximum);
        if (!inCamera && !inSun && !inFlashlight) continue;
        if (cursor >= actual.size() || actual[cursor].instance != id || actual[cursor].camera != inCamera ||
            actual[cursor].shadow != inSun || actual[cursor].flashlight != inFlashlight)
            throw std::runtime_error("Visibility IDs, order or pass masks differ from the full population oracle");
        ++cursor;
    }
    if (cursor != actual.size()) throw std::runtime_error("Visibility contains extra or duplicate results");
}

double milliseconds(Clock::time_point first, Clock::time_point last) {
    return std::chrono::duration<double, std::milli>(last-first).count();
}

void run(const Case& test, std::ostream& output, bool profile) {
    const auto initial = positions(test);
    const float cameraHalf = test.dense ? 600.f : 180.f;
    const viewer::Frustum camera(glm::ortho(-cameraHalf, cameraHalf, -10.f, 10.f, -cameraHalf, cameraHalf));
    // Sunlight includes casters outside the camera region. The flashlight is
    // deliberately independent too, exercising the union of all three passes.
    const viewer::Frustum sun(glm::ortho(220.f, 500.f, -10.f, 10.f, -180.f, 180.f));
    const viewer::Frustum flashlight(glm::ortho(-250.f, -200.f, -10.f, 10.f, -25.f, 25.f));
    const VisibilityBounds cameraRegion{{-180,-10,-180}, {180,10,180}};
    const auto* region = test.dense ? nullptr : &cameraRegion;
    for (int round = 0; round < rounds; ++round) {
        std::vector<VisibilityBounds> bounds;
        bounds.reserve(initial.size());
        for (const auto center : initial) bounds.push_back(body(center));
        viewer::ZombieVisibility index;
        index.reset(bounds.size());
        for (std::size_t id = 0; id < bounds.size(); ++id) index.update(id, bounds[id]);
        std::vector<viewer::ZombieVisibilityResult> visible;
        viewer::ZombieVisibilityStats stats;
        viewer::frame_stage_profile::Profile phases;
        std::uint64_t checksum = 1469598103934665603ull;
        double updateMilliseconds = 0, queryMilliseconds = 0;
        for (int frame = -warmup; frame < frames; ++frame) {
            // A common 3 m/s translation, evaluated on a fixed 60 Hz timeline.
            // Movers are distributed throughout the population, rather than
            // concentrated in one region. A 4 m initial offset ensures cell
            // crossings during measurement. Setup/pose arithmetic is untimed.
            const glm::vec3 movement(4+float(frame+warmup+1)*timestep*3, 0, 0);
            for (std::size_t moving = 0; moving < test.moving; ++moving) {
                const auto id = (moving*53+17)%bounds.size();
                bounds[id] = body(initial[id]+movement);
            }
            viewer::frame_stage_profile::Capture capture(profile && frame >= 0 ? &phases : nullptr);
            const auto start = Clock::now();
            for (std::size_t moving = 0; moving < test.moving; ++moving)
                index.update((moving*53+17)%bounds.size(), bounds[(moving*53+17)%bounds.size()]);
            const auto updated = Clock::now();
            stats = index.query(camera, &sun, true, visible, &flashlight, region);
            const auto queried = Clock::now();
            if (frame == 0 || frame == frames-1) verify(bounds, camera, sun, flashlight, region, visible);
            if (test.dense && visible.size() != test.population)
                throw std::runtime_error("Dense open case must retain the complete population");
            if (frame < 0) continue;
            updateMilliseconds += milliseconds(start, updated);
            queryMilliseconds += milliseconds(updated, queried);
            // Consume IDs and every pass mask outside measured sections. Each
            // round restarts the same timeline for comparisons across binaries.
            checksum = checksum*1099511628211ull+visible.size();
            for (const auto& item : visible) {
                const auto mask = unsigned(item.camera) | (unsigned(item.shadow)<<1) | (unsigned(item.flashlight)<<2);
                checksum = checksum*1099511628211ull+item.instance*8+mask;
            }
        }
        output << test.name << ',' << round << ',' << test.population << ',' << test.moving << ',' << stats.groups << ','
               << visible.size() << ',' << updateMilliseconds/frames << ',' << queryMilliseconds/frames << ','
               << (updateMilliseconds+queryMilliseconds)/frames << ',' << checksum;
        if (profile) {
            using viewer::frame_stage_profile::Stage;
            for (const auto stage : {Stage::zombieRefit, Stage::zombieTraversal, Stage::zombieSort}) {
                const auto& sample = phases.stages[std::size_t(stage)];
                if (sample.calls != frames) throw std::runtime_error("Missing measured visibility phase samples");
                output << ',' << sample.seconds*1000/frames;
            }
        }
        output << '\n';
    }
}
}

int main(int argc, char** argv) {
    try {
        const char* outputPath = nullptr;
        bool profile = false;
        for (int argument = 1; argument < argc; ++argument) {
            if (std::string_view(argv[argument]) == "--profile" && !profile) profile = true;
            else if (!outputPath && std::string_view(argv[argument]) != "--profile") outputPath = argv[argument];
            else throw std::runtime_error("Usage: benchmark_zombie_visibility [OUTPUT.csv] [--profile]");
        }
        std::ofstream file;
        if (outputPath) {
            file.open(outputPath);
            if (!file) throw std::runtime_error("Cannot open benchmark output");
        }
        std::ostream& output = outputPath ? file : std::cout;
        output << "case,round,population,moving,groups,visible,update_ms,query_ms,total_ms,checksum";
        if (profile) output << ",refit_ms,traversal_ms,sort_ms";
        output << '\n' << std::fixed << std::setprecision(6);
        for (const auto& test : {Case{"spread-53k-idle",53000,0}, Case{"spread-53k-moving",53000,1000},
                                 Case{"dense-100k",100000,0,true}, Case{"separated-1k",1000,0,false,true}})
            run(test, output, profile);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
