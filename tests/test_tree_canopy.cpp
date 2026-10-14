#include "tree_canopy.hpp"
#include "visibility.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::TreeInstance tree(float x, float z, float base = 0, float height = 16) {
    viewer::TreeInstance result;
    result.transform = glm::translate(glm::mat4(1), {x, base, z});
    result.dimensions = {10, height, 10};
    return result;
}

void checkCanopy() {
    require(viewer::estimateTreeCanopyShade({}).empty(), "Empty layers acquired canopy data");
    std::vector<viewer::TreeInstance> trees{tree(0,0)};
    auto shades = viewer::estimateTreeCanopyShade(trees);
    require(shades[0].lowerLight == 1 && shades[0].upperLight == 1,
            "An isolated tree shaded itself");
    for (int x = -2; x <= 2; ++x) for (int z = -2; z <= 2; ++z)
        if (x != 0 || z != 0) trees.push_back(tree(x * 8.0f, z * 8.0f));
    shades = viewer::estimateTreeCanopyShade(trees);
    require(shades[0].lowerLight < .6f && shades[0].upperLight > .95f,
            "Dense lower foliage was not shaded while the exposed crown stayed bright");
    require(shades[0].lowerLight < shades[1].lowerLight,
            "Forest edge was as dark as its interior");
    for (const auto& shade : shades)
        require(shade.lowerLight >= .30f && shade.upperLight <= 1 && shade.upperLight >= shade.lowerLight,
                "Canopy brightness escaped its bounds or darkened the crown more than its base");

    trees.push_back(tree(1000, 1000));
    const auto remote = viewer::estimateTreeCanopyShade(trees);
    require(remote.back().lowerLight == 1 && remote.back().upperLight == 1 &&
            remote[0].lowerLight == shades[0].lowerLight, "Remote trees influenced local canopy cover");

    auto moved = trees;
    for (auto& instance : moved) {
        instance.transform[3] += glm::vec4(128, 64, -256, 0);
        // Transparent padding and mirrored/sheared card geometry are not canopy
        // size. Changing them must not alter the visible-dimension estimate.
        instance.boundsMin = {-500,-500,-500};
        instance.boundsMax = {500,500,500};
        instance.transform[0].x = -1;
        instance.transform[2].x = .3f;
    }
    const auto translated = viewer::estimateTreeCanopyShade(moved);
    for (std::size_t i = 0; i < shades.size(); ++i)
        require(std::abs(translated[i].lowerLight - shades[i].lowerLight) < 1e-6f &&
                std::abs(translated[i].upperLight - shades[i].upperLight) < 1e-6f &&
                translated[i].baseY == 64, "Translation, terrain datum or geometry padding changed cover");
    require(trees[0].transform == glm::mat4(1), "Canopy estimation mutated an authoritative tree matrix");

    std::reverse(trees.begin(), trees.end());
    auto reordered = viewer::estimateTreeCanopyShade(trees);
    for (std::size_t i = 0; i < remote.size(); ++i)
        require(std::abs(reordered[i].lowerLight - remote[remote.size() - 1 - i].lowerLight) < 1e-6f,
                "Tree order affected canopy brightness");

    shades = viewer::estimateTreeCanopyShade({tree(0,0,20), tree(2,0,0,8)});
    require(shades[0].lowerLight == 1 && shades[0].upperLight == 1,
            "A crown entirely below a hillside tree shaded it");
    shades = viewer::estimateTreeCanopyShade({tree(0,0), tree(2,0,0,32)});
    require(shades[0].upperLight < 1 && shades[0].lowerLight <= shades[0].upperLight,
            "An overtopping neighbor did not shade the shorter crown");
}

void checkSpatialQueries() {
    std::vector<viewer::VisibilityBounds> bounds;
    for (int i = 0; i < 1024; ++i) bounds.push_back({{i * 10.0f,0,-1},{i * 10.0f + 2,0,1}});
    const viewer::VisibilityIndex index(bounds);
    std::vector<std::size_t> selected{9999};
    const auto stats = index.query(viewer::VisibilityBounds{{5120,0,0},{5120,0,0}}, selected);
    require(selected == std::vector<std::size_t>{512} && stats.testedInstances < 100,
            "Point query lost a touching boundary, retained stale results or scanned the whole index");
    index.query(viewer::VisibilityBounds{{-2,0,-2},{-1,0,-1}}, selected);
    require(selected.empty(), "Disjoint spatial query retained trees");
    index.query(viewer::VisibilityBounds{{0,-1,-2},{22,1,2}}, selected);
    std::sort(selected.begin(), selected.end());
    require(selected == std::vector<std::size_t>({0,1,2}), "Box query missed enclosed or touching bounds");
}
} // namespace

int main() {
    try {
        checkCanopy();
        checkSpatialQueries();
        std::cout << "Tree canopy coverage regressions passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
