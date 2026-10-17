#pragma once

#include <filesystem>
#include <string_view>

namespace viewer {

enum class VegetationSource { OsmTuned2, Orthophoto, Osm, SentinelAstra, None };

struct VegetationSelection {
    VegetationSource source = VegetationSource::OsmTuned2;
    bool explicitSelection = false;
    // Exactly one --vegetation argument is permitted, including repeated values.
    void select(std::string_view value);
};

struct VegetationPaths {
    std::filesystem::path trees; // Empty when trees come only from forest polygons, or disabled.
    std::filesystem::path forests;
    bool requireTrees = false;
    bool requireForests = false;
};

VegetationPaths vegetationPaths(const VegetationSelection& selection,
    const std::filesystem::path& map, const std::filesystem::path& orthophotoForests,
    const std::filesystem::path& treeOverride = {});

} // namespace viewer
