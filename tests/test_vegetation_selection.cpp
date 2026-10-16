#include "vegetation_selection.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Invalid vegetation selection was accepted");
}
}

int main() {
    try {
        viewer::VegetationSelection original;
        auto paths = viewer::vegetationPaths(original, "maps/town.glb", "assets/forest_patches.json");
        require(paths.trees == "maps/trees.instances.json" && paths.forests == "assets/forest_patches.json",
                "Default vegetation paths changed");
        require(!paths.requireTrees && !paths.requireForests, "Implicit defaults must retain optional loading");
        for (const auto* mode : {"original", "osm", "sentinel-astra"}) {
            viewer::VegetationSelection selected;
            selected.select(mode);
            paths = viewer::vegetationPaths(selected, "maps/town.glb", "assets/forest_patches.json");
            require(paths.requireForests, "Explicit source must not silently skip stale or missing forests");
            if (selected.source == viewer::VegetationSource::Osm) {
                require(paths.trees == "maps/osm/trees.instances.json" && paths.forests == "maps/osm/forest_patches.json",
                        "OSM vegetation mixed with original paths");
                require(paths.requireTrees, "Missing OSM trees must be reported");
            } else if (selected.source == viewer::VegetationSource::SentinelAstra) {
                require(paths.trees.empty() && !paths.requireTrees, "Astra must not load original detected trees");
                require(paths.forests == "assets/forest_patches_astra.json", "Incorrect Astra forest configuration");
            } else require(paths.requireTrees, "Explicit original must require its tree dataset");
            for (const auto* other : {"original", "osm", "sentinel-astra"})
                rejects([&] { selected.select(other); });
            rejects([&] { viewer::vegetationPaths(selected, "map.glb", "forest.json", "custom-trees.json"); });
            rejects([&] { viewer::vegetationPaths(selected, "map.glb", "forest.json", {}, "custom-forests.json"); });
        }
        rejects([] { viewer::VegetationSelection selection; selection.select("unknown"); });
        paths = viewer::vegetationPaths(original, "map.glb", "forest.json", "trees.json", "custom.json");
        require(paths.trees == "trees.json" && paths.forests == "custom.json" && paths.requireTrees && paths.requireForests,
                "Legacy explicit file selection changed");
        std::cout << "Vegetation source selection, exclusivity and path checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
