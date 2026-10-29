#include "server/world.hpp"
#include "box_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace multiplayer;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F run, const char* message) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}

FirearmHit head() {
    FirearmHit report;
    report.hit.zombie = 0; report.hit.bone = 1; report.hit.headshot = true;
    return report;
}
Command fire(std::uint64_t sequence, std::uint8_t slot = 0, bool hit = true) {
    Command command;
    command.sequence = sequence; command.slot = slot; command.left = true;
    command.movement.yawDegrees = -90;
    command.firearmShot = FirearmShot{slot, hit ? std::vector<FirearmHit>{head()} : std::vector<FirearmHit>{}};
    return command;
}
Command release(std::uint64_t sequence, std::uint8_t slot = 0) {
    Command command; command.sequence = sequence; command.slot = slot;
    return command;
}
void advance(World& world, int ticks = 30) { for (int i = 0; i < ticks; ++i) world.step(); }
void send(World& world, std::uint32_t player, const Command& command) {
    world.submit(player, Commands{{command}}); world.step();
}

void openScene(Scene& scene, glm::vec3 feet = {0, 0, -8}) {
    viewer::Model model; viewer::Primitive floor;
    for (const auto point : {glm::vec3(-1200, 0, -1200), {-1200, 0, 1200},
                             {1200, 0, 1200}, {1200, 0, -1200}}) {
        viewer::Vertex vertex; vertex.position = point; floor.vertices.push_back(vertex);
    }
    floor.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(floor);
    viewer::DrawInstance draw; draw.name = "terrain"; model.draws.push_back(draw);
    model.boundsMin = {-1200, 0, -1200}; model.boundsMax = {1200, 8, 1200};
    scene.model = std::move(model);
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model);
    scene.spawnEye = {0, 1.7f, 0}; scene.spawnOrientation = {-90, 0};
    scene.vehicles->add({60, .68f, 60});
    scene.zombies = viewer::ZombieLayer{};
    auto& asset = scene.zombies.assets.emplace_back();
    asset.model = viewer::BoxLayer::unitCube(); asset.model.draws.emplace_back();
    for (auto& vertex : asset.model.primitives[0].vertices)
        vertex.position = vertex.position * glm::vec3(.4f, 1.8f, .4f) + glm::vec3(0, .9f, 0);
    asset.model.boundsMin = {-.2f, 0, -.2f}; asset.model.boundsMax = {.2f, 1.8f, .2f};
    asset.skeleton.resize(2);
    asset.skeleton[0].name = "torso";
    asset.skeleton[1].name = "head"; asset.skeleton[1].parent = 0;
    asset.skeleton[1].referenceWorld = glm::translate(glm::mat4(1), {0, 1.65f, 0});
    asset.ragdollBones[std::size_t(viewer::RagdollPart::head)] = 1;
    asset.ragdollBones[std::size_t(viewer::RagdollPart::torso)] = 0;
    asset.frameCount = 2; asset.duration = 1;
    asset.boneFrames = {glm::mat4(1), asset.skeleton[1].referenceWorld,
                        glm::mat4(1), asset.skeleton[1].referenceWorld};
    auto& zombie = scene.zombies.instances.emplace_back();
    zombie.restRoot = zombie.restTransform = zombie.transform = glm::translate(glm::mat4(1), feet);
    scene.zombies.setPerceptionEnabled(false);
    scene.zombies.rebuildSpatialIndex();
}

