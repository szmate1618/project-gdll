#include "tree_color.hpp"
#include <glm/gtc/epsilon.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(glm::vec3 a, glm::vec3 b) {
    return glm::all(glm::epsilonEqual(a, b, 1e-6f));
}
void checkColors() {
    viewer::Model model;
    // White padding and a bright texel just below the material's mask threshold
    // must not change the mean of the two surviving black/white texels.
    model.images = {{4, 1, {255,255,255,0, 255,0,0,159, 0,0,0,160, 255,255,255,255}}};
    model.textures = {{0}};
    viewer::Material material;
    material.baseColor = {.4f,.6f,.8f,.8f};
    material.alphaMode = "MASK";
    material.unlit = true;
    material.doubleSided = true;
    material.baseColorTexture = 0;
    model.materials = {material};
    const auto mean = viewer::visibleTextureColor(model, material);
    require(mean && close(*mean, {.2f,.3f,.4f}), "Mask padding, material alpha or linear color averaging is wrong");
    const glm::vec3 target(.03f,.05f,.01f);
    const auto tinted = viewer::matchTreeTextureColors(model, target);
    require(close(*viewer::visibleTextureColor(model, tinted[0]), target), "Visible mean did not match the canopy");
    require(tinted[0].baseColor.a == material.baseColor.a && tinted[0].alphaCutoff == material.alphaCutoff &&
            tinted[0].doubleSided && tinted[0].baseColorTexture == 0, "Tint changed the silhouette or texture contract");
    require(model.materials[0].baseColor == material.baseColor, "Tint mutated the shared source asset");

    model.images[0] = {1,1,{128,128,128,255}};
    material.baseColor = {1,1,1,1};
    const auto gray = viewer::visibleTextureColor(model, material);
    require(gray && close(*gray, glm::vec3(.2158605f)), "sRGB texels were averaged without linear decoding");
    model.materials = {material,material};
    model.materials[1].baseColor = {.5f,.7f,.9f,1};
    for (const auto& tint : viewer::matchTreeTextureColors(model, target))
        require(close(*viewer::visibleTextureColor(model, tint), target), "Per-card material factors were not matched independently");

    model.images[0] = {1,1,{255,255,255,0}};
    require(!viewer::visibleTextureColor(model, material), "Empty mask has a visible average");
    require(viewer::matchTreeTextureColors(model, target)[0].baseColor == model.materials[0].baseColor,
            "Empty masks should retain their materials");
    material.baseColorTexture = -1;
    require(!viewer::visibleTextureColor(model, material), "Untextured material has a texture average");
    model.materials = {material};
    require(viewer::matchTreeTextureColors(model, target)[0].baseColor == material.baseColor,
            "Untextured material was tinted");
    material.baseColorTexture = 0;
    model.images[0] = {1,1,{0,128,0,255}};
    model.materials = {material};
    const auto dark = viewer::matchTreeTextureColors(model, target)[0];
    require(std::isfinite(dark.baseColor.r) && std::isfinite(dark.baseColor.b), "Absent source channels caused invalid gains");
}
}
int main() {
    try { checkColors(); std::cout << "Tree texture color matching regressions passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
