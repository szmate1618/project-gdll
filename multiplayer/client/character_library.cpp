#include "character_library.hpp"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace multiplayer {
namespace {
using Json = nlohmann::json;
bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos;
}
std::string readSelection(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("Invalid character library size");
    std::ifstream input(path);
    const auto document = Json::parse(input);
    if (document.at("schema") != "godollo.characters" || document.at("version") != 1) throw std::runtime_error("Unsupported character library");
    const auto id = document.at("active").get<std::string>();
    if (!id.empty() && !validId(id)) throw std::runtime_error("Invalid active character ID");
    return id;
}
CharacterRecord readRecord(const std::filesystem::path& path) {
    CharacterRecord record; record.path = path; record.id = path.stem().u8string();
    if (!validId(record.id)) throw std::runtime_error("Invalid character filename");
    CharacterCanvas canvas;
    if (!loadCharacterProfile(path, canvas, record.face, &record.name)) throw std::runtime_error("Character save disappeared");
    record.name = CharacterLibrary::checkedName(record.name);
    record.base = canvas.base(); record.ink = canvas.ink();
    return record;
}
std::string folded(std::string name) {
    for (auto& c : name) if (static_cast<unsigned char>(c) < 128) c = char(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
}
std::optional<std::filesystem::path> activeCharacterProfile(const std::filesystem::path& legacy) {
    const auto root = legacy.parent_path() / "characters";
    if (!std::filesystem::exists(root / "library.json"))
        return std::filesystem::exists(legacy) ? std::optional<std::filesystem::path>(legacy) : std::nullopt;
    const auto id = readSelection(root / "library.json");
    if (id.empty()) return {};
    const auto path = root / "current" / (id + ".json");
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Active character save is missing; choose another in Characters");
    return path;
}
std::string CharacterLibrary::checkedName(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n"), last = value.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) throw std::runtime_error("Enter a character name");
    auto name = value.substr(first, last - first + 1);
    std::size_t characters = 0;
    for (unsigned char c : name) {
        if (c < 32 || c == 127) throw std::runtime_error("Character names cannot contain control characters");
        if ((c & 0xc0) != 0x80) ++characters;
    }
    if (characters > 32 || name.size() > 128) throw std::runtime_error("Use at most 32 characters for the name");
    return name;
}
CharacterLibrary::CharacterLibrary(std::filesystem::path legacy) : root_(legacy.parent_path() / "characters") {
    if (!std::filesystem::exists(root_ / "library.json")) {
        std::filesystem::create_directories(root_ / "current");
        if (std::filesystem::exists(legacy)) {
            try {
                CharacterCanvas canvas; std::string face, name;
                loadCharacterProfile(legacy, canvas, face, &name);
                checkedName(name);
                const auto imported = root_ / "current/legacy.json";
                if (!std::filesystem::exists(imported)) saveCharacterProfile(imported, canvas, face, name);
                active_ = "legacy";
            } catch (const std::exception& error) { warning_ = std::string("Original save kept: ") + error.what(); }
        }
        selectActive(active_);
    } else active_ = readSelection(root_ / "library.json");
    scan();
    if (!active_.empty() && std::none_of(characters_.begin(), characters_.end(), [&](const auto& c) { return c.id == active_; })) {
        warning_ = "The active save is unavailable. Choose another character.";
        selectActive("");
    }
}
void CharacterLibrary::scan() {
    std::vector<CharacterRecord> records;
    if (std::filesystem::exists(root_ / "current")) for (const auto& item : std::filesystem::directory_iterator(root_ / "current")) {
        if (!item.is_regular_file() || item.path().extension() != ".json") continue;
        try { records.push_back(readRecord(item.path())); }
        catch (const std::exception& error) { warning_ = "A character save could not be loaded: " + item.path().filename().u8string() + " (" + error.what() + ")"; }
    }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.name == b.name ? a.id < b.id : a.name < b.name; });
    characters_ = std::move(records);
}
const CharacterRecord& CharacterLibrary::get(const std::string& id) const {
    const auto found = std::find_if(characters_.begin(), characters_.end(), [&](const auto& record) { return record.id == id; });
    if (found == characters_.end()) throw std::runtime_error("Character is no longer in the current list");
    return *found;
}
CharacterRecord CharacterLibrary::save(const std::string& id, const std::string& value, const CharacterCanvas& canvas, const std::string& face) {
    const auto name = checkedName(value);
    for (const auto& record : characters_) if (record.id != id && folded(record.name) == folded(name))
        throw std::runtime_error("A character with this name already exists");
    auto stableId = id;
    if (stableId.empty()) {
        std::mt19937_64 random(std::random_device{}());
        do {
            std::ostringstream text; text << std::hex << random(); stableId = text.str();
        } while (std::filesystem::exists(root_ / "current" / (stableId + ".json")) || std::filesystem::exists(root_ / "retired" / (stableId + ".json")));
    } else get(id);
    const auto path = root_ / "current" / (stableId + ".json");
    saveCharacterProfile(path, canvas, face, name);
    scan();
    return get(stableId);
}
void CharacterLibrary::selectActive(const std::string& id) {
    std::filesystem::create_directories(root_);
    const auto temporary = root_ / "library.json.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const Json document{{"schema", "godollo.characters"}, {"version", 1}, {"active", id}};
        if (!output || !(output << document.dump() << '\n') || !output.flush()) throw std::runtime_error("Cannot save character selection");
    }
    readSelection(temporary);
    replaceCharacterFile(temporary, root_ / "library.json");
    active_ = id;
}
void CharacterLibrary::use(const std::string& id) { get(id); selectActive(id); }
void CharacterLibrary::retire(const std::string& id) {
    const auto source = get(id).path;
    const auto destination = root_ / "retired" / source.filename();
    std::filesystem::create_directories(destination.parent_path());
    if (std::filesystem::exists(destination)) throw std::runtime_error("A retired save already uses this character ID");
    const bool active = active_ == id;
    if (active) selectActive("");
    try { std::filesystem::rename(source, destination); }
    catch (...) { if (active) selectActive(id); throw; }
    scan();
}
} // namespace multiplayer
