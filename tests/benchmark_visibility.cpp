#include "visibility.hpp"
#include "model_visibility.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {

// Convert the engine's integer output explicitly so scene generation does not
// depend on a standard library's uniform_real_distribution implementation.
class Random {
public:
    explicit Random(std::uint32_t seed) : engine_(seed) {}
    float between(float minimum, float maximum) {
        const float unit = static_cast<float>(engine_() >> 8) / 16777216.0f;
        return minimum + unit * (maximum - minimum);
    }
private:
    std::mt19937 engine_;
};

struct Scene {
    std::string name;
    std::vector<viewer::VisibilityBounds> bounds;
    glm::vec2 halfExtent;
    float eyeHeight = 1.8f;
    glm::vec3 origin{0};
};

void addBox(Scene& scene, glm::vec3 base, glm::vec3 size) {
    scene.bounds.push_back({base, base + size});
}

std::vector<Scene> makeScenes() {
    std::vector<Scene> scenes;
    Random random(0x43554c4cU);

    Scene town{"town_grid", {}, {1120, 1120}};
    for (int x = -80; x < 80; ++x) for (int z = -80; z < 80; ++z) {
        const glm::vec3 base{x * 14.0f + random.between(2, 4), 0,
                             z * 14.0f + random.between(2, 4)};
        addBox(town, base, {random.between(6, 9), random.between(4, 32), random.between(6, 9)});
    }
    scenes.push_back(std::move(town));

    Scene forest{"forest_clusters", {}, {1300, 1300}};
    for (int cluster = 0; cluster < 32; ++cluster) {
        const glm::vec2 center{random.between(-1000, 1000), random.between(-1000, 1000)};
        for (int tree = 0; tree < 2048; ++tree) {
            const float angle = random.between(0, 6.283185307f);
            const float radius = std::sqrt(random.between(0, 1)) * 180.0f;
            const float width = random.between(2, 12);
            const glm::vec3 base{center.x + std::cos(angle) * radius - width * .5f,
                                 random.between(-3, 3),
                                 center.y + std::sin(angle) * radius - width * .5f};
            addBox(forest, base, {width, random.between(5, 28), width});
        }
    }
    scenes.push_back(std::move(forest));

    Scene corridor{"road_corridor", {}, {130, 3000}};
    for (int i = 0; i < 24000; ++i) {
        const float side = (i % 2 == 0) ? -1.0f : 1.0f;
        const glm::vec3 base{side * random.between(8, 120), random.between(-2, 2),
                             random.between(-3000, 3000)};
        addBox(corridor, base, {random.between(.3f, 6), random.between(1, 15), random.between(.3f, 6)});
    }
    scenes.push_back(std::move(corridor));

    Scene mixed{"sparse_mixed", {}, {2400, 2400}};
    for (int i = 0; i < 16000; ++i) {
        const glm::vec3 base{random.between(-2400, 2400), random.between(-8, 8),
                             random.between(-2400, 2400)};
        // Large warehouses/terrain chunks mixed with poles and small props.
        const float scale = (i % 20 == 0) ? 30.0f : 1.0f;
        addBox(mixed, base, {scale * random.between(.5f, 6), random.between(1, 35),
                             scale * random.between(.5f, 6)});
    }
    scenes.push_back(std::move(mixed));
    return scenes;
}

struct QueryFrame {
    glm::mat4 projectionView;
    viewer::HorizontalVisibilityRange range;
};

// The range follows the camera, as it does inside the runtime old-forest limit.
// A zero radius selects the normal unlimited query rather than a point range.
template<class Index>
viewer::VisibilityStats queryFrame(const Index& index, const QueryFrame& frame,
    std::vector<std::size_t>& visible, viewer::VisibilityQueryState* state = nullptr) {
    return index.query(frame.projectionView, visible,
                       frame.range.radius > 0 ? &frame.range : nullptr, true, state);
}

