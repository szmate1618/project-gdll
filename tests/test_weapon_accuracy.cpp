#include "weapon_accuracy.hpp"
#include "fps_controller.hpp"
#include "weapons.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void movement(const std::filesystem::path& directory) {
    const viewer::FPSConfig config;
    const std::array<float, 4> baselines{.5f, .8f, .6f, 1.f};
    for (std::size_t slot = 0; slot < 4; ++slot) {
        const auto weapon = viewer::loadWeaponConfig(directory / (std::string(viewer::weaponIds[slot]) + ".gameplay.toml"), viewer::weaponIds[slot]);
        struct Sample { float speed, posture, factor; };
        for (const auto sample : {Sample{0, 0, 1}, {0, 1, 0}, {0, 2, 0},
                                  {.75f, 1, .5f}, {1.5f, 1, 1}, {.375f, 2, .5f}, {.75f, 2, 1},
                                  {1.5f, 0, 1.5f}, {3, 0, 2}, {4.5f, 0, 3}, {6, 0, 4},
                                  {0, .5f, .5f}, {3, .5f, 1.5f}, {.375f, 1.5f, .375f}}) {
            const float sigma = viewer::hipFireInaccuracy(weapon, sample.speed, sample.posture, config, false);
            require(std::abs(sigma - sample.factor * baselines[slot]) < .00001f,
                    "Hip-fire profiles must interpolate over actual speed and continuous stance");
            require(viewer::hipFireInaccuracy(weapon, sample.speed, sample.posture, config, true) == 0,
                    "Scoped sniper aiming must retain its separate sway without random hip-fire spread");
        }
    }
}
void normalDistribution() {
    viewer::ShotInaccuracy random(7521), repeated(7521);
    require(random.sample(0) == glm::vec2(0), "An exact stance must not have random error");
    require(random.sample(1) == repeated.sample(1), "Zero inaccuracy must not advance the random stream");
    constexpr int count = 100000;
    double sumX = 0, sumY = 0, squaresX = 0, squaresY = 0, fourthX = 0, covariance = 0;
    int outside95 = 0;
    for (int i = 0; i < count; ++i) {
        const auto sample = random.sample(1);
        sumX += sample.x; sumY += sample.y;
        squaresX += sample.x * sample.x; squaresY += sample.y * sample.y;
        fourthX += std::pow(sample.x, 4); covariance += sample.x * sample.y;
        outside95 += glm::dot(sample, sample) > 5.99146455f;
    }
    require(std::abs(sumX / count) < .015 && std::abs(sumY / count) < .015 &&
            std::abs(squaresX / count - 1) < .02 && std::abs(squaresY / count - 1) < .02 &&
            std::abs(covariance / count) < .02 && fourthX / count > 2.8 && fourthX / count < 3.2,
            "Shot offsets must have independent, centered normal components with the configured variance");
    require(outside95 > 4500 && outside95 < 5500,
            "The crosshair's 95 percent radius must match the normal spread, including its tails");
    viewer::ShotInaccuracy small(61), large(61);
    for (int i = 0; i < 10; ++i)
        require(glm::distance(large.sample(2), small.sample(.5f) * 4.f) < .00001f,
                "Standard deviation must scale the same normal samples linearly");
}
void raysAndCrosshair() {
    for (const auto forward : {glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(1, 0, 0)}) {
        for (const auto offset : {glm::vec2(0), glm::vec2(5, 0), glm::vec2(0, 5)}) {
            const auto ray = viewer::inaccurateShotDirection(forward, offset);
            require(std::abs(glm::length(ray) - 1) < .00001f, "Inaccurate rays must remain normalized, including vertical aim");
            if (offset == glm::vec2(0)) require(glm::distance(ray, forward) < .00001f, "Zero spread must retain the original aim exactly");
            else {
                const float angle = glm::degrees(std::acos(glm::clamp(glm::dot(ray, forward), -1.f, 1.f)));
                require(std::abs(angle - 5) < .0001f, "Normal samples must represent angular degrees rather than world-space distances");
            }
        }
    }
    require(viewer::inaccuracyCrosshairRadius(0, 1.7320508f) == 0, "Exact aim must have no crosshair expansion");
    const float radius = viewer::inaccuracyCrosshairRadius(1, 1.7320508f);
    require(radius > .073f && radius < .075f, "One-degree normal spread must project to the expected crosshair radius at 60-degree FOV");
    require(std::abs(viewer::inaccuracyCrosshairRadius(1, 3.4641016f) - 2 * radius) < .00001f,
            "Crosshair expansion must follow the camera projection");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected weapon configuration directory");
        movement(argv[1]); normalDistribution(); raysAndCrosshair();
        std::cout << "Movement accuracy profiles, normal shot offsets and projected crosshair radius passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
