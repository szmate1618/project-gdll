#include "model_visibility.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

glm::mat4 perspective(float aspect = 1.0f) {
    return glm::perspective(glm::radians(60.0f), aspect, 1.0f, 100.0f);
}

viewer::Primitive cube() {
    viewer::Primitive primitive;
    for (int corner = 0; corner < 8; ++corner) {
        viewer::Vertex vertex;
        vertex.position = {(corner & 1) ? 1.0f : -1.0f,
                           (corner & 2) ? 1.0f : -1.0f,
                           (corner & 4) ? 1.0f : -1.0f};
        primitive.vertices.push_back(vertex);
    }
    return primitive;
}

bool clipIntersects(const viewer::Model& model, const viewer::DrawInstance& draw,
                    const glm::mat4& projectionView) {
    bool allOutside[6] = {true, true, true, true, true, true};
    for (const auto& vertex : model.primitives[draw.primitive].vertices) {
        const auto clip = projectionView * draw.transform * glm::vec4(vertex.position, 1.0f);
        for (int axis = 0; axis < 3; ++axis) {
            allOutside[axis * 2] &= clip[axis] < -clip.w;
            allOutside[axis * 2 + 1] &= clip[axis] > clip.w;
        }
    }
    return std::none_of(std::begin(allOutside), std::end(allOutside), [](bool value) { return value; });
}

void requireMatchesVertexOracle(const viewer::Model& model, const glm::mat4& matrix, const char* message) {
    viewer::ModelVisibility visibility(model);
    std::vector<std::size_t> actual;
    visibility.query(matrix, actual);
    std::sort(actual.begin(), actual.end());
    std::vector<std::size_t> expected;
    for (std::size_t draw = 0; draw < model.draws.size(); ++draw)
        if (clipIntersects(model, model.draws[draw], matrix)) expected.push_back(draw);
    require(actual == expected, message);
}

void testTransformedDrawBounds() {
    viewer::Model model;
    model.primitives.push_back(cube());
    glm::mat4 skew(1.0f);
    skew[0][0] = -2.0f; // Reflection and non-uniform scale.
    skew[1][0] = 0.35f; // Shear.
    skew = glm::translate(glm::mat4(1.0f), {1.5f, 0.0f, -7.0f}) * skew;
    model.draws.push_back({0, skew, "skewed"});
    model.draws.push_back({0, glm::translate(glm::mat4(1.0f), {40.0f, 0.0f, -7.0f}), "outside"});
    requireMatchesVertexOracle(model, perspective(), "Transformed draw bounds diverged from vertex oracle");

    const auto view = glm::lookAt(glm::vec3(10, 5, 20), glm::vec3(20, 5, 20), glm::vec3(0, 1, 0));
    model.draws[0].transform = glm::translate(glm::mat4(1.0f), {20.0f, 5.0f, 20.0f}) * skew;
    model.draws[1].transform = glm::translate(glm::mat4(1.0f), {0.0f, 5.0f, 20.0f});
    requireMatchesVertexOracle(model, perspective(0.45f) * view,
                               "Rotated camera/aspect ratio produced incorrect model visibility");
}

void testClipEdgeAndReusedMesh() {
    viewer::Model model;
    model.primitives.push_back(cube());
    model.draws.push_back({0, glm::translate(glm::scale(glm::mat4(1.0f), {5.0f, 1.0f, 1.0f}), {0.9f, 0.0f, -5.0f}), "edge"});
    model.draws.push_back({0, glm::translate(glm::mat4(1.0f), {-1.0f, 0.0f, -5.0f}), "left"});
    model.draws.push_back({0, glm::translate(glm::mat4(1.0f), {30.0f, 0.0f, -5.0f}), "right"});
    viewer::ModelVisibility visibility(model);
    std::vector<std::size_t> visible;
    visibility.query(perspective(), visible);
    std::sort(visible.begin(), visible.end());
    require(visible == std::vector<std::size_t>({0, 1}),
            "Draws sharing a mesh did not retain their disjoint transformed bounds");
    requireMatchesVertexOracle(model, perspective(), "Clip edge bounds were culled despite visible geometry");
}

void testSpatialIndexAndEmptyModel() {
    viewer::Model model;
    model.primitives.push_back(cube());
    for (int x = -50; x <= 50; ++x) for (int z = -50; z <= 50; ++z)
        model.draws.push_back({0, glm::translate(glm::mat4(1.0f), {x * 20.0f, 0.0f, z * 20.0f}), ""});
    viewer::ModelVisibility visibility(model);
    const auto view = glm::lookAt(glm::vec3(0, 0, 50), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    std::vector<std::size_t> visible;
    const auto stats = visibility.query(perspective(0.45f) * view, visible);
    require(!visible.empty() && stats.testedInstances < model.draws.size() / 10,
            "Model visibility scanned every distant draw");

    viewer::ModelVisibility empty(viewer::Model{});
    visible = {1};
    const auto emptyStats = empty.query(perspective(), visible);
    require(visible.empty() && emptyStats.testedNodes == 0 && emptyStats.testedInstances == 0,
            "Empty model visibility retained stale results");
}

} // namespace

int main() {
    try {
        testTransformedDrawBounds();
        testClipEdgeAndReusedMesh();
        testSpatialIndexAndEmptyModel();
        std::cout << "Model visibility tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Model visibility test failed: " << error.what() << '\n';
        return 1;
    }
}
