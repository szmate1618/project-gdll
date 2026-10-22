#include "server/options.hpp"
#include "server/world.hpp"
#include "zombie_navigation.hpp"
#include "zombie_navmesh_paths.hpp"
#include "../../tests/navmesh_fixture.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function, const char* message) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

void options() {
    std::vector<std::string> arguments{"server", "map.glb", "--spawn", "13", "8",
        "--scenario", "horde scenario.toml", "--port", "27021", "--no-trees"};
    const auto original = arguments;
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    const auto parsed = multiplayer::parseServerOptions(static_cast<int>(pointers.size()), pointers.data());
    require(parsed.scenario == "horde scenario.toml" && parsed.scene.map == "map.glb" &&
        parsed.scene.port == 27021 && parsed.scene.spawn == glm::vec2(13, 8) && !parsed.scene.trees,
        "Server-only scenario option changed shared command arguments");
    require(arguments == original, "Server option parsing mutated caller arguments");
    rejects([&] { multiplayer::parseOptions(static_cast<int>(pointers.size()), pointers.data(), false); },
        "Scenario flag leaked into the unchanged client/shared parser");
    for (auto invalid : std::vector<std::vector<std::string>>{
            {"server", "--scenario"}, {"server", "--scenario", "--port", "27020"},
            {"server", "--scenario", "one", "--scenario", "two"},
            {"server", "--zombie-navmesh", "old.bin"}}) {
        pointers.clear();
        for (auto& argument : invalid) pointers.push_back(argument.data());
        rejects([&] { multiplayer::parseServerOptions(static_cast<int>(pointers.size()), pointers.data()); },
            "Malformed scenario or removed navigation flag was accepted");
    }
}

// The small cache is a real GDNAV02 binary, including its physical-clearance
// contract. It deliberately records a replay hash rather than claiming GLB identity.
void integer(std::vector<unsigned char>& bytes, std::size_t at, std::uint64_t value, unsigned count = 8) {
    for (unsigned byte = 0; byte < count; ++byte) bytes[at + byte] = static_cast<unsigned char>(value >> (8 * byte));
}
void real(std::vector<unsigned char>& bytes, std::size_t at, double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    integer(bytes, at, bits);
}
void writeMesh(const std::filesystem::path& path, const viewer::navmesh_path::Mesh& mesh,
               double radius = .3, double skin = .002) {
    const auto vertexBytes = mesh.vertices().size() * 16;
    std::size_t polygonBytes = 0;
    for (const auto polygon : mesh.polygons()) polygonBytes += 4 + polygon.edgeCount * 8;
    std::vector<unsigned char> bytes(128 + vertexBytes + polygonBytes);
    std::memcpy(bytes.data(), "GDNAV02\0", 8);
    integer(bytes, 8, mesh.vertices().size(), 4); integer(bytes, 12, mesh.polygons().size(), 4);
    real(bytes, 16, mesh.minimumX()); real(bytes, 24, mesh.minimumZ());
    real(bytes, 32, mesh.maximumX()); real(bytes, 40, mesh.maximumZ());
    real(bytes, 48, radius); real(bytes, 56, skin); real(bytes, 64, .05); real(bytes, 72, .005);
    integer(bytes, 80, vertexBytes); integer(bytes, 88, polygonBytes);
    std::fill(bytes.begin() + 96, bytes.begin() + 128, 0x6a);
    std::size_t at = 128;
    for (const auto vertex : mesh.vertices()) { real(bytes, at, vertex.x); real(bytes, at + 8, vertex.z); at += 16; }
    for (const auto polygon : mesh.polygons()) {
        integer(bytes, at, polygon.edgeCount, 4); at += 4;
        for (unsigned edge = 0; edge < polygon.edgeCount; ++edge, at += 4)
            integer(bytes, at, mesh.edges()[polygon.firstEdge + edge].vertex, 4);
        for (unsigned edge = 0; edge < polygon.edgeCount; ++edge, at += 4)
            integer(bytes, at, mesh.edges()[polygon.firstEdge + edge].neighbor, 4);
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(file), "Could not write the synthetic navigation cache");
}

