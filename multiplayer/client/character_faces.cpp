#include "character_faces.hpp"
#include "json.hpp"
#include "stb_image.h"
#include <fstream>
#include <memory>
#include <stdexcept>

namespace multiplayer {
std::vector<CharacterFace> loadCharacterFaces(const std::filesystem::path& root) {
    std::ifstream stream(root / "gui/character-faces.json");
    if (!stream) throw std::runtime_error("Cannot read character face selection");
    const auto entries = nlohmann::json::parse(stream).at("icons");
    if (entries.size() != 20) throw std::runtime_error("Character creation requires 20 face icons");
    std::vector<CharacterFace> faces;
    for (auto entry = entries.begin(); entry != entries.end(); ++entry)
        faces.push_back({entry.key(), entry.value().at("name").get<std::string>(),
                         root / "data/assets/game-icons/character-faces" / (entry.key() + ".png")});
    return faces;
}
viewer::ImageData loadCharacterFaceImage(const std::filesystem::path& path) {
    int width = 0, height = 0, channels = 0;
    const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load(path.u8string().c_str(), &width, &height, &channels, 4), stbi_image_free);
    if (!pixels || width <= 0 || height <= 0 || width > 1024 || height > 1024)
        throw std::runtime_error("Character face textures missing. Run tools/prepare_hud_icons.py");
    viewer::ImageData image; image.width = width; image.height = height;
    image.rgba.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(width) * height * 4);
    return image;
}
} // namespace multiplayer
