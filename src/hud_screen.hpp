#pragma once

namespace viewer {
enum class HudScreen { Gameplay, Inventory, Skills, Health, Map, Settings, MissionMenu, Crafting };

// Crafting adds a menu frame while keeping physical object handling active.
constexpr bool hudShowsGameplay(HudScreen screen) {
    return screen == HudScreen::Gameplay || screen == HudScreen::Crafting;
}
}
