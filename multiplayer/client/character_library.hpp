#pragma once
#include "character_profile.hpp"
#include <optional>
#include <vector>

namespace multiplayer {
struct CharacterRecord {
    std::string id, name, face;
    CharacterCanvas::Color base{}, ink{};
    std::filesystem::path path;
};
// A collection is beside the former single save: characters/current/*.json,
// characters/retired/*.json and library.json containing the active stable ID.
// Opening the library copies a valid legacy save; the original is retained.
class CharacterLibrary {
public:
    explicit CharacterLibrary(std::filesystem::path legacyProfile);
    const std::vector<CharacterRecord>& characters() const { return characters_; }
    const std::string& activeId() const { return active_; }
    const std::string& warning() const { return warning_; }
    const CharacterRecord& get(const std::string& id) const;
    CharacterRecord save(const std::string& id, const std::string& name, const CharacterCanvas& canvas, const std::string& face);
    void use(const std::string& id);
    void retire(const std::string& id);
    static std::string checkedName(const std::string& name);
private:
    void scan();
    void selectActive(const std::string& id);
    std::filesystem::path root_;
    std::vector<CharacterRecord> characters_;
    std::string active_, warning_;
};
// Read-only session lookup. No library yet means the original save still works;
// an initialized collection with no active character deliberately returns null.
std::optional<std::filesystem::path> activeCharacterProfile(const std::filesystem::path& legacyProfile);
} // namespace multiplayer
