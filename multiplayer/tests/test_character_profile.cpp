#include "client/character_profile.hpp"
#include "client/character_library.hpp"
#include "client/character_skin.hpp"
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
    // The session must compose the saved colors, icon alpha and painting using
    // the same pixel rules as the editor, without relying on downloaded assets.
    const auto root = path.parent_path();
    std::filesystem::create_directories(root / "gui");
    std::filesystem::create_directories(root / "data/assets/game-icons/character-faces");
    auto catalog = nlohmann::json{{"icons", {{"smitten", {{"name", "Saved face"}}}}}};
    for (int i = 0; i < 19; ++i) catalog["icons"]["other-" + std::to_string(i)] = {{"name", "Unused face"}};
    std::ofstream(root / "gui/character-faces.json") << catalog.dump();
    constexpr unsigned char png[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,
        8,6,0,0,0,244,34,127,138,0,0,0,17,73,68,65,84,120,156,99,248,255,255,127,195,255,255,255,25,
        0,32,107,6,123,190,157,75,192,0,0,0,0,73,69,78,68,174,66,96,130};
    {
        std::ofstream image(root / "data/assets/game-icons/character-faces/smitten.png", std::ios::binary);
        image.write(reinterpret_cast<const char*>(png), sizeof(png));
    }
    viewer::ImageData icon; icon.width = 2; icon.height = 1;
    icon.rgba = {255,255,255,128,255,255,255,0}; replaced.applyFace(icon);
    const auto appearance = multiplayer::loadSavedCharacterAppearance(root, path);
    require(appearance.rgb.size() == multiplayer::characterTextureBytes, "Session appearance must contain all six RGB sides");
    std::size_t offset = 0;
    for (const auto& side : replaced.images()) for (std::size_t pixel = 0; pixel < side.rgba.size(); pixel += 4)
        for (int channel = 0; channel < 3; ++channel)
            require(appearance.rgb[offset++] == side.rgba[pixel + channel], "Session upload differs from the saved editor appearance");
    multiplayer::CharacterLibrary collection(path);
    auto other = replaced; other.setBaseColor({9,8,7,255});
    const auto selected = collection.save("", "Another survivor", other, "smitten");
    require(multiplayer::loadSavedCharacterAppearance(root, path).rgb == appearance.rgb,
            "Saving a different character must not change the appearance uploaded for a session");
    collection.use(selected.id);
    const auto nextSession = multiplayer::loadSavedCharacterAppearance(root, path);
    const auto backCorner = std::size_t(256 * 256 * 3);
    require(nextSession.rgb[backCorner] == 9 && nextSession.rgb[backCorner + 1] == 8 && nextSession.rgb[backCorner + 2] == 7,
            "Sessions must upload the explicitly chosen roster character");
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
