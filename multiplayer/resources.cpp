#include "resources.hpp"
#include "application_paths.hpp"

namespace multiplayer {
const std::filesystem::path& resourceRoot() {
    static const auto root = [] {
        const auto& directory = executableDirectory();
        if (std::filesystem::is_directory(directory / "storage/weapons")) return directory;
        return std::filesystem::path(MP_ROOT);
    }();
    return root;
}
std::filesystem::path weaponModelDirectory() {
    const auto& directory = executableDirectory();
    if (std::filesystem::is_directory(directory / "storage/weapons")) return directory / "assets/weapons";
    return std::filesystem::path(MP_WEAPON_ASSETS);
}
std::filesystem::path audioDirectory() {
    const auto& directory = executableDirectory();
    if (std::filesystem::is_directory(directory / "storage/weapons")) return directory / "assets/audio";
#ifdef MP_AUDIO_DIR
    return std::filesystem::path(MP_AUDIO_DIR);
#else
    return resourceRoot() / "assets/audio";
#endif
}
}
