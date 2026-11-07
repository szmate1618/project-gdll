#include "fire_renderer.hpp"
#include "particle_draw_state.hpp"
#include "scene_depth.hpp"
#include "shader_program.hpp"
#include "visibility.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
FireRenderer::FireRenderer(const std::filesystem::path& shaders) {
    const ParticleDrawState state;
    try {
        program_ = createShaderProgram(shaders/"fire.vert",shaders/"fire.frag");
        projection_ = glGetUniformLocation(program_,"uProjectionView");
        right_ = glGetUniformLocation(program_,"uRight");
        glGenVertexArrays(1,&vao_); glGenBuffers(1,&buffer_);
        glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER,buffer_);
        glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(sizeof(scratch_)),nullptr,GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Instance),nullptr);
        glVertexAttribDivisor(0,1);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(offsetof(Instance,sizeSeed)));
        glVertexAttribDivisor(1,1);
    } catch (...) { release(); throw; }
}
FireRenderer::~FireRenderer() { release(); }
void FireRenderer::release() noexcept {
    glDeleteBuffers(1,&buffer_); glDeleteVertexArrays(1,&vao_);
    if (program_) glDeleteProgram(program_);
}
void FireRenderer::draw(const Camera& camera, float aspect, const FireEffects& fire, double seconds) {
    const Frustum frustum(camera.projection(aspect)*camera.view());
    std::size_t count = 0;
    for (const auto& patch : fire.patches()) {
        const float age = float(seconds-patch.born);
        if (!patch.active || age<0 || age>=molotovFireSeconds) continue;
        const auto point = patch.contact.point+patch.contact.normal*.012f;
        if (!frustum.intersects(point-glm::vec3(.5f,.05f,.5f),point+glm::vec3(.5f,1,.5f))) continue;
        scratch_[count++] = {glm::vec4(point,age),{.6f,float(patch.seed%65536u)/65536.f}};
    }
    if (!count) return;
    const ParticleDrawState state;
    glUseProgram(program_);
    const auto matrix = sceneProjection(camera,aspect)*camera.view();
    const auto view = camera.view();
    auto right = glm::vec3(view[0][0],0,view[2][0]);
    right = glm::length(right)>.001f ? glm::normalize(right) : glm::vec3(1,0,0);
    glUniformMatrix4fv(projection_,1,GL_FALSE,glm::value_ptr(matrix));
    glUniform3fv(right_,1,glm::value_ptr(right));
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_ONE);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER,buffer_);
    glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(count*sizeof(Instance)),scratch_.data());
    glDrawArraysInstanced(GL_TRIANGLES,0,6,GLsizei(count));
}
} // namespace viewer
