#include "signals.hpp"
#include "client/snapshots.hpp"
#include "server/world.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F run) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid signal accepted");
}
viewer::Model ground() {
    viewer::Model model;
    viewer::Primitive p;
    for (const glm::vec3 v : {glm::vec3(-100, 0, -100), {-100, 0, 100}, {100, 0, 100}, {100, 0, -100}}) {
        viewer::Vertex vertex; vertex.position = v; p.vertices.push_back(vertex);
    }
    p.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(p);
    viewer::DrawInstance draw; draw.name = "terrain"; model.draws.push_back(draw);
    model.boundsMin = {-100, 0, -100}; model.boundsMax = {100, 0, 100};
    return model;
}
void wire() {
    using namespace multiplayer;
    Command command; command.sequence = 1; command.signal = SignalKind::Danger;
    auto bytes = encode(Commands{{command}});
    require(std::get<Commands>(decode(bytes.data(), bytes.size())).commands[0].signal == SignalKind::Danger,
            "Signal lost in command round trip");
    command.signal = static_cast<SignalKind>(4); rejects([&] { encode(Commands{{command}}); });
    bytes.back() = 255; rejects([&] { decode(bytes.data(), bytes.size()); });
    Snapshot snapshot; snapshot.tick = 10;
    snapshot.signals = {{1, SignalKind::Move, {3, 2, 1}, 370}, {1, SignalKind::Danger, {9, 2, 1}, 365}};
    bytes = encode(snapshot);
    const auto copy = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
    require(copy.signals.size() == 2 && copy.signals[0].position == glm::vec3(3, 2, 1) &&
            copy.signals[0].expires == 370 && copy.signals[1].id == 1 && copy.signals[1].expires == 365 &&
            copy.signals[1].kind == SignalKind::Danger, "Multiple signals from one player lost in snapshot round trip");
    for (std::size_t n = 0; n < bytes.size(); ++n) rejects([&] { decode(bytes.data(), n); });
    snapshot.signals[0].expires = 371; rejects([&] { encode(snapshot); });
    snapshot.signals[0].expires = 10; rejects([&] { encode(snapshot); });
    snapshot = copy;
    snapshot.signals[0].position.x = std::numeric_limits<float>::quiet_NaN(); rejects([&] { encode(snapshot); });
    snapshot = copy; snapshot.signals.resize(7, copy.signals[0]);
    rejects([&] { encode(snapshot); }); // A sender cannot exceed the six active ping budget.
    snapshot.signals.clear();
    for (std::uint32_t player = 1; player <= 8; ++player)
        for (std::uint64_t ping = 0; ping < 6; ++ping)
            snapshot.signals.push_back({player, SignalKind::Backup, {float(ping), 0, 0}, 370 - ping * 60});
    bytes = encode(snapshot);
    require(std::get<Snapshot>(decode(bytes.data(), bytes.size())).signals.size() == 48,
            "Full eight-player signal set lost in transit");
    snapshot.signals.push_back({9, SignalKind::Backup, {0, 0, 0}, 370});
    rejects([&] { encode(snapshot); });
    Snapshots buffer;
    require(buffer.push(copy, 0), "Signal snapshot not published");
    auto expired = copy; expired.tick = 370; expired.signals.clear();
    require(buffer.push(expired, 6) && !buffer.push(copy, 7) && buffer.latest().signals.empty(),
            "Reordered snapshot resurrected expired signals");
}
void geometry() {
    using namespace multiplayer;
    require(signalWheelSelection({0, 0}) == SignalKind::None && signalWheelSelection({23, 0}) == SignalKind::None,
            "Wheel center must cancel");
    require(signalWheelSelection({0, -80}) == SignalKind::Danger && signalWheelSelection({80, 45}) == SignalKind::Move &&
            signalWheelSelection({-80, 45}) == SignalKind::Backup, "Wheel selected wrong sector");
    const auto model = ground(); viewer::CollisionWorld collision(model);
    viewer::Camera camera; camera.setPosition({0, 2, 0}); camera.setOrientation(0, -45);
    auto target = signalRayTarget(collision, camera);
    require(target && glm::length(*target - glm::vec3(2, 0, 0)) < .005f, "Camera ray did not hit rendered ground");
    camera.setOrientation(0, 45); require(!signalRayTarget(collision, camera), "Sky ray created a signal");
    camera.setOrientation(-90, 0);
    for (int zoom = 0; zoom < 3; ++zoom) {
        for (const glm::ivec2 size : {glm::ivec2(1280, 720), glm::ivec2(600, 1000)}) {
            auto front = signalPlacement(camera, {0, 2, -20}, size.x, size.y);
            require(!front.atEdge && front.position == glm::vec3(0, 2, -20), "Visible marker moved");
            for (const float distance : {.01f, 20000.0f}) {
                const auto result = signalPlacement(camera, {0, 2, -distance}, size.x, size.y);
                const auto clip = camera.projection(float(size.x) / size.y) * camera.view() * glm::vec4(result.position, 1);
                require(!result.atEdge && std::abs(clip.z / clip.w) < 1 && std::abs(clip.x / clip.w) < .001f,
                        "Near/far-clipped center marker disappeared or moved to edge");
            }
            for (const glm::vec3 p : {glm::vec3(-100, 2, -1), {100, 2, -1}, {0, 100, -1}, {0, -100, -1},
                                       {-10, 2, 10}, {10, 2, 10}, {0, 2, 10}, {0, 2, 0}}) {
                const auto result = signalPlacement(camera, p, size.x, size.y);
                const auto clip = camera.projection(float(size.x) / size.y) * camera.view() * glm::vec4(result.position, 1);
                const auto ndc = glm::vec3(clip) / clip.w;
                require(result.atEdge && clip.w > 0 && std::isfinite(ndc.x) && std::isfinite(ndc.y), "Behind marker not moved in front");
                if (!(std::abs(ndc.x) <= 1 - 56.0f / size.x + .001f &&
                      std::abs(ndc.y) <= 1 - 56.0f / size.y + .001f && std::abs(ndc.z) < 1))
                    std::cerr << "frustum zoom=" << zoom << " target=" << p.x << ',' << p.y << ',' << p.z
                              << " ndc=" << ndc.x << ',' << ndc.y << ',' << ndc.z << '\n';
                require(std::abs(ndc.x) <= 1 - 56.0f / size.x + .001f &&
                        std::abs(ndc.y) <= 1 - 56.0f / size.y + .001f && std::abs(ndc.z) < 1,
                        "Marker outside safe frustum");
                if (std::abs(p.x) > 1) require(ndc.x * p.x > 0, "Behind marker flipped side");
            }
        }
        camera.cycleViewMode();
    }
}
void server(const char* map) {
    using namespace multiplayer;
    Options options; options.map = map; options.trees = options.forests = false; options.zombieCount = 0;
    Scene scene(options); scene.model = ground(); scene.spawnEye = {0, 1.7f, 0};
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model);
    World world(scene); const auto first = world.join(), second = world.join();
    Command c; c.sequence = 1; c.signal = SignalKind::Backup;
    for (int i = 0; i < 5; ++i) world.submit(first, Commands{{c}});
    while (world.players().at(first).resolved != 1 && world.tick() < 10) world.step();
    auto s = world.snapshot(first);
    require(s.signals.size() == 1 && s.signals[0].position == world.players().at(first).controller.feet(),
            "Backup did not mark sender location");
    const auto backup = s.signals[0];
    require(backup.expires == world.tick() + 360, "Signal lifetime is not six seconds");
    while (world.tick() < 10) world.step();
    world.submit(first, Commands{{c}});
    c.sequence = 2; c.signal = SignalKind::Danger; c.movement.yawDegrees = 0; c.pitch = -45;
    world.submit(first, Commands{{c}}); world.step();
    require(world.snapshot(second).signals.size() == 1, "Cooldown bypassed");
    while (world.tick() < 70) world.step();
    require(world.snapshot(first).signals[0].expires == backup.expires, "Held or duplicate command refreshed signal");
    c.sequence = 3; world.submit(first, Commands{{c}}); world.step();
    s = world.snapshot(second);
    require(s.signals.size() == 2 && s.signals[0].kind == SignalKind::Backup &&
            s.signals[1].id == first && s.signals[1].kind == SignalKind::Danger &&
            std::abs(s.signals[1].position.y) < .005f && s.signals[1].position.x > 1,
            "Danger replaced an earlier ping or missed the camera ray hit");
    const auto danger = s.signals[1];
    const auto third = world.join();
    const auto bytes = encode(world.snapshot(third));
    const auto lateJoin = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
    require(lateJoin.signals.size() == 2 && lateJoin.signals[0].expires == backup.expires &&
            lateJoin.signals[1].expires == danger.expires, "Late join missed concurrent pings or changed expiry");
    Command other; other.sequence = 1; other.signal = SignalKind::Backup;
    world.submit(second, Commands{{other}}); world.step();
    require(world.snapshot(first).signals.size() == 3, "Signals from another player replaced existing pings");
    while (world.tick() < 140) world.step();
    c.sequence = 4; c.signal = SignalKind::Move; c.pitch = 45;
    world.submit(first, Commands{{c}}); world.step();
    s = world.snapshot(first);
    require(s.signals.size() == 3 && s.signals[0].expires == backup.expires && s.signals[1].expires == danger.expires,
            "Sky miss added or changed existing pings");
    while (world.tick() < backup.expires - 1) world.step();
    require(world.snapshot(second).signals.size() == 3, "Ping expired before six seconds");
    world.step(); s = world.snapshot(second);
    require(s.signals.size() == 2 && s.signals[0].kind == SignalKind::Danger &&
            s.signals[0].expires == danger.expires, "Oldest ping did not expire independently");
    while (world.tick() < danger.expires) world.step();
    require(world.snapshot(second).signals.size() == 1 && world.snapshot(second).signals[0].id == second,
            "Expiring one player's ping removed another player's ping");
    world.step(); require(world.snapshot(second).signals.empty(), "Last ping did not expire");
    c.sequence = 5; c.signal = SignalKind::Move; c.pitch = -45;
    world.submit(first, Commands{{c}}); world.step();
    require(world.snapshot(second).signals[0].kind == SignalKind::Move, "Move signal not shared");
    for (int i = 0; i < 60; ++i) world.step();
    c.sequence = 6; c.signal = SignalKind::Backup;
    world.submit(first, Commands{{c}}); world.step();
    require(world.snapshot(second).signals.size() == 2, "New backup replaced a move ping");
    world.leave(first); require(world.snapshot(second).signals.empty(), "Disconnect left sender's pings behind");
    for (std::uint64_t ping = 0; ping < 8; ++ping) {
        if (ping) for (int i = 0; i < 59; ++i) world.step();
        other.sequence = ping + 2; world.submit(second, Commands{{other}}); world.step();
        s = world.snapshot(third);
        require(s.signals.size() == std::min<std::size_t>(ping + 1, 6), "Steady pings exceeded or missed the active budget");
        require(world.players().at(second).signals.size() == s.signals.size(), "Expired pings accumulated on server");
        require(s.signals.back().expires == world.tick() + 360, "New ping did not get its own six-second lifetime");
        encode(s);
    }
}

}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected synthetic map"); wire(); geometry(); server(argv[1]);
        std::cout << "Signal wire, wheel, ray, frustum and shared lifetime checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