void protocolReports() {
    auto command = fire(7, 3);
    command.firearmShot->hits[0].hit.point = {.1f, -.2f, .3f};
    command.firearmShot->hits[0].hit.woundRegion = viewer::ZombieWoundRegion::head;
    const auto bytes = encode(Commands{{command}});
    const auto copy = std::get<Commands>(decode(bytes.data(), bytes.size())).commands.front();
    require(copy.sequence == 7 && copy.firearmShot && copy.firearmShot->slot == 3 &&
            copy.firearmShot->hits[0].hit.bone == 1 && copy.firearmShot->hits[0].hit.joint == -1 &&
            copy.firearmShot->hits[0].hit.point == glm::vec3(.1f, -.2f, .3f) &&
            copy.firearmShot->hits[0].hit.woundRegion == viewer::ZombieWoundRegion::head,
        "Command round trip lost its trusted shot identity or local contact");
    auto miss = fire(1, 0, false);
    const auto missBytes = encode(Commands{{miss}});
    require(std::get<Commands>(decode(missBytes.data(), missBytes.size())).commands[0].firearmShot->hits.empty(),
        "Explicit miss was lost in the protocol");
    auto maximum = fire(1, 2);
    maximum.firearmShot->hits.resize(maxFirearmHits, head());
    for (std::size_t pellet = 0; pellet < maxFirearmHits; ++pellet)
        maximum.firearmShot->hits[pellet].pellet = static_cast<std::uint8_t>(pellet);
    const auto maximumBytes = encode(Commands{{maximum}});
    require(std::get<Commands>(decode(maximumBytes.data(), maximumBytes.size())).commands[0].firearmShot->hits.size() == maxFirearmHits,
        "Protocol did not preserve the supported maximum pellet count");
    maximum.firearmShot->hits.push_back(head());
    rejects([&] { encode(Commands{{maximum}}); }, "An oversized pellet list was accepted");
    auto invalid = command; invalid.left = false;
    rejects([&] { encode(Commands{{invalid}}); }, "Shot without firing input was accepted");
    invalid = command; invalid.firearmShot->slot = 4;
    rejects([&] { encode(Commands{{invalid}}); }, "Grenade slot was accepted as a firearm report");
    invalid = command; invalid.firearmShot->hits.push_back(invalid.firearmShot->hits[0]);
    rejects([&] { encode(Commands{{invalid}}); }, "Duplicate pellet was accepted");
    invalid = command; invalid.firearmShot->hits[0].hit.point.x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { encode(Commands{{invalid}}); }, "Nonfinite local hit point was accepted");
    invalid = command; invalid.firearmShot->hits[0].hit.bone = static_cast<std::int16_t>(maxBones);
    rejects([&] { encode(Commands{{invalid}}); }, "Out-of-range hit bone was accepted");
    invalid = command; invalid.firearmShot->hits[0].hit.zombie = static_cast<std::uint32_t>(maxZombies);
    rejects([&] { encode(Commands{{invalid}}); }, "Out-of-range zombie identity was accepted");
    invalid = command; invalid.firearmShot->hits[0].hit.woundRegion = static_cast<viewer::ZombieWoundRegion>(9);
    rejects([&] { encode(Commands{{invalid}}); }, "Invalid wound region was accepted");
    rejects([&] { decode(bytes.data(), bytes.size() - 1); }, "Truncated firearm report was accepted");
}

void movedTargetsAndCommandOrder(const Options& options) {
    Scene scene(options); openScene(scene);
    World world(scene); const auto player = world.join(); advance(world, 4);
    // The displayed head was on the aiming ray. It has already moved off it
    // by the time this report reaches the server.
    auto& zombie = scene.zombies.instances[0];
    zombie.restRoot = zombie.restTransform = zombie.transform = glm::translate(glm::mat4(1), {40, 0, -8});
    scene.zombies.rebuildSpatialIndex();
    auto shot = fire(1);
    world.submit(player, Commands{{shot}}); world.submit(player, Commands{{shot}}); world.step();
    require(world.players().at(player).shots == 1 && scene.zombies.damage().injury(0) &&
            scene.zombies.damage().injury(0)->health == 8,
        "Trusted head hit was retested against the current server ray");
    advance(world);
    world.submit(player, Commands{{shot}}); advance(world);
    require(world.players().at(player).shots == 1 && scene.zombies.damage().injury(0)->health == 8,
        "Duplicate or held command repeated a reported hit");
    send(world, player, release(2));
    auto later = fire(4); later.movement.yawDegrees = 90;
    world.submit(player, Commands{{later}});
    world.step();
    require(world.players().at(player).resolved == 2, "Out-of-order shot skipped a missing command");
    world.submit(player, Commands{{release(3)}}); advance(world, 2);
    require(world.players().at(player).resolved == 4 && world.players().at(player).shots == 2 &&
            scene.zombies.damage().injury(0)->health == 6,
        "Queued command order lost or repeated its trusted hit");
}

