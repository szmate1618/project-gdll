#include "scene.hpp"
#include "client/snapshots.hpp"
#include "server/world.hpp"
#include "../../tests/house_fixture.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void house(multiplayer::Scene& scene) {
    scene.floorplans.reset(); scene.collision.reset(); scene.doors.reset();
    scene.model = house_fixture::house();
    for (auto& draw : scene.model.draws) if (draw.name.rfind("building_house", 0) == 0)
        draw.name.replace(0, 14, "building_render_0");
    scene.doors = std::make_unique<viewer::DoorLayer>(viewer::prepareDoorPanels(scene.model), scene.physics);
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model, std::vector<viewer::TrunkCollider>{}, scene.doors.get());
    scene.floorplans = std::make_unique<viewer::FloorplanLayer>(scene.model, *scene.doors, *scene.collision);
    scene.spawnEye = {0, 1.7f, 8};
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
        std::cout << "Independent multiplayer floorplan and stable door playback tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
