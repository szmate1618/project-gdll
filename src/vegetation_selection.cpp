#include "vegetation_selection.hpp"

#include <stdexcept>

namespace viewer {

void VegetationSelection::select(std::string_view value) {
    if (explicitSelection)
        throw std::runtime_error("Specify --vegetation only once: original, osm or sentinel-astra");
    if (value == "original") source = VegetationSource::Original;
    else if (value == "osm") source = VegetationSource::Osm;
    else if (value == "sentinel-astra") source = VegetationSource::SentinelAstra;
    else throw std::runtime_error("--vegetation requires original, osm or sentinel-astra");
    explicitSelection = true;
}

VegetationPaths vegetationPaths(const VegetationSelection& selection,
    const std::filesystem::path& map, const std::filesystem::path& originalForests,
    const std::filesystem::path& treeOverride, const std::filesystem::path& forestOverride) {
    if (selection.explicitSelection && (!treeOverride.empty() || !forestOverride.empty()))
        throw std::runtime_error("Use --vegetation without --trees/--forests overrides");
    if (selection.source == VegetationSource::Osm)
        return {map.parent_path() / "osm/trees.instances.json",
                map.parent_path() / "osm/forest_patches.json", true, true};
    if (selection.source == VegetationSource::SentinelAstra)
        return {{}, originalForests.parent_path() / "forest_patches_astra.json", false, true};
    return {treeOverride.empty() ? map.parent_path() / "trees.instances.json" : treeOverride,
            forestOverride.empty() ? originalForests : forestOverride,
            selection.explicitSelection || !treeOverride.empty(),
            selection.explicitSelection || !forestOverride.empty()};
}

} // namespace viewer
