#include "sword_geometry.hpp"
#include "sword_melee.hpp"
#include "melee_weapon_geometry.hpp"

namespace viewer {
Model makeSwordModel() {
    Model model; model.materials.emplace_back();
    // Narrow six-sided outline: tapered heel, parallel edges, pointed tip.
    meleeWeaponPrism(model, std::array<glm::vec2, 6>{{{0, swordBladeBase}, {.026f, .10f},
        {.026f, .94f}, {0, swordBladeTip}, {-.026f, .94f}, {-.026f, .10f}}}, .006f, {.78f, .82f, .87f});
    meleeWeaponPrism(model, std::array<glm::vec2, 4>{{{-.13f, -.015f}, {.13f, -.015f},
        {.13f, .015f}, {-.13f, .015f}}}, .018f, {.32f, .29f, .19f});
    meleeWeaponPrism(model, std::array<glm::vec2, 4>{{{-.019f, -.19f}, {.019f, -.19f},
        {.019f, -.015f}, {-.019f, -.015f}}}, .018f, {.16f, .085f, .04f});
    meleeWeaponPrism(model, std::array<glm::vec2, 4>{{{-.03f, -.23f}, {.03f, -.23f},
        {.03f, -.19f}, {-.03f, -.19f}}}, .024f, {.32f, .29f, .19f});
    model.boundsMin = {-.13f, -.23f, -.024f}; model.boundsMax = {.13f, swordBladeTip, .024f};
    return model;
}
} // namespace viewer
