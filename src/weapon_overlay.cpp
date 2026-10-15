#include "weapon_overlay.hpp"
#include "shader_program.hpp"
#include <array>
#include <cstddef>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
WeaponOverlay::WeaponOverlay(const std::filesystem::path& shaders) {
    // One shared cube with flat face colors; shape differences are just scale.
    const std::array<glm::vec3, 8> corners{{{-.5f,-.5f,-.5f}, {.5f,-.5f,-.5f}, {.5f,.5f,-.5f}, {-.5f,.5f,-.5f},
                                         {-.5f,-.5f,.5f}, {.5f,-.5f,.5f}, {.5f,.5f,.5f}, {-.5f,.5f,.5f}}};
    const std::array<std::array<int, 4>, 6> faces{{{{0,1,2,3}}, {{5,4,7,6}}, {{4,0,3,7}},
                                                 {{1,5,6,2}}, {{3,2,6,7}}, {{4,5,1,0}}}};
    struct Vertex { glm::vec3 position; float shade; };
    std::vector<Vertex> vertices;
    const std::array<float, 6> shades{.48f, .52f, .35f, .42f, .65f, .25f};
    for (std::size_t f = 0; f < faces.size(); ++f)
        for (const int corner : {0,2,1,0,3,2}) vertices.push_back({corners[faces[f][corner]], shades[f]});
    program_ = createShaderProgram(shaders / "weapon.vert", shaders / "weapon.frag");
    transform_ = glGetUniformLocation(program_, "uTransform");
    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, shade)));
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}
WeaponOverlay::~WeaponOverlay() {
    glDeleteBuffers(1, &buffer_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}
void WeaponOverlay::draw(std::size_t slot, int width, int height) {
    if (slot >= 4 || width <= 0 || height <= 0) return;
    GLint previousProgram = 0, previousVao = 0, previousFront = 0, previousCullMode = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_FRONT_FACE, &previousFront);
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
    const auto depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    GLboolean depthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glUseProgram(program_); glBindVertexArray(vao_);
    const auto projection = glm::perspective(glm::radians(60.0f), static_cast<float>(width) / height, .01f, 5.0f);
    const std::array<glm::vec3, 4> sizes{{{.12f,.12f,.25f}, {.16f,.16f,.4f}, {.12f,.12f,.65f}, {.16f,.13f,.55f}}};
    const auto rotation = glm::rotate(glm::mat4(1), glm::radians(-12.0f), glm::vec3(0, 1, 0));
    const auto drawBox = [&](glm::vec3 center, glm::vec3 size) {
        const auto transform = projection * glm::translate(glm::mat4(1), center) * rotation * glm::scale(glm::mat4(1), size);
        glUniformMatrix4fv(transform_, 1, GL_FALSE, glm::value_ptr(transform));
        glDrawArrays(GL_TRIANGLES, 0, 36);
    };
    drawBox({.24f, -.20f, -.65f}, sizes[slot]);
    drawBox({.26f, -.31f, -.52f}, {.08f,.18f,.09f});
    glBindVertexArray(static_cast<GLuint>(previousVao)); glUseProgram(static_cast<GLuint>(previousProgram));
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glCullFace(static_cast<GLenum>(previousCullMode)); glFrontFace(static_cast<GLenum>(previousFront));
    glDepthMask(depthWrite);
}
} // namespace viewer
