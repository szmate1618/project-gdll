#include "client/character_profile.hpp"
#include "json.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void check(const std::filesystem::path& path) {
    using Canvas = multiplayer::CharacterCanvas;
    Canvas original;
    std::string id = "unchanged";
    require(!multiplayer::loadCharacterProfile(path, original, id) && id == "unchanged", "A missing save must preserve the destination");
    original.setBaseColor({10, 20, 30, 255}); original.setBrushColor({210, 220, 230, 255}); original.setFaceColor({140, 150, 160, 255});
    for (auto point : {glm::vec3{0,.9f,.2f}, {0,.9f,-.2f}, {.3f,.9f,0}, {-.3f,.9f,0}, {0,1.8f,0}, {0,0,0}}) original.paint(point);
    multiplayer::saveCharacterProfile(path, original, "clown");
    require(std::filesystem::file_size(path) < 30000, "Sparse painted profiles must remain compact");
    Canvas restored;
    require(multiplayer::loadCharacterProfile(path, restored, id) && id == "clown", "Saved icon must round-trip");
    require(restored.base() == original.base() && restored.brush() == original.brush() && restored.ink() == original.ink(), "All three colors must round-trip");
    for (int face = 0; face < 6; ++face)
        require(restored.images()[face].rgba == original.images()[face].rgba && restored.painting()[face].rgba == original.painting()[face].rgba,
                "All six painting layers must round-trip exactly");
    restored.setBaseColor({80, 70, 60, 255});
    require(restored.painting()[0].rgba == original.painting()[0].rgba, "Loaded strokes must remain independent of the base");
    multiplayer::saveCharacterProfile(path, restored, "smitten");
    Canvas replaced; multiplayer::loadCharacterProfile(path, replaced, id);
    require(id == "smitten" && replaced.base() == restored.base(), "Saving again must replace the previous profile");
    bool rejectedSave = false;
    try { multiplayer::saveCharacterProfile(path, original, "../invalid"); }
    catch (const std::exception&) { rejectedSave = true; }
    multiplayer::loadCharacterProfile(path, replaced, id);
    require(rejectedSave && id == "smitten" && replaced.base() == restored.base(),
            "Failed validation must preserve the last good save");
    std::ifstream input(path);
    auto document = nlohmann::json::parse(input);
    const auto valid = document;
    const auto reject = [&] {
        std::ofstream(path) << document.dump();
        id = "preserved";
        bool failed = false;
        try { multiplayer::loadCharacterProfile(path, replaced, id); } catch (const std::exception&) { failed = true; }
        require(failed && id == "preserved" && replaced.base() == restored.base(), "Invalid profiles must not partially mutate the destination");
    };
    document["version"] = 2; reject();
    document = valid; document["base"][0] = -1; reject();
    document = valid; document["paint"][0] = nlohmann::json::array({{65535, 2, 1, 2, 3}}); reject();
    document = valid; document["paint"][0] = {{0, 4, 1, 2, 3}, {2, 4, 1, 2, 3}}; reject();
    document = valid; document["face"] = "../outside"; reject();
}
} // namespace
int main(int argc, char** argv) {
    const auto directory = std::filesystem::path(argc > 1 ? argv[1] : "build") /
        ("character-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        std::filesystem::create_directories(directory);
        check(directory / "character.json");
        std::cout << "Character colors, six paint layers, atomic replacement and malformed saves passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::filesystem::remove_all(directory);
    return result;
}
