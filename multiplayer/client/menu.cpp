#include "menu.hpp"
#include "window.hpp"
#include "resources.hpp"
#include "shader_program.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <stdexcept>

namespace multiplayer {
namespace {
// Five columns, seven bits per column; enough for menu labels and numeric IPs.
std::array<unsigned char, 5> glyph(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'A': return {126,17,17,17,126}; case 'B': return {127,73,73,73,54};
    case 'C': return {62,65,65,65,34}; case 'D': return {127,65,65,34,28};
    case 'E': return {127,73,73,73,65}; case 'F': return {127,9,9,9,1};
    case 'G': return {62,65,73,73,122}; case 'H': return {127,8,8,8,127};
    case 'I': return {0,65,127,65,0}; case 'J': return {32,64,65,63,1};
    case 'K': return {127,8,20,34,65}; case 'L': return {127,64,64,64,64};
    case 'M': return {127,2,12,2,127}; case 'N': return {127,4,8,16,127};
    case 'O': return {62,65,65,65,62}; case 'P': return {127,9,9,9,6};
    case 'Q': return {62,65,81,33,94}; case 'R': return {127,9,25,41,70};
    case 'S': return {70,73,73,73,49}; case 'T': return {1,1,127,1,1};
    case 'U': return {63,64,64,64,63}; case 'V': return {31,32,64,32,31};
    case 'W': return {63,64,56,64,63}; case 'X': return {99,20,8,20,99};
    case 'Y': return {7,8,112,8,7}; case 'Z': return {97,81,73,69,67};
    case '0': return {62,81,73,69,62}; case '1': return {0,66,127,64,0};
    case '2': return {66,97,81,73,70}; case '3': return {33,65,69,75,49};
    case '4': return {24,20,18,127,16}; case '5': return {39,69,69,69,57};
    case '6': return {60,74,73,73,48}; case '7': return {1,113,9,5,3};
    case '8': return {54,73,73,73,54}; case '9': return {6,73,73,41,30};
    case '.': return {0,96,96,0,0}; case ':': return {0,54,54,0,0};
    case '-': return {8,8,8,8,8}; case '_': return {64,64,64,64,64};
    case '/': return {32,16,8,4,2}; case '\\': return {2,4,8,16,32};
    case '[': return {0,127,65,65,0}; case ']': return {0,65,65,127,0};
    case '(': return {0,28,34,65,0}; case ')': return {0,65,34,28,0};
    case '!': return {0,0,95,0,0}; case '?': return {2,1,81,9,6};
    case '=': return {20,20,20,20,20}; case '+': return {8,8,62,8,8};
    case ' ': return {}; default: return {0,2,1,2,0};
    }
}
GameMenu& menu(GLFWwindow* window) { return *static_cast<GameMenu*>(glfwGetWindowUserPointer(window)); }
GameMode mode(int row) { return static_cast<GameMode>(row + 1); }
}
GameMenu::GameMenu(const ClientDisplay& display, bool hidden) {
    window_ = createClientWindow(display, hidden);
    try {
        glfwMakeContextCurrent(window_); glfwSwapInterval(1); viewer::initializeOpenGL();
        const auto shaders = resourceRoot() / "shaders";
        program_ = viewer::createShaderProgram(shaders / "game_menu.vert", shaders / "game_menu.frag");
        glGenVertexArrays(1, &vao_); glGenBuffers(1, &buffer_);
        glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
        glBindVertexArray(0);
    } catch (...) {
        glDeleteBuffers(1, &buffer_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
        glfwDestroyWindow(window_); glfwTerminate(); throw;
    }
}
GameMenu::~GameMenu() {
    glDeleteBuffers(1, &buffer_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
    glfwDestroyWindow(window_); glfwTerminate();
}
bool GameMenu::closed() const { return glfwWindowShouldClose(window_); }
void GameMenu::callbacks() {
    glfwSetWindowUserPointer(window_, this); glfwSetKeyCallback(window_, key);
    glfwSetCharCallback(window_, character); glfwSetMouseButtonCallback(window_, click);
    glfwSetCursorPosCallback(window_, nullptr); glfwSetWindowFocusCallback(window_, nullptr);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}
void GameMenu::key(GLFWwindow* window, int code, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    auto& self = menu(window);
    if (self.loading_) { if (code == GLFW_KEY_ESCAPE) self.cancelled_ = true; return; }
    if (code == GLFW_KEY_ESCAPE) { if (self.editing_) self.editing_ = 0; else self.choice_ = GameMode::Quit; }
    else if (code == GLFW_KEY_TAB) self.editing_ = (self.editing_ + 1) % 3;
    else if (self.editing_) {
        auto& value = self.editing_ == 1 ? self.port : self.address;
        if (code == GLFW_KEY_BACKSPACE && !value.empty()) value.pop_back();
        if (code == GLFW_KEY_ENTER) self.editing_ = 0;
    } else if (code == GLFW_KEY_UP) self.selected_ = (self.selected_ + 3) % 4;
    else if (code == GLFW_KEY_DOWN) self.selected_ = (self.selected_ + 1) % 4;
    else if (code == GLFW_KEY_ENTER) self.choice_ = mode(self.selected_);
    else if (code >= GLFW_KEY_1 && code <= GLFW_KEY_4) self.choice_ = mode(code - GLFW_KEY_1);
}
void GameMenu::character(GLFWwindow* window, unsigned int c) {
    auto& self = menu(window);
    if (self.loading_ || !self.editing_ || c < 32 || c > 126) return;
    auto& value = self.editing_ == 1 ? self.port : self.address;
    if (value.size() < (self.editing_ == 1 ? 5u : 64u)) value += static_cast<char>(c);
}
void GameMenu::click(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
    auto& self = menu(window);
    if (self.loading_) return;
    double x, y; int width, height;
    glfwGetCursorPos(window, &x, &y); glfwGetWindowSize(window, &width, &height);
    const double scale = std::min(width / 960.0, height / 600.0);
    x = (x - (width - 960 * scale) / 2) / scale;
    y = (y - (height - 600 * scale) / 2) / scale;
    self.editing_ = 0;
    if (x >= 60 && x <= 440 && y >= 135 && y < 355) {
        const int row = static_cast<int>((y - 135) / 55);
        if (y - (135 + row * 55) < 45) self.choice_ = mode(row);
    }
    if (x >= 490 && x <= 900 && y >= 190 && y <= 230) self.editing_ = 1;
    if (x >= 490 && x <= 900 && y >= 285 && y <= 325) self.editing_ = 2;
}
GameMode GameMenu::choose(const std::string& message) {
    choice_ = GameMode::Menu; loading_ = false; cancelled_ = false; callbacks();
    glfwSetWindowTitle(window_, "Godollo | Choose a game mode");
    while (!closed() && choice_ == GameMode::Menu) {
        glfwPollEvents(); draw(message, false); glfwWaitEventsTimeout(.016);
    }
    return closed() ? GameMode::Quit : choice_;
}
bool GameMenu::progress(const std::string& message) {
    loading_ = true; callbacks(); glfwPollEvents(); draw(message, true);
    return !closed() && !cancelled_;
}
void GameMenu::rectangle(float x, float y, float w, float h, float r, float g, float b) {
    vertices_.insert(vertices_.end(), {{x,y,r,g,b},{x+w,y,r,g,b},{x+w,y+h,r,g,b},
        {x,y,r,g,b},{x+w,y+h,r,g,b},{x,y+h,r,g,b}});
}
void GameMenu::text(float x, float y, const std::string& value, float scale) {
    const float start = x;
    for (const char c : value) {
        if (c == '\n' || x + 6 * scale > 900) { x = start; y += 10 * scale; if (c == '\n') continue; }
        if (y > 565) break;
        const auto columns = glyph(c);
        for (int col = 0; col < 5; ++col) for (int row = 0; row < 7; ++row)
            if (columns[col] & (1 << row)) rectangle(x + col * scale, y + row * scale, scale, scale, .85f, .9f, .94f);
        x += 6 * scale;
    }
}
void GameMenu::draw(const std::string& message, bool loading) {
    int width, height; glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0) return;
    const bool depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(.025f,.04f,.055f,1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float scale = std::min(width / 960.f, height / 600.f);
    glViewport(static_cast<int>((width - 960 * scale) / 2), static_cast<int>((height - 600 * scale) / 2),
               static_cast<int>(960 * scale), static_cast<int>(600 * scale));
    vertices_.clear();
    rectangle(60, 100, 840, 2, .2f, .55f, .45f);
    text(60, 55, "GODOLLO", 4);
    if (loading) {
        text(60, 150, message, 2);
        text(60, 510, "ESC - CANCEL / RETURN TO MENU", 1.5f);
    } else {
        const std::array<const char*, 4> labels{"1  SINGLE PLAYER", "2  HOST GAME", "3  JOIN GAME", "4  QUIT"};
        for (int row = 0; row < 4; ++row) {
            const bool selected = selected_ == row;
            rectangle(60, 135.f + row * 55, 380, 45, selected ? .12f : .06f, selected ? .3f : .12f, selected ? .26f : .15f);
            text(78, 150.f + row * 55, labels[row]);
        }
        text(490, 145, "HOST: UP TO 8 PLAYERS", 1.5f);
        text(490, 170, "UDP PORT", 1.5f);
        rectangle(490, 190, 410, 40, .08f, editing_ == 1 ? .3f : .15f, .2f);
        text(505, 202, port + (editing_ == 1 ? "_" : ""));
        text(490, 260, "JOIN: NUMERIC IP:PORT", 1.5f);
        rectangle(490, 285, 410, 40, .08f, editing_ == 2 ? .3f : .15f, .2f);
        text(505, 298, address + (editing_ == 2 ? "_" : ""), 1.5f);
        text(60, 385, "SINGLE PLAYER IS PRIVATE AND WORKS OFFLINE.", 1.5f);
        text(60, 415, "CLICK OR USE 1-4 / ARROWS + ENTER. TAB EDITS FIELDS.", 1.5f);
        text(60, 445, "ESC DURING PLAY LEAVES THE SESSION.", 1.5f);
        text(60, 490, message, 1.5f);
    }
    glUseProgram(program_); glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)), vertices_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    glBindVertexArray(0); glUseProgram(0);
    glfwSwapBuffers(window_);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (blend) glEnable(GL_BLEND);
}
}
