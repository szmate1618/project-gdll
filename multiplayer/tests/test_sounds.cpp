#include "server/sounds.hpp"
#include "server/world.hpp"
#include "client/weapon_audio.hpp"
#include "client/sound_cues.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}
void events() {
    ServerSounds server;
    server.emit(100, SoundKind::shot, 1, {2, 0, 0});
    require(server.forListener(100, 1, {}).empty(), "Owner received its own predicted sound");
    require(server.forListener(100, 2, {}).size() == 1, "Nearby listener missed an authoritative sound");
    require(server.forListener(100, 2, {43, 0, 0}).empty(), "Far listener received an inaudible sound");
    server.emit(101, SoundKind::heard, 0, {1, 0, 0});
    require(server.forListener(101, 1, {}).size() == 1, "Zombie zero was mistaken for a player-owned sound");
    require(server.forListener(130, 2, {}).size() == 2, "Repeated sound history expired early");
    server.expire(132);
    require(server.forListener(132, 2, {}).empty(), "Old sound history did not expire");
    for (std::size_t i = 0; i < maxSoundEvents + 50; ++i) server.emit(200, SoundKind::shot, 1, {});
    require(server.forListener(200, 2, {}).size() == maxSoundEvents, "Sound history grew without bound");

    Snapshot snapshot; snapshot.tick = 200; snapshot.sounds = server.forListener(200, 2, {});
    const auto bytes = encode(snapshot);
    const auto copy = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
    require(copy.sounds.size() == maxSoundEvents && copy.sounds.front().id == snapshot.sounds.front().id &&
            copy.sounds.back().position == snapshot.sounds.back().position, "Wire lost sound identity or position");
    auto bad = snapshot; bad.sounds.push_back(bad.sounds.front());
    rejects([&] { encode(bad); }, "Oversized event batch accepted");
    bad = snapshot; bad.sounds[1].id = bad.sounds[0].id;
    rejects([&] { encode(bad); }, "Duplicate event IDs accepted");
    bad = snapshot; bad.sounds[0].tick = 201;
    rejects([&] { encode(bad); }, "Future event accepted");
    bad = snapshot; bad.sounds[0].tick = 169;
    rejects([&] { encode(bad); }, "Expired wire event accepted");
    bad = snapshot; bad.sounds[0].kind = static_cast<SoundKind>(255);
    rejects([&] { encode(bad); }, "Unknown effect accepted");
    bad = snapshot; bad.sounds[0].weapon = 4;
    rejects([&] { encode(bad); }, "A grenade was accepted as firearm audio");
    bad = snapshot; bad.sounds[0].position.x = std::numeric_limits<float>::infinity();
    rejects([&] { encode(bad); }, "Nonfinite sound source accepted");
}
void playback() {
    RemoteSoundCues client;
    Snapshot initial; initial.tick = 100;
    initial.sounds.push_back({1, 100, SoundKind::shot, 2, 0, {1, 0, 0}});
    initial.explosions.push_back({1, {}, .1f});
    require(client.receive(initial, 1).empty(), "Join replayed preexisting sounds/explosions");
    Snapshot next = initial; next.tick = 103;
    next.sounds.push_back({2, 102, SoundKind::shot, 2, 1, {2, 3, 4}});
    next.sounds.push_back({3, 102, SoundKind::shot, 1, 0, {}});
    auto cues = client.receive(next, 1);
    require(cues.size() == 1 && cues[0].effect == viewer::AudioEffect::smg && cues[0].position == glm::vec3(2,3,4),
            "Remote shot clip/source or owner suppression is wrong");
    next.tick = 106;
    require(client.receive(next, 1).empty(), "Repeated snapshot replayed a sound");
    next.sounds.push_back({4, 104, SoundKind::chase, 0, 0, {3, 0, 0}});
    cues = client.receive(next, 1);
    require(cues.size() == 1 && cues[0].zombie == 0 && cues[0].priority == 3,
            "An event retained across a lost snapshot failed to recover its reaction");
    next.tick = 125; next.sounds.push_back({5, 105, SoundKind::reload, 2, 2, {}});
    next.explosions = {{2, {}, .1f}, {3, {}, 1.f}};
    cues = client.receive(next, 1);
    require(cues.size() == 1 && cues[0].effect == viewer::AudioEffect::explosion,
            "Stale reactions/reloads/explosions played after a stall");
    require(client.receive(next, 1).empty(), "Explosion replayed from a repeated snapshot");
}
void prediction(const viewer::Weapons& definitions, const viewer::PlayerInventory& inventory) {
    WeaponAudioPrediction client;
    PlayerState state; state.id = 1; state.slot = 0; state.loaded = 2; state.reserve = 120;
    client.reconcile(state);
    Command press; press.sequence = 1; press.left = true;
    const auto clip = viewer::AudioEffect::pistol;
    require(client.advance(press, 0, definitions, inventory) == std::vector<viewer::AudioEffect>{clip},
            "Local shot did not play before any server acknowledgement");
    ++press.sequence;
    require(client.advance(press, 1, definitions, inventory).empty(), "Held semi-automatic input repeated a sound");
    press.left = false; ++press.sequence; client.advance(press, 1, definitions, inventory);
    press.left = true; ++press.sequence;
    require(client.advance(press, 1, definitions, inventory).size() == 1, "Second valid click did not play");
    press.left = false; ++press.sequence; client.advance(press, 2, definitions, inventory);
    press.left = true; ++press.sequence;
    require(client.advance(press, 2, definitions, inventory) == std::vector<viewer::AudioEffect>{viewer::AudioEffect::emptyClick},
            "Unacknowledged predicted shots did not consume the presentation magazine");
    ++press.sequence;
    require(client.advance(press, 3, definitions, inventory).empty(), "Held empty trigger clicked repeatedly");
    state.resolved = press.sequence; state.loaded = 0; client.reconcile(state);
    require(client.advance(press, 4, definitions, inventory).empty(), "Server confirmation replayed a local shot/click");
    press.left = false; press.reload = true; ++press.sequence;
    require(client.advance(press, 4, definitions, inventory) == std::vector<viewer::AudioEffect>{viewer::AudioEffect::reload},
            "Eligible reload did not play immediately");
    press.reload = false; press.left = true; ++press.sequence;
    require(client.advance(press, 5, definitions, inventory).empty(), "Predicted reload allowed an early shot");

    WeaponAudioPrediction automatic;
    state.slot = 1; state.loaded = 3; state.resolved = 0; automatic.reconcile(state);
    Command smg; smg.slot = 1; smg.left = true; smg.sequence = 1;
    require(automatic.advance(smg, 0, definitions, inventory).size() == 1, "Automatic weapon did not start immediately");
    ++smg.sequence;
    require(automatic.advance(smg, .001, definitions, inventory).empty(), "Automatic fire ignored cooldown");
    ++smg.sequence;
    require(automatic.advance(smg, 1, definitions, inventory).size() == 1, "Automatic weapon did not continue while held");
    smg.interaction = true; ++smg.sequence;
    require(automatic.advance(smg, 2, definitions, inventory).empty(), "Interaction input fired a weapon sound");
}
void authority(const Options& options) {
    Scene scene(options);
    World world(scene);
    const auto shooter = world.join(), listener = world.join();
    Command shot; shot.sequence = 1; shot.left = true;
    shot.firearmShot = FirearmShot{};
    Command release; release.sequence = 2;
    Command idle; idle.sequence = 3;
    world.submit(shooter, Commands{{shot, release, idle}});
    for (int i = 0; i < 8; ++i) world.step();
    const auto own = world.snapshot(shooter), remote = world.snapshot(listener);
    require(own.sounds.empty() && remote.sounds.size() == 1 && remote.sounds[0].kind == SoundKind::shot &&
            remote.sounds[0].source == shooter, "World firing did not emit only a remote event");
    Command reload; reload.sequence = 4; reload.reload = true;
    world.submit(shooter, Commands{{reload}}); world.step();
    bool found = false;
    for (const auto& event : world.snapshot(listener).sounds) found |= event.kind == SoundKind::reload;
    require(found && world.snapshot(shooter).sounds.empty(), "World reload sound did not preserve owner suppression");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test map");
        events(); playback();
        Options options; options.map = argv[1]; options.trees = options.forests = options.cars = false; options.zombieCount = 0;
        Scene scene(options); prediction(scene.initialWeapons, scene.initialInventory);
        authority(options);
        std::cout << "Sound wire, bounded history, filtering, deduplication and immediate prediction passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
