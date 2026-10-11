#pragma once
#include <filesystem>
#include <string>
namespace viewer {
// Streaming SHA-256, shared by runtime layer provenance checks.
std::string fileSha256(const std::filesystem::path& path);
}
