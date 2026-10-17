#include "vegetation_selection.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Invalid vegetation selection was accepted");
}
}
int main() {
    try {
        viewer::VegetationSelection defaults;
        auto paths = viewer::vegetationPaths(defaults, "maps/town.glb", "assets/forest_patches.json");
        require(paths.trees == "maps/trees.instances.json" && paths.forests == "maps/osm/forest_patches.json",
                "Default must combine trees and use OSM forests");
        require(!paths.requireTrees && !paths.requireForests, "Sample maps retain optional automatic loading");
        for (const auto* mode : {"osm-tuned2", "orthophoto", "osm", "sentinel-astra", "none"}) {
            viewer::VegetationSelection selected;
            selected.select(mode);
            paths = viewer::vegetationPaths(selected, "maps/town.glb", "assets/forest_patches.json");
            if (selected.source == viewer::VegetationSource::None) {
                require(paths.trees.empty() && paths.forests.empty(), "None must disable all vegetation");
            } else {
                require(paths.requireForests, "Explicit source must require its forests");
                if (selected.source == viewer::VegetationSource::SentinelAstra) {
                    require(paths.trees.empty() && !paths.requireTrees, "Astra trees come only from polygons");
                    require(paths.forests == "assets/forest_patches_astra.json", "Wrong Astra forests");
                } else {
                    require(paths.requireTrees, "Explicit preset must require its tree layer");
                    if (selected.source == viewer::VegetationSource::Orthophoto)
                        require(paths.trees == "maps/trees.original.instances.json" && paths.forests == "assets/forest_patches.json",
                                "Orthophoto must select preserved detections and original polygons");
                    else if (selected.source == viewer::VegetationSource::Osm)
                        require(paths.trees == "maps/osm/trees.instances.json", "Wrong OSM tree file");
                    else require(paths.trees == "maps/trees.instances.json", "Wrong combined tree file");
                }
            }
            for (const auto* other : {"osm-tuned2", "orthophoto", "osm", "sentinel-astra", "none"})
                rejects([&] { selected.select(other); });
            rejects([&] { viewer::vegetationPaths(selected, "map.glb", "forest.json", "custom.json"); });
        }
        for (const auto* invalid : {"original", "unknown"})
            rejects([&] { viewer::VegetationSelection selected; selected.select(invalid); });
        paths = viewer::vegetationPaths(defaults, "maps/map.glb", "forest.json", "custom-trees.json");
        require(paths.trees == "custom-trees.json" && paths.forests == "maps/osm/forest_patches.json" && paths.requireTrees,
                "Tree override must retain the default forest preset");
        std::cout << "Vegetation preset paths and exclusivity checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
