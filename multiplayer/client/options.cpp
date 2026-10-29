#include "options.hpp"
#include <iostream>
#include <stdexcept>
namespace multiplayer {
GameOptions parseGameOptions(int argc, char** argv) {
    GameOptions result;
    std::vector<char*> shared;
    shared.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        GameMode mode = GameMode::Menu;
        if (argument == "--single-player") mode = GameMode::SinglePlayer;
        if (argument == "--host") mode = GameMode::Host;
        if (argument == "--connect") mode = GameMode::Join;
        if (mode != GameMode::Menu) {
            if (result.mode != GameMode::Menu) throw std::runtime_error("Choose only one of --single-player, --host or --connect");
            result.mode = mode;
            if (mode != GameMode::Join) continue;
        }
        if (argument == "--help") std::cout << "Game: no mode flag opens the menu. --single-player starts a private local game;\n"
            "--host [--port 27020] hosts; --connect IP:PORT joins. Escape leaves a session.\n";
        shared.push_back(argv[i]);
        // Leave option values to the shared parser, even when they resemble flags.
        if (argument == "--connect" || argument == "--port" || argument == "--zombies" ||
            argument == "--zombie-count" || argument == "--zombie-radius" || argument == "--vegetation" ||
            argument == "--seconds" || argument == "--lag-ms" || argument == "--loss-percent" ||
            argument == "--time-of-day" || argument == "--day-length" || argument == "--screenshot" || argument == "--resolution") {
            if (i + 1 < argc) shared.push_back(argv[++i]);
        } else if (argument == "--spawn" || argument == "--look") {
            for (int j = 0; j < 2 && i + 1 < argc; ++j) shared.push_back(argv[++i]);
        }
    }
    result.scene = parseOptions(static_cast<int>(shared.size()), shared.data(), false);
    if (result.scene.headless && result.mode == GameMode::Menu) result.mode = GameMode::Join;
    return result;
}
}
