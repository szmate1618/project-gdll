#include "scene.hpp"
#include "file_fingerprint.hpp"
#include "guardrail_layer.hpp"
#include "model_merge.hpp"
#include "pole_layer.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace multiplayer {
Options parseOptions(int argc, char** argv, bool server) {
    Options o;
    auto next = [&](int& i) -> std::string {
        if (++i >= argc) throw std::runtime_error("Missing option value");
        return argv[i];
    };
    auto number = [&](int& i) {
        const auto s = next(i); std::size_t end = 0; const double n = std::stod(s, &end);
        if (end != s.size() || !std::isfinite(n)) throw std::runtime_error("Invalid numeric option");
        return n;
    };
    bool map = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--connect" && !server) o.address = next(i);
        else if (arg == "--port" && server) {
            const auto n = number(i);
            if (n < 1 || n > 65535 || std::floor(n) != n) throw std::runtime_error("Invalid port");
            o.port = static_cast<std::uint16_t>(n);
        } else if (arg == "--spawn") {
            const auto x = number(i), z = number(i);
            if (std::abs(x) > 1e7 || std::abs(z) > 1e7) throw std::runtime_error("Spawn outside local frame");
            o.spawn = glm::vec2(x, z);
        } else if (arg == "--zombies") o.zombieAssets = next(i);
        else if (arg == "--zombie-count") {
            const auto n = number(i);
            if (n < 0 || n > maxZombies || std::floor(n) != n) throw std::runtime_error("Invalid zombie count");
            o.zombieCount = static_cast<std::size_t>(n);
        } else if (arg == "--zombie-radius") {
            const auto n = number(i);
            if (n < 5 || n > 1000) throw std::runtime_error("Invalid zombie radius");
            o.zombieRadius = static_cast<float>(n);
        } else if (arg == "--no-trees") o.trees = o.forests = false;
        else if (arg == "--no-forests") o.forests = false;
        else if (arg == "--seconds") {
            o.seconds = number(i);
            if (o.seconds <= 0 || o.seconds > 86400) throw std::runtime_error("Invalid duration");
        } else if (arg == "--lag-ms") {
            const auto n = number(i);
            if (n < 0 || n > 2000) throw std::runtime_error("Invalid artificial lag");
            o.fakeLagMs = static_cast<float>(n);
        } else if (arg == "--loss-percent") {
            const auto n = number(i);
            if (n < 0 || n > 50) throw std::runtime_error("Invalid artificial loss");
            o.fakeLossPercent = static_cast<float>(n);
        } else if (arg == "--headless" && !server) o.headless = true;
        else if (arg == "--hidden" && !server) o.hidden = true;
        else if (arg == "--look" && !server) {
            const auto yaw = number(i), pitch = number(i);
            if (std::abs(yaw) > 180 || std::abs(pitch) > 89) throw std::runtime_error("Invalid camera orientation");
            o.look = {yaw, pitch};
        }
        else if (arg == "--bot" && !server) o.bot = true;
        else if (arg == "--screenshot" && !server) o.screenshot = next(i);
        else if (arg == "--help") {
            std::cout << (server ? "godollo_server" : "godollo_client") << " [map.glb] "
                << (server ? "[--port 27020]" : "[--connect 127.0.0.1:27020] [--headless] [--hidden] [--bot] [--look YAW PITCH] [--screenshot FILE]")
                << "\nShared: --spawn X Z --zombies PATH --zombie-count N --zombie-radius METERS\n"
                   "        --no-trees --no-forests --seconds N --lag-ms N --loss-percent N\n";
            std::exit(0);
        } else if (!arg.empty() && arg[0] != '-' && !map) { o.map = arg; map = true; }
        else throw std::runtime_error("Unknown option: " + arg);
    }
    if (o.map.extension() != ".glb") throw std::runtime_error("Multiplayer requires a self-contained GLB map");
    return o;
}

