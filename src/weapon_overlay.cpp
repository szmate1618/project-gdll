#include "weapon_overlay.hpp"
#include "model.hpp"
#include "shader_program.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
struct OverlayVertex { glm::vec3 position, normal, color; float pump; };

void appendModel(const Model& model, bool shotgun, std::vector<OverlayVertex>& vertices) {
    for (const auto& draw : model.draws) {
        const auto& primitive = model.primitives.at(draw.primitive);
        const auto color = primitive.material < 0 ? glm::vec3(.25f) :
            glm::vec3(model.materials.at(static_cast<std::size_t>(primitive.material)).baseColor);
        const auto normals = glm::transpose(glm::inverse(glm::mat3(draw.transform)));
        const float pump = shotgun && draw.name == "TacticalPump_Pump" ? 1.0f : 0.0f;
        for (const auto index : primitive.indices) {
            const auto& vertex = primitive.vertices.at(index);
            vertices.push_back({glm::vec3(draw.transform * glm::vec4(vertex.position, 1)),
                                glm::normalize(normals * vertex.normal), color * glm::vec3(vertex.color), pump});
        }
    }
}

class WeaponDrawState {
public:
    explicit WeaponDrawState(bool overlay) {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
        glGetIntegerv(GL_FRONT_FACE, &front_); glGetIntegerv(GL_CULL_FACE_MODE, &cullMode_);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunction_); glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite_);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth_); glGetDoublev(GL_DEPTH_RANGE, depthRange_);
        depth_ = glIsEnabled(GL_DEPTH_TEST); blend_ = glIsEnabled(GL_BLEND);
        cull_ = glIsEnabled(GL_CULL_FACE); scissor_ = glIsEnabled(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDepthRange(0, 1);
        glDisable(GL_BLEND); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
        if (overlay) {
            glDisable(GL_SCISSOR_TEST);
            glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
        }
    }
    ~WeaponDrawState() {
        glBindVertexArray(static_cast<GLuint>(vao_)); glUseProgram(static_cast<GLuint>(program_));
        if (depth_) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (blend_) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (cull_) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if (scissor_) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        glCullFace(static_cast<GLenum>(cullMode_)); glFrontFace(static_cast<GLenum>(front_));
        glDepthFunc(static_cast<GLenum>(depthFunction_)); glDepthMask(depthWrite_);
        glClearDepth(clearDepth_); glDepthRange(depthRange_[0], depthRange_[1]);
    }
private:
    GLint program_ = 0, vao_ = 0, front_ = 0, cullMode_ = 0, depthFunction_ = 0;
    GLboolean depth_ = GL_FALSE, blend_ = GL_FALSE, cull_ = GL_FALSE, scissor_ = GL_FALSE, depthWrite_ = GL_TRUE;
    GLdouble clearDepth_ = 1, depthRange_[2]{};
};
} // namespace

WeaponOverlay::WeaponOverlay(const std::filesystem::path& shaders, const std::filesystem::path& models,
                             WeaponViewBody body) {
    // Load and flatten once, preserving part transforms and flat source normals.
    // One shared buffer and one draw per equipped weapon; no per-frame loading.
    std::vector<OverlayVertex> vertices;
    for (std::size_t slot = 0; slot < weaponIds.size(); ++slot) {
        ranges_[slot].first = static_cast<GLint>(vertices.size());
        const auto model = loadModel(models / (std::string(weaponIds[slot]) + ".glb"));
        poses_[slot] = slot == 4 ? glm::rotate(glm::translate(glm::mat4(1), {.17f, -.17f, -.5f}),
                                             glm::radians(-15.0f), glm::vec3(0, 0, 1)) :
                                  weaponViewPose(model, slot == 0, body);
        appendModel(model, slot == 2, vertices);
        ranges_[slot].count = static_cast<GLsizei>(vertices.size()) - ranges_[slot].first;
    }
    program_ = createShaderProgram(shaders / "weapon.vert", shaders / "weapon.frag");
    transform_ = glGetUniformLocation(program_, "uTransform");
    pose_ = glGetUniformLocation(program_, "uPose");
    pumpTravel_ = glGetUniformLocation(program_, "uPumpTravel");
    flashPosition_ = glGetUniformLocation(program_, "uMuzzleFlashPosition");
    flashColor_ = glGetUniformLocation(program_, "uMuzzleFlashColor");
    flashRange_ = glGetUniformLocation(program_, "uMuzzleFlashRange");
    GLint previousVao = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(OverlayVertex)), vertices.data(), GL_STATIC_DRAW);
    for (GLuint attribute = 0; attribute < 4; ++attribute) glEnableVertexAttribArray(attribute);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, normal)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, color)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, pump)));
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}

WeaponOverlay::~WeaponOverlay() {
    glDeleteBuffers(1, &buffer_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}

glm::vec3 WeaponOverlay::muzzlePosition(std::size_t slot, glm::vec3 modelMuzzle, const WeaponMotionPose& motion) const {
    return glm::vec3(animatedWeaponPose(poses_.at(slot), motion) * glm::vec4(modelMuzzle, 1));
}

void WeaponOverlay::draw(std::size_t slot, int width, int height, const MuzzleFlashLight& flash,
                         const WeaponMotionPose& motion) {
    if (slot >= ranges_.size() || width <= 0 || height <= 0) return;
    const WeaponDrawState restore(true);
    const auto projection = glm::perspective(glm::radians(60.0f), static_cast<float>(width) / height, .01f, 5.0f);
    const auto pose = animatedWeaponPose(poses_[slot], motion);
    drawGeometry(slot, projection, pose, flash, motion.pumpTravel);
}

void WeaponOverlay::drawWorld(std::size_t slot, const Camera& camera, float aspect, const glm::mat4& eyeToWorld,
                              const MuzzleFlashLight& worldFlash, const WeaponMotionPose& motion) {
    if (slot >= ranges_.size() || !std::isfinite(aspect) || aspect <= 0) return;
    const WeaponDrawState restore(false);
    const auto view = camera.view();
    const auto pose = view * eyeToWorld * animatedWeaponPose(poses_[slot], motion);
    drawGeometry(slot, camera.projection(aspect), pose, worldFlash.transformed(view), motion.pumpTravel);
}

void WeaponOverlay::drawGeometry(std::size_t slot, const glm::mat4& projection, const glm::mat4& modelToEye,
                                 const MuzzleFlashLight& flash, float pumpTravel) {
    glUseProgram(program_); glBindVertexArray(vao_);
    const auto transform = projection * modelToEye;
    glUniformMatrix4fv(transform_, 1, GL_FALSE, glm::value_ptr(transform));
    glUniformMatrix4fv(pose_, 1, GL_FALSE, glm::value_ptr(modelToEye));
    glUniform1f(pumpTravel_, pumpTravel);
    glUniform3fv(flashPosition_, 1, glm::value_ptr(flash.position));
    glUniform3fv(flashColor_, 1, glm::value_ptr(flash.color));
    glUniform1f(flashRange_, flash.range);
    glDrawArrays(GL_TRIANGLES, ranges_[slot].first, ranges_[slot].count);
}
} // namespace viewer
