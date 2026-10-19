#include "weapon_overlay.hpp"
#include "model.hpp"
#include "shader_program.hpp"
#include <array>
#include <cstddef>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
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

WeaponOverlay::WeaponOverlay(const std::filesystem::path& shaders, const std::filesystem::path& models,
                             WeaponViewBody body) {
    // Load and flatten once, preserving part transforms and flat source normals.
    // One shared buffer and one draw per equipped weapon; no per-frame loading.
    std::vector<OverlayVertex> vertices;
    const std::array<const char*, 4> names{"pistol.glb", "submachine_gun.glb", "shotgun.glb", "sniper.glb"};
    for (std::size_t slot = 0; slot < names.size(); ++slot) {
        ranges_[slot].first = static_cast<GLint>(vertices.size());
        const auto model = loadModel(models / names[slot]);
        poses_[slot] = weaponViewPose(model, slot == 0, body);
        appendModel(model, vertices);
        ranges_[slot].count = static_cast<GLsizei>(vertices.size()) - ranges_[slot].first;
    }
    program_ = createShaderProgram(shaders / "weapon.vert", shaders / "weapon.frag");
    transform_ = glGetUniformLocation(program_, "uTransform");
    pose_ = glGetUniformLocation(program_, "uPose");
    flashPosition_ = glGetUniformLocation(program_, "uMuzzleFlashPosition");
    flashColor_ = glGetUniformLocation(program_, "uMuzzleFlashColor");
    flashRange_ = glGetUniformLocation(program_, "uMuzzleFlashRange");
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

WeaponOverlay::~WeaponOverlay() {
    glDeleteBuffers(1, &buffer_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}

glm::vec3 WeaponOverlay::muzzlePosition(std::size_t slot, glm::vec3 modelMuzzle) const {
    return glm::vec3(poses_.at(slot) * glm::vec4(modelMuzzle, 1));
}

void WeaponOverlay::draw(std::size_t slot, int width, int height, const MuzzleFlashLight& flash) {
    if (slot >= ranges_.size() || width <= 0 || height <= 0) return;
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
    glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_); glBindVertexArray(vao_);
    const auto projection = glm::perspective(glm::radians(60.0f), static_cast<float>(width) / height, .01f, 5.0f);
    const auto transform = projection * poses_[slot];
    glUniformMatrix4fv(transform_, 1, GL_FALSE, glm::value_ptr(transform));
    glUniformMatrix4fv(pose_, 1, GL_FALSE, glm::value_ptr(poses_[slot]));
    glUniform3fv(flashPosition_, 1, glm::value_ptr(flash.position));
    glUniform3fv(flashColor_, 1, glm::value_ptr(flash.color));
    glUniform1f(flashRange_, flash.range);
    glDrawArrays(GL_TRIANGLES, ranges_[slot].first, ranges_[slot].count);
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
