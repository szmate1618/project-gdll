#pragma once
#include "character_canvas.hpp"
#include "character_faces.hpp"
#include "opengl.hpp"
#include <filesystem>
#include <string>

namespace multiplayer {
enum class CharacterColorTarget { Base, Brush, Face };
// Menu-owned preview; all resources and painting remain local to this client.
class CharacterCreation {
public:
    explicit CharacterCreation(std::filesystem::path profilePath = {});
    ~CharacterCreation();
    CharacterCreation(const CharacterCreation&) = delete;
    CharacterCreation& operator=(const CharacterCreation&) = delete;
    // Bottom-left GL viewport; pointer is in top-left-origin viewport fractions.
    void draw(glm::ivec4 viewport, glm::vec2 pointer, bool paint, bool orbit);
    void releasePointer();
    const std::string& faceName() const { return faceName_; }
    const std::vector<CharacterFace>& faces() const { return faces_; }
    std::size_t faceIndex() const { return faceIndex_; }
    void selectFace(std::size_t index);
    CharacterCanvas::Color color(CharacterColorTarget target) const;
    void setColor(CharacterColorTarget target, CharacterCanvas::Color color);
    void reset();
    bool save();
    const std::string& status() const { return status_; }
private:
    void changed();
    void uploadImages();
    CharacterCanvas canvas_;
    std::string faceName_;
    std::vector<CharacterFace> faces_;
    std::size_t faceIndex_ = 0;
    std::string status_;
    std::filesystem::path profilePath_;
    GLuint program_ = 0, vao_ = 0, buffer_ = 0, texture_ = 0;
    GLint matrix_ = -1, brush_ = -1, hover_ = -1;
    float yaw_ = .4f, pitch_ = .15f;
    glm::vec2 previousPointer_{0};
    bool orbiting_ = false;
    std::optional<glm::vec3> previousPaint_;
};
}
