#include "host_setup.hpp"
#include "zombie_scenario_config.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <stdexcept>

namespace multiplayer {
namespace {
viewer::ZombieScenarioConfig validateScenario(const std::filesystem::path& path, const std::filesystem::path& map) {
    auto scenario = viewer::loadZombieScenarioConfig(path);
    if (scenario.count() > maxZombies) throw std::runtime_error("Scenario exceeds the multiplayer population limit.");
    if (scenario.needsNavigation()) {
        auto navigation = map; navigation.replace_extension(".navmesh");
        if (!std::filesystem::is_regular_file(navigation))
            throw std::runtime_error("This scenario needs prepared navigation for the selected map.");
    }
    for (const auto& group : scenario.hordes)
        for (const auto& density : {group.spawnDensity, group.destinationDensity})
            if (!density.empty() && !std::filesystem::is_regular_file(density))
                throw std::runtime_error("A scenario placement image is missing: " + density.filename().string());
    return scenario;
}
}
HostSetup::HostSetup(const Options& defaults, const std::vector<std::filesystem::path>& directories) :
    port(std::to_string(defaults.port)), birds(defaults.birds), startHour(defaults.dayNight.startHour),
    dayLengthSeconds(defaults.dayNight.dayLengthSeconds), defaults_(defaults) {
    scenarios_.push_back({{}, "Default session", "Normal population near the starting area.", {}, defaults.zombieCount, false});
    std::set<std::filesystem::path> paths;
    for (const auto& directory : directories) {
        if (!std::filesystem::is_directory(directory)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const auto file = entry.is_directory() ? entry.path() / "scenario.toml" : entry.path();
            if (file.extension() == ".toml" && std::filesystem::is_regular_file(file))
                paths.insert(std::filesystem::weakly_canonical(file));
        }
    }
    for (const auto& file : paths) {
        HostScenario item;
        item.path = file;
        item.name = (file.filename() == "scenario.toml" ? file.parent_path().filename() : file.stem()).string();
        std::replace(item.name.begin(), item.name.end(), '-', ' ');
        try {
            const auto config = viewer::loadZombieScenarioConfig(file);
            item.count = config.count(); item.traveling = config.needsNavigation();
            item.description = std::to_string(config.hordes.size()) + " zombie groups. " +
                (item.traveling ? "Includes scripted travel." : "No initial scripted travel.");
            validateScenario(file, defaults.map);
        } catch (const std::exception& error) { item.error = error.what(); }
        scenarios_.push_back(std::move(item));
    }
}
void HostSetup::restoreOptions() {
    birds = defaults_.birds; startHour = defaults_.dayNight.startHour; dayLengthSeconds = defaults_.dayNight.dayLengthSeconds;
}
ServerOptions HostSetup::options() const {
    if (selected >= scenarios_.size()) throw std::runtime_error("Choose a scenario.");
    unsigned value = 0;
    const auto parsed = std::from_chars(port.data(), port.data() + port.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || value < 1 || value > 65535)
        throw std::runtime_error("UDP port must be between 1 and 65535.");
    if (!std::isfinite(startHour) || startHour < 0 || startHour >= 24 || !std::isfinite(dayLengthSeconds) ||
        dayLengthSeconds < 0 || (dayLengthSeconds > 0 && dayLengthSeconds < 1) || dayLengthSeconds > 86400)
        throw std::runtime_error("Choose a valid starting time and day/night cycle.");
    ServerOptions result; result.scene = defaults_; result.scenario = scenarios_[selected].path;
    result.scene.port = static_cast<std::uint16_t>(value); result.scene.birds = birds;
    result.scene.dayNight = {startHour, dayLengthSeconds};
    if (!result.scenario.empty()) validateScenario(result.scenario, result.scene.map);
    return result;
}
} // namespace multiplayer
