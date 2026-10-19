#include "vegetation_blast.hpp"
#include "tree_visibility.hpp"
#include "forest_tree_visibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(glm::vec4 a, glm::vec4 b) { return glm::length(a - b) < .0001f; }
viewer::TreeInstance shrub(glm::vec3 root = {0, 0, 0}) {
    viewer::TreeInstance tree;
    tree.transform = glm::translate(glm::mat4(1), root);
    tree.boundsMin = root + glm::vec3(-2, 0, -2);
    tree.boundsMax = root + glm::vec3(2, 3, 2);
    tree.dimensions = {4, 3, 4};
    tree.blastResponsive = true;
    return tree;
}
void motion() {
    auto tree = shrub({2, 7, -3});
    tree.transform = glm::rotate(tree.transform, .7f, glm::vec3(0, 1, 0));
    tree.transform = glm::scale(tree.transform, {-2, 3, 2});
    tree.transform[0].y = .2f; // Preserve a reflected, sheared source matrix.
    auto inert = tree; inert.blastResponsive = false;
    const viewer::VegetationBlastMotion motion({tree, inert, shrub({40, 7, -3})});
    viewer::GrenadeExplosion event{1, {-1, 7, -3}, .1f};
    const auto bends = motion.sample({event});
    require(bends.size() == 1 && bends[0].instance == 0,
            "Only nearby responsive shrubs should move");
    const auto& bend = bends[0];
    require(bend.rest == tree.transform && bend.transform[3] == tree.transform[3],
            "The root and authoritative matrix must be preserved");
    const auto top = bend.transform * glm::vec4(0, 1, 0, 1);
    const auto restTop = tree.transform * glm::vec4(0, 1, 0, 1);
    require(top.x > restTop.x && top.y == restTop.y && top.z == restTop.z,
            "Initial push should be outward in world space without lifting roots");
    // The shared quarter-height intersection of all three cards stays together.
    const auto intersection = bend.transform * glm::vec4(0, .25f, 0, 1);
    require(near(intersection, glm::mix(bend.transform[3], top, .25f)),
            "Horizontal and upright cards separated during the bend");
    for (const auto point : {glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 1, 1)}) {
        const auto rest = tree.transform * point;
        const auto moved = bend.transform * point;
        const float slope = (top.x - restTop.x) / (restTop.y - tree.transform[3].y);
        require(std::abs(moved.x - rest.x - slope * (rest.y - tree.transform[3].y)) < .0001f &&
                moved.y == rest.y, "World shear lost the source rotation, reflection or shear");
    }
    require(motion.sample({event})[0].transform == bend.transform, "Sampling accumulated deformation");
    event.age = .35f;
    require((motion.sample({event})[0].transform * glm::vec4(0, 1, 0, 1)).x < restTop.x,
            "Shrub should rebound before settling");
    for (float age : {0.0f, -.1f, viewer::VegetationBlastMotion::duration, 1.0f,
                      std::numeric_limits<float>::quiet_NaN()}) {
        event.age = age;
        require(motion.sample({event}).empty(), "Inactive events must leave shrubs at rest");
    }
    require(motion.sample({}).empty(), "Clearing events must restore rest pose");
}
void rangeAndBounds() {
    const auto tree = shrub();
    const viewer::VegetationBlastMotion motion({tree});
    const auto top = glm::vec4(0, 3, 0, 1);
    const auto near = motion.sample({{1, {-1, 1.5f, 0}, .1f}})[0].transform * top;
    const auto far = motion.sample({{1, {-9, 1.5f, 0}, .1f}})[0].transform * top;
    require(near.x > far.x && far.x > 0, "Blast response should fade with distance");
    require(motion.sample({{1, {-12, 1.5f, 0}, .1f}}).empty() &&
            motion.sample({{1, {0, 20, 0}, .1f}}).empty(), "Blast range must be a 3D sphere");
    require(std::isfinite(motion.sample({{1, {0, 1.5f, 0}, .1f}})[0].transform[1].x),
            "A directly overhead explosion produced an invalid direction");
    std::vector<viewer::GrenadeExplosion> events;
    for (unsigned i = 0; i < 128; ++i) events.push_back({i, {-1, 1.5f, -1}, .1f});
    const auto bent = motion.sample(events)[0].transform;
    const auto displacement = bent * top - top;
    require(glm::length(glm::vec2(displacement.x, displacement.z)) <=
            3 * viewer::VegetationBlastMotion::maxSlope + .0001f,
            "Overlapping explosions exceeded the conservative motion limit");
    const auto bounds = viewer::vegetationBlastBounds(tree);
    for (float x : {-2.0f, 2.0f}) for (float y : {0.0f, 3.0f}) for (float z : {-2.0f, 2.0f}) {
        const glm::vec3 point(bent * glm::vec4(x, y, z, 1));
        require(glm::all(glm::greaterThanEqual(point, bounds.minimum)) &&
                glm::all(glm::lessThanEqual(point, bounds.maximum)), "Animated shrub escaped culling bounds");
    }
    auto edge = shrub({4, 0, 0}); // Rest bounds start at x=2, outside identity frustum.
    const std::vector<viewer::TreeInstance> trees{edge};
    std::vector<std::size_t> visible;
    viewer::TreeVisibility(trees).query(glm::mat4(1), visible);
    require(visible.size() == 1, "Flat visibility omitted motion extending into the view");
    viewer::ForestPatch patch; patch.firstTree = 0; patch.treeCount = 1;
    viewer::ForestTreeVisibility(trees, {patch}).query(glm::mat4(1), {true}, true, visible);
    require(visible.size() == 1, "Forest group visibility omitted motion extending into the view");
    edge.blastResponsive = false;
    const auto unchanged = viewer::vegetationBlastBounds(edge);
    require(unchanged.minimum == edge.boundsMin && unchanged.maximum == edge.boundsMax,
            "Ordinary tree bounds changed");
}
}
int main() {
    try { motion(); rangeAndBounds(); std::cout << "Vegetation blast motion and bounds passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
