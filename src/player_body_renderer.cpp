#include "player_body_renderer.hpp"
#include "player_body.hpp"
#include "box_layer.hpp"
#include "shader_program.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
struct OverlayVertex { glm::vec3 position, normal, color; };

void appendModel(const Model& model, std::vector<OverlayVertex>& vertices) {
    for (const auto& draw : model.draws) {
        const auto& primitive = model.primitives.at(draw.primitive);
        const auto color = primitive.material < 0 ? glm::vec3(.25f) :
            glm::vec3(model.materials.at(static_cast<std::size_t>(primitive.material)).baseColor);
        const auto normals = glm::transpose(glm::inverse(glm::mat3(draw.transform)));
        for (const auto index : primitive.indices) {
            const auto& vertex = primitive.vertices.at(index);
            vertices.push_back({glm::vec3(draw.transform * glm::vec4(vertex.position, 1)),
                                glm::normalize(normals * vertex.normal), color * glm::vec3(vertex.color)});
        }
    }
}
} // namespace

PlayerBodyRenderer::PlayerBodyRenderer(const std::filesystem::path& shaders) {
    std::vector<OverlayVertex> vertices;
    auto cube = BoxLayer::unitCube();
    cube.draws.emplace_back();
    for (auto& material : cube.materials) material.baseColor = {.58f, .39f, .28f, 1};
    appendModel(cube, vertices);
    vertexCount_ = static_cast<GLsizei>(vertices.size());
    program_ = createShaderProgram(shaders / "player_body.vert", shaders / "player_body.frag");
    transform_ = glGetUniformLocation(program_, "uTransform");
    opacity_ = glGetUniformLocation(program_, "uOpacity");
    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(OverlayVertex)), vertices.data(), GL_STATIC_DRAW);
    for (GLuint attribute = 0; attribute < 3; ++attribute) glEnableVertexAttribArray(attribute);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, normal)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, color)));
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}
PlayerBodyRenderer::~PlayerBodyRenderer() {
    glDeleteBuffers(1, &buffer_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}
void PlayerBodyRenderer::captureBody(const PlayerBody& body, float seconds) { capturePose(body.pose(), seconds); }
void PlayerBodyRenderer::capturePose(const PlayerBodyPose& body, float seconds) {
    if (!body.enabled) { bodyHistoryCount_ = 0; bodyTime_ = 0; return; }
    if (seconds <= 0) return;
    bodyTime_ += seconds;
    BodySample sample;
    sample.time = bodyTime_;
    sample.hip = glm::vec3(body.parts[static_cast<std::size_t>(PlayerBodyPart::hip)][3]);
    for (std::size_t part = 0; part < sample.arms.size(); ++part)
        sample.arms[part] = body.parts[static_cast<std::size_t>(PlayerBodyPart::leftUpperArm) + part];
    // Do not draw a streak across teleport/reset discontinuities.
    if (bodyHistoryCount_ && glm::distance(sample.hip, bodyHistory_[0].hip) > 1.f)
        bodyHistoryCount_ = 0;
    const auto last = std::min(bodyHistoryCount_, bodyHistory_.size() - 1);
    for (auto i = last; i > 0; --i) bodyHistory_[i] = bodyHistory_[i - 1];
    bodyHistory_[0] = sample;
    bodyHistoryCount_ = std::min(bodyHistoryCount_ + 1, bodyHistory_.size());
}
void PlayerBodyRenderer::drawBodyTrail(const PlayerBodyPose& body, const glm::mat4& projectionView,
                                     float secondsBehindNewest) {
    if (bodyHistoryCount_ < 2) return;
    const bool left = (body.striking | body.returning) & 1;
    const bool right = (body.striking | body.returning) & 2;
    if (!left && !right) return;
    GLint srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0, equationRgb = 0, equationAlpha = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb); glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // The opaque arm and scene already own depth. Ghosts only read it.
    glDepthMask(GL_FALSE);
    const double displayedTime = bodyHistory_[0].time - std::max(0.f, secondsBehindNewest);
    for (int tap = 4; tap > 0; --tap) {
        const double time = displayedTime - tap * .008;
        for (std::size_t i = 1; i < bodyHistoryCount_; ++i) {
            const auto& newer = bodyHistory_[i - 1];
            const auto& older = bodyHistory_[i];
            if (time < older.time || time > newer.time) continue;
            const float t = static_cast<float>((time - older.time) / (newer.time - older.time));
            for (std::size_t part = 0; part < newer.arms.size(); ++part) {
                if (!(part < 3 ? left : right)) continue;
                // Interpolate only nearby recorded poses; no velocity extrapolation
                // through an impact, and no camera-turn smear of an idle guard.
                const auto pose = older.arms[part] * (1.f - t) + newer.arms[part] * t;
                const auto& displayed = body.parts[static_cast<std::size_t>(PlayerBodyPart::leftUpperArm) + part];
                const float travel = glm::distance(glm::vec3(pose[3]), glm::vec3(displayed[3]));
                const float opacity = .035f * (5 - tap) * std::clamp(travel / .04f, 0.f, 1.f);
                if (opacity < .005f) continue;
                const auto transform = projectionView * pose;
                glUniform1f(opacity_, opacity);
                glUniformMatrix4fv(transform_, 1, GL_FALSE, glm::value_ptr(transform));
                glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
            }
            break;
        }
    }
    glUniform1f(opacity_, 1.f);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
    glBlendEquationSeparate(equationRgb, equationAlpha);
}
void PlayerBodyRenderer::drawBody(const PlayerBody& body, const glm::mat4& projectionView, int width, int height) {
    drawPose(body.pose(), projectionView, width, height);
}
void PlayerBodyRenderer::drawPose(const PlayerBodyPose& body, const glm::mat4& projectionView, int width, int height,
                                  float secondsBehindNewest) {
    if (!body.enabled) { bodyHistoryCount_ = 0; bodyTime_ = 0; return; }
    if (width <= 0 || height <= 0) return;
    GLint previousProgram = 0, previousVao = 0, previousFront = 0, previousCullMode = 0, previousDepthFunction = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_FRONT_FACE, &previousFront);
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunction);
    const auto depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    const auto scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depthWrite = GL_TRUE;
    GLdouble clearDepth = 1, depthRange[2]{};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    glGetDoublev(GL_DEPTH_RANGE, depthRange);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDepthRange(0, 1);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glUseProgram(program_); glBindVertexArray(vao_);
    glUniform1f(opacity_, 1.f);
    for (std::size_t part = 0; part < playerBodyPartCount; ++part) {
        const auto transform = projectionView * body.parts[part];
        glUniformMatrix4fv(transform_, 1, GL_FALSE, glm::value_ptr(transform));
        glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    }
    drawBodyTrail(body, projectionView, secondsBehindNewest);
    glBindVertexArray(static_cast<GLuint>(previousVao)); glUseProgram(static_cast<GLuint>(previousProgram));
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glCullFace(static_cast<GLenum>(previousCullMode)); glFrontFace(static_cast<GLenum>(previousFront));
    glDepthFunc(static_cast<GLenum>(previousDepthFunction)); glDepthMask(depthWrite);
    glClearDepth(clearDepth); glDepthRange(depthRange[0], depthRange[1]);
}
} // namespace viewer
