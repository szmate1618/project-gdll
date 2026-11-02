#pragma once
#include <filesystem>
namespace viewer {
const std::filesystem::path& executableDirectory();
// Resolving install/build-local user storage does not create it.
std::filesystem::path userDataDirectory();
}
