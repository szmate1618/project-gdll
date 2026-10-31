#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <glm/vec4.hpp>
#include "server/options.hpp"

namespace multiplayer {
class CharacterCreation;
enum class MenuScreen { Choose, Host, Character, Loading };
enum class MenuCommand { None, SinglePlayer, Host, StartHost, Join, Character, Quit, Back, Cancel };

// RmlUi layout, themes and local interaction. GameMenu owns window/session work.
class MenuView {
public:
    explicit MenuView(const std::filesystem::path& directory);
    ~MenuView();
    void connection(const std::string& port, const std::string& address);
    void configureHost(const Options& defaults, const std::vector<std::filesystem::path>& scenarioDirectories);
    ServerOptions hostOptions();
    void editCharacter(CharacterCreation& creation); // Borrows the menu-owned preview until view destruction.
    std::string port() const;
    std::string address() const;
    void draw(MenuScreen screen, const std::string& message, const std::string& face, int width, int height);
    void key(int key, int action, int modifiers);
    void character(unsigned int codepoint);
    void pointer(float normalizedX, float normalizedY);
    void button(int button, bool pressed, int modifiers);
    void wheel(float notches);
    void focusLost();
    MenuCommand takeCommand();
    glm::ivec4 previewViewport() const; // Framebuffer pixels; bottom-left GL origin.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace multiplayer
