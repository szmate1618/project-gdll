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
        checkVolumeBatching();
        std::cout << "Forest visibility and volume batching tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
