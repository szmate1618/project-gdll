#pragma once
#include "opengl.hpp"
#include "options.hpp"
#include <memory>
#include <string>
#include <vector>
struct GLFWwindow;
namespace multiplayer {
class CharacterCreation;
// Owns the application window and the small menu renderer. Gameplay borrows
// its context; all GLFW and GL work remains on the application thread.
class GameMenu {
public:
    GameMenu(const ClientDisplay& display, bool hidden);
    ~GameMenu();
    GameMenu(const GameMenu&) = delete;
    GameMenu& operator=(const GameMenu&) = delete;
    GLFWwindow* window() const { return window_; }
    GameMode choose(const std::string& message);
    bool progress(const std::string& message);
    bool closed() const;
    std::string address = "127.0.0.1:27020";
    std::string port = "27020";
private:
    struct Vertex { float x, y, r, g, b; };
    void callbacks();
    void activate(int row);
    void leaveCharacterCreation();
    void draw(const std::string& message, bool loading);
    void rectangle(float x, float y, float w, float h, float r, float g, float b);
    void text(float x, float y, const std::string& value, float scale = 2);
    static void key(GLFWwindow*, int, int, int, int);
    static void character(GLFWwindow*, unsigned int);
    static void click(GLFWwindow*, int, int, int);
    GLFWwindow* window_ = nullptr;
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    int selected_ = 0, editing_ = 0; // 1: host port, 2: join address
    bool loading_ = false, cancelled_ = false;
    bool creating_ = false;
    std::unique_ptr<CharacterCreation> characterCreation_;
    GameMode choice_ = GameMode::Menu;
    std::vector<Vertex> vertices_;
};
}
