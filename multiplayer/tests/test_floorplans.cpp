#include "scene.hpp"
#include "client/snapshots.hpp"
#include "server/world.hpp"
#include "zombie_replication_fixture.hpp"
#include "session_settings.hpp"
#include "../../tests/house_fixture.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void house(multiplayer::Scene& scene, const std::string& name = "building_render_0", bool irregular = false) {
    scene.floorplans.reset(); scene.collision.reset(); scene.doors.reset();
    scene.model = irregular ? house_fixture::outlined({{-5,-6},{-5,6},{2,6},{2,3},{5,3},{5,-6}}) : house_fixture::house();
    for (auto& draw : scene.model.draws) if (draw.name.rfind("building_house", 0) == 0)
        draw.name.replace(0, 14, name);
    scene.doors = std::make_unique<viewer::DoorLayer>(viewer::prepareDoorPanels(scene.model), scene.physics);
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model, std::vector<viewer::TrunkCollider>{}, scene.doors.get());
    scene.floorplans = std::make_unique<viewer::FloorplanLayer>(scene.model, *scene.doors, *scene.collision);
    scene.spawnEye = {0, 1.7f, 8};
}
void irregular(multiplayer::Scene& server, multiplayer::Scene& client) {
    house(server, "building_irregular", true); house(client, "building_irregular", true);
    multiplayer::World world(server); const auto player = world.join();
    server.doors->setAngle(0, .7f); world.step();
    require(server.floorplans->residentCount() == 1, "Server must generate a near-orthogonal concave house");
    multiplayer::Snapshots playback;
    require(playback.push(world.snapshot(player), 1), "Irregular house snapshot must be accepted");
    playback.apply(client, 1); client.floorplans->update({client.spawnEye});
    const auto& a = server.floorplans->resident().begin()->second; const auto& b = client.floorplans->resident().begin()->second;
    require(a.footprint == b.footprint && a.footprint.size() == 6 && a.rooms.size() == b.rooms.size() && a.doors.size() == b.doors.size(),
            "Peers must independently produce the same concave outline, rooms and reserved door IDs");
    for (std::size_t i = 0; i < a.structure.primitives.size(); ++i) {
        const auto& first = a.structure.primitives[i]; const auto& second = b.structure.primitives[i];
        require(first.indices == second.indices && first.vertices.size() == second.vertices.size(), "Peers must generate identical irregular topology");
        for (std::size_t j = 0; j < first.vertices.size(); ++j)
            require(first.vertices[j].position == second.vertices[j].position, "Peers must generate identical clipped coordinates");
    }
    world.leave(player); world.step();
}
void encounters(multiplayer::Scene& server, multiplayer::Scene& client) {
    std::string name;
    for (int n = 0; n < 1000; ++n) {
        name = "building_encounter_" + std::to_string(n);
        if (viewer::hasInteriorZombieEncounter(name)) break;
    }
    house(server, name); house(client, name);
    constexpr std::size_t outdoor = 1, total = outdoor + viewer::interiorZombieReserve;
    for (auto* scene : {&server, &client}) {
        scene->zombies = viewer::ZombieLayer{};
        scene->zombies.assets.emplace_back(); scene->zombies.instances.resize(total);
        for (auto& actor : scene->zombies.instances) actor.removed = true;
    }
    server.zombies.rebuildSpatialIndex(); server.zombies.setPerceptionEnabled(false);
    server.interiorZombies = std::make_unique<viewer::InteriorZombies>(outdoor);
    const auto settings = multiplayer::sessionSettings(multiplayer::Options{}, server);
    const auto configured = multiplayer::applySessionSettings(multiplayer::Options{}, settings);
    require(settings.zombieCount == total && configured.replicaZombieCount == total,
            "Session discovery must advertise ordinary actors plus the shared reserve exactly once");
    multiplayer::World world(server);
    const auto player = world.join(); server.doors->setAngle(1, .7f); world.step();
    require(!server.zombies.instances[outdoor].removed, "Authoritative streaming must activate a reserved encounter actor");
    multiplayer::Snapshots playback;
    const auto deliver = [&](double seconds) {
        auto snapshot = world.snapshot(player);
        while (const auto setups = world.nextSetups(player)) {
            playback.pushSetups(client, *setups);
            world.setupsSent(player, *setups); world.acknowledgeSetups(player, test_replication::acknowledge(*setups));
        }
        test_replication::appendSamples(snapshot, world.snapshot(player));
        require(playback.push(snapshot, seconds), "Interior zombie movement snapshot must be accepted");
        playback.apply(client, seconds);
    };
    deliver(1);
    require(!client.zombies.instances[outdoor].removed && client.floorplans->residentCount() == 0 &&
            glm::distance(glm::vec3(client.zombies.instances[outdoor].restRoot[3]),
                          glm::vec3(server.zombies.instances[outdoor].restRoot[3])) < .02f,
            "A hidden client reserve must receive an actor without locally generating a floorplan");
    const auto root = server.zombies.instances[outdoor].restRoot;
    world.step();
    server.floorplans->update({});
    server.interiorZombies->update(*server.floorplans, server.zombies, *server.collision, {});
    require(server.zombies.instances[outdoor].removed, "Eviction must immediately remove sleepers from authoritative interest");
    deliver(2);
    playback.apply(client, 2 + multiplayer::zombieTimeoutSeconds);
    require(client.zombies.instances[outdoor].removed, "Sleeping interior actors must leave replicated interest");
    world.step(); deliver(8);
    require(!client.zombies.instances[outdoor].removed && server.zombies.instances[outdoor].restRoot == root &&
            client.zombies.instances.size() == total, "Reentry must reuse the same client/server actor slots");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Test map required");
        multiplayer::Options options; options.map = argv[1]; options.zombieCount = 0;
        options.trees = options.forests = options.cars = false;
        multiplayer::Scene server(options), client(options, {}, std::nullopt, multiplayer::SceneRole::Replica);
        house(server); house(client);
        multiplayer::World world(server);
        const auto player = world.join();
        server.doors->setAngle(1, .6f);
        world.step();
        require(server.floorplans->residentCount() == 1 && client.floorplans->residentCount() == 0,
                "Server must stream around authoritative players while the client remains independent");
        const auto& panel = server.floorplans->resident().begin()->second.doors.back();
        require(panel.storey == 1, "Replication fixture must exercise an upper-storey door");
        const auto door = panel.id;
        const float angle = panel.panel.inward ? -.8f : .8f;
        server.doors->setAngle(door, angle);
        const auto snapshot = world.snapshot(player);
        multiplayer::Snapshots playback;
        require(playback.push(snapshot, 1), "Server snapshot must be accepted");
        playback.apply(client, 1);
        require(!client.doors->present(door), "A distant replicated angle must not allocate interior geometry");
        client.floorplans->update({{0, 1.7f, 8}});
        playback.apply(client, 1);
        require(client.floorplans->residentCount() == 1 && client.doors->present(door) && std::abs(client.doors->angle(door) - angle) < .001f,
                "Locally generated stable door IDs must receive the existing replicated door angle");
        const auto& a = server.floorplans->renderModel(); const auto& b = client.floorplans->renderModel();
        require(a.draws.size() == b.draws.size() && a.primitives.size() == b.primitives.size(), "Peers must generate identical topology");
        for (std::size_t i = 0; i < a.primitives.size(); ++i) {
            require(a.primitives[i].indices == b.primitives[i].indices && a.primitives[i].vertices.size() == b.primitives[i].vertices.size(), "Peers must generate identical triangles");
            for (std::size_t j = 0; j < a.primitives[i].vertices.size(); ++j)
                require(a.primitives[i].vertices[j].position == b.primitives[i].vertices[j].position, "Peers must independently generate identical coordinates");
        }
        client.floorplans->update({{0, 1.7f, 100}}); playback.apply(client, 1);
        require(!client.doors->present(door) && server.doors->present(door), "Client eviction must not change authoritative geometry");
        client.floorplans->update({{0, 1.7f, 8}}); playback.apply(client, 1);
        require(std::abs(client.doors->angle(door) - angle) < .001f, "Regenerated client panels must recover current authoritative movement");
        world.leave(player); world.step();
        require(server.floorplans->residentCount() == 0, "The server must evict when no player remains nearby");
        irregular(server, client); encounters(server, client);
        std::cout << "Independent multiplayer floorplan and stable door playback tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
