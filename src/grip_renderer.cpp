#include "grip_renderer.hpp"
#include "scene_depth.hpp"
#include "shader_program.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
class GripDrawState {
public:
    GripDrawState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer_);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunction_);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite_);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode_);
        glGetIntegerv(GL_FRONT_FACE, &frontFace_);
        glGetIntegerv(GL_BLEND_SRC_RGB, &sourceRgb_);
        glGetIntegerv(GL_BLEND_DST_RGB, &destinationRgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha_);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &destinationAlpha_);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRgb_);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha_);
        depth_ = glIsEnabled(GL_DEPTH_TEST);
        blend_ = glIsEnabled(GL_BLEND);
        cull_ = glIsEnabled(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(sceneReversedDepth ? GL_GREATER : GL_LESS);
        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    ~GripDrawState() {
        glBindVertexArray(static_cast<GLuint>(vao_));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer_));
        glUseProgram(static_cast<GLuint>(program_));
        glDepthFunc(static_cast<GLenum>(depthFunction_));
        glDepthMask(depthWrite_);
        glCullFace(static_cast<GLenum>(cullMode_));
        glFrontFace(static_cast<GLenum>(frontFace_));
        glBlendEquationSeparate(static_cast<GLenum>(equationRgb_), static_cast<GLenum>(equationAlpha_));
        glBlendFuncSeparate(static_cast<GLenum>(sourceRgb_), static_cast<GLenum>(destinationRgb_),
                            static_cast<GLenum>(sourceAlpha_), static_cast<GLenum>(destinationAlpha_));
        if (depth_) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (blend_) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (cull_) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    }
private:
    GLint program_ = 0, vao_ = 0, buffer_ = 0, depthFunction_ = GL_LESS;
    GLint cullMode_ = GL_BACK, frontFace_ = GL_CCW;
    GLint sourceRgb_ = GL_ONE, destinationRgb_ = GL_ZERO;
    GLint sourceAlpha_ = GL_ONE, destinationAlpha_ = GL_ZERO;
    GLint equationRgb_ = GL_FUNC_ADD, equationAlpha_ = GL_FUNC_ADD;
    GLboolean depthWrite_ = GL_TRUE, depth_ = GL_FALSE, blend_ = GL_FALSE, cull_ = GL_FALSE;
};
} // namespace

GripRenderer::GripRenderer(const std::filesystem::path& shaderDirectory, const Model& model) {
    for (const auto& draw : model.draws) {
        const auto& primitive = model.primitives.at(draw.primitive);
        const auto normalMatrix = glm::transpose(glm::inverse(glm::mat3(draw.transform)));
        if (primitive.indices.size() % 3 != 0)
            throw std::runtime_error("Grip model requires triangles");
        const bool reflected = glm::determinant(glm::mat3(draw.transform)) < 0;
        for (std::size_t triangle = 0; triangle < primitive.indices.size(); triangle += 3) {
            for (const std::size_t corner : {0u, reflected ? 2u : 1u, reflected ? 1u : 2u}) {
                const auto& vertex = primitive.vertices.at(primitive.indices[triangle + corner]);
                model_.push_back({glm::vec3(draw.transform * glm::vec4(vertex.position, 1)),
                                  glm::normalize(normalMatrix * vertex.normal)});
            }
        }
    }
    if (model_.empty()) throw std::runtime_error("Grip model is empty");
    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    try {
        program_ = createShaderProgram(shaderDirectory / "grip.vert", shaderDirectory / "grip.frag");
        viewProjectionLocation_ = glGetUniformLocation(program_, "uViewProjection");
        viewDirectionLocation_ = glGetUniformLocation(program_, "uViewDirection");
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &buffer_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    } catch (...) {
        glBindVertexArray(static_cast<GLuint>(previousVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
        release();
        throw;
    }
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}

GripRenderer::~GripRenderer() { release(); }

void GripRenderer::release() noexcept {
    glDeleteBuffers(1, &buffer_);
    glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}

void GripRenderer::draw(const Camera& camera, float aspect, const std::vector<GripPreviewPose>& poses) {
    if (poses.empty()) return;
    const auto maximum = std::min<std::size_t>(std::numeric_limits<GLsizei>::max(),
        static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(Vertex));
    if (poses.size() > maximum / model_.size())
        throw std::runtime_error("Too many grip preview vertices");
    triangles_.clear();
    triangles_.reserve(poses.size() * model_.size() / 3);
    for (const auto& pose : poses) {
        const auto rotation = glm::mat3_cast(pose.rotation);
        for (std::size_t index = 0; index < model_.size(); index += 3) {
            Triangle triangle{};
            glm::vec3 center(0);
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& vertex = model_[index + corner];
                triangle.vertices[corner] = {pose.position + rotation * vertex.position, rotation * vertex.normal};
                center += triangle.vertices[corner].position;
            }
            triangle.depth = glm::dot(center / 3.f - camera.position(), camera.direction());
            triangles_.push_back(triangle);
        }
    }
    // Sort all translucent front surfaces together, including overlapping hands.
    std::sort(triangles_.begin(), triangles_.end(), [](const auto& a, const auto& b) { return a.depth > b.depth; });
    batch_.clear();
    batch_.reserve(poses.size() * model_.size());
    for (const auto& triangle : triangles_)
        batch_.insert(batch_.end(), std::begin(triangle.vertices), std::end(triangle.vertices));

    const GripDrawState state;
    glUseProgram(program_);
    const auto viewProjection = sceneProjection(camera, aspect) * camera.view();
    glUniformMatrix4fv(viewProjectionLocation_, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform3fv(viewDirectionLocation_, 1, glm::value_ptr(camera.direction()));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch_.size() * sizeof(Vertex)), batch_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch_.size()));
}

} // namespace viewer
