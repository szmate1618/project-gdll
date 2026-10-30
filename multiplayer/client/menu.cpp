#include "menu.hpp"
#include "menu_view.hpp"
#include "character_creation.hpp"
#include "window.hpp"
#include "resources.hpp"
#include "opengl.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>

namespace multiplayer {
namespace {
GameMenu& menu(GLFWwindow* window) { return *static_cast<GameMenu*>(glfwGetWindowUserPointer(window)); }
}
GameMenu::GameMenu(const ClientDisplay& display, bool hidden) {
    window_ = createClientWindow(display, hidden);
    try {
        glfwMakeContextCurrent(window_); glfwSwapInterval(1); viewer::initializeOpenGL();
        view_ = std::make_unique<MenuView>(resourceRoot() / "ui");
    } catch (...) {
        glfwDestroyWindow(window_); glfwTerminate(); throw;
    }
}
GameMenu::~GameMenu() {
    characterCreation_.reset();
    view_.reset();
    glfwDestroyWindow(window_); glfwTerminate();
}
void GameMenu::handleCommand() {
    switch (view_->takeCommand()) {
    case MenuCommand::SinglePlayer: choice_ = GameMode::SinglePlayer; break;
    case MenuCommand::Host: choice_ = GameMode::Host; break;
    case MenuCommand::Join: choice_ = GameMode::Join; break;
    case MenuCommand::Quit: choice_ = GameMode::Quit; break;
    case MenuCommand::Character:
        creating_ = true;
        glfwSetWindowTitle(window_, "Godollo | Character creation");
        break;
    case MenuCommand::Back: leaveCharacterCreation(); break;
    case MenuCommand::Cancel: cancelled_ = true; break;
    case MenuCommand::None: break;
    }
}
void GameMenu::leaveCharacterCreation() {
    creating_ = false;
    if (characterCreation_) characterCreation_->releasePointer();
    glfwSetWindowTitle(window_, "Godollo | Choose a game mode");
}
bool GameMenu::closed() const { return glfwWindowShouldClose(window_); }
void GameMenu::callbacks() {
    glfwSetWindowUserPointer(window_, this); glfwSetKeyCallback(window_, key);
    glfwSetCharCallback(window_, character); glfwSetMouseButtonCallback(window_, click);
    glfwSetCursorPosCallback(window_, pointer); glfwSetWindowFocusCallback(window_, focus);
    glfwSetScrollCallback(window_, nullptr);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    double x = 0, y = 0; glfwGetCursorPos(window_, &x, &y); pointer(window_, x, y);
}
void GameMenu::key(GLFWwindow* window, int code, int, int action, int mods) { menu(window).view_->key(code, action, mods); }
void GameMenu::character(GLFWwindow* window, unsigned int c) { menu(window).view_->character(c); }
void GameMenu::click(GLFWwindow* window, int button, int action, int mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) return;
    double x = 0, y = 0; glfwGetCursorPos(window, &x, &y); pointer(window, x, y);
    menu(window).view_->button(button == GLFW_MOUSE_BUTTON_LEFT ? 0 : 1, action == GLFW_PRESS, mods);
}
void GameMenu::pointer(GLFWwindow* window, double x, double y) {
    int width = 0, height = 0; glfwGetWindowSize(window, &width, &height);
    if (width > 0 && height > 0) menu(window).view_->pointer(float(x) / width, float(y) / height);
}
void GameMenu::focus(GLFWwindow* window, int focused) {
    if (!focused) {
        auto& self = menu(window);
        self.view_->focusLost();
        if (self.characterCreation_) self.characterCreation_->releasePointer();
    }
}
GameMode GameMenu::choose(const std::string& message) {
    choice_ = GameMode::Menu; cancelled_ = false;
    view_->connection(port, address); callbacks();
    std::string status = message;
    glfwSetWindowTitle(window_, "Godollo | Choose a game mode");
    while (!closed() && choice_ == GameMode::Menu) {
        glfwPollEvents();
        if (creating_ && !characterCreation_) {
            try { characterCreation_ = std::make_unique<CharacterCreation>(); }
            catch (const std::exception& error) {
                status = error.what(); std::cerr << "Character creation: " << status << '\n';
                leaveCharacterCreation();
            }
        }
        draw(status, false); handleCommand(); glfwWaitEventsTimeout(.016);
    }
    return closed() ? GameMode::Quit : choice_;
}
bool GameMenu::progress(const std::string& message) {
    callbacks(); glfwPollEvents(); draw(message, true); handleCommand();
    return !closed() && !cancelled_;
}
void GameMenu::draw(const std::string& message, bool loading) {
    int width = 0, height = 0; glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0) return;
    const bool depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // A session may have deleted its still-current shader. Unbind it before
    // the UI backend saves/restores GL state, so it never restores a dead name.
    glUseProgram(0); glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearDepth(1);
    glClearColor(.025f,.04f,.055f,1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    view_->draw(loading ? MenuScreen::Loading : creating_ ? MenuScreen::Character : MenuScreen::Choose,
                message, characterCreation_ ? characterCreation_->faceName() : "", width, height);
    port = view_->port(); address = view_->address();
    if (creating_ && !loading && characterCreation_) {
        const auto viewport = view_->previewViewport();
        const int top = height - viewport.y - viewport.w;
        double x = 0, y = 0; int windowWidth = 0, windowHeight = 0;
        glfwGetCursorPos(window_, &x, &y); glfwGetWindowSize(window_, &windowWidth, &windowHeight);
        const glm::vec2 pointer{windowWidth > 0 ? (float(x) / windowWidth * width - viewport.x) / std::max(viewport.z, 1) : -1,
                                windowHeight > 0 ? (float(y) / windowHeight * height - top) / std::max(viewport.w, 1) : -1};
        const bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED);
        characterCreation_->draw(viewport, pointer,
            focused && glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
            focused && glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    }
    glfwSwapBuffers(window_);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (blend) glEnable(GL_BLEND);
}
} // namespace multiplayer
