#pragma once
#include "character_canvas.hpp"
#include "opengl.hpp"
#include <string>

namespace multiplayer {
// Menu-owned preview; all resources and painting remain local to this client.
class CharacterCreation {
public:
    CharacterCreation();
    ~CharacterCreation();
    CharacterCreation(const CharacterCreation&) = delete;
    CharacterCreation& operator=(const CharacterCreation&) = delete;
    // Bottom-left GL viewport; pointer is in top-left-origin viewport fractions.
    void draw(glm::ivec4 viewport, glm::vec2 pointer, bool paint, bool orbit);
    void releasePointer();
    const std::string& faceName() const { return faceName_; }
private:
    void uploadImages();
    CharacterCanvas canvas_;
    std::string faceName_;
    GLuint program_ = 0, vao_ = 0, buffer_ = 0, texture_ = 0;
    GLint matrix_ = -1, brush_ = -1, hover_ = -1;
    float yaw_ = .4f, pitch_ = .15f;
    glm::vec2 previousPointer_{0};
    bool orbiting_ = false;
    std::optional<glm::vec3> previousPaint_;
};
}
