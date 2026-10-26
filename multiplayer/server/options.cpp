#include "options.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace multiplayer {

ServerOptions parseServerOptions(int argc, char** argv) {
    if (argc < 1 || !argv) throw std::invalid_argument("Server command requires a program name");
    ServerOptions result;
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    bool help = false;
    for (int i = 0; i < argc; ++i) {
        if (!argv[i]) throw std::invalid_argument("Null server command argument");
        const std::string argument = argv[i];
        if (i > 0 && argument == "--scenario") {
            if (!result.scenario.empty()) throw std::invalid_argument("Repeated --scenario option");
            if (++i == argc || !argv[i] || std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0)
                throw std::invalid_argument("--scenario requires a scenario filename");
            result.scenario = argv[i];
        } else {
            help |= argument == "--help";
            arguments.push_back(argument);
        }
    }
    std::vector<char*> sharedArguments;
    sharedArguments.reserve(arguments.size());
    for (auto& argument : arguments) sharedArguments.push_back(argument.data());
    if (help) std::cout << "Server: --scenario FILE loads idle/travelling zombie groups from TOML.\n"
                          "Only travelling groups require the map's .navmesh; ordinary AI never requests paths.\n";
    result.scene = parseOptions(static_cast<int>(sharedArguments.size()), sharedArguments.data(), true);
    return result;
}

} // namespace multiplayer
