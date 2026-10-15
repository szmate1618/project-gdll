#include "players.hpp"
#include "box_layer.hpp"
#include "shader_program.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace multiplayer {
PlayerRenderer::PlayerRenderer() {
    const auto cube = viewer::BoxLayer::unitCube();
    std::vector<glm::vec3> vertices;
    for (const auto i : cube.primitives.front().indices) vertices.push_back(cube.primitives.front().vertices[i].position);
    const auto path = std::filesystem::path(MP_ROOT) / "multiplayer/client";
    program_ = viewer::createShaderProgram(path / "players.vert", path / "players.frag");
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    matrix_ = glGetUniformLocation(program_, "uMatrix"); color_ = glGetUniformLocation(program_, "uColor");
}
PlayerRenderer::~PlayerRenderer() {
    glDeleteBuffers(1, &vbo_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}
void PlayerRenderer::draw(const viewer::Camera& camera, const std::vector<PlayerState>& players,
                          std::uint32_t self, int width, int height) {
    GLint program = 0, vao = 0, front = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_FRONT_FACE, &front); glFrontFace(GL_CCW);
    const auto vp = camera.projection(static_cast<float>(width) / height) * camera.view();
    glUseProgram(program_); glBindVertexArray(vao_);
    for (const auto& p : players) {
        if (p.id == self) continue;
        const float heightMeters = p.movement.stance == viewer::PlayerStance::Standing ? 1.8f :
            p.movement.stance == viewer::PlayerStance::Crouched ? 1.1f : .6f;
        const auto model = glm::translate(glm::mat4(1), p.movement.feet + glm::vec3(0, heightMeters * .5f, 0)) *
            glm::rotate(glm::mat4(1), glm::radians(-p.yaw), glm::vec3(0, 1, 0)) *
            glm::scale(glm::mat4(1), glm::vec3(.6f, heightMeters, .4f));
        const auto m = vp * model;
        glUniformMatrix4fv(matrix_, 1, GL_FALSE, glm::value_ptr(m));
        glUniform3f(color_, .15f + .12f * (p.id % 4), .65f, .95f);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glFrontFace(static_cast<GLenum>(front));
    glBindVertexArray(static_cast<GLuint>(vao)); glUseProgram(static_cast<GLuint>(program));
}
} // namespace multiplayer