viewer::Model ground() {
    viewer::Model model;
    viewer::Primitive primitive;
    for (const glm::vec3 position : {glm::vec3(-100, 0, -100), {-100, 0, 100}, {100, 0, 100}, {100, 0, -100}}) {
        viewer::Vertex vertex; vertex.position = position; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3}; model.primitives.push_back(primitive);
    viewer::DrawInstance draw; draw.name = "terrain"; model.draws.push_back(draw);
    model.boundsMin = {-100, 0, -100}; model.boundsMax = {100, 0, 100};
    return model;
}

std::unique_ptr<multiplayer::Scene> sceneFixture(const std::filesystem::path& asset,
                                               const std::vector<viewer::TrunkCollider>& trunks) {
    multiplayer::Options options;
    options.map = asset; options.trees = options.forests = false; options.zombieCount = 0;
    auto scene = std::make_unique<multiplayer::Scene>(options);
    require(scene->zombies.navigation() == nullptr, "Shared scene construction initialized client navigation");
    scene->model = ground();
    scene->collision = std::make_unique<viewer::CollisionWorld>(scene->model, trunks, scene->doors.get());
    scene->collision->setBoxes(scene->boxes.get()); scene->spawnEye = {13, 1.7f, 8};
    scene->zombies = viewer::ZombieLayer{};
    scene->zombies.assets.emplace_back();
    auto& rig = scene->zombies.assets.back();
    rig.skeleton.resize(1); rig.boneFrames = {glm::mat4(1), glm::mat4(1)};
    rig.frameCount = 2; rig.duration = 1;
    viewer::ZombieInstance actor;
    actor.transform = actor.restTransform = actor.restRoot =
        glm::rotate(glm::translate(glm::mat4(1), glm::vec3(3, 0, 8)), 1.57079632679f, {0, 1, 0});
    scene->zombies.instances.push_back(actor);
    scene->zombies.setPerceptionStaggering(false);
    scene->zombies.rebuildSpatialIndex();
    auto& behavior = scene->zombies.behaviors.create(0, 0);
    behavior.state = viewer::ZombieState::investigating;
    behavior.target = {13, 0, 8}; behavior.targetPlayer = 1;
    return scene;
}

double segmentDistance(glm::vec3 a, glm::vec3 b, glm::vec3 center) {
    const double x = double(b.x) - a.x, z = double(b.z) - a.z;
    const double squared = x * x + z * z;
    const double t = squared ? std::clamp(((double(center.x) - a.x) * x +
        (double(center.z) - a.z) * z) / squared, 0., 1.) : 0.;
    return std::hypot(double(a.x) + t * x - center.x, double(a.z) + t * z - center.z);
}

glm::vec3 scenarioTreeMotion(const std::filesystem::path& asset) {
    const viewer::TrunkCollider trunk{{8, 0, 8}, .6f, 5};
    auto scene = sceneFixture(asset, {trunk});
    const auto fingerprint = scene->fingerprint;
    auto cache = asset;
    cache.replace_extension(".navmesh");
    scene->zombies.enableNavigation(cache);
    scene->zombies.setScenarioDestinations({{13,0,8}});
    multiplayer::World world(*scene);
    require(scene->zombies.navigation(), "Server discarded the prepared scenario navigation");
    require(scene->fingerprint == fingerprint, "Server navigation changed the client content handshake");
    double minimum = 1000;
    float sideways = 0;
    for (int step = 0; step < 1000; ++step) {
        const glm::vec3 before(scene->zombies.instances[0].restRoot[3]);
        world.step();
        const glm::vec3 after(scene->zombies.instances[0].restRoot[3]);
        minimum = std::min(minimum, segmentDistance(before, after, trunk.base));
        sideways = std::max(sideways, std::abs(after.z - 8));
        require(glm::distance(before, after) <= 1.2f * multiplayer::tickSeconds + .0001f,
            "Authoritative navigation advanced more than once per tick");
        require(scene->zombies.navigation()->segmentClear(before, after),
            "Accepted server motion escaped the navmesh corridor");
    }
    require(minimum >= double(viewer::ZombieCrowd::bodyRadius) + trunk.radius,
        "Accepted authoritative swept movement intersected a trunk");
    const auto player = world.join();
    const auto snapshot = world.snapshot(player);
    require(snapshot.zombies.size() == 1 && snapshot.zombies[0].id == 0 &&
        snapshot.zombies[0].root == scene->zombies.instances[0].restRoot &&
        snapshot.zombies[0].transform == scene->zombies.instances[0].transform,
        "Snapshot did not publish the server's accepted navigation pose");
    {
        const auto* navigation = scene->zombies.navigation();
        require(navigation->stats().foundPaths > 0 && navigation->stats().steeringCalls > 0 &&
            navigation->stats().obstacleTests > 0 && navigation->stats().velocitySamples > 0,
            "Authoritative motion did not exercise independent paths and actual Detour tree sampling");
        require(navigation->stats().steeringCalls <= world.tick() && navigation->pathArenaBytes() == 10'000'000,
            "Server navigation duplicated updates or lost its fixed path arena");
        require(navigation->stats().requests == 1, "Scenario movement started another path search");
        require(sideways > .6f, "Scenario steering did not pass around the head-on trunk");
    }
    return glm::vec3(scene->zombies.instances[0].restRoot[3]);
}