void weaponRulesAndMisses(const Options& options) {
    Scene scene(options); openScene(scene);
    World world(scene); const auto player = world.join(); advance(world, 4);
    const auto& state = world.players().at(player);
    const int initialLoaded = state.weapons.equipped().loaded;
    require(initialLoaded >= 2, "Weapon-rule fixture needs at least two loaded rounds");
    send(world, player, fire(1, 0, false));
    require(state.shots == 1 && !scene.zombies.damage().injury(0) && state.weapons.equipped().loaded == initialLoaded - 1,
        "An explicit miss reran server hit detection or failed to consume ammo");
    advance(world);
    send(world, player, release(2));
    auto missing = fire(3); missing.firearmShot.reset(); send(world, player, missing);
    send(world, player, release(4));
    auto wrongWeapon = fire(5); wrongWeapon.firearmShot->slot = 3; send(world, player, wrongWeapon);
    send(world, player, release(6));
    auto excessive = fire(7); excessive.firearmShot->hits[0].pellet = 1; send(world, player, excessive);
    send(world, player, release(8));
    auto duplicate = fire(9); duplicate.firearmShot->hits.push_back(duplicate.firearmShot->hits[0]);
    send(world, player, duplicate);
    require(state.shots == 1 && state.weapons.equipped().loaded == initialLoaded - 1 && !scene.zombies.damage().injury(0),
        "Missing, mismatched or excessive reports bypassed authoritative shot acceptance");
    send(world, player, release(10)); send(world, player, fire(11));
    send(world, player, release(12)); send(world, player, fire(13));
    require(state.shots == 2 && state.weapons.equipped().loaded == initialLoaded - 2,
        "Trusted reports bypassed the weapon's firing interval");
    std::uint64_t sequence = 13;
    for (int tick = 0; tick < 30; ++tick) send(world, player, fire(++sequence));
    require(state.shots == 2, "An early rejected semiautomatic press fired later without release");
    while (state.weapons.equipped().loaded > 0) {
        send(world, player, release(++sequence)); advance(world);
        send(world, player, fire(++sequence, 0, false));
    }
    const auto shots = state.shots;
    send(world, player, release(++sequence)); advance(world);
    send(world, player, fire(++sequence, 0, false));
    require(state.shots == shots && state.weapons.equipped().loaded == 0,
        "Trusted reports fired an empty weapon");
}

void sniperWeaponBinding(const Options& options) {
    Scene scene(options); openScene(scene, {800, 0, 0});
    World world(scene); const auto player = world.join();
    // World exposes inspection as const; this fixture owns the mutable world
    // and establishes an equipped weapon before submitting any combat input.
    auto& state = const_cast<ServerPlayer&>(world.players().at(player));
    require(state.weapons.equip(3), "Sniper fixture could not equip its owned weapon");
    state.held.slot = 3;
    const int sniperLoaded = state.weapons.state(3).loaded, pistolLoaded = state.weapons.state(0).loaded;
    require(sniperLoaded > 0, "Sniper fixture needs a loaded round");
    advance(world, 4);
    auto shot = fire(1, 3);
    auto switchWeapon = release(2, 0); switchWeapon.movement.yawDegrees = 90;
    world.submit(player, Commands{{shot, switchWeapon, release(3, 0), release(4, 0)}});
    world.step();
    require(state.resolved == 2 && state.shots == 1 && state.weapons.state(3).loaded == sniperLoaded - 1 &&
            state.weapons.state(0).loaded == pistolLoaded && scene.zombies.damage().injury(0) &&
            scene.zombies.damage().injury(0)->health == 6,
        "A later weapon switch changed the accepted sniper shot or rejected its distant hit");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected synthetic map asset");
        Options options; options.map = argv[1]; options.zombieCount = 0;
        options.trees = options.forests = options.cars = false;
        protocolReports(); movedTargetsAndCommandOrder(options); weaponRulesAndMisses(options); sniperWeaponBinding(options);
        std::cout << "Trusted firearm reports, command order, misses, weapon rules and sniper binding passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
