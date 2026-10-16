#include "resources.hpp"
#ifdef _WIN32
#include <windows.h>
#include <array>
#include <stdexcept>
#endif

namespace multiplayer {
const std::filesystem::path& resourceRoot() {
    static const auto root = [] {
#ifdef _WIN32
        std::array<wchar_t, 32768> path{};
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size == path.size()) throw std::runtime_error("Cannot locate client executable");
        const auto directory = std::filesystem::path(path.data()).parent_path();
#else
        const auto directory = std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
        if (std::filesystem::is_directory(directory / "items")) return directory;
        return std::filesystem::path(MP_ROOT);
    }();
    return root;
}
}
