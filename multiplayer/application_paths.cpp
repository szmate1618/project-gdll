#include "application_paths.hpp"
#ifdef _WIN32
#include <windows.h>
#include <array>
#include <stdexcept>
#endif

namespace multiplayer {
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
std::filesystem::path userDataDirectory() { return executableDirectory() / "userdata"; }
}
