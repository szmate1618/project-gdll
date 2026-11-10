#include "binding_renderer.hpp"
#include "scene_depth.hpp"
#include "shader_program.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {

BindingRenderer::BindingRenderer(const std::filesystem::path& shaderDirectory) {
    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    try {
        program_ = createShaderProgram(shaderDirectory / "debug.vert", shaderDirectory / "debug.frag");
        viewProjectionLocation_ = glGetUniformLocation(program_, "uViewProjection");
        GLfloat range[2]{1, 1};
        glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
        lineWidth_ = std::clamp(2.f, range[0], range[1]);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vertices_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vertices_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, color)));
    } catch (...) {
        glBindVertexArray(static_cast<GLuint>(previousVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
        release();
        throw;
    }
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}

BindingRenderer::~BindingRenderer() { release(); }

void BindingRenderer::release() noexcept {
    glDeleteBuffers(1, &vertices_);
    glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void BindingRenderer::draw(const Camera& camera, float aspect, const std::vector<glm::vec3>& endpoints) {
    batch_.clear();
    if (endpoints.empty()) return;
    if (endpoints.size() % 2 != 0)
        throw std::runtime_error("Binding lines require pairs of world endpoints");
    if (endpoints.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
        endpoints.size() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(Vertex))
        throw std::runtime_error("Too many binding line endpoints");
    batch_.reserve(endpoints.size());
    for (const auto& position : endpoints) batch_.push_back({position, {1.f, .72f, .16f}});

    GLint previousProgram = 0, previousVao = 0, previousBuffer = 0, previousDepthFunction = 0;
    GLboolean previousDepthWrite = GL_TRUE;
    GLfloat previousLineWidth = 1;
    const GLboolean previousDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean previousBlend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunction);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthWrite);
    glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);

    glUseProgram(program_);
    auto projectionView = sceneProjection(camera, aspect) * camera.view();
    // Surface intersections share depth with their owning geometry. A tiny clip
    // bias keeps the highlight stable without revealing it through other props.
    const float bias = sceneReversedDepth ? 1.e-6f : -1.e-6f;
    for (int column = 0; column < 4; ++column)
        projectionView[column][2] += bias * projectionView[column][3];
    glUniformMatrix4fv(viewProjectionLocation_, 1, GL_FALSE, glm::value_ptr(projectionView));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(sceneReversedDepth ? GL_GEQUAL : GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glLineWidth(lineWidth_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vertices_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch_.size() * sizeof(Vertex)),
                 batch_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(batch_.size()));

    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
    glUseProgram(static_cast<GLuint>(previousProgram));
    glDepthFunc(static_cast<GLenum>(previousDepthFunction));
    glDepthMask(previousDepthWrite);
    if (previousDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (previousBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glLineWidth(previousLineWidth);
}

} // namespace viewer
