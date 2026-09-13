#include "tree_visibility.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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

viewer::TreeInstance box(glm::vec3 minimum, glm::vec3 maximum) {
    viewer::TreeInstance result;
    result.boundsMin = minimum;
    result.boundsMax = maximum;
    return result;
}

void testClipPlanes() {
    const viewer::TreeFrustum frustum(perspective());
    require(frustum.intersects({-.1f, -.1f, -5.1f}, {.1f, .1f, -4.9f}), "Visible tree was culled");
    require(!frustum.intersects({-1, -1, 2}, {1, 1, 3}), "Tree behind the eye was accepted");
    require(!frustum.intersects({-.1f, -.1f, -.4f}, {.1f, .1f, -.2f}), "Tree before near plane was accepted");
    require(frustum.intersects({-.1f, -.1f, -1.1f}, {.1f, .1f, -.9f}), "Tree crossing near plane was culled");
    require(!frustum.intersects({-1, -1, -105}, {1, 1, -102}), "Tree beyond far plane was accepted");
    require(frustum.intersects({-1, -1, -101}, {1, 1, -99}), "Tree crossing far plane was culled");
    require(!frustum.intersects({3.2f, -.1f, -5.1f}, {3.8f, .1f, -4.9f}), "Tree beyond right plane was accepted");
    require(!frustum.intersects({-3.8f, -.1f, -5.1f}, {-3.2f, .1f, -4.9f}), "Tree beyond left plane was accepted");
    require(!frustum.intersects({-.1f, 3.2f, -5.1f}, {.1f, 3.8f, -4.9f}), "Tree beyond top plane was accepted");
    require(!frustum.intersects({-.1f, -3.8f, -5.1f}, {.1f, -3.2f, -4.9f}), "Tree beyond bottom plane was accepted");
    require(frustum.intersects({0, 0, -10}, {100, 10, -5}), "Intersecting canopy was culled because its center is outside");
    require(frustum.intersects({-100, -100, -110}, {100, 100, 10}), "Bounds containing the entire frustum were culled");
    const viewer::TreeFrustum narrow(perspective(.3f));
    require(frustum.intersects({2, -.1f, -5.1f}, {2.1f, .1f, -4.9f}), "Wide viewport rejected its visible bounds");
    require(!narrow.intersects({2, -.1f, -5.1f}, {2.1f, .1f, -4.9f}), "Narrow viewport reused the wide frustum");
    const viewer::TreeFrustum canonical(glm::mat4(1));
    require(canonical.intersects({1, -.1f, -.1f}, {1.005f, .1f, .1f}), "Exact clip-plane contact must remain visible");
    require(canonical.classify({-.1f, -.1f, -.1f}, {.1f, .1f, .1f}) == viewer::TreeFrustum::Relation::Inside,
            "Wholly visible bounds were not classified inside");
}

void testCameraRotation() {
    const auto view = glm::lookAt(glm::vec3(10, 5, 20), glm::vec3(20, 5, 20), glm::vec3(0, 1, 0));
    const viewer::TreeFrustum frustum(perspective() * view);
    require(frustum.intersects({19, 4, 19}, {21, 6, 21}), "Translated/rotated camera rejected a tree ahead");
    require(!frustum.intersects({-1, 4, 19}, {1, 6, 21}), "Translated/rotated camera accepted a tree behind");
    const viewer::TreeFrustum infinite(glm::infinitePerspective(glm::radians(60.0f), 1.0f, 1.0f));
    require(infinite.intersects({-1, -1, -100000}, {1, 1, -99999}), "Infinite far plane rejected distant geometry");
}

// Independent clip-space oracle: reject only when every corner is outside the
// same OpenGL clip inequality. Test bounds avoid the deliberate 1 cm tolerance.
bool clipSpaceIntersects(const viewer::TreeInstance& instance, const glm::mat4& matrix) {
    bool allOutside[6] = {true, true, true, true, true, true};
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 vertex{(corner & 1) ? instance.boundsMax.x : instance.boundsMin.x,
                               (corner & 2) ? instance.boundsMax.y : instance.boundsMin.y,
                               (corner & 4) ? instance.boundsMax.z : instance.boundsMin.z};
        const auto clip = matrix * glm::vec4(vertex, 1);
        for (int axis = 0; axis < 3; ++axis) {
            allOutside[axis * 2] &= clip[axis] < -clip.w;
            allOutside[axis * 2 + 1] &= clip[axis] > clip.w;
        }
    }
    return std::none_of(std::begin(allOutside), std::end(allOutside), [](bool value) { return value; });
}

void testSpatialIndex() {
    std::vector<viewer::TreeInstance> instances;
    for (int x = -50; x <= 50; ++x) {
        for (int z = -50; z <= 50; ++z) {
            const glm::vec3 base(x * 20.0f + 1.3f, -1.4f, z * 20.0f + 2.7f);
            instances.push_back(box(base, base + glm::vec3(3, 10, 4)));
        }
    }
    const viewer::TreeVisibility index(instances);
    const auto view = glm::lookAt(glm::vec3(0, 7, 50), glm::vec3(0, 7, -1), glm::vec3(0, 1, 0));
    std::vector<std::size_t> visible{999999};
    for (const auto& matrix : {perspective(.45f) * view, perspective(2.0f) * view}) {
        const auto stats = index.query(matrix, visible);
        std::vector<std::size_t> expected;
        for (std::size_t i = 0; i < instances.size(); ++i)
            if (clipSpaceIntersects(instances[i], matrix)) expected.push_back(i);
        std::sort(visible.begin(), visible.end());
        require(visible == expected, "Spatial index dropped visible bounds or returned outside/duplicate indices");
        require(!visible.empty() && visible.size() < instances.size() / 10, "Narrow scene fixture did not limit candidates");
        require(stats.testedInstances < instances.size() / 10, "Spatial index scanned every instance despite distant clusters");
        require(stats.testedNodes > 1, "Spatial index did not traverse its hierarchy");
    }
    const auto outsideView = glm::lookAt(glm::vec3(10000, 0, 10000), glm::vec3(10001, 0, 10000), glm::vec3(0, 1, 0));
    const auto rejected = index.query(perspective() * outsideView, visible);
    require(visible.empty() && rejected.testedNodes == 1 && rejected.testedInstances == 0,
            "Entirely outside spatial root was not rejected before testing instances");
    const auto allInside = glm::ortho(-2000.0f, 2000.0f, -2000.0f, 2000.0f, -2000.0f, 2000.0f);
    const auto accepted = index.query(allInside, visible);
    require(visible.size() == instances.size() && accepted.testedNodes == 1 && accepted.testedInstances == 0,
            "Entirely visible spatial root was not accepted without per-instance tests");
}

void testEmptyAndInvalidBounds() {
    const viewer::TreeVisibility empty;
    std::vector<std::size_t> visible{0};
    const auto stats = empty.query(perspective(), visible);
    require(visible.empty() && stats.testedNodes == 0 && stats.testedInstances == 0, "Empty layer query retained stale visibility");
    for (const auto& invalid : {box({1, 0, 0}, {0, 1, 1}),
                               box({0, 0, 0}, {1, std::numeric_limits<float>::infinity(), 1})}) {
        bool rejected = false;
        try { const viewer::TreeVisibility bad({invalid}); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Invalid spatial bounds were accepted");
    }
}

} // namespace

int main() {
    try {
        testClipPlanes();
        testCameraRotation();
        testSpatialIndex();
        testEmptyAndInvalidBounds();
        std::cout << "Tree visibility tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Tree visibility test failed: " << error.what() << '\n';
        return 1;
    }
}
