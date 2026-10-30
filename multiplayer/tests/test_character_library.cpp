#include "client/character_library.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) { bool failed = false; try { action(); } catch (const std::exception&) { failed = true; } require(failed, "Invalid library operation was accepted"); }
std::string bytes(const std::filesystem::path& path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
void check(const std::filesystem::path& root) {
    using namespace multiplayer;
    const auto legacy = root / "character.json";
    CharacterCanvas canvas; canvas.setBrushColor({120,30,60,255}); canvas.paint({0,.9f,.2f});
    saveCharacterProfile(legacy, canvas, "clown");
    const auto original = bytes(legacy);
    require(activeCharacterProfile(legacy) == legacy, "Unmigrated sessions must keep using the single save");
    CharacterLibrary library(legacy);
    require(library.characters().size() == 1 && library.activeId() == "legacy", "The original character must be imported and selected");
    require(bytes(legacy) == original && library.get("legacy").name == "Survivor", "Migration must preserve the original bytes and supply a name");
    const auto first = library.save("legacy", "Mára", canvas, "clown");
    require(first.id == "legacy" && first.path.filename() == "legacy.json", "Renaming must keep the stable save ID");
    require(activeCharacterProfile(legacy) == first.path, "Sessions must resolve the selected collection save");
    const auto second = library.save("", "Aron", canvas, "smitten");
    require(second.id != first.id && library.activeId() == first.id, "Creating a character must not silently switch who is in use");
    rejects([&] { library.save("", "aron", canvas, "clown"); });
    rejects([&] { library.save("", "  ", canvas, "clown"); });
    rejects([&] { library.save("../outside", "Escape", canvas, "clown"); });
    rejects([&] { library.use("missing"); });
    library.use(second.id);
    CharacterLibrary reopened(legacy);
    require(reopened.characters().size() == 2 && reopened.activeId() == second.id, "Names and explicit game selection must survive restart");
    std::string face, name; CharacterCanvas restored;
    loadCharacterProfile(second.path, restored, face, &name);
    require(name == "Aron" && face == "smitten" && restored.painting()[0].rgba == canvas.painting()[0].rgba,
            "Each named character must retain its own icon and paint layers");
    const auto saved = bytes(second.path);
    reopened.retire(second.id);
    const auto retired = root / "characters/retired" / second.path.filename();
    require(!std::filesystem::exists(second.path) && bytes(retired) == saved, "Retirement must move the complete save without changing it");
    require(reopened.characters().size() == 1 && reopened.activeId().empty() && !activeCharacterProfile(legacy),
            "Retiring the active character must require a new explicit selection");
    CharacterLibrary afterRetirement(legacy);
    require(afterRetirement.characters().size() == 1 && afterRetirement.activeId().empty(), "Reopening must not resurrect a retired character or reselect the legacy save");
    afterRetirement.use(first.id);
    afterRetirement.retire(first.id);
    require(afterRetirement.characters().empty() && bytes(legacy) == original, "Retiring everything must preserve the untouched original import source");
    require(CharacterLibrary(legacy).characters().empty(), "The original import must not repeat after retirement");
    CharacterLibrary empty(root / "empty/character.json");
    require(empty.characters().empty() && empty.activeId().empty(), "A fresh library must show its empty state");
    std::ofstream(root / "characters/current/broken.json") << "not json";
    CharacterLibrary damaged(legacy);
    require(damaged.characters().empty() && !damaged.warning().empty() && std::filesystem::exists(root / "characters/current/broken.json"),
            "Malformed saves must be reported and retained");
}
}
int main(int argc, char** argv) {
    const auto root = std::filesystem::path(argc > 1 ? argv[1] : "build") / ("character-library-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try { check(root); std::cout << "Character migration, naming, selection, independent saves and retirement passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::filesystem::remove_all(root); return result;
}
