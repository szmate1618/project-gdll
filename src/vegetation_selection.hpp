#pragma once

#include <filesystem>
#include <string_view>

namespace viewer {

enum class VegetationSource { Original, Osm, SentinelAstra };

struct VegetationSelection {
    VegetationSource source = VegetationSource::Original;
    bool explicitSelection = false;
    // Exactly one --vegetation argument is permitted, including repeated values.
    void select(std::string_view value);
};

struct VegetationPaths {
    std::filesystem::path trees; // Empty for Sentinel-Astra: trees come from its polygons.
    std::filesystem::path forests;
    bool requireTrees = false;
    bool requireForests = false;
};

VegetationPaths vegetationPaths(const VegetationSelection& selection,
    const std::filesystem::path& map, const std::filesystem::path& originalForests,
    const std::filesystem::path& treeOverride = {}, const std::filesystem::path& forestOverride = {});

} // namespace viewer
