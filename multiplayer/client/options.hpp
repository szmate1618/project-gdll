#pragma once
#include "scene.hpp"
namespace multiplayer {
enum class GameMode { Menu, SinglePlayer, Host, Join, Quit };
struct GameOptions { Options scene; GameMode mode = GameMode::Menu; };
GameOptions parseGameOptions(int argc, char** argv);
}
