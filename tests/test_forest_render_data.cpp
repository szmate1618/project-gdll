#include "forest_tree_visibility.hpp"
#include "forest_volume_batch.hpp"
#include "tree_visibility.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::TreeInstance tree(glm::vec3 position) {
    viewer::TreeInstance instance;
    instance.boundsMin = position - glm::vec3(.2f);
    instance.boundsMax = position + glm::vec3(.2f);
    return instance;
}

void checkTreeSelection() {
    std::vector<viewer::TreeInstance> trees;
    for (int i = 0; i < 10000; ++i)
        trees.push_back(tree({(i % 100) * .1f - 5, (i / 100) * .1f - 5, -100}));
    trees.push_back(tree({0, 0, 100})); // Patch behind the color camera.
    trees.push_back(tree({15, 0, -100})); // Detected canopy outside patch bounds.
    trees.push_back(tree({0, 0, -50})); // Ordinary tree.
    const std::vector<viewer::ForestPatch> patches{
        {"front", {{-5,-5,-101},{5,5,-99}}, 0, 10000, 0, 0, {10001}},
        {"back", {{-1,-1,99},{1,1,101}}, 10000, 1, 0, 0, {}}};
    const viewer::ForestTreeVisibility index(trees, patches);
    const auto projection = glm::perspective(glm::radians(60.0f), 1.0f, 1.0f, 200.0f);
    std::vector<std::size_t> visible;
    const auto distant = index.query(projection, {false, false}, true, visible);
    require(visible == std::vector<std::size_t>{10002}, "Distant forest leaked individual trees");
    require(distant.queriedPatches == 0 && distant.testedNodes == 1,
            "Volume patches were traversed before their trees were rejected");
    index.query(projection, {false, false}, false, visible);
    require(visible == std::vector<std::size_t>{10002}, "Culling bypass ignored volume LoD");

    // Scope/detail results must equal the previously validated flat index.
    const viewer::TreeVisibility reference(trees);
    for (const auto& matrix : {projection, glm::perspective(glm::radians(1.0f), 1.0f, 1.0f, 200.0f)}) {
        std::vector<std::size_t> expected;
        reference.query(matrix, expected);
        const auto stats = index.query(matrix, {true, true}, true, visible);
        std::sort(expected.begin(), expected.end());
        std::sort(visible.begin(), visible.end());
        require(visible == expected, "Grouped visibility changed scope/frustum selection");
        require(stats.queriedPatches == 1, "Offscreen patch queried individual trees");
    }
    index.query(projection, {true, true}, false, visible);
    require(visible.size() == trees.size(), "Culling bypass omitted detailed trees");

    const auto canopyView = projection * glm::lookAt(glm::vec3(15,0,-90), glm::vec3(15,0,-100), glm::vec3(0,1,0));
    index.query(canopyView, {true, false}, true, visible);
    require(std::find(visible.begin(), visible.end(), 10001) != visible.end(),
            "Detected tree padding was culled using the forest footprint");

    const auto lightView = projection * glm::lookAt(glm::vec3(0), glm::vec3(0,0,1), glm::vec3(0,1,0));
    const auto shadows = index.query(lightView, {true, true}, true, visible);
    require(visible == std::vector<std::size_t>{10000} && shadows.queriedPatches == 1,
            "Light frustum reused color visibility");
    index.query(lightView, {true, false}, true, visible);
    require(visible.empty(), "Light query ignored the camera's volume/detail choice");

    auto overlapping = patches;
    overlapping[1].detectedTrees.push_back(0);
    bool rejected = false;
    try { const viewer::ForestTreeVisibility invalid(trees, overlapping); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Overlapping tree ownership was accepted");
}

void checkTemporalTreeSelection() {
    std::vector<viewer::TreeInstance> trees;
    for (const float z : {-100.0f, 100.0f})
        for (int i = 0; i < 256; ++i)
            trees.push_back(tree({(i % 32 - 15.5f) * 2, (i / 32 - 3.5f) * 2, z}));
    trees.push_back(tree({0, 0, -25}));
    const std::vector<viewer::ForestPatch> patches{
        {"front", {}, 0, 256, 0, 0, {}}, {"back", {}, 256, 256, 0, 0, {}}};
    const viewer::ForestTreeVisibility index(trees, patches);
    viewer::ForestTreeVisibilityQueryState camera, light, billboard;
    const auto projection = glm::perspective(glm::radians(60.0f), 1.0f, 1.0f, 200.0f);
    const auto scoped = glm::perspective(glm::radians(1.0f), 1.0f, 1.0f, 200.0f);
    const auto backwards = projection * glm::lookAt(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
    const viewer::HorizontalVisibilityRange frontRange{{0, -100}, 8};
    const viewer::HorizontalVisibilityRange backRange{{0, 100}, 8};
    const auto compare = [&](const viewer::ForestTreeVisibility& visibility, const glm::mat4& matrix,
                             const std::vector<bool>& detail, bool culling,
                             viewer::ForestTreeVisibilityQueryState& temporal,
                             const viewer::HorizontalVisibilityRange* range = nullptr) {
        std::vector<std::size_t> expected, actual{99999};
        const auto baseline = visibility.query(matrix, detail, culling, expected, range);
        const auto cached = visibility.query(matrix, detail, culling, actual, range, &temporal);
        require(actual == expected, "Temporal forest visibility reused stale tree selection");
        require(cached.testedNodes == baseline.testedNodes &&
                cached.testedInstances == baseline.testedInstances &&
                cached.queriedPatches == baseline.queriedPatches,
                "Temporal forest visibility changed spatial traversal");
        if (!culling) require(cached.testedPlanes == 0 && cached.temporalRejects == 0,
                              "Culling bypass still tested cached planes");
        return cached;
    };
    std::vector<std::size_t> visible;
    const auto baseline = index.query(scoped, {true, true}, true, visible);
    compare(index, scoped, {true, true}, true, camera);
    const auto repeated = compare(index, scoped, {true, true}, true, camera);
    require(repeated.temporalRejects > 0 && repeated.testedPlanes < baseline.testedPlanes,
            "Forest groups did not retain or aggregate rejecting-plane hints");

    // Range cutoffs remain active during a frustum bypass and must not read or
    // update plane hints. Only the front patch intersects this short range.
    index.query(backwards, {true, true}, false, visible, &frontRange, &camera);
    require(!visible.empty() && visible.size() < 256 &&
            std::all_of(visible.begin(), visible.end(), [](std::size_t i) { return i < 256; }),
            "Temporal frustum bypass lost the horizontal range cutoff");

    std::vector<viewer::VisibilityBounds> billboardBounds;
    for (const auto& instance : trees)
        billboardBounds.push_back({instance.boundsMin - glm::vec3(3, 0, 3),
                                   instance.boundsMax + glm::vec3(3, 0, 3)});
    const viewer::ForestTreeVisibility billboardIndex(trees, patches, billboardBounds);
    // Repeated scopes, camera turns, detail switches, independent light and
    // billboard indexes, range changes and bypasses all use current visibility.
    for (int frame = 0; frame < 16; ++frame) {
        const std::vector<bool> detail{(frame & 1) != 0, (frame & 2) != 0};
        const auto& matrix = frame % 3 == 0 ? scoped : (frame % 3 == 1 ? backwards : projection);
        const auto* range = frame % 3 == 0 ? nullptr : (frame % 3 == 1 ? &frontRange : &backRange);
        compare(index, matrix, detail, frame % 4 != 0, camera, range);
        compare(index, backwards, detail, true, light, range);
        compare(billboardIndex, matrix, detail, true, billboard, range);
    }

    // Replacements can grow and shrink both the group list and each group's
    // index; previous plane choices are ordering hints, never visibility data.
    auto expandedTrees = trees;
    expandedTrees.push_back(tree({0, 0, -50}));
    auto expandedPatches = patches;
    expandedPatches.push_back({"added", {}, 513, 1, 0, 0, {}});
    const viewer::ForestTreeVisibility expanded(expandedTrees, expandedPatches);
    compare(expanded, projection, {true, true, true}, true, camera);
    const viewer::ForestTreeVisibility ordinary(trees, {});
    compare(ordinary, scoped, {}, true, camera);
    compare(index, backwards, {true, true}, true, camera);
    const viewer::ForestTreeVisibility empty;
    compare(empty, projection, {}, true, camera);
    compare(index, projection, {true, false}, true, camera);
}

void checkVolumeBatching() {
    viewer::ForestLayer forest;
    viewer::Primitive triangle;
    triangle.material = 0;
    for (const auto position : {glm::vec3(0), glm::vec3(2,0,0), glm::vec3(0,2,0)}) {
        viewer::Vertex vertex;
        vertex.position = position;
        vertex.normal = {0,0,1};
        vertex.uv = {position.x, position.y};
        vertex.color = {.1f,.2f,.3f,1};
        triangle.vertices.push_back(vertex);
    }
    triangle.indices = {0,1,2};
    forest.volumes.primitives.push_back(triangle);
    triangle.material = 1;
    forest.volumes.primitives.push_back(triangle);
    forest.volumes.materials.resize(2);
    forest.volumes.materials[0].baseColorTexture = 0;
    forest.volumes.images.push_back({1,1,{255,0,0,255}});
    forest.volumes.textures.push_back({0});
    glm::mat4 reflected = glm::translate(glm::mat4(1), {10,20,30});
    reflected[0][0] = -2;
    reflected[1][1] = 3;
    reflected[1][0] = .5f; // Preserve shear as well as reflection.
    forest.volumes.draws = {{0,glm::mat4(1),"first wall"}, {1,glm::mat4(1),"first top"},
                           {0,reflected,"second wall"}, {1,reflected,"second top"}};
    forest.patches = {{"first", {}, 0,0,0,2,{}}, {"second", {}, 0,0,2,2,{}}};
    const auto batched = viewer::batchForestVolumes(forest);
    require(batched.model.primitives.size() == 2, "Volumes were not grouped by material");
    require(batched.model.images[0].rgba == forest.volumes.images[0].rgba &&
            batched.model.materials[0].baseColorTexture == 0, "Batch lost its shared texture");
    for (std::size_t batch = 0; batch < 2; ++batch) {
        const auto& geometry = batched.model.primitives[batch];
        require(geometry.vertices.size() == 6 && geometry.indices.size() == 6,
                "Batch lost or duplicated volume geometry");
        const auto& ranges = batched.ranges[batch];
        require(ranges.size() == 2 && ranges[0].patch == 0 && ranges[1].patch == 1 &&
                ranges[0].firstIndex == 0 && ranges[1].firstIndex == 3 &&
                ranges[0].indexCount == 3 && ranges[1].indexCount == 3,
                "Batch ranges cannot independently select a patch");
        require(geometry.vertices[3].position == glm::vec3(10,20,30) &&
                geometry.vertices[4].position == glm::vec3(6,20,30) &&
                geometry.vertices[5].position == glm::vec3(11,26,30),
                "Volume draw transform was not baked exactly once");
        require(geometry.vertices[5].uv == glm::vec2(0,2) &&
                geometry.vertices[5].color == triangle.vertices[2].color,
                "Batch changed metric texture coordinates or vertex color");
        for (const auto& range : ranges) {
            const auto& a = geometry.vertices[geometry.indices[range.firstIndex]];
            const auto& b = geometry.vertices[geometry.indices[range.firstIndex + 1]];
            const auto& c = geometry.vertices[geometry.indices[range.firstIndex + 2]];
            require(glm::dot(glm::cross(b.position-a.position,c.position-a.position),a.normal) > 0,
                    "Reflected volume lost outward winding");
        }
    }
    forest.patches[1].firstDraw = 1;
    bool rejected = false;
    try { viewer::batchForestVolumes(forest); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Overlapping volume ranges were accepted");
}
} // namespace

int main() {
    try {
        checkTreeSelection();
        checkTemporalTreeSelection();
        checkVolumeBatching();
        std::cout << "Forest visibility and volume batching tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
