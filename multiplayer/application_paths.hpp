#pragma once
#include <filesystem>

namespace multiplayer {
const std::filesystem::path& executableDirectory();
// Writable user files belong beside the running executable, including in a
// development build. Resolving the path does not create the directory.
std::filesystem::path userDataDirectory();
}
