#include "scene.hpp"
#include "client/snapshots.hpp"
#include "server/world.hpp"
#include "zombie_replication_fixture.hpp"
#include "session_settings.hpp"
#include "../../tests/house_fixture.hpp"
#include "../../tests/apartment_fixture.hpp"

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
void prefab(multiplayer::Scene& server, multiplayer::Scene& client) {
    for (auto* scene : {&server, &client}) {
        scene->floorplans.reset(); scene->collision.reset(); scene->doors.reset();
        scene->model = apartment_fixture::block(24,20,11);
        scene->doors = std::make_unique<viewer::DoorLayer>(viewer::prepareDoorPanels(scene->model), scene->physics);
        scene->collision = std::make_unique<viewer::CollisionWorld>(scene->model, std::vector<viewer::TrunkCollider>{}, scene->doors.get());
        scene->floorplans = std::make_unique<viewer::FloorplanLayer>(scene->model, *scene->doors, *scene->collision);
        scene->spawnEye = {0,1.7f,12};
    }
    multiplayer::World world(server);
    const auto player = world.join();
    server.doors->setAngle(0,.7f); world.step();
    require(server.floorplans->residentCount() == 1, "Server must stream a tall apartment block");
    const auto& authority = server.floorplans->resident().begin()->second;
    const auto upper = std::find_if(authority.doors.begin(), authority.doors.end(), [](const auto& d) { return d.storey == 10; });
    require(upper != authority.doors.end(), "Apartment fixture must reserve and install top-storey door IDs");
    server.doors->setAngle(upper->id,-.9f);
    multiplayer::Snapshots playback;
    require(playback.push(world.snapshot(player),1), "Apartment snapshot must be accepted");
    playback.apply(client,1); client.floorplans->update({client.spawnEye}); playback.apply(client,1);
    const auto& replica = client.floorplans->resident().begin()->second;
    require(replica.kind == viewer::HouseFloorplan::Kind::PrefabApartmentBlock && replica.rooms.size() == authority.rooms.size() &&
            replica.doors.size() == authority.doors.size() && replica.stairs.size() == 10,
            "Peers must independently generate the same eleven-storey apartment topology");
    for (std::size_t i = 0; i < authority.rooms.size(); ++i)
        require(authority.rooms[i].polygon == replica.rooms[i].polygon, "Apartment polygons must match exactly between peers");
    require(std::abs(client.doors->angle(upper->id)+.9f) < .001f,
            "Replicated angles must reach reserved apartment doors above the family-house storey limit");
    const auto id = upper->id;
    client.floorplans->update({}); client.floorplans->update({client.spawnEye}); playback.apply(client,1);
    require(std::abs(client.doors->angle(id)+.9f) < .001f,
            "Apartment door IDs and movement must survive independent client eviction/reentry");
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
void midnightEncounter(multiplayer::Scene& server) {
    std::vector<viewer::InteriorZombiePlacement> placements;
    std::vector<std::size_t> upstairs;
    for (int n = 0; n < 1000 && upstairs.empty(); ++n) {
        const auto name = "building_encounter_" + std::to_string(n);
        if (!viewer::hasInteriorZombieEncounter(name)) continue;
        house(server, name);
        server.doors->setAngle(1, .7f);
        server.floorplans->update({server.spawnEye});
        const auto& plan = server.floorplans->resident().begin()->second;
        placements = viewer::placeInteriorZombies(plan, *server.doors, *server.collision);
        for (std::size_t id = 0; id < placements.size(); ++id)
            if (plan.rooms.at(placements[id].room).storey > 0) upstairs.push_back(id);
    }
    require(!upstairs.empty(), "Midnight server fixture must place an upstairs encounter");
    server.zombies = viewer::ZombieLayer{};
    server.zombies.assets.emplace_back(); server.zombies.instances.resize(placements.size());
    for (auto& actor : server.zombies.instances) actor.removed = true;
    server.zombies.rebuildSpatialIndex();
    server.interiorZombies = std::make_unique<viewer::InteriorZombies>(0, placements.size());
    server.dayNight = {23, 1}; // Midnight lies between the second and third 60 Hz ticks.
    multiplayer::World world(server);
    world.join();
    world.step(); world.step();
    for (const auto id : upstairs)
        require(server.zombies.behaviors.find(id) && server.zombies.behaviors.find(id)->state == viewer::ZombieState::midnight,
                "Authoritative upstairs actors must remain inactive before the configured midnight");
    world.step();
    for (const auto id : upstairs)
        require(server.zombies.behaviors.find(id) && server.zombies.behaviors.find(id)->state != viewer::ZombieState::midnight,
                "Server behavior must wake upstairs actors on its negotiated day/night clock, without an exact midnight tick");
}
void playerHouseMembership(multiplayer::Scene& server) {
    house(server, "building_house_cache");
    server.interiorZombies.reset(); server.zombies = viewer::ZombieLayer{};
    server.spawnEye = {0, 1.7f, 0};
    multiplayer::World world(server);
    const auto inside = world.join();
    // Safe joining deliberately relocates indoor spawns outdoors. Put this
    // observer inside explicitly to exercise membership before doors open.
    auto& observer = const_cast<multiplayer::ServerPlayer&>(world.players().at(inside));
    observer.controller.teleport({0, .01f, 0});
    observer.camera.setPosition(observer.controller.eyePosition());
    world.step();
    require(world.players().at(inside).house.house() == std::optional<std::string>("building_house_cache"),
            "Authoritative players must expose cached building IDs even before an interior is generated");
    server.spawnEye = {0, 1.7f, 8};
    const auto outside = world.join(); world.step();
    require(world.players().at(outside).house.outside() && !world.players().at(outside).house.house(),
            "Authoritative outside players must expose an explicit empty house membership");
}
void playerScentEmission(multiplayer::Scene& server) {
    for (const bool inside : {false,true}) {
        house(server,"building_scent_cache");
        server.interiorZombies.reset(); server.zombies = viewer::ZombieLayer{};
        server.dogs = viewer::DogLayer{};
        // Keep a dog population to enable its session-owned scent field; no
        // living actor should attack or alter this emission-only fixture.
        server.dogs.instances.resize(1); server.dogs.instances[0].removed = true;
        multiplayer::World world(server); const auto id = world.join();
        auto& player = const_cast<multiplayer::ServerPlayer&>(world.players().at(id));
        if (inside) {
            player.controller.teleport({0,.01f,0});
            player.camera.setPosition(player.controller.eyePosition());
        }
        player.vitals.applyDamage(1);
        for (int tick = 0; tick < 65; ++tick) world.step();
        require(player.house.outside() != inside && player.vitals.bleeding(),
                "Scent fixture must retain its authoritative bleeding and house states");
        require(inside ? server.dogs.scent().activeCells() == 0 : server.dogs.scent().at(player.controller.feet()) > 0,
                "Only an authoritative bleeding outdoor player may emit scent");
        if (!inside) {
            const float before = server.dogs.scent().at(player.controller.feet());
            player.vitals.setBleeding(false);
            for (int tick = 0; tick < 60; ++tick) world.step();
            require(server.dogs.scent().at(player.controller.feet()) < before,
                    "Clearing the wound must stop deposits while old scent continues decaying");
        }
    }
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
        irregular(server, client); prefab(server,client); encounters(server, client);
        midnightEncounter(server);
        playerHouseMembership(server);
        playerScentEmission(server);
        std::cout << "Independent multiplayer floorplan and stable door playback tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
