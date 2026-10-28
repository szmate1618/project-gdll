#include "bird_renderer.hpp"

#include "bird_flock.hpp"
#include "shader_program.hpp"
#include "visibility.hpp"

#include <cmath>
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {

BirdRenderer::BirdRenderer(const std::filesystem::path& shaderDirectory) {
    GLint vao = 0, buffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    try {
        program_ = createShaderProgram(shaderDirectory / "bird.vert", shaderDirectory / "bird.frag");
        projectionViewLocation_ = glGetUniformLocation(program_, "uProjectionView");
        timeLocation_ = glGetUniformLocation(program_, "uTime");
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &instances_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, instances_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FlockBird),
                              reinterpret_cast<void*>(offsetof(FlockBird, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FlockBird),
                              reinterpret_cast<void*>(offsetof(FlockBird, velocity)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(FlockBird),
                              reinterpret_cast<void*>(offsetof(FlockBird, phase)));
        for (GLuint attribute = 0; attribute < 3; ++attribute) glVertexAttribDivisor(attribute, 1);
    } catch (...) {
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
        release();
        throw;
    }
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
}

BirdRenderer::~BirdRenderer() { release(); }

void BirdRenderer::release() noexcept {
    glDeleteBuffers(1, &instances_);
    glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void BirdRenderer::draw(const Camera& camera, float aspect, double seconds,
                        const std::vector<FlockBird>& birds) {
    if (birds.empty()) return;
    const auto projectionView = camera.projection(aspect) * camera.view();
    const Frustum frustum(projectionView);
    visible_.clear();
    visible_.reserve(birds.size());
    // A cube enclosing all three cards, including the small wing bank.
    constexpr glm::vec3 extent(.75f);
    for (const auto& bird : birds)
        if (frustum.intersects(bird.position - extent, bird.position + extent)) visible_.push_back(bird);
    if (visible_.empty()) return;

    GLint program = 0, vao = 0, buffer = 0;
    GLboolean depthWrite = GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    const auto depth = glIsEnabled(GL_DEPTH_TEST);
    const auto blend = glIsEnabled(GL_BLEND);
    const auto cull = glIsEnabled(GL_CULL_FACE);

    glUseProgram(program_);
    glUniformMatrix4fv(projectionViewLocation_, 1, GL_FALSE, glm::value_ptr(projectionView));
    glUniform1f(timeLocation_, static_cast<float>(std::fmod(seconds, 3600.0)));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instances_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(visible_.size() * sizeof(FlockBird)),
                 visible_.data(), GL_STREAM_DRAW);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 18, static_cast<GLsizei>(visible_.size()));

    glBindVertexArray(static_cast<GLuint>(vao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
    glUseProgram(static_cast<GLuint>(program));
    glDepthMask(depthWrite);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

} // namespace viewer
