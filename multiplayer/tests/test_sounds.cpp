#include "server/sounds.hpp"
#include "server/world.hpp"
#include "client/weapon_feedback.hpp"
#include "client/sound_cues.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool quiet(const WeaponFeedback& feedback) { return feedback.sounds.empty() && !feedback.firearmSlot; }
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}
void events() {
    ServerSounds server;
    server.emit(100, SoundKind::shot, 1, {2, 0, 0});
    require(server.forListener(100, 1, {}).empty(), "Owner received its own predicted sound");
    require(server.forListener(100, 2, {}).size() == 1, "Nearby listener missed an authoritative sound");
    require(server.forListener(100, 2, {43, 0, 0}).size() == 1,
            "Far observer lost the shot event needed for remote weapon visuals");
    server.emit(101, SoundKind::heard, 0, {1, 0, 0});
    require(server.forListener(101, 1, {}).size() == 1, "Zombie zero was mistaken for a player-owned sound");
    require(server.forListener(101, 2, {43, 0, 0}).size() == 1,
            "A non-firearm sound bypassed its hearing range");
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
void remoteShotPlayback() {
    RemoteSoundCues client;
    Snapshot initial; initial.tick = 100;
    initial.sounds.push_back({1, 100, SoundKind::shot, 2, 0, {1, 0, 0}});
    std::vector<SoundEvent> shots(1);
    require(client.receive(initial, 1, &shots, 100.0 / 60).empty() && shots.empty(),
            "Join replayed an old remote shot or retained an old visual output");

    Snapshot next = initial; next.tick = 106;
    next.sounds.push_back({2, 104, SoundKind::shot, 2, 2, {2, 3, 4}});
    next.sounds.push_back({3, 104, SoundKind::shot, 1, 0, {}});
    next.sounds.push_back({4, 105, SoundKind::reload, 2, 1, {}});
    const auto early = client.receive(next, 1, &shots, 103.0 / 60);
    require(early.size() == 1 && early[0].effect == viewer::AudioEffect::reloadRifle && shots.empty(),
            "A remote shot fired ahead of its render time or delayed unrelated audio");
    require(client.receive(next, 1, &shots, 103.5 / 60).empty() && shots.empty(),
            "A held shot played early or an unrelated cue repeated");
    const auto due = client.receive(next, 1, &shots, 104.0 / 60);
    require(due.size() == 1 && due[0].effect == viewer::AudioEffect::shotgun &&
            due[0].position == glm::vec3(2, 3, 4) && shots.size() == 1 &&
            shots[0].id == 2 && shots[0].source == 2 && shots[0].weapon == 2 &&
            shots[0].tick == 104 && shots[0].position == due[0].position,
            "Remote shot sound and visual event did not become due together with the same identity");
    require(client.receive(next, 1, &shots, 105.0 / 60).empty() && shots.empty(),
            "Repeated snapshots replayed remote shot feedback or retained its previous visual output");

    auto older = next; older.tick = 105;
    older.sounds.push_back({5, 105, SoundKind::shot, 3, 0, {}});
    require(client.receive(older, 1, &shots, 105.0 / 60).empty() && shots.empty(),
            "An out-of-order snapshot introduced remote shot feedback");
    Snapshot advanced; advanced.tick = 140;
    advanced.sounds.push_back({6, 120, SoundKind::shot, 3, 0, {}});
    require(client.receive(advanced, 1, &shots, 140.0 / 60).empty() && shots.empty(),
            "A stale remote shot produced sound or visual feedback after a stall");
    require(client.receive(next, 1, &shots, 140.0 / 60).empty() && shots.empty(),
            "An old snapshot replayed a shot after deduplication history expired");

    // The consumer can discard every audio cue without changing visual receipt;
    // this adapter needs neither an audio backend nor interpolated player state.
    RemoteSoundCues muted;
    muted.receive(initial, 1);
    (void)muted.receive(next, 1, &shots, 104.0 / 60);
    require(shots.size() == 1 && shots[0].id == 2,
            "A muted consumer lost remote firearm visuals");
}
void prediction(const viewer::Weapons& definitions, const viewer::PlayerInventory& inventory) {
    WeaponFeedbackPrediction client;
    PlayerState state; state.id = 1; state.slot = 0; state.loaded = 2; state.reserve = 120;
    client.reconcile(state);
    Command press; press.sequence = 1; press.left = true;
    const auto clip = viewer::AudioEffect::pistol;
    const auto first = client.advance(press, 0, definitions, inventory);
    require(first.sounds == std::vector<viewer::AudioEffect>{clip} && first.firearmSlot == 0,
            "Local shot did not produce matching sound and firearm feedback before acknowledgement");
    ++press.sequence;
    require(quiet(client.advance(press, 1, definitions, inventory)), "Held semi-automatic input repeated feedback");
    press.left = false; ++press.sequence; client.advance(press, 1, definitions, inventory);
    press.left = true; ++press.sequence;
    const auto second = client.advance(press, 1, definitions, inventory);
    require(second.sounds == std::vector<viewer::AudioEffect>{clip} && second.firearmSlot == 0,
            "Second valid click did not produce firearm feedback");
    press.left = false; ++press.sequence; client.advance(press, 2, definitions, inventory);
    press.left = true; ++press.sequence;
    const auto empty = client.advance(press, 2, definitions, inventory);
    require(empty.sounds == std::vector<viewer::AudioEffect>{viewer::AudioEffect::emptyClick} && !empty.firearmSlot,
            "An empty predicted magazine fired or lost its click");
    ++press.sequence;
    require(quiet(client.advance(press, 3, definitions, inventory)), "Held empty trigger repeated feedback");
    state.resolved = press.sequence; state.loaded = 0; state.shots = 2; client.reconcile(state);
    require(quiet(client.advance(press, 4, definitions, inventory)), "Delayed confirmation replayed local shot feedback");
    press.left = false; press.reload = true; ++press.sequence;
    const auto reload = client.advance(press, 4, definitions, inventory);
    require(reload.sounds == std::vector<viewer::AudioEffect>{viewer::AudioEffect::reload} && !reload.firearmSlot,
            "Eligible reload produced firearm feedback or lost its immediate sound");
    press.reload = false; press.left = true; ++press.sequence;
    require(quiet(client.advance(press, 5, definitions, inventory)), "Predicted reload allowed an early shot");

    WeaponFeedbackPrediction automatic;
    state.slot = 1; state.loaded = 3; state.resolved = 0; automatic.reconcile(state);
    Command smg; smg.slot = 1; smg.left = true; smg.sequence = 1;
    const auto start = automatic.advance(smg, 0, definitions, inventory);
    require(start.sounds == std::vector<viewer::AudioEffect>{viewer::AudioEffect::smg} && start.firearmSlot == 1,
            "Automatic weapon did not produce immediate firearm feedback");
    ++smg.sequence;
    require(quiet(automatic.advance(smg, .001, definitions, inventory)), "Automatic fire ignored cooldown");
    ++smg.sequence;
    const auto continued = automatic.advance(smg, 1, definitions, inventory);
    require(continued.sounds.size() == 1 && continued.firearmSlot == 1,
            "Automatic weapon did not continue feedback while held");
    smg.interaction = true; ++smg.sequence;
    require(quiet(automatic.advance(smg, 2, definitions, inventory)), "Interaction input fired weapon feedback");
}
void feedbackExclusions(const viewer::Weapons& definitions, const viewer::PlayerInventory& inventory) {
    PlayerState state; state.id = 1; state.loaded = 2;
    Command press; press.sequence = 1; press.left = true;
    const auto excluded = [&](const PlayerState& authoritative, const Command& input, const char* message) {
        WeaponFeedbackPrediction client;
        client.reconcile(authoritative);
        require(quiet(client.advance(input, 0, definitions, inventory)), message);
    };
    auto changed = state; changed.vehicle = 0;
    excluded(changed, press, "Seated input produced firearm feedback");
    changed = state; changed.weaponSwitchRemaining = .5f;
    excluded(changed, press, "Weapon switch produced firearm feedback");
    changed = state; changed.reloadRemaining = .5f;
    excluded(changed, press, "Active reload produced firearm feedback");
    auto input = press; input.slot = 1;
    excluded(state, input, "Unconfirmed equipment selection produced firearm feedback");
    input = press; input.interaction = true;
    excluded(state, input, "Interaction click produced firearm feedback");
    changed = state; changed.slot = static_cast<std::uint8_t>(viewer::punchSlot);
    input = press; input.slot = changed.slot;
    excluded(changed, input, "Melee produced firearm feedback");
    changed = state; changed.slot = 4; changed.loaded = 1;
    input = press; input.slot = 4;
    require(bool(definitions.state(4).config.grenade), "Grenade exclusion fixture has no grenade");
    excluded(changed, input, "Grenade throw produced firearm feedback");

    // Pellet count belongs to hit reporting. Presentation gets one shell event.
    require(definitions.state(2).config.pellets > 1, "Shotgun feedback fixture has no pellets");
    WeaponFeedbackPrediction shotgun;
    changed = state; changed.slot = 2; changed.loaded = 1; shotgun.reconcile(changed);
    input = press; input.slot = 2;
    const auto shell = shotgun.advance(input, 0, definitions, inventory);
    require(shell.sounds == std::vector<viewer::AudioEffect>{viewer::AudioEffect::shotgun} && shell.firearmSlot == 2,
            "Shotgun shell did not produce exactly one sound and firearm event");
    ++input.sequence;
    require(quiet(shotgun.advance(input, 1, definitions, inventory)), "Held shotgun repeated its shell feedback");

    WeaponFeedbackPrediction sniper;
    changed = state; changed.slot = 3; changed.loaded = 1; sniper.reconcile(changed);
    input = press; input.slot = 3;
    const auto scoped = sniper.advance(input, 0, definitions, inventory);
    require(scoped.sounds == std::vector<viewer::AudioEffect>{viewer::AudioEffect::sniper} && scoped.firearmSlot == 3,
            "Sniper shot lost its own firearm slot or sound");
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
        events(); playback(); remoteShotPlayback();
        Options options; options.map = argv[1]; options.trees = options.forests = options.cars = false; options.zombieCount = 0;
        Scene scene(options); prediction(scene.initialWeapons, scene.initialInventory);
        feedbackExclusions(scene.initialWeapons, scene.initialInventory);
        authority(options);
        std::cout << "Sound wire, bounded history, filtering, deduplication and immediate prediction passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
