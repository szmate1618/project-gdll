#include "options.hpp"
#include "session.hpp"
#include "server/runtime.hpp"
#ifdef MP_GRAPHICS
#include "menu.hpp"
#include <GLFW/glfw3.h>
#endif
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

int main(int argc, char** argv) {
    try {
        auto game = multiplayer::parseGameOptions(argc, argv);
#ifndef MP_GRAPHICS
        if (!game.scene.headless) throw std::runtime_error("This build supports --headless clients only");
#endif
#ifdef MP_GRAPHICS
        std::unique_ptr<multiplayer::GameMenu> menu;
        if (!game.scene.headless) {
            menu = std::make_unique<multiplayer::GameMenu>(game.scene);
        }
#endif
        auto mode = game.mode;
        std::string message;
        while (true) {
#ifdef MP_GRAPHICS
            if (mode == multiplayer::GameMode::Menu) mode = menu->choose(message);
#endif
            if (mode == multiplayer::GameMode::Quit) break;
            std::unique_ptr<multiplayer::LocalServer> server;
            multiplayer::SessionFeedback feedback;
            GLFWwindow* window = nullptr;
#ifdef MP_GRAPHICS
            if (menu) {
                window = menu->window();
                feedback = [&](const std::string& status) { return menu->progress(status); };
            }
#endif
            const auto stopServer = [&] {
                if (!server) return;
                server->requestStop();
                while (!server->state().finished) {
                    if (feedback) feedback("Stopping local server...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                server.reset();
            };
            try {
                auto options = game.scene;
#ifdef MP_GRAPHICS
                if (menu) {
                    options.address = menu->address;
                }
#endif
                if (mode == multiplayer::GameMode::SinglePlayer || mode == multiplayer::GameMode::Host) {
                    multiplayer::ServerOptions authoritative; authoritative.scene = options;
#ifdef MP_GRAPHICS
                    if (menu && mode == multiplayer::GameMode::Host) {
                        authoritative = menu->hostOptions(); options = authoritative.scene;
                    }
#endif
                    authoritative.scene.seconds = 0;
                    const bool single = mode == multiplayer::GameMode::SinglePlayer;
                    server = std::make_unique<multiplayer::LocalServer>(authoritative,
                        multiplayer::ServerSettings{single, single ? 1u : multiplayer::maxPlayers});
                    bool cancelled = false;
                    while (!server->state().port && !server->state().finished) {
                        if (feedback && !feedback(single ? "Starting private local server..." : "Starting host server...")) {
                            cancelled = true; break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    const auto state = server->state();
                    if (!state.error.empty()) throw std::runtime_error(state.error);
                    if (!cancelled && !state.port) throw std::runtime_error("Local server stopped during startup");
                    if (!cancelled) options.address = "127.0.0.1:" + std::to_string(state.port);
                    else { stopServer(); mode = multiplayer::GameMode::Menu; continue; }
                }
                std::function<std::optional<double>()> serverSimulationMilliseconds;
                if (server) serverSimulationMilliseconds = [&] { return server->state().simulationMilliseconds; };
                multiplayer::runClientSession(options, window, feedback, [&] {
                    if (!server) return;
                    const auto state = server->state();
                    if (state.finished) throw std::runtime_error(state.error.empty() ? "Local server stopped" : state.error);
                }, serverSimulationMilliseconds);
                message = "Session ended.";
                stopServer();
            } catch (const std::exception& error) {
                message = error.what();
                std::cerr << "Game: " << message << std::endl;
                stopServer();
                if (game.scene.headless || game.scene.seconds) return 1;
            }
            if (game.scene.headless || game.scene.seconds) break;
#ifdef MP_GRAPHICS
            if (menu->closed()) break;
#endif
            mode = multiplayer::GameMode::Menu;
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << "Game: " << error.what() << '\n'; return 1; }
}
