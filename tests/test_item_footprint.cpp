#include "inventory/item_footprint.hpp"
#include "inventory/item_config.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function, const char* message) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
std::vector<glm::vec3> box(glm::vec3 size) {
    std::vector<glm::vec3> result;
    for (const float x : {-1.f, 1.f}) for (const float y : {-1.f, 1.f}) for (const float z : {-1.f, 1.f})
        result.push_back(size * glm::vec3(x, y, z) * .5f);
    return result;
}
std::vector<glm::vec3> transformed(std::vector<glm::vec3> points, glm::quat rotation, glm::vec3 translation) {
    for (auto& point : points) point = rotation * point + translation;
    return points;
}
void sameSize(const viewer::ItemFootprint& a, const viewer::ItemFootprint& b) {
    require(a.width == b.width && a.height == b.height, "Rigid transforms changed the item grid footprint");
    require(glm::length(a.dimensions - b.dimensions) < 5e-5f, "Rigid transforms changed the oriented box dimensions");
}
}

int main() {
    try {
        const auto shape = box({1.2f, .32f, .025f});
        const auto reference = viewer::estimateItemFootprint(shape, 4);
        require(reference.width == 2 && reference.height == 5 &&
                glm::length(reference.dimensions - glm::vec3(1.2f, .32f, .025f)) < 1e-6f,
                "A long thin prop needs the two broadest physical dimensions in portrait cells");
        for (int index = 0; index < 24; ++index) {
            const auto q = glm::angleAxis(.19f * index, glm::normalize(glm::vec3(1.f + index, .7f, 2.3f)));
            const auto cloud = transformed(shape, q, {3, -7, 5});
            const auto result = viewer::estimateItemFootprint(cloud, 4);
            sameSize(reference, result);
            const auto local = glm::conjugate(result.rotation);
            glm::vec3 low(std::numeric_limits<float>::infinity()), high = -low;
            for (const auto point : cloud) {
                const auto inBox = local * point;
                low = glm::min(low, inBox); high = glm::max(high, inBox);
            }
            require(glm::length(high - low - result.dimensions) < 2e-5f,
                    "Returned orientation does not describe the measured box axes");
        }

        // A PCA-only orientation remains ambiguous for a cube. Its measured
        // footprint must still be invariant under arbitrary world rotations.
        const auto cube = box({.5f, .5f, .5f});
        const auto cubeFootprint = viewer::estimateItemFootprint(cube, 4);
        require(cubeFootprint.width == 2 && cubeFootprint.height == 2, "An exact two-cell cube gained a rounding cell");
        for (int index = 0; index < 12; ++index)
            sameSize(cubeFootprint, viewer::estimateItemFootprint(transformed(cube,
                glm::angleAxis(.37f * index, glm::normalize(glm::vec3(.8f, 1.1f, .6f))), {}), 4));

        const auto broad = viewer::estimateItemFootprint(box({1.2f, .9f, .004f}), 4);
        const auto thicker = viewer::estimateItemFootprint(box({1.2f, .9f, .2f}), 4);
        require(broad.width == 4 && broad.height == reference.height && broad.width > reference.width,
                "A broad flat object must occupy more columns than a thin object of equal length");
        require(broad.width == thicker.width && broad.height == thicker.height,
                "The thinnest physical dimension must not consume inventory cells");
        auto flat = box({.8f, .4f, 0});
        sameSize(viewer::estimateItemFootprint(flat, 5), viewer::estimateItemFootprint(transformed(flat,
            glm::angleAxis(.93f, glm::normalize(glm::vec3(1, 2, 3))), {1, 2, 3}), 5));
        const auto line = viewer::estimateItemFootprint({{0, 0, 0}, {0, 2, 0}}, 4);
        require(line.width == 1 && line.height == 8 && line.dimensions.y < 1e-6f,
                "A line-like prop must keep its length and minimum one-cell width");
        const auto point = viewer::estimateItemFootprint({{1, 2, 3}}, 4);
        require(point.width == 1 && point.height == 1, "Degenerate point geometry must remain a one-cell item");
        auto duplicates = shape;
        duplicates.insert(duplicates.end(), shape.begin(), shape.end());
        std::reverse(duplicates.begin(), duplicates.end());
        sameSize(reference, viewer::estimateItemFootprint(duplicates, 4));

        auto compound = box({.8f, .2f, .05f});
        const auto extension = transformed(box({.2f, .8f, .05f}), {1, 0, 0, 0}, {.3f, .3f, 0});
        compound.insert(compound.end(), extension.begin(), extension.end());
        const auto compoundReference = viewer::estimateItemFootprint(compound, 4);
        for (int index = 0; index < 8; ++index)
            sameSize(compoundReference, viewer::estimateItemFootprint(transformed(compound,
                glm::angleAxis(.31f * index, glm::normalize(glm::vec3(1, 3, 2))), {2, 4, 1}), 4));

        const std::vector<viewer::ItemFootprintReference> references{
            {box({.35f, .15f, .02f}), 2, 4}, {box({.75f, .25f, .02f}), 8, 3},
            {box({.001f, .001f, .001f}), 1, 1}};
        const auto scale = viewer::calibrateItemFootprint(references);
        require(std::abs(scale - 10) < 1e-4f, "Calibration must recover the authored multi-cell physical scale");
        auto rotatedReferences = references;
        for (auto& item : rotatedReferences)
            item.points = transformed(item.points, glm::angleAxis(.73f, glm::normalize(glm::vec3(2, 1, 3))), {2, 3, -1});
        require(std::abs(viewer::calibrateItemFootprint(rotatedReferences) - scale) < 1e-4f,
                "Calibration depended on the source model axes");
        require(viewer::calibrateItemFootprint({}) == 0 &&
                viewer::calibrateItemFootprint({{box({.01f, .01f, .01f}), 1, 1}}) == 0,
                "Small fixture catalogs must leave the caller's explicit broom-derived fallback intact");
        const auto broom = viewer::loadItem(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage/items", "broom");
        const auto broomPoints = viewer::itemModelFootprintPoints(broom.inventory().model);
        const auto broomScale = viewer::calibrateItemFootprint({{broomPoints, broom.inventory().width, broom.inventory().height}});
        require(std::abs(broomScale - 5.5f / 1.3f) < 1e-4f &&
                viewer::estimateItemFootprint(broomPoints, broomScale).height == 6 &&
                viewer::estimateItemFootprint(broomPoints).height == 6,
                "Fallback calibration must follow the existing 1.30 m, six-cell broom definition");

        viewer::ItemModelConfig primitives;
        viewer::ItemModelPart part;
        part.shape = viewer::ItemModelShape::Frustum; part.size = {.4f, .8f, .2f}; part.topSize = {.1f, .05f};
        part.center = {2, 1, -3}; part.rotation = glm::angleAxis(.7f, glm::normalize(glm::vec3(1, 2, 3)));
        primitives.parts.push_back(part);
        const auto corners = viewer::itemModelFootprintPoints(primitives);
        require(corners.size() == 8, "Primitive footprint source lost frustum corners");
        int top = 0, bottom = 0;
        for (const auto corner : corners) {
            const auto local = glm::conjugate(part.rotation) * (corner - part.center);
            if (local.y > 0) {
                ++top; require(std::abs(std::abs(local.x) - .05f) < 1e-6f, "Frustum top was replaced by the wider collider bounds");
            } else {
                ++bottom; require(std::abs(std::abs(local.x) - .2f) < 1e-6f, "Frustum base geometry was lost");
            }
        }
        require(top == 4 && bottom == 4, "Frustum footprint source has unbalanced end faces");
        primitives.mesh.resize(3);
        primitives.mesh[0].position = {0, 0, 0}; primitives.mesh[1].position = {.3f, 0, 0};
        primitives.mesh[2].position = {0, .2f, 0};
        const auto visual = viewer::itemModelFootprintPoints(primitives);
        require(visual.size() == 3 && visual[1] == primitives.mesh[1].position,
                "A custom visual mesh must replace collision primitives when measuring storage");

        const auto huge = viewer::estimateItemFootprint(box({200, 100, 10}), 4);
        require(huge.width == 400 && huge.height == 800, "Large props must not be clamped to a backpack grid");
        rejects([&] { viewer::estimateItemFootprint({}, 4); }, "Empty item geometry was accepted");
        rejects([&] { viewer::estimateItemFootprint({{std::numeric_limits<float>::quiet_NaN(), 0, 0}}, 4); },
                "Nonfinite geometry was accepted");
        rejects([&] { viewer::estimateItemFootprint(shape, 0); }, "A zero physical scale was accepted");
        rejects([&] { viewer::estimateItemFootprint(shape, std::numeric_limits<float>::infinity()); },
                "A nonfinite physical scale was accepted");
        rejects([&] { viewer::estimateItemFootprint(box({1e10f, 1, 1}), 4); }, "Integer cell overflow was silently clamped");
        rejects([&] { viewer::calibrateItemFootprint({{shape, 0, 2}}); }, "Invalid authored footprints were accepted");
        std::cout << "Physical item footprint checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
