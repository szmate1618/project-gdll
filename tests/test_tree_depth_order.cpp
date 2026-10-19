#include "tree_depth_order.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<viewer::VisibilityBounds> boxes(const std::vector<glm::vec3>& centers) {
    std::vector<viewer::VisibilityBounds> result;
    for (const auto center : centers) result.push_back({center-glm::vec3(1), center+glm::vec3(1)});
    return result;
}
void stableBands() {
    const auto bounds = boxes({{0,0,-40},{0,0,-9},{0,0,-15},{0,0,-8},
                               {0,0,-7.99f},{0,0,2},{0,0,-1016},{0,0,-5000}});
    std::vector<std::size_t> selected{7,0,2,6,1,5,4,3};
    viewer::TreeDepthOrder order;
    order.sort(selected, bounds, {0,0,0}, {0,0,-1});
    require(selected == std::vector<std::size_t>({5,4,2,1,3,0,7,6}),
            "Depth bands changed membership, tie order, boundaries or far-band clamping");
    const auto expected = selected;
    order.sort(selected, bounds, {0,0,0}, {0,0,-1});
    require(selected == expected, "Repeated depth ordering was unstable");
    selected = {7,6};
    order.sort(selected, bounds, {0,0,0}, {0,0,-1});
    require(selected == std::vector<std::size_t>({7,6}), "Same-band trees changed query order");
    selected = {2};
    order.sort(selected, bounds, {0,0,0}, {0,0,-1});
    require(selected == std::vector<std::size_t>({2}), "Scratch reuse retained a stale tree");
    selected.clear();
    order.sort(selected, bounds, {0,0,0}, {0,0,-1});
    require(selected.empty(), "Empty selection produced trees");
}
void cameraFramesAndSubset() {
    viewer::TreeDepthOrder order;
    auto bounds = boxes({{10,0,0},{100,0,0},{30,0,0}});
    std::vector<std::size_t> selected{0,1,2};
    order.sort(selected, bounds, {50,0,0}, {-1,0,0});
    require(selected == std::vector<std::size_t>({1,2,0}), "Depth ordering ignored the translated camera direction");
    bounds = boxes({{0,100,0},{0,200,0},{0,140,0}});
    selected = {0,1,2};
    order.sort(selected, bounds, {0,250,0}, {0,-1,0});
    require(selected == std::vector<std::size_t>({1,2,0}), "Aerial view did not use vertical camera depth");
    bounds.resize(10000);
    bounds[9999] = {{0,0,-10},{0,0,-10}};
    bounds[55] = {{0,0,-100},{0,0,-100}};
    selected = {55,9999};
    order.sort(selected, bounds, {0,0,0}, {0,0,-1});
    require(selected == std::vector<std::size_t>({9999,55}), "Ordering lost original indices from a visibility subset");
}
} // namespace

int main() {
    try { stableBands(); cameraFramesAndSubset(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Tree depth bands preserve candidates and stable ties across camera frames\n";
}
