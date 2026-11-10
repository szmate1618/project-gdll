#pragma once

#include "item_model.hpp"
#include <filesystem>

namespace viewer {
// Reads optional [model]/[[model.parts]] from an inventory descriptor.
// Errors include the descriptor path; no model returns empty parts.
ItemModelConfig loadItemModelConfig(const std::filesystem::path& file);
} // namespace viewer
