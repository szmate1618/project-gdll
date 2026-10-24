#include "options.hpp"
#include "runtime.hpp"
#include <csignal>
#include <iostream>

namespace {
volatile std::sig_atomic_t running = 1;
void stop(int) { running = 0; }
}
int main(int argc, char** argv) {
    try {
        const auto options = multiplayer::parseServerOptions(argc, argv);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        multiplayer::runServer(options, {}, [] { return !running; });
        return 0;
    } catch (const std::exception& error) { std::cerr << "Server: " << error.what() << '\n'; return 1; }
}