std::vector<QueryFrame> makeTrajectory(const Scene& scene, const std::string& trajectory,
                                      std::size_t frames, float rangeRadius = 0) {
    std::vector<QueryFrame> matrices;
    matrices.reserve(frames);
    Random random(0x4652414dU);
    const auto start = scene.origin + glm::vec3(0, scene.eyeHeight, scene.halfExtent.y * .2f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        glm::vec3 eye = start;
        float yaw = .17f, pitch = -.025f, fov = 60.0f, aspect = 16.0f / 9.0f;
        if (trajectory == "smooth") {
            // Walking at about 5 m/s and turning 7 degrees/s, sampled at 60 Hz.
            const float seconds = static_cast<float>(frame) / 60.0f;
            eye += glm::vec3(3.0f * seconds, .08f * std::sin(seconds * 5.0f), -4.0f * seconds);
            yaw += .12f * seconds;
            pitch += .015f * std::sin(seconds);
        } else if (trajectory == "teleport") {
            // Deliberately little coherence: jump, turn and change projection
            // every frame. This exposes predictor overhead and stale hints.
            eye = scene.origin + glm::vec3(random.between(-.8f, .8f) * scene.halfExtent.x,
                   random.between(scene.eyeHeight, 60),
                   random.between(-.8f, .8f) * scene.halfExtent.y);
            yaw = random.between(-3.141592654f, 3.141592654f);
            pitch = random.between(-.35f, .35f);
            fov = random.between(35, 90);
            aspect = random.between(.7f, 2.2f);
        }
        const glm::vec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                                  -std::cos(yaw) * std::cos(pitch)};
        const auto projection = glm::perspective(glm::radians(fov), aspect, .1f, 900.0f);
        matrices.push_back({projection * glm::lookAt(eye, eye + direction, glm::vec3(0, 1, 0)),
                            {{eye.x, eye.z}, rangeRadius}});
    }
    return matrices;
}

template<class Index>
void verify(const Index& index, const std::vector<QueryFrame>& matrices,
            const std::string& label) {
    viewer::VisibilityQueryState state;
    std::vector<std::size_t> baseline, temporal;
    // Also verify the sequence wrap used after the timing warm-up.
    for (int pass = 0; pass < 2; ++pass) for (std::size_t frame = 0; frame < matrices.size(); ++frame) {
        const auto fixedStats = queryFrame(index, matrices[frame], baseline);
        const auto temporalStats = queryFrame(index, matrices[frame], temporal, &state);
        if (baseline != temporal || fixedStats.testedNodes != temporalStats.testedNodes ||
            fixedStats.testedInstances != temporalStats.testedInstances)
            throw std::runtime_error(label + ": temporal query changed visibility or traversal at frame " +
                                     std::to_string(frame));
    }
}

struct Measurement {
    double microseconds = 0;
    double planes = 0;
    double temporalRejects = 0;
    double visible = 0;
};

template<class Index>
Measurement measure(const Index& index, const std::vector<QueryFrame>& matrices,
                    std::size_t boundsCount, bool useTemporalHints) {
    viewer::VisibilityQueryState state;
    auto* temporal = useTemporalHints ? &state : nullptr;
    std::vector<std::size_t> visible;
    visible.reserve(boundsCount);
    // Allocate caches and warm both code/data paths before starting the clock.
    for (const auto& matrix : matrices) queryFrame(index, matrix, visible, temporal);

    std::size_t planes = 0, temporalRejects = 0, visibleCount = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (const auto& matrix : matrices) {
        const auto stats = queryFrame(index, matrix, visible, temporal);
        planes += stats.testedPlanes;
        temporalRejects += stats.temporalRejects;
        visibleCount += visible.size();
    }
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    const double count = static_cast<double>(matrices.size());
    return {std::chrono::duration<double, std::micro>(elapsed).count() / count,
            static_cast<double>(planes) / count, static_cast<double>(temporalRejects) / count,
            static_cast<double>(visibleCount) / count};
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) * .5;
}

std::size_t parseCount(const char* argument, const std::string& name, std::size_t maximum) {
    const std::string text(argument);
    std::size_t end = 0;
    const auto value = std::stoul(text, &end);
    if (text.empty() || text[0] == '-' || end != text.size() || value == 0 || value > maximum)
        throw std::runtime_error(name + " must be between 1 and " + std::to_string(maximum));
    return value;
}

struct Summary {
    double logSpeedups = 0;
    std::size_t cases = 0;
};

void warmProcessor(const viewer::VisibilityIndex& index, const Scene& scene) {
    const auto matrix = makeTrajectory(scene, "stationary", 1).front();
    viewer::VisibilityQueryState state;
    std::vector<std::size_t> visible;
    visible.reserve(scene.bounds.size());
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    // Sustained initial work avoids a cold CPU frequency bias in the first
    // short samples. Both query variants still get their own per-pass warm-up.
    do {
        queryFrame(index, matrix, visible);
        queryFrame(index, matrix, visible, &state);
    } while (std::chrono::steady_clock::now() < until);
}

