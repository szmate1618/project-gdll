#include "asset_validation.hpp"
#include "scene.hpp"
#include "session_settings.hpp"
#include <json.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void integer(std::vector<std::uint8_t>& bytes, std::uint32_t n) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(n >> shift));
}
void number(std::vector<std::uint8_t>& bytes, float n) {
    std::uint32_t bits; std::memcpy(&bits, &n, 4); integer(bytes, bits);
}
void write(const std::filesystem::path& path, Json document, std::vector<std::uint8_t> binary) {
    auto json = document.dump(); while (json.size() % 4) json += ' ';
    while (binary.size() % 4) binary.push_back(0);
    std::vector<std::uint8_t> file;
    integer(file, 0x46546c67); integer(file, 2); integer(file, static_cast<std::uint32_t>(28 + json.size() + binary.size()));
    integer(file, static_cast<std::uint32_t>(json.size())); integer(file, 0x4e4f534a);
    file.insert(file.end(), json.begin(), json.end());
    integer(file, static_cast<std::uint32_t>(binary.size())); integer(file, 0x004e4942);
    file.insert(file.end(), binary.begin(), binary.end());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    require(bool(out), "Cannot write asset fixture");
}
Json geometry(std::vector<std::uint8_t>& binary, bool upright) {
    const std::vector<glm::vec3> vertices = upright ?
        std::vector<glm::vec3>{{-.5f, 0, 0}, {.5f, 0, 0}, {.5f, 1.8f, 0}, {-.5f, 1.8f, 0}} :
        std::vector<glm::vec3>{{-100, 0, -100}, {-100, 0, 100}, {100, 0, 100}, {100, 0, -100}};
    for (const auto p : vertices) { number(binary, p.x); number(binary, p.y); number(binary, p.z); }
    for (const std::uint8_t index : {0, 1, 2, 0, 2, 3}) binary.push_back(index);
    while (binary.size() % 4) binary.push_back(0);
    return Json{
        {"asset", {{"version", "2.0"}}}, {"scene", 0}, {"scenes", Json::array({{{"nodes", {0}}}})},
        {"nodes", Json::array({{{"mesh", 0}, {"name", upright ? "zombie" : "terrain"}}})},
        {"meshes", Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}}, {"indices", 1}}})}}})},
        {"buffers", Json::array({{{"byteLength", binary.size()}}})},
        {"bufferViews", Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 48}},
            {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 6}}})},
        {"accessors", Json::array({{{"bufferView", 0}, {"componentType", 5126}, {"count", 4}, {"type", "VEC3"}},
            {{"bufferView", 1}, {"componentType", 5121}, {"count", 6}, {"type", "SCALAR"}}})}
    };
}
template<class F> void rejects(F run, const std::string& expected) {
    try { run(); } catch (const std::exception& error) {
        require(std::string(error.what()).find(expected) != std::string::npos, "Asset rejected for an unrelated reason"); return;
    }
    throw std::runtime_error("Incompatible asset accepted");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Fixture directory argument missing");
        const std::filesystem::path root(argv[1]); std::filesystem::create_directories(root);
        std::vector<std::uint8_t> binary; auto document = geometry(binary, false);
        const auto map = root / "ground.glb"; write(map, document, binary);
        multiplayer::Options options; options.map = map; options.trees = options.forests = false; options.zombieCount = 0;
        multiplayer::Scene valid(options);
        auto external = document; external["buffers"][0]["uri"] = "outside.bin";
        const auto outside = root / "outside.bin";
        { std::ofstream out(outside, std::ios::binary);
          out.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size())); }
        options.map = root / "external.glb"; write(options.map, external, binary);
        require(!viewer::loadModel(options.map).draws.empty(), "External-buffer GLB must be a valid viewer asset");
        rejects([&] { multiplayer::Scene invalid(options); }, "External buffers URI");
        external = document; external["images"] = Json::array({{{"uri", "outside.png"}}});
        const auto image = root / "external-image.glb"; write(image, external, binary);
        rejects([&] { multiplayer::validateSessionGlb(image); }, "External images URI");
        const auto uppercase = root / "external.GLB"; write(uppercase, external, binary);
        rejects([&] { multiplayer::validateSessionGlb(uppercase); }, "External images URI");
        auto embedded = document;
        embedded["buffers"].push_back({{"byteLength", 4}, {"uri", "data:application/octet-stream;base64,AAAAAA=="}});
        const auto data = root / "data-uri.glb"; write(data, embedded, binary);
        multiplayer::validateSessionGlb(data);

        binary.clear(); auto zombie = geometry(binary, true);
        while (zombie["nodes"].size() < multiplayer::maxBones + 1) zombie["nodes"].push_back(Json::object());
        const auto times = binary.size(); number(binary, 0); number(binary, 1);
        const auto translations = binary.size(); for (int i = 0; i < 6; ++i) number(binary, 0);
        zombie["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", times}, {"byteLength", 8}});
        zombie["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", translations}, {"byteLength", 24}});
        zombie["accessors"].push_back({{"bufferView", 2}, {"componentType", 5126}, {"count", 2}, {"type", "SCALAR"}});
        zombie["accessors"].push_back({{"bufferView", 3}, {"componentType", 5126}, {"count", 2}, {"type", "VEC3"}});
        zombie["buffers"][0]["byteLength"] = binary.size();
        zombie["animations"] = Json::array({{{"name", "idle"},
            {"samplers", Json::array({{{"input", 2}, {"output", 3}}})},
            {"channels", Json::array({{{"sampler", 0}, {"target", {{"node", 0}, {"path", "translation"}}}}})}}});
        const auto rig = root / "large-rig.glb"; write(rig, zombie, binary);
        require(viewer::loadZombieModel(rig).skeleton.size() == 129, "Large skeleton must load in the shared viewer");
        options.map = map; options.zombieAssets = rig; options.zombieCount = 1;
        rejects([&] { multiplayer::Scene invalid(options); }, "129 skeleton nodes");
        zombie["nodes"].erase(zombie["nodes"].end() - 1);
        const auto boundary = root / "boundary-rig.glb"; write(boundary, zombie, binary);
        options.zombieAssets = boundary;
        multiplayer::Scene atLimit(options);
        require(atLimit.zombies.assets[0].skeleton.size() == multiplayer::maxBones, "Compatible boundary skeleton rejected");
        // This tiny map cannot safely place 110,000 bodies. A replica must load
        // only shared assets and hidden ID slots, without a placement search.
        options.zombieCount = multiplayer::maxZombies;
        multiplayer::Scene replica(options, {}, std::nullopt, multiplayer::SceneRole::Replica);
        require(replica.zombies.instances.size() == 110000 &&
            std::all_of(replica.zombies.instances.begin(), replica.zombies.instances.end(),
                [](const auto& instance) { return instance.removed; }) && !replica.zombies.navigation(),
            "Large replica initialization generated visible placements or navigation");
        options.zombieCount = multiplayer::maxZombies + 1;
        rejects([&] { multiplayer::Scene tooLarge(options, {}, std::nullopt, multiplayer::SceneRole::Replica); },
                "Zombie population exceeds multiplayer capacity");

        // A map door enables the shared reserve without requiring any eager
        // house generation. Keep this fixture far from the player spawn.
        std::vector<std::uint8_t> townBytes;
        auto town = geometry(townBytes, false);
        const auto doorOffset = townBytes.size();
        for (const glm::vec3 p : {glm::vec3(-.5f, 0, 0), {.5f, 0, 0}, {.5f, 2.1f, 0}, {-.5f, 2.1f, 0}})
            { number(townBytes, p.x); number(townBytes, p.y); number(townBytes, p.z); }
        for (const std::uint8_t index : {0, 1, 2, 0, 2, 3}) townBytes.push_back(index);
        while (townBytes.size() % 4) townBytes.push_back(0);
        town["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", doorOffset}, {"byteLength", 48}});
        town["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", doorOffset + 48}, {"byteLength", 6}});
        town["accessors"].push_back({{"bufferView", 2}, {"componentType", 5126}, {"count", 4}, {"type", "VEC3"}});
        town["accessors"].push_back({{"bufferView", 3}, {"componentType", 5121}, {"count", 6}, {"type", "SCALAR"}});
        town["meshes"].push_back({{"primitives", Json::array({{{"attributes", {{"POSITION", 2}}}, {"indices", 3}}})}});
        town["nodes"].push_back({{"mesh", 1}, {"name", "building_test_doors"}, {"translation", {20, 0, 0}}});
        town["scenes"][0]["nodes"].push_back(1);
        town["buffers"][0]["byteLength"] = townBytes.size();
        options.map = root / "town.glb"; options.zombieCount = 1;
        write(options.map, town, townBytes);
        multiplayer::Scene authority(options);
        require(authority.interiorZombies && authority.interiorZombies->reserveCount() == 10000 &&
                authority.zombies.instances.size() == 10001 && !authority.zombies.instances[0].removed &&
                std::all_of(authority.zombies.instances.begin() + 1, authority.zombies.instances.end(),
                    [](const auto& z) { return z.removed; }) && authority.floorplans->residentCount() == 0,
                "Authority must allocate exactly 10,000 additional dormant slots before any interior loads");
        const auto configured = multiplayer::applySessionSettings(options, multiplayer::sessionSettings(options, authority));
        multiplayer::Scene townReplica(configured, {}, std::nullopt, multiplayer::SceneRole::Replica);
        require(townReplica.zombies.instances.size() == 10001 && !townReplica.interiorZombies &&
                townReplica.fingerprint == authority.fingerprint,
                "Replica must load the advertised reserve once with matching session identity");
        options.zombieCount = 0;
        multiplayer::Scene disabled(options);
        require(disabled.zombies.instances.empty() && !disabled.interiorZombies,
                "Disabled zombie populations must not allocate an interior pool");
        std::cout << "Self-contained GLB and skeleton startup validation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
