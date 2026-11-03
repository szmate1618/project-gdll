#include "client/flashlights.hpp"
#include "server/world.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void wireState() {
    using namespace multiplayer;
    for (bool enabled : {false, true}) {
        Command command;
        command.sequence = 1;
        command.flashlight = enabled;
        const auto input = encode(Commands{{command}});
        require(std::get<Commands>(decode(input.data(), input.size())).commands.at(0).flashlight == enabled,
                "Command must preserve the flashlight's held state");
        Snapshot snapshot;
        snapshot.players.push_back({});
        snapshot.players.back().id = 1;
        snapshot.players.back().flashlight = enabled;
        const auto state = encode(snapshot);
        require(std::get<Snapshot>(decode(state.data(), state.size())).players.at(0).flashlight == enabled,
                "Snapshot must preserve each player's flashlight toggle");
    }
}

void presentedLights() {
    using namespace multiplayer;
    PlayerState self, remote, disabled, seated;
    self.id = 1;
    remote.id = 2;
    remote.movement.feet = {4, 2, 3};
    remote.yaw = 0;
    remote.pitch = 0;
    disabled.id = 3; disabled.flashlight = false;
    seated.id = 4; seated.vehicle = 0; seated.seat = 0;
    auto lights = remoteFlashlights({self, remote, disabled, seated}, self.id);
    require(lights.size() == 1, "Only enabled remote on-foot players may emit beams");
    require(lights[0].position.x > 4 && lights[0].position.x < 4.2f &&
            lights[0].position.y > 3.5f && lights[0].position.y < 3.6f,
            "Standing flashlight must start at the held offset near the remote eye");
    require(lights[0].direction.x > .99f, "Remote yaw must aim the beam east independently of the viewer");
    const auto standingY = lights[0].position.y;
    remote.movement.stance = viewer::PlayerStance::Crouched;
    lights = remoteFlashlights({remote}, self.id);
    require(std::abs(standingY - lights[0].position.y - .7f) < .001f,
            "Crouching must lower the remote flashlight with the eye");
    remote.movement.stance = viewer::PlayerStance::Prone;
    lights = remoteFlashlights({remote}, self.id);
    require(std::abs(standingY - lights[0].position.y - 1.2f) < .001f,
            "Prone flashlight must follow the lower eye");
    remote.yaw = 90; remote.pitch = 45;
    lights = remoteFlashlights({remote}, self.id);
    require(lights[0].direction.y > .65f && lights[0].direction.z > .65f,
            "Remote pitch and yaw must turn the beam together");
    remote.flashlight = false;
    require(remoteFlashlights({remote}, self.id).empty() && remoteFlashlights({}, self.id).empty(),
            "Switching off or leaving must remove the remote beam");
}

void authoritativeState(const std::filesystem::path& asset) {
    using namespace multiplayer;
    Options options; options.map = asset;
    options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options);
    World world(scene);
    const auto source = world.join(), observer = world.join();
    const auto enabled = [&](std::uint32_t player) {
        const auto snapshot = world.snapshot(observer);
        for (const auto& state : snapshot.players) if (state.id == player) return state.flashlight;
        throw std::runtime_error("Missing player in flashlight snapshot");
    };
    require(enabled(source) && enabled(observer), "New players must start with their flashlight enabled");
    Command command; command.sequence = 1; command.flashlight = false;
    world.submit(source, Commands{{command}});
    world.submit(source, Commands{{command}}); // Network retry of a held state.
    for (int i = 0; i < 5; ++i) world.step();
    require(!enabled(source) && enabled(observer), "Flashlight off must reach other players independently");
    for (int i = 0; i < 15; ++i) world.step();
    require(!enabled(source), "Held input and timeout must preserve the toggle");
    command.sequence = 2; command.flashlight = true;
    world.submit(source, Commands{{command}}); world.step();
    require(enabled(source), "A later command must restore the remote beam");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        wireState(); presentedLights(); authoritativeState(argv[1]);
        std::cout << "Multiplayer flashlight checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