template<class Index>
void runCases(const Index& index, const Scene& scene, std::size_t boundsCount,
              std::size_t frames, std::size_t repeats, float rangeRadius, Summary& summary) {
    for (const std::string trajectory : {"stationary", "smooth", "teleport"}) {
        const auto matrices = makeTrajectory(scene, trajectory, frames, rangeRadius);
        verify(index, matrices, scene.name + "/" + trajectory);
        std::array<std::vector<double>, 2> times;
        std::array<Measurement, 2> measurements;
        for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
            // Alternate which variant runs first to spread thermal/frequency
            // drift over the comparison.
            for (std::size_t offset = 0; offset < 2; ++offset) {
                const auto variant = (repeat + offset) % 2;
                measurements[variant] = measure(index, matrices, boundsCount, variant == 1);
                times[variant].push_back(measurements[variant].microseconds);
            }
        }
        const double baselineUs = median(times[0]), temporalUs = median(times[1]);
        const auto& baseline = measurements[0];
        const auto& temporal = measurements[1];
        const double speedup = baselineUs / temporalUs;
        summary.logSpeedups += std::log(speedup);
        std::cout << scene.name << ',' << boundsCount << ',' << trajectory << ','
                  << frames << ',' << repeats << ',' << rangeRadius << ',' << baselineUs << ',' << temporalUs << ','
                  << speedup << ',' << (baseline.planes == 0 ? 0 :
                      100 * (baseline.planes - temporal.planes) / baseline.planes)
                  << ',' << baseline.planes << ',' << temporal.planes << ','
                  << temporal.temporalRejects << ',' << temporal.visible << '\n';
        ++summary.cases;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::size_t frames = 240, repeats = 9;
        std::string modelPath;
        float rangeRadius = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string option(argv[i]);
            if (option == "--help") {
                std::cout << "Usage: benchmark_visibility [--frames N] [--repeats N] [--model PATH] [--range METERS]\n"
                             "CPU-only BVH benchmark; four synthetic scenes plus optional loaded GLB.\n"
                             "Outputs CSV. Default: 240 frames, 9 repeats, unlimited horizontal range.\n";
                return 0;
            }
            if (option == "--model" && i + 1 < argc) {
                modelPath = argv[++i];
                continue;
            }
            if (option == "--range" && i + 1 < argc) {
                const std::string argument(argv[++i]);
                std::size_t end = 0;
                rangeRadius = std::stof(argument, &end);
                if (end != argument.size() || !std::isfinite(rangeRadius) || rangeRadius <= 0 || rangeRadius > 1000000)
                    throw std::runtime_error("--range must be finite and greater than zero, at most 1000000 meters");
                continue;
            }
            if ((option != "--frames" && option != "--repeats") || i + 1 == argc)
                throw std::runtime_error("Unknown or incomplete option: " + option);
            const auto value = parseCount(argv[++i], option, option == "--frames" ? 100000 : 1000);
            if (option == "--frames") frames = value;
            else repeats = value;
        }
        std::cout << "# CPU-only queries; no loading, rendering, GPU or BVH build time.\n"
                     "# Four synthetic AABB scenes; --model optionally adds production ModelVisibility on loaded draws.\n"
                     "# Fixed plane order compared with temporal rejecting-plane hints.\n"
                     "# --range optionally applies a horizontal camera-centered radius in meters.\n"
                     "# Complete ordered results and traversal counts verified on every frame before timing.\n"
                     "# 2 s startup warmup, warmed passes, alternating variant order, median timings.\n";
#ifdef NDEBUG
        std::cout << "# Build: NDEBUG defined; use an optimized Release build for comparisons.\n";
#else
        std::cout << "# WARNING: NDEBUG absent; these timings may come from an unoptimized build.\n";
#endif
        std::cout << "scene,objects,trajectory,frames,repeats,range_m,baseline_us,temporal_us,speedup,plane_reduction_pct,baseline_planes,temporal_planes,temporal_rejects,visible\n";
        std::cout << std::fixed << std::setprecision(3);
        Summary summary;
        for (const auto& scene : makeScenes()) {
            const viewer::VisibilityIndex index(scene.bounds);
            if (summary.cases == 0) warmProcessor(index, scene);
            runCases(index, scene, scene.bounds.size(), frames, repeats, rangeRadius, summary);
        }
        if (!modelPath.empty()) {
            const auto model = viewer::loadModel(modelPath);
            const viewer::ModelVisibility index(model);
            const auto center = (model.boundsMin + model.boundsMax) * .5f;
            const auto halfExtent = (model.boundsMax - model.boundsMin) * .5f;
            const Scene scene{"model", {}, {halfExtent.x, halfExtent.z}, 1.8f, center};
            runCases(index, scene, model.draws.size(), frames, repeats, rangeRadius, summary);
        }
        std::cout << "# Geometric mean temporal speedup across " << summary.cases
                  << " cases: " << std::exp(summary.logSpeedups / static_cast<double>(summary.cases))
                  << "x\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Visibility benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