void scenarioPopulation(const std::filesystem::path& asset, const std::filesystem::path& cache) {
    multiplayer::Options options;
    options.map = options.zombieAssets = asset;
    options.trees = options.forests = false;
    options.zombieCount = 1;
    unsigned calls = 0;
    const multiplayer::ZombiePopulationLoader loader = [&](const auto& source, const auto&, const auto&) {
        require(source == asset, "Authoritative population did not receive the selected zombie assets");
        ++calls;
        viewer::ZombieLayer layer;
        layer.assets.emplace_back();
        auto& rig = layer.assets.back();
        rig.skeleton.resize(1); rig.boneFrames = {glm::mat4(1), glm::mat4(1)};
        rig.frameCount = 2; rig.duration = 1;
        viewer::ZombieInstance actor;
        actor.transform = actor.restTransform = actor.restRoot =
            glm::translate(glm::mat4(1), glm::vec3(2 + calls, 0, 8));
        layer.instances.push_back(actor);
        layer.rebuildSpatialIndex();
        layer.enableNavigation(cache);
        layer.setScenarioDestinations({{13, 0, 8}});
        return layer;
    };
    multiplayer::Scene scene(options, loader);
    require(calls == 1 && scene.zombies.instances.size() == 1,
        "Scene did not initialize the authoritative scenario population exactly once");
    multiplayer::Scene sameContent(options, loader);
    require(scene.fingerprint == sameContent.fingerprint,
        "Scenario placement changed the shared client content fingerprint");
    const auto planned = scene.zombies.navigation()->pathFor(0);
    require(planned != 0, "Scenario did not have a route before starting the world");
    scene.model = ground();
    scene.collision = std::make_unique<viewer::CollisionWorld>(scene.model);
    scene.spawnEye = {12, 1.7f, 10};
    // A prepared scenario owns its mesh already. Starting the world must retain
    // its preplanned routes without loading a second mesh or requiring players.
    scene.mapPath = cache.parent_path() / "unneeded.glb";
    multiplayer::World world(scene);
    require(scene.zombies.navigation()->pathFor(0) == planned,
        "World startup discarded a preplanned scenario route");
    for (int step = 0; step < 240; ++step) world.step();
    const auto traveled = glm::vec3(scene.zombies.instances[0].restRoot[3]).x - 3;
    require(std::abs(traveled - 4.8f) < .02f,
        "Scenario zombies did not follow their routes before any player connected");
    const auto player = world.join();
    const auto snapshot = world.snapshot(player);
    require(snapshot.zombies.size() == 1 && snapshot.zombies[0].root == scene.zombies.instances[0].restRoot,
        "Scenario movement was absent from authoritative zombie snapshots");
    for (int step = 0; step < 30; ++step) world.step();
    const auto* behavior = scene.zombies.behaviors.find(0);
    require(behavior && behavior->state == viewer::ZombieState::chasing && behavior->targetPlayer == player &&
        glm::vec3(scene.zombies.instances[0].restRoot[3]).z > 8.1f,
        "Authoritative player perception did not override the scenario's initial destination");
    require(!scene.zombies.navigation()->pathFor(0) && scene.zombies.navigation()->stats().requests == 1,
        "Authoritative behavior takeover requested another path or retained scenario ownership");
}

