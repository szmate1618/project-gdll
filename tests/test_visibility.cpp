#include "visibility.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::VisibilityBounds box(glm::vec3 center, glm::vec3 halfSize = glm::vec3(0.25f)) {
    return {center - halfSize, center + halfSize};
}

glm::mat4 perspective(float fov = 60, float aspect = 1.3f, float near = 0.2f, float far = 160) {
    return glm::perspective(glm::radians(fov), aspect, near, far);
}

// Independent oracle: transform all eight world-space AABB corners into clip
// space, then reject only if every corner is beyond the same clip boundary.
// Oracle fixtures stay away from the production culler's 1 cm tolerance.
bool clipIntersects(const viewer::VisibilityBounds& bounds, const glm::mat4& matrix) {
    bool outside[6] = {true, true, true, true, true, true};
    for (int corner = 0; corner < 8; ++corner) {
        const auto point = glm::vec3((corner & 1) ? bounds.maximum.x : bounds.minimum.x,
                                     (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
                                     (corner & 4) ? bounds.maximum.z : bounds.minimum.z);
        const auto clip = matrix * glm::vec4(point, 1);
        for (int axis = 0; axis < 3; ++axis) {
            outside[axis * 2] &= clip[axis] < -clip.w;
            outside[axis * 2 + 1] &= clip[axis] > clip.w;
        }
    }
    return std::none_of(std::begin(outside), std::end(outside), [](bool value) { return value; });
}

void requireOracle(const std::vector<viewer::VisibilityBounds>& bounds, const glm::mat4& matrix,
                   std::vector<std::size_t> actual) {
    std::vector<std::size_t> expected;
    for (std::size_t i = 0; i < bounds.size(); ++i)
        if (clipIntersects(bounds[i], matrix)) expected.push_back(i);
    std::sort(actual.begin(), actual.end());
    require(actual == expected, "Frustum query disagreed with clip-space corner oracle");
}

std::vector<std::size_t> requireSame(const viewer::VisibilityIndex& index, const glm::mat4& matrix,
                                     viewer::VisibilityQueryState& state,
                                     const viewer::HorizontalVisibilityRange* range = nullptr,
                                     bool frustumCulling = true) {
    std::vector<std::size_t> fixed{9999}, coherent{9998};
    const auto fixedStats = index.query(matrix, fixed, range, frustumCulling);
    const auto coherentStats = index.query(matrix, coherent, range, frustumCulling, &state);
    require(fixed == coherent, "Temporal coherence changed visibility or traversal order");
    require(fixedStats.testedNodes == coherentStats.testedNodes &&
                fixedStats.testedInstances == coherentStats.testedInstances,
            "Temporal coherence changed BVH traversal");
    require(fixedStats.temporalRejects == 0, "Stateless query reported temporal rejects");
    if (!frustumCulling)
        require(fixedStats.testedPlanes == 0 && coherentStats.testedPlanes == 0 &&
                    coherentStats.temporalRejects == 0,
                "Disabled frustum culling performed plane tests");
    return coherent;
}

std::vector<viewer::VisibilityBounds> scene() {
    std::vector<viewer::VisibilityBounds> bounds;
    for (int z = -8; z <= 8; ++z)
        for (int x = -7; x <= 7; ++x)
            for (int y = 0; y < 3; ++y)
                bounds.push_back(box({x * 13.7f + 0.31f, y * 7.3f - 3.1f, z * 11.9f + 0.47f},
                                     {0.7f, 1.1f, 0.9f}));
    return bounds;
}

void testMotionAndProjectionChanges() {
    const viewer::VisibilityIndex index(scene());
    viewer::VisibilityQueryState state;
    for (int frame = 0; frame < 120; ++frame) {
        const float time = frame * 0.025f;
        const glm::vec3 eye(8 * std::sin(time), 4, 65 - frame * 0.15f);
        const auto view = glm::lookAt(eye, eye + glm::vec3(std::sin(time * 0.2f), -0.05f, -1), {0, 1, 0});
        requireSame(index, perspective() * view, state);
    }

    // Reuse warm history across cuts, teleports, scope zoom, resized windows,
    // changed depth range, and a switch to a shadow-style orthographic camera.
    const auto nearView = glm::lookAt(glm::vec3(0, 5, 45), glm::vec3(0, 3, 0), glm::vec3(0, 1, 0));
    const auto cutView = glm::lookAt(glm::vec3(-45, 12, -30), glm::vec3(0, 3, 35), glm::vec3(0, 1, 0));
    const auto teleport = glm::lookAt(glm::vec3(500, 70, 350), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    for (const auto& matrix : std::vector<glm::mat4>{
             perspective() * cutView, perspective() * teleport, perspective() * nearView,
             perspective(8) * nearView, perspective(105, 0.35f) * cutView,
             perspective(50, 3.2f, 12, 65) * nearView,
             glm::ortho(-35.0f, 35.0f, -20.0f, 20.0f, 1.0f, 180.0f) * cutView,
             perspective() * nearView}) {
        requireSame(index, matrix, state);
    }

    // Deliberately separated from boundaries so the exact oracle and the
    // conservative 1 cm production tolerance have the same expected result.
    const std::vector<viewer::VisibilityBounds> oracleBounds{
        box({0, 0, -5}), box({12, 0, -5}), box({-12, 0, -5}), box({0, 12, -5}),
        box({0, -12, -5}), box({0, 0, 5}), box({0, 0, -220}), box({8, 2, -35})};
    const viewer::VisibilityIndex oracleIndex(oracleBounds);
    for (const auto& matrix : std::vector<glm::mat4>{
             perspective(), perspective(20, 0.6f), perspective(100, 2, 2, 40),
             glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f),
             perspective() * glm::lookAt(glm::vec3(30, 0, 0), glm::vec3(0, 0, -5), glm::vec3(0, 1, 0))}) {
        requireOracle(oracleBounds, matrix, requireSame(oracleIndex, matrix, state));
    }
}

void testRejectingNodeAndInstancePlanes() {
    const auto matrix = perspective(60, 1, 1, 100);
    for (const auto center : {glm::vec3(100, 0, -5), glm::vec3(0, 0, -200)}) {
        // Entire rejected subtrees need only one plane on subsequent frames.
        const viewer::VisibilityIndex outside(std::vector<viewer::VisibilityBounds>{box(center)});
        viewer::VisibilityQueryState state;
        std::vector<std::size_t> fixed, coherent;
        const auto baseline = outside.query(matrix, fixed);
        outside.query(matrix, coherent, nullptr, true, &state);
        const auto warm = outside.query(matrix, coherent, nullptr, true, &state);
        require(fixed.empty() && coherent.empty(), "Outside node unexpectedly visible");
        require(baseline.testedPlanes > 1 && warm.testedPlanes == 1 && warm.temporalRejects == 1,
                "Cached rejecting node plane did not reduce stationary plane tests");

        // A visible object keeps the root intersecting, exercising per-instance
        // history rather than allowing a single root rejection to hide it.
        std::vector<viewer::VisibilityBounds> bounds{box({0, 0, -5})};
        for (int i = 0; i < 24; ++i) bounds.push_back(box(center + glm::vec3(0, i * 0.001f, 0)));
        const viewer::VisibilityIndex mixed(bounds);
        state = viewer::VisibilityQueryState{};
        const auto fixedLeaf = mixed.query(matrix, fixed);
        mixed.query(matrix, coherent, nullptr, true, &state);
        const auto warmLeaf = mixed.query(matrix, coherent, nullptr, true, &state);
        require(fixed == std::vector<std::size_t>({0}) && fixed == coherent,
                "Intersecting leaf query returned incorrect instances");
        require(warmLeaf.testedInstances == bounds.size() && warmLeaf.temporalRejects == 24 &&
                    warmLeaf.testedPlanes + 24 <= fixedLeaf.testedPlanes,
                "Cached rejecting instance planes did not reduce stationary plane tests");
    }
}

void testStateLifetimeAndIndependentStreams() {
    const auto bounds = scene();
    viewer::VisibilityIndex index(bounds);
    viewer::VisibilityQueryState cameraState, lightState;
    const auto light = glm::ortho(-40.0f, 40.0f, -40.0f, 40.0f, 1.0f, 240.0f) *
        glm::lookAt(glm::vec3(75, 100, 45), glm::vec3(0), glm::vec3(0, 1, 0));
    for (int frame = 0; frame < 20; ++frame) {
        const auto camera = perspective() * glm::lookAt(glm::vec3(frame * 2, 3, 50),
                                                       glm::vec3(0), glm::vec3(0, 1, 0));
        requireSame(index, camera, cameraState);
        requireSame(index, light, lightState);
    }
    auto copiedState = cameraState;
    auto movedState = std::move(copiedState);
    requireSame(index, light, movedState);
    requireSame(index, perspective(), cameraState);
    const auto copiedIndex = index;
    auto movedIndex = viewer::VisibilityIndex(copiedIndex);
    const auto finalIndex = std::move(movedIndex);
    requireSame(copiedIndex, light, cameraState);
    requireSame(finalIndex, perspective(), cameraState);

    // Same-sized replacements must recheck current bounds; history must also
    // remain safe when the replacement has a different BVH and object count.
    std::vector<viewer::VisibilityBounds> replacements(48, box({100, 0, -5}));
    index = viewer::VisibilityIndex(replacements);
    requireSame(index, perspective(), cameraState);
    replacements.assign(48, box({0, 0, -5}));
    index = viewer::VisibilityIndex(replacements);
    require(requireSame(index, perspective(), cameraState).size() == replacements.size(),
            "Same-sized replacement retained stale visibility");
    replacements.assign(3, box({0, 0, -5}));
    index = viewer::VisibilityIndex(replacements);
    require(requireSame(index, perspective(), cameraState).size() == 3,
            "Different-sized replacement retained stale visibility");
    index = viewer::VisibilityIndex{};
    require(requireSame(index, perspective(), cameraState).empty(), "Empty replacement retained results");
    index = finalIndex;
    requireSame(index, light, cameraState);
}

void testDiscontinuityFallbackAndRecovery() {
    const viewer::VisibilityIndex index(std::vector<viewer::VisibilityBounds>{box({1000, 0, -100})});
    viewer::VisibilityQueryState state;
    const auto projection = perspective(60, 1, 1, 100);
    std::vector<std::size_t> fixed, coherent;
    index.query(projection, coherent, nullptr, true, &state);
    const auto initialWarm = index.query(projection, coherent, nullptr, true, &state);
    require(initialWarm.testedPlanes == 1 && initialWarm.temporalRejects == 1,
            "Initial steady view did not learn its rejecting plane");

    // The bound changes from right-plane rejection to far-plane rejection.
    // A cut should use fixed order once, then relearn the useful plane.
    const auto turned = projection * glm::lookAt(glm::vec3(0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    const auto teleported = projection * glm::lookAt(glm::vec3(-100, 0, 0), glm::vec3(-99, 0, 0),
                                                     glm::vec3(0, 1, 0));
    for (const auto& matrix : {turned, teleported}) {
        const auto baseline = index.query(matrix, fixed);
        const auto cut = index.query(matrix, coherent, nullptr, true, &state);
        require(fixed == coherent && cut.testedPlanes == baseline.testedPlanes && cut.temporalRejects == 0,
                "Large camera discontinuity did not use fixed plane order");
        index.query(matrix, coherent, nullptr, true, &state);
        const auto recovered = index.query(matrix, coherent, nullptr, true, &state);
        require(fixed == coherent && recovered.testedPlanes == 1 && recovered.temporalRejects == 1 &&
                    recovered.testedPlanes < baseline.testedPlanes,
                "Steady view did not recover temporal rejection after a camera cut");
    }
}

void testAppendEmptyAndAabbQueries() {
    const auto bounds = scene();
    const viewer::VisibilityIndex index(bounds), empty;
    viewer::VisibilityQueryState state;
    const auto expected = requireSame(index, perspective(), state);
    std::vector<std::size_t> appended{99999, 88888};
    index.appendQuery(perspective(), appended, nullptr, true, &state);
    require(appended.size() == expected.size() + 2 && appended[0] == 99999 && appended[1] == 88888 &&
                std::equal(expected.begin(), expected.end(), appended.begin() + 2),
            "Temporal append query changed prefix or result order");
    const viewer::VisibilityIndex second(std::vector<viewer::VisibilityBounds>{
        box({100, 0, -5}), box({0, 0, -5}), box({0, 0, -20})});
    viewer::VisibilityQueryState secondState;
    const auto secondExpected = requireSame(second, perspective(), secondState);
    const auto firstSize = appended.size();
    second.appendQuery(perspective(), appended, nullptr, true, &secondState);
    require(appended.size() == firstSize + secondExpected.size() &&
                std::equal(secondExpected.begin(), secondExpected.end(), appended.begin() + firstSize),
            "Appending another index changed its local source indices");
    const auto previous = appended;
    const auto appendStats = empty.appendQuery(perspective(), appended, nullptr, true, &state);
    require(appended == previous && appendStats.testedNodes == 0 && appendStats.testedPlanes == 0 &&
                appendStats.temporalRejects == 0,
            "Empty append changed results or reported work");
    const auto clearStats = empty.query(perspective(), appended, nullptr, true, &state);
    require(appended.empty() && clearStats.testedNodes == 0 && clearStats.testedInstances == 0,
            "Empty query failed to clear previous results");

    // Frustum history must not affect exact overlap queries used by collisions.
    const viewer::VisibilityBounds region{{-30, -5, -30}, {30, 20, 30}};
    const auto stats = index.query(region, appended);
    std::vector<std::size_t> expectedAabb;
    for (std::size_t i = 0; i < bounds.size(); ++i)
        if (glm::all(glm::lessThanEqual(bounds[i].minimum, region.maximum)) &&
            glm::all(glm::greaterThanEqual(bounds[i].maximum, region.minimum))) expectedAabb.push_back(i);
    std::sort(appended.begin(), appended.end());
    require(appended == expectedAabb && stats.testedPlanes == 0 && stats.temporalRejects == 0,
            "AABB query changed after adding temporal frustum history");
    requireSame(index, perspective(), state);
}

void testBoundariesAndDegenerateFrusta() {
    const std::vector<viewer::VisibilityBounds> bounds{
        {{1, 0, 0}, {1, 0, 0}}, {{1.005f, 0, 0}, {1.005f, 0, 0}},
        {{1.02f, 0, 0}, {1.02f, 0, 0}}, {{-1, -1, -1}, {1, 1, 1}},
        {{-1.02f, 0, 0}, {-1.02f, 0, 0}}, {{0, 0, 0}, {0, 0, 0}}};
    const viewer::VisibilityIndex index(bounds);
    viewer::VisibilityQueryState state;
    for (int repeat = 0; repeat < 3; ++repeat) {
        auto visible = requireSame(index, glm::mat4(1), state);
        std::sort(visible.begin(), visible.end());
        require(visible == std::vector<std::size_t>({0, 1, 3, 5}),
                "Clip boundary or conservative tolerance changed");
        require(requireSame(index, glm::mat4(0), state).size() == bounds.size(),
                "Degenerate planes rejected bounds");
        auto invalid = glm::mat4(1);
        invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
        requireSame(index, invalid, state);
    }
    const std::vector<viewer::VisibilityBounds> distant{
        box({0, 0, -5}), box({0, 0, -1000000}), box({100, 0, -5}), box({0, 0, 5})};
    const viewer::VisibilityIndex distantIndex(distant);
    for (int repeat = 0; repeat < 3; ++repeat) {
        auto visible = requireSame(distantIndex, glm::infinitePerspective(glm::radians(60.0f), 1.0f, 0.1f), state);
        std::sort(visible.begin(), visible.end());
        require(visible == std::vector<std::size_t>({0, 1}),
                "Infinite far plane rejected distant in-frustum bounds");
    }
    // An unchanged degenerate far plane must not disable the coherence path.
    // Keep visible and rejected instances in one intersecting leaf so both the
    // current frustum and remembered right-plane rejection are exercised.
    const viewer::VisibilityIndex infiniteIndex(std::vector<viewer::VisibilityBounds>{
        box({0, 0, -5}), box({100, 0, -5})});
    viewer::VisibilityQueryState infiniteState;
    const auto infiniteProjection = glm::infinitePerspective(glm::radians(60.0f), 1.0f, 0.1f);
    std::vector<std::size_t> visible;
    infiniteIndex.query(infiniteProjection, visible, nullptr, true, &infiniteState);
    const auto repeated = infiniteIndex.query(infiniteProjection, visible, nullptr, true, &infiniteState);
    require(visible == std::vector<std::size_t>{0} && repeated.temporalRejects > 0,
            "Stable infinite projection disabled temporal plane hints");
}

void testIntersectionPredicate() {
    const viewer::Frustum unit(glm::mat4(1));
    for (int axis = 0; axis < 3; ++axis) for (const float side : {-1.f, 1.f}) {
        glm::vec3 point(0);
        point[axis] = side;
        require(unit.intersects(point, point), "Boolean frustum test rejected a clip-plane point");
        point[axis] = side * 1.005f;
        require(unit.intersects(point, point), "Boolean frustum test lost the 1 cm tolerance");
        point[axis] = side * 1.01f;
        require(unit.intersects(point, point), "Boolean frustum test rejected the tolerance boundary");
        point[axis] = side * std::nextafter(1.01f, std::numeric_limits<float>::infinity());
        require(!unit.intersects(point, point), "Boolean frustum test exceeded the tolerance by one float step");
        point[axis] = side * 1.02f;
        require(!unit.intersects(point, point), "Boolean frustum test retained a point beyond tolerance");
        const auto crossing = box(point, glm::vec3(.03f));
        require(unit.intersects(crossing.minimum, crossing.maximum),
                "Boolean frustum test rejected bounds crossing a plane with an outside center");
    }

    // The boolean fast path must agree with full classification for perspective,
    // shadow, infinite and degenerate projections, including oblique planes.
    const auto view = glm::lookAt(glm::vec3(35, 12, 45), glm::vec3(-5, 2, -10), glm::vec3(0, 1, 0));
    auto invalid = glm::mat4(1);
    invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
    auto bounds = scene();
    bounds.push_back({glm::vec3(-1000000), glm::vec3(1000000)});
    bounds.push_back(box({0, 0, -1000000}));
    bounds.push_back(box({0, 0, 0}, glm::vec3(0)));
    for (const auto& matrix : std::vector<glm::mat4>{
             glm::mat4(1), glm::mat4(0), invalid, perspective(), perspective(8, .35f) * view,
             perspective(105, 3.2f, 2, 240) * view,
             glm::ortho(-35.f, 35.f, -20.f, 20.f, 1.f, 180.f) * view,
             glm::infinitePerspective(glm::radians(60.f), 1.f, .1f) * view}) {
        const viewer::Frustum frustum(matrix);
        for (const auto& bound : bounds)
            require(frustum.intersects(bound.minimum, bound.maximum) ==
                        (frustum.classify(bound.minimum, bound.maximum) != viewer::Frustum::Relation::Outside),
                    "Boolean frustum test disagreed with full classification");
    }
}

void testHorizontalRangeAndFrustumBypass() {
    // Known circle overlaps exercise tangency, geometry rooted beyond the
    // range, and vertically unlimited range while the frustum is disabled.
    const std::vector<viewer::VisibilityBounds> bounds{
        box({0, 0, -5}), {{4, -1, -1}, {8, 1, 1}}, box({0, 1000, 0}),
        {{-12, -1, -1}, {-8, 1, 1}}, box({0, 0, 8}), box({30, 0, 0})};
    const viewer::VisibilityIndex index(bounds);
    viewer::VisibilityQueryState state;
    viewer::HorizontalVisibilityRange range{{0, 0}, 4};
    auto sorted = [](std::vector<std::size_t> indices) {
        std::sort(indices.begin(), indices.end());
        return indices;
    };
    require(sorted(requireSame(index, perspective(), state, &range, false)) ==
                std::vector<std::size_t>({1, 2}),
            "Frustum bypass failed circle tangency or unlimited vertical range");
    range.radius = 8;
    require(sorted(requireSame(index, perspective(), state, &range, false)) ==
                std::vector<std::size_t>({0, 1, 2, 3, 4}),
            "Changing radius reused stale range visibility");
    range.center = {30, 0};
    require(sorted(requireSame(index, perspective(), state, &range, false)) ==
                std::vector<std::size_t>({5}),
            "Moving the range center reused stale visibility");
    range.center = {0, -5};
    range.radius = 1;
    require(requireSame(index, perspective(), state, &range) == std::vector<std::size_t>({0}),
            "Combined range and frustum query missed in-range geometry");

    // Move and resize the circle through a multi-level BVH with warmed plane
    // hints, comparing against stateless queries on every frame.
    const auto manyBounds = scene();
    const viewer::VisibilityIndex many(manyBounds);
    for (int frame = 0; frame < 90; ++frame) {
        const float phase = frame * 0.11f;
        range.center = {std::sin(phase) * 70, std::cos(phase) * 60};
        range.radius = 5 + (frame % 7) * 13;
        const auto matrix = perspective() * glm::lookAt(glm::vec3(frame * 0.5f, 4, 65),
                                                         glm::vec3(0), glm::vec3(0, 1, 0));
        requireSame(many, matrix, state, &range);
        requireSame(many, matrix, state, &range, false);
    }
    range = {{0, 0}, 200};
    const auto inside = requireSame(many, glm::mat4(0), state, &range, false);
    require(inside.size() == manyBounds.size(), "Range containment omitted a full subtree");
    range.radius = 0;
    require(requireSame(many, glm::mat4(0), state, &range, false).size() == 3,
            "Shrinking a previously containing range retained a full subtree");

    std::vector<std::size_t> appended{999};
    range = {{30, 0}, 8};
    const auto appendedStats = index.appendQuery(perspective(), appended, &range, false, &state);
    require(appended == std::vector<std::size_t>({999, 5}) && appendedStats.testedPlanes == 0 &&
                appendedStats.temporalRejects == 0,
            "Range-only append changed prefix or performed plane tests");

    // Disabled culling must not overwrite the snapshot of the last actual
    // frustum query, even if the camera jumps while it is disabled.
    const viewer::VisibilityIndex distant(std::vector<viewer::VisibilityBounds>{box({1000, 0, -100})});
    viewer::VisibilityQueryState distantState;
    const auto projection = perspective(60, 1, 1, 100);
    std::vector<std::size_t> fixed, temporal;
    distant.query(projection, temporal, nullptr, true, &distantState);
    distant.query(projection, temporal, nullptr, true, &distantState);
    const auto cut = projection * glm::lookAt(glm::vec3(0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    require(requireSame(distant, cut, distantState, nullptr, false).size() == 1,
            "Frustum bypass retained the old frustum rejection");
    const auto baseline = distant.query(cut, fixed);
    const auto enabled = distant.query(cut, temporal, nullptr, true, &distantState);
    require(fixed == temporal && enabled.temporalRejects == 0 && enabled.testedPlanes == baseline.testedPlanes,
            "Disabled culling corrupted discontinuity fallback history");
}

} // namespace

int main() {
    try {
        testMotionAndProjectionChanges();
        testRejectingNodeAndInstancePlanes();
        testStateLifetimeAndIndependentStreams();
        testDiscontinuityFallbackAndRecovery();
        testAppendEmptyAndAabbQueries();
        testBoundariesAndDegenerateFrusta();
        testIntersectionPredicate();
        testHorizontalRangeAndFrustumBypass();
        std::cout << "Visibility temporal coherence tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Visibility temporal coherence test failed: " << error.what() << '\n';
        return 1;
    }
}
