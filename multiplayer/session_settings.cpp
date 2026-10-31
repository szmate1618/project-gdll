#include "session_settings.hpp"

namespace multiplayer {
SessionSettings sessionSettings(const Options& options, const Scene& scene) {
    SessionSettings settings;
    settings.zombieCount = static_cast<std::uint32_t>(scene.zombies.instances.size());
    settings.zombieRadius = options.zombieRadius;
    settings.startHour = options.dayNight.startHour;
    settings.dayLengthSeconds = options.dayNight.dayLengthSeconds;
    settings.spawn = options.spawn; settings.look = scene.spawnOrientation;
    settings.trees = options.trees; settings.forests = options.forests;
    settings.cars = options.cars; settings.birds = options.birds;
    settings.vegetation = static_cast<std::uint8_t>(options.vegetation.source);
    settings.explicitVegetation = options.vegetation.explicitSelection;
    return settings;
}
Options applySessionSettings(const Options& local, const SessionSettings& settings) {
    auto result = local;
    result.zombieCount = settings.zombieCount; result.replicaZombieCount = settings.zombieCount;
    result.zombieRadius = settings.zombieRadius;
    result.spawn = settings.spawn;
    if (!local.explicitLook) result.look = settings.look;
    result.dayNight = {settings.startHour, settings.dayLengthSeconds};
    result.trees = settings.trees; result.forests = settings.forests;
    result.cars = settings.cars; result.birds = settings.birds;
    result.vegetation.source = static_cast<viewer::VegetationSource>(settings.vegetation);
    result.vegetation.explicitSelection = settings.explicitVegetation;
    return result;
}
} // namespace multiplayer