Scene::Scene(const Options& o) : model(viewer::loadModel(o.map)), physics(std::make_shared<viewer::PhysicsWorld>()) {
    std::vector<std::filesystem::path> content{o.map};
    const auto adjacent = [&](const char* name) { return o.map.parent_path() / name; };
    if (std::filesystem::exists(adjacent("poles.instances.json"))) {
        viewer::appendPoleLayer(model, viewer::loadPoleLayer(adjacent("poles.instances.json"), o.map));
        content.push_back(adjacent("poles.instances.json"));
    }
    if (std::filesystem::exists(adjacent("guardrails.glb"))) {
        viewer::appendStaticModel(model, viewer::loadGuardrailLayer(adjacent("guardrails.glb"), o.map).model);
        content.push_back(adjacent("guardrails.glb"));
    }
    doors = std::make_unique<viewer::DoorLayer>(viewer::prepareDoorPanels(model), physics);
    boxes = std::make_unique<viewer::BoxLayer>(physics);
    if (o.trees && std::filesystem::exists(adjacent("trees.instances.json"))) {
        trees = viewer::loadTreeLayer(adjacent("trees.instances.json"), o.map);
        content.push_back(adjacent("trees.instances.json"));
    }
    const auto forestConfig = std::filesystem::path(MP_ROOT) / "forest_patches.json";
    if (o.forests && model.geographicFrame && std::filesystem::exists(forestConfig)) {
        if (!trees) trees.emplace();
        viewer::CollisionWorld placement(model, {}, doors.get());
        forests = viewer::loadForestLayer(forestConfig, o.map, model, placement, *trees);
        content.push_back(forestConfig);
    }
    std::vector<viewer::TrunkCollider> trunks;
    if (trees) {
        for (const auto& asset : trees->assets) content.push_back(asset.path);
        trunks.reserve(trees->instances.size());
        for (const auto& tree : trees->instances) {
            const float h = tree.dimensions.y;
            const float radius = std::min(h * .5f, glm::clamp(.035f * std::min(tree.dimensions.x, tree.dimensions.z), .10f, .50f));
            trunks.push_back({glm::vec3(tree.transform[3]), radius, h});
        }
    }
    collision = std::make_unique<viewer::CollisionWorld>(model, trunks, doors.get());
    collision->setBoxes(boxes.get());
    const auto center = (model.boundsMin + model.boundsMax) * .5f;
    const auto spawn = o.spawn.value_or(glm::vec2(center.x, center.z));
    viewer::FPSController player;
    if (!player.spawn({spawn.x, model.boundsMax.y + 2, spawn.y}, *collision))
        throw std::runtime_error("No safe spawn near requested location");
    spawnEye = player.eyePosition();
    const auto extent = model.boundsMax - model.boundsMin;
    const std::size_t count = extent.x >= 100 && extent.z >= 100 ? o.zombieCount : 0;
    if (count) {
        if (std::filesystem::is_directory(o.zombieAssets))
            for (const auto& entry : std::filesystem::directory_iterator(o.zombieAssets))
                if (entry.path().extension() == ".gltf") throw std::runtime_error("Multiplayer requires self-contained GLB zombie assets");
        zombies = viewer::loadZombieLayer(o.zombieAssets, *collision, model, spawn, count, o.zombieRadius);
        if (std::filesystem::is_directory(o.zombieAssets)) {
            std::vector<std::filesystem::path> assets;
            for (const auto& entry : std::filesystem::directory_iterator(o.zombieAssets))
                if (entry.path().extension() == ".glb") assets.push_back(entry.path());
            std::sort(assets.begin(), assets.end());
            content.insert(content.end(), assets.begin(), assets.end());
        } else {
            if (o.zombieAssets.extension() != ".glb") throw std::runtime_error("Multiplayer requires GLB zombie assets");
            content.push_back(o.zombieAssets);
        }
    }
    std::vector<std::filesystem::path> items;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(MP_ROOT) / "items"))
        if (entry.path().extension() == ".toml") items.push_back(entry.path());
    std::sort(items.begin(), items.end());
    content.insert(content.end(), items.begin(), items.end());
    content.push_back(std::filesystem::path(MP_ROOT) / "inventory.toml");
    // Hash content, not machine-specific paths. Loaders validate manifest provenance too.
    std::ostringstream identity;
    identity << protocolVersion << ':' << count << ':' << std::hexfloat << o.zombieRadius << ':'
             << spawnEye.x << ':' << spawnEye.y << ':' << spawnEye.z << ':' << o.trees << ':' << o.forests;
    for (const auto& file : content) identity << ':' << viewer::fileSha256(file);
    const auto text = identity.str();
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int bytes = 0;
    if (EVP_Digest(text.data(), text.size(), digest, &bytes, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot hash multiplayer content");
    std::ostringstream hex;
    for (unsigned int i = 0; i < bytes; ++i) hex << std::hex << std::setw(2) << std::setfill('0') << unsigned(digest[i]);
    fingerprint = hex.str();
    std::cout << "Scene ready: " << zombies.instances.size() << " zombies, fingerprint " << fingerprint << '\n';
}
} // namespace multiplayer
