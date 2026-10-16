#pragma once
#include <filesystem>

namespace multiplayer {
// Windows packages carry resources beside the executable; development uses the checkout.
const std::filesystem::path& resourceRoot();
}
