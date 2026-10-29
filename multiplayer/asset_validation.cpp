#include "asset_validation.hpp"
#include "protocol.hpp"
#include <json.hpp>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace multiplayer {
void validateSessionGlb(const std::filesystem::path& path) {
    try {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".glb") throw std::runtime_error("Expected a self-contained GLB asset");
        std::ifstream stream(path, std::ios::binary);
        const auto integer = [&] {
            unsigned char bytes[4]{};
            if (!stream.read(reinterpret_cast<char*>(bytes), 4)) throw std::runtime_error("Truncated GLB header");
            return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
                (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
        };
        const auto magic = integer(), version = integer(), length = integer();
        if (magic != 0x46546c67 || version != 2 || length != std::filesystem::file_size(path) || length < 20)
            throw std::runtime_error("Invalid GLB header");
        const auto jsonBytes = integer(), type = integer();
        if (type != 0x4e4f534a || jsonBytes > length - 20 || jsonBytes % 4)
            throw std::runtime_error("Invalid GLB JSON chunk");
        std::string json(jsonBytes, '\0');
        if (!stream.read(json.data(), jsonBytes)) throw std::runtime_error("Truncated GLB JSON");
        const auto document = nlohmann::json::parse(json);
        for (const auto* collection : {"buffers", "images"}) {
            if (!document.contains(collection)) continue;
            for (const auto& resource : document.at(collection)) {
                if (!resource.contains("uri")) continue;
                const auto uri = resource.at("uri").get<std::string>();
                if (uri.compare(0, 5, "data:") != 0)
                    throw std::runtime_error("External " + std::string(collection) + " URI: " + uri);
            }
        }
    } catch (const std::exception& error) {
        throw std::runtime_error("Multiplayer asset '" + path.string() + "': " + error.what());
    }
}
void validateSessionZombies(const viewer::ZombieLayer& zombies) {
    if (zombies.instances.size() > maxZombies)
        throw std::runtime_error("Zombie population exceeds multiplayer capacity");
    for (std::size_t i = 0; i < zombies.assets.size(); ++i)
        if (zombies.assets[i].skeleton.size() > maxBones)
            throw std::runtime_error("Zombie asset " + std::to_string(i) + " has " +
                std::to_string(zombies.assets[i].skeleton.size()) +
                " skeleton nodes; multiplayer supports at most " + std::to_string(maxBones));
}
} // namespace multiplayer
