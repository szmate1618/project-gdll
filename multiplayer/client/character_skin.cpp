#include "character_skin.hpp"
#include "character_profile.hpp"
#include "character_faces.hpp"
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
namespace {
CharacterAppearance flatten(const CharacterCanvas& canvas) {
    static_assert(CharacterCanvas::resolution == characterTextureResolution && CharacterCanvas::faceCount == characterTextureFaces);
    CharacterAppearance appearance; appearance.rgb.reserve(characterTextureBytes);
    for (const auto& side : canvas.images())
        for (std::size_t i = 0; i < side.rgba.size(); i += 4)
            appearance.rgb.insert(appearance.rgb.end(), side.rgba.begin() + i, side.rgba.begin() + i + 3);
    return appearance;
}
}
CharacterAppearance defaultCharacterAppearance() { return flatten(CharacterCanvas{}); }
CharacterAppearance loadSavedCharacterAppearance(const std::filesystem::path& root, const std::filesystem::path& profile) {
    CharacterCanvas canvas; std::string face;
    const auto faces = loadCharacterFaces(root);
    auto icon = faces.begin();
    if (loadCharacterProfile(profile, canvas, face)) {
        icon = std::find_if(faces.begin(), faces.end(), [&](const auto& candidate) { return candidate.id == face; });
        if (icon == faces.end()) throw std::runtime_error("Saved character face is unavailable");
    }
    canvas.applyFace(loadCharacterFaceImage(icon->image));
    return flatten(canvas);
}
} // namespace multiplayer
