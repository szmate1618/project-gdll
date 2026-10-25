#include "resources.hpp"
#ifdef _WIN32
#include <windows.h>
#include <array>
#include <stdexcept>
#endif

namespace multiplayer {
namespace {
const std::filesystem::path& executableDirectory() {
    static const auto directory = [] {
#ifdef _WIN32
        std::array<wchar_t, 32768> path{};
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size == path.size()) throw std::runtime_error("Cannot locate client executable");
        return std::filesystem::path(path.data()).parent_path();
#else
        return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
    }();
    return directory;
}
} // namespace
const std::filesystem::path& resourceRoot() {
    static const auto root = [] {
        const auto& directory = executableDirectory();
        if (std::filesystem::is_directory(directory / "weapons")) return directory;
        return std::filesystem::path(MP_ROOT);
    }();
    return root;
}
std::filesystem::path weaponModelDirectory() {
    const auto& directory = executableDirectory();
    if (std::filesystem::is_directory(directory / "weapons")) return directory / "assets/weapons";
    return std::filesystem::path(MP_WEAPON_ASSETS);
}
std::filesystem::path audioDirectory() {
    const auto& directory = executableDirectory();
    if (std::filesystem::is_directory(directory / "weapons")) return directory / "assets/audio";
#ifdef MP_AUDIO_DIR
    return std::filesystem::path(MP_AUDIO_DIR);
#else
    return resourceRoot() / "assets/audio";
#endif
}
}
