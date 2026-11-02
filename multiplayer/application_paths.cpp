#include "application_paths.hpp"
#include "../src/application_paths.hpp"
namespace multiplayer {
const std::filesystem::path& executableDirectory() { return viewer::executableDirectory(); }
std::filesystem::path userDataDirectory() { return viewer::userDataDirectory(); }
}
