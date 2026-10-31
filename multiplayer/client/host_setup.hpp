#pragma once

#include "server/options.hpp"
#include <vector>

namespace multiplayer {
struct HostScenario {
    std::filesystem::path path; // Empty selects the ordinary procedural population.
    std::string name, description, error;
    std::size_t count = 0;
    bool traveling = false;
};

// A session draft. Selecting an authored scenario never changes its file.
class HostSetup {
public:
    explicit HostSetup(const Options& defaults, const std::vector<std::filesystem::path>& scenarioDirectories);
    const std::vector<HostScenario>& scenarios() const { return scenarios_; }
    std::size_t selected = 0;
    std::string port;
    bool birds;
    double startHour, dayLengthSeconds;
    void restoreOptions();
    ServerOptions options() const; // Revalidates files and throws a user-facing error.
private:
    Options defaults_;
    std::vector<HostScenario> scenarios_;
};
} // namespace multiplayer
