#include "character_profile.hpp"
#include "json.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace multiplayer {
namespace {
using Json = nlohmann::json;
constexpr int pixelsPerSide = CharacterCanvas::resolution * CharacterCanvas::resolution;
int integer(const Json& value, int maximum) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number <= std::uint64_t(maximum)) return int(number);
    } else if (value.is_number_integer()) {
        const auto number = value.get<std::int64_t>();
        if (number >= 0 && number <= maximum) return int(number);
    }
    throw std::runtime_error("Invalid character profile number");
}
CharacterCanvas::Color color(const Json& value) {
    if (!value.is_array() || value.size() != 3) throw std::runtime_error("Invalid character profile color");
    return {std::uint8_t(integer(value[0], 255)), std::uint8_t(integer(value[1], 255)),
            std::uint8_t(integer(value[2], 255)), 255};
}
Json rgb(CharacterCanvas::Color color) { return {color[0], color[1], color[2]}; }
} // namespace

std::filesystem::path characterProfilePath() {
    if (const char* path = std::getenv("GD_CHARACTER_PROFILE")) return std::filesystem::u8path(path);
#ifdef _WIN32
    if (const wchar_t* directory = _wgetenv(L"LOCALAPPDATA")) return std::filesystem::path(directory) / L"Godollo/character.json";
#else
    if (const char* directory = std::getenv("XDG_DATA_HOME")) return std::filesystem::u8path(directory) / "godollo/character.json";
    if (const char* directory = std::getenv("HOME")) return std::filesystem::u8path(directory) / ".local/share/godollo/character.json";
#endif
    throw std::runtime_error("Cannot locate local character storage. Set GD_CHARACTER_PROFILE.");
}
bool loadCharacterProfile(const std::filesystem::path& path, CharacterCanvas& canvas, std::string& faceId) {
    if (!std::filesystem::exists(path)) return false;
    if (std::filesystem::file_size(path) > 32 * 1024 * 1024) throw std::runtime_error("Character profile is too large");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read character profile");
    const auto document = Json::parse(input);
    if (document.at("schema") != "godollo.character" || integer(document.at("version"), 1) != 1 ||
        integer(document.at("resolution"), CharacterCanvas::resolution) != CharacterCanvas::resolution)
        throw std::runtime_error("Unsupported character profile format");
    const auto id = document.at("face").get<std::string>();
    if (id.empty() || id.size() > 64 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos)
        throw std::runtime_error("Invalid character face identifier");
    CharacterCanvas loaded;
    loaded.setBaseColor(color(document.at("base"))); loaded.setBrushColor(color(document.at("brush")));
    loaded.setFaceColor(color(document.at("ink")));
    auto layers = loaded.painting();
    const auto& faces = document.at("paint");
    if (!faces.is_array() || faces.size() != CharacterCanvas::faceCount) throw std::runtime_error("Invalid character paint sides");
    for (int face = 0; face < CharacterCanvas::faceCount; ++face) {
        if (!faces[face].is_array() || faces[face].size() > pixelsPerSide) throw std::runtime_error("Invalid character paint runs");
        int end = 0;
        for (const auto& run : faces[face]) {
            if (!run.is_array() || run.size() != 5) throw std::runtime_error("Invalid character paint run");
            const int start = integer(run[0], pixelsPerSide - 1), length = integer(run[1], pixelsPerSide);
            if (start < end || length == 0 || length > pixelsPerSide - start) throw std::runtime_error("Overlapping or out-of-bounds character paint");
            const auto ink = color(Json::array({run[2], run[3], run[4]}));
            end = start + length;
            for (int pixel = start; pixel < end; ++pixel)
                std::copy(ink.begin(), ink.end(), layers[face].rgba.begin() + pixel * 4);
        }
    }
    loaded.restorePainting(layers);
    canvas = std::move(loaded); faceId = id;
    return true;
}
void saveCharacterProfile(const std::filesystem::path& path, const CharacterCanvas& canvas, const std::string& faceId) {
    Json faces = Json::array();
    for (const auto& layer : canvas.painting()) {
        auto runs = Json::array();
        const auto& pixels = layer.rgba;
        for (int pixel = 0; pixel < pixelsPerSide;) {
            if (!pixels[pixel * 4 + 3]) { ++pixel; continue; }
            const int start = pixel++;
            while (pixel < pixelsPerSide && std::equal(pixels.begin() + start * 4, pixels.begin() + start * 4 + 4,
                                                       pixels.begin() + pixel * 4)) ++pixel;
            runs.push_back({start, pixel - start, pixels[start * 4], pixels[start * 4 + 1], pixels[start * 4 + 2]});
        }
        faces.push_back(std::move(runs));
    }
    const Json document{{"schema", "godollo.character"}, {"version", 1}, {"resolution", CharacterCanvas::resolution},
                        {"face", faceId}, {"base", rgb(canvas.base())}, {"brush", rgb(canvas.brush())},
                        {"ink", rgb(canvas.ink())}, {"paint", std::move(faces)}};
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = std::filesystem::u8path(path.u8string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output || !(output << document.dump() << '\n') || !output.flush())
            throw std::runtime_error("Cannot write character profile");
    }
    CharacterCanvas verified; std::string verifiedFace;
    loadCharacterProfile(temporary, verified, verifiedFace);
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace character profile");
#else
    std::filesystem::rename(temporary, path);
#endif
}
} // namespace multiplayer
