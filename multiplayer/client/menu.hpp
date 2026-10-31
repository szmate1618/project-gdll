#pragma once
#include "options.hpp"
#include "server/options.hpp"
#include <memory>
#include <string>
struct GLFWwindow;
namespace multiplayer {
class CharacterCreation;
class MenuView;
// Owns the application window and RmlUi menu. Gameplay borrows
// its context; all GLFW and GL work remains on the application thread.
class GameMenu {
public:
    explicit GameMenu(const Options& defaults);
    ~GameMenu();
    GameMenu(const GameMenu&) = delete;
    GameMenu& operator=(const GameMenu&) = delete;
    GLFWwindow* window() const { return window_; }
    GameMode choose(const std::string& message);
    bool progress(const std::string& message);
    bool closed() const;
    ServerOptions hostOptions();
    std::string address = "127.0.0.1:27020";
    std::string port = "27020";
private:
    void callbacks();
    void handleCommand();
    bool prepareCharacter();
    void leaveCharacterCreation();
    void draw(const std::string& message, bool loading);
    static void key(GLFWwindow*, int, int, int, int);
    static void character(GLFWwindow*, unsigned int);
    static void click(GLFWwindow*, int, int, int);
    static void pointer(GLFWwindow*, double, double);
    static void focus(GLFWwindow*, int);
    static void scroll(GLFWwindow*, double, double);
    GLFWwindow* window_ = nullptr;
    std::unique_ptr<MenuView> view_;
    bool cancelled_ = false;
    bool creating_ = false;
    bool hosting_ = false;
    std::unique_ptr<CharacterCreation> characterCreation_;
    GameMode choice_ = GameMode::Menu;
    std::string status_;
};
}
