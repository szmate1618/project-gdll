#pragma once
#include <filesystem>

namespace multiplayer {
// Client packages carry resources beside the executable; development uses the checkout.
const std::filesystem::path& resourceRoot();
// Generated models live in the build tree during development and beside the
// executable in client packages. Missing package assets must fail locally.
std::filesystem::path weaponModelDirectory();
}