void failures(const std::filesystem::path& asset, const std::filesystem::path& directory,
              const std::filesystem::path& cache) {
    auto scene = sceneFixture(asset, {});
    scene->mapPath = directory / "missing.glb";
    {
        multiplayer::World world(*scene);
        const auto player = world.join();
        for (int step = 0; step < 20; ++step) world.step();
        require(scene->zombies.navigation() == nullptr &&
                    scene->zombies.instances[0].restRoot[3].x > 3 && world.snapshot(player).zombies.size() == 1,
                "Ordinary behavior required a navmesh or stopped original direct movement");
    }
    // Even an allocated navigation object must never be used by ordinary AI.
    scene->zombies.enableNavigation(cache);
    {
        multiplayer::World world(*scene);
        (void)world.join();
        for (int step = 0; step < 20; ++step) world.step();
        require(scene->zombies.navigation()->stats().requests == 0,
                "An ordinary behavior actor requested a navmesh path");
    }
    const auto mesh = test_navmesh::convexSquares(16, 16);
    const auto invalid = directory / "unsafe-padding.bin";
    writeMesh(invalid, mesh, .1);
    rejects([&] { viewer::ZombieNavigation unsafe(invalid, 1); }, "Unsafe body-clearance cache was accepted");
    writeMesh(invalid, mesh, .3, .0001);
    rejects([&] { viewer::ZombieNavigation unsafe(invalid, 1); }, "Unsafe skin-clearance cache was accepted");

    const viewer::CollisionWorld collision(ground());
    viewer::ZombieNavigation navigation(cache, 1);
    const glm::vec3 start{3, 0, 8};
    require(navigation.plan(0, start, {13, 0, 8}), "Cold route fixture did not plan");
    const auto velocity = navigation.follow(0, start, 3, multiplayer::tickSeconds, collision);
    require(glm::length(velocity) > 0 && navigation.pathFor(0) != 0, "Valid server route fixture did not plan");
    require(!navigation.plan(0, start, {50, 0, 8}) &&
        navigation.pathFor(0) == 0 && navigation.lastStatus() == viewer::navmesh_path::Status::blockedEndpoint,
        "Uncovered new goal reused the previous route or fell back to direct pursuit");
    std::set<std::pair<unsigned, unsigned>> holes;
    for (unsigned z = 0; z < 16; ++z) holes.insert({8, z});
    viewer::ZombieNavigation separated(test_navmesh::convexSquares(16, 16, holes), 1);
    require(separated.plan(0, start, {6, 0, 8}) &&
        glm::length(separated.follow(0, start, 3, multiplayer::tickSeconds, collision)) > 0 &&
        separated.pathFor(0) != 0, "Same-component route fixture did not plan");
    require(!separated.plan(0, start, {13, 0, 8}) &&
        separated.pathFor(0) == 0 && separated.lastStatus() == viewer::navmesh_path::Status::noPath,
        "Disconnected new goal retained an obsolete route or bypassed the navmesh");
}
} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Expected the existing test GLB asset");
    options();
    const auto directory = std::filesystem::temp_directory_path() /
        ("agent-server-navigation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{directory};
    const auto cache = directory / "navigation.bin";
    writeMesh(cache, test_navmesh::convexSquares(16, 16));
    failures(argv[1], directory, cache);
    scenarioPopulation(argv[1], cache);
    const auto fixture = directory / "map.glb";
    std::filesystem::copy_file(argv[1], fixture);
    writeMesh(directory / "map.navmesh", test_navmesh::convexSquares(16, 16));
    const auto navigated = scenarioTreeMotion(fixture);
    require(navigated.x > 10, "Scenario server navigation did not resolve the head-on trunk blockage");
    std::cout << "Scenario-only navigation, ordinary behavior isolation, failure safety, Detour tree bypass and authoritative snapshots passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
