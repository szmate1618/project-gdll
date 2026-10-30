#include "character_creation.hpp"
#include "character_profile.hpp"
#include "resources.hpp"
#include "shader_program.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <random>
#include <stdexcept>

namespace multiplayer {
namespace {
struct Vertex { glm::vec3 position, normal; glm::vec2 uv; float face; };
}
CharacterCreation::CharacterCreation(std::filesystem::path profilePath) : faces_(loadCharacterFaces(resourceRoot())), profilePath_(std::move(profilePath)) {
    std::mt19937 random(std::random_device{}());
    faceIndex_ = std::uniform_int_distribution<std::size_t>(0, faces_.size() - 1)(random);
    try {
        if (profilePath_.empty()) profilePath_ = characterProfilePath();
        CharacterCanvas loaded; std::string id;
        if (loadCharacterProfile(profilePath_, loaded, id)) {
            const auto face = std::find_if(faces_.begin(), faces_.end(), [&](const auto& face) { return face.id == id; });
            if (face == faces_.end()) throw std::runtime_error("Saved face icon is unavailable");
            canvas_ = std::move(loaded); faceIndex_ = std::size_t(face - faces_.begin());
            status_ = "Saved character loaded";
        }
    } catch (const std::exception& error) { status_ = std::string("Could not load saved character: ") + error.what(); }
    faceName_ = faces_[faceIndex_].name;
    canvas_.applyFace(loadCharacterFaceImage(faces_[faceIndex_].image));
    const auto path = resourceRoot() / "multiplayer/client";
    std::vector<Vertex> vertices;
    const std::array<glm::vec2, 4> corners{{{0,0}, {1,0}, {1,1}, {0,1}}};
    for (int i = 0; i < CharacterCanvas::faceCount; ++i)
        for (const int corner : {0, 2, 1, 0, 3, 2})
            vertices.push_back({CharacterCanvas::surfacePoint(i, corners[corner]),
                CharacterCanvas::surfaces()[i].normal, corners[corner], float(i)});
    program_ = viewer::createShaderProgram(path / "character_creation.vert", path / "character_creation.frag");
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, face)));
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    matrix_ = glGetUniformLocation(program_, "uMatrix");
    brush_ = glGetUniformLocation(program_, "uBrush"); hover_ = glGetUniformLocation(program_, "uHover");
    glGenTextures(1, &texture_); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, CharacterCanvas::resolution, CharacterCanvas::resolution,
        CharacterCanvas::faceCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uploadImages();
}
CharacterCreation::~CharacterCreation() {
    glDeleteTextures(1, &texture_); glDeleteBuffers(1, &buffer_);
    glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}
void CharacterCreation::uploadImages() {
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0); glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    for (int i = 0; i < CharacterCanvas::faceCount; ++i)
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, CharacterCanvas::resolution, CharacterCanvas::resolution,
            1, GL_RGBA, GL_UNSIGNED_BYTE, canvas_.images()[i].rgba.data());
}
void CharacterCreation::changed() { status_ = "Unsaved changes"; }
void CharacterCreation::selectFace(std::size_t index) {
    const auto& face = faces_.at(index);
    const auto icon = loadCharacterFaceImage(face.image);
    canvas_.applyFace(icon); faceIndex_ = index; faceName_ = face.name;
    uploadImages(); changed(); releasePointer();
}
CharacterCanvas::Color CharacterCreation::color(CharacterColorTarget target) const {
    if (target == CharacterColorTarget::Base) return canvas_.base();
    if (target == CharacterColorTarget::Brush) return canvas_.brush();
    return canvas_.ink();
}
void CharacterCreation::setColor(CharacterColorTarget target, CharacterCanvas::Color color) {
    color[3] = 255;
    if (this->color(target) == color) return;
    if (target == CharacterColorTarget::Base) canvas_.setBaseColor(color);
    else if (target == CharacterColorTarget::Brush) canvas_.setBrushColor(color);
    else canvas_.setFaceColor(color);
    if (target != CharacterColorTarget::Brush) uploadImages();
    changed(); releasePointer();
}
void CharacterCreation::reset() {
    canvas_.reset(); selectFace(0);
    yaw_ = .4f; pitch_ = .15f;
}
bool CharacterCreation::save() {
    try {
        if (profilePath_.empty()) profilePath_ = characterProfilePath();
        saveCharacterProfile(profilePath_, canvas_, faces_[faceIndex_].id);
        status_ = "Character saved locally";
        return true;
    } catch (const std::exception& error) { status_ = std::string("Save failed: ") + error.what(); return false; }
}
void CharacterCreation::releasePointer() { orbiting_ = false; previousPaint_.reset(); }
void CharacterCreation::draw(glm::ivec4 viewport, glm::vec2 pointer, bool paint, bool orbit) {
    if (viewport.z <= 0 || viewport.w <= 0) { releasePointer(); return; }
    const bool inside = pointer.x >= 0 && pointer.y >= 0 && pointer.x <= 1 && pointer.y <= 1;
    if (orbit && inside) {
        if (orbiting_) {
            const auto delta = pointer - previousPointer_;
            yaw_ -= delta.x * 5; pitch_ = std::clamp(pitch_ + delta.y * 3, -1.2f, 1.2f);
        }
        previousPointer_ = pointer;
    }
    orbiting_ = orbit && inside;
    const glm::vec3 target{0,.9f,0};
    const auto eye = target + 3.1f * glm::vec3(std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_));
    const auto matrix = glm::perspective(glm::radians(40.f), float(viewport.z) / viewport.w, .1f, 20.f) *
        glm::lookAt(eye, target, glm::vec3(0,1,0));
    std::optional<CharacterCanvas::Hit> hit;
    if (inside) {
        auto far = glm::inverse(matrix) * glm::vec4(pointer.x * 2 - 1, 1 - pointer.y * 2, 1, 1);
        hit = CharacterCanvas::intersect(eye, glm::vec3(far) / far.w - eye);
    }
    glActiveTexture(GL_TEXTURE0); glBindSampler(0, 0);
    if (paint && !orbit && hit) {
        const auto start = previousPaint_.value_or(hit->position);
        const int steps = std::max(1, int(std::ceil(glm::distance(start, hit->position) / (CharacterCanvas::brushRadius * .4f))));
        bool changed = false;
        for (int i = 1; i <= steps; ++i)
            changed = canvas_.paint(glm::mix(start, hit->position, float(i) / steps)) || changed;
        if (changed) { uploadImages(); this->changed(); }
        previousPaint_ = hit->position;
    } else previousPaint_.reset();
    glViewport(viewport.x, viewport.y, viewport.z, viewport.w);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDepthRange(0,1);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glDisable(GL_BLEND);
    glUseProgram(program_); glBindVertexArray(vao_); glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
    glUniformMatrix4fv(matrix_, 1, GL_FALSE, glm::value_ptr(matrix));
    glUniform1i(hover_, hit.has_value() && !orbit);
    if (hit) glUniform4f(brush_, hit->position.x, hit->position.y, hit->position.z, CharacterCanvas::brushRadius);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0); glUseProgram(0); glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
}
}
