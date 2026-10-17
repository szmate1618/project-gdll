#include "vegetation_selection.hpp"

#include <stdexcept>

namespace viewer {

void VegetationSelection::select(std::string_view value) {
    if (explicitSelection)
        throw std::runtime_error("Specify --vegetation only once: osm-tuned2, orthophoto, osm, sentinel-astra or none");
    if (value == "osm-tuned2") source = VegetationSource::OsmTuned2;
    else if (value == "orthophoto") source = VegetationSource::Orthophoto;
    else if (value == "osm") source = VegetationSource::Osm;
    else if (value == "sentinel-astra") source = VegetationSource::SentinelAstra;
    else if (value == "none") source = VegetationSource::None;
    else throw std::runtime_error("--vegetation requires osm-tuned2, orthophoto, osm, sentinel-astra or none");
    explicitSelection = true;
}

VegetationPaths vegetationPaths(const VegetationSelection& selection,
    const std::filesystem::path& map, const std::filesystem::path& orthophotoForests,
    const std::filesystem::path& treeOverride) {
    if (selection.explicitSelection && !treeOverride.empty())
        throw std::runtime_error("Use --vegetation without --trees overrides");
    const bool required = selection.explicitSelection;
    switch (selection.source) {
    case VegetationSource::None: return {};
    case VegetationSource::Osm:
        return {map.parent_path() / "osm/trees.instances.json",
                map.parent_path() / "osm/forest_patches.json", true, true};
    case VegetationSource::SentinelAstra:
        return {{}, orthophotoForests.parent_path() / "forest_patches_astra.json", false, true};
    case VegetationSource::Orthophoto:
        return {map.parent_path() / "trees.original.instances.json", orthophotoForests, true, true};
    case VegetationSource::OsmTuned2:
        return {treeOverride.empty() ? map.parent_path() / "trees.instances.json" : treeOverride,
                map.parent_path() / "osm/forest_patches.json", required || !treeOverride.empty(), required};
    }
    throw std::runtime_error("Invalid vegetation source");
}

} // namespace viewer
