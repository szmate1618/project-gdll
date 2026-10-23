#include "blood_mist_renderer.hpp"
#include "shader_program.hpp"
#include "visibility.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
struct GLState {
    GLint program, vao, buffer, activeTexture, texture;
    GLint sourceRgb, destRgb, sourceAlpha, destAlpha, equationRgb, equationAlpha;
    GLboolean depthWrite, depthTest, blend, cull;
    GLState() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glGetIntegerv(GL_BLEND_SRC_RGB,&sourceRgb); glGetIntegerv(GL_BLEND_DST_RGB,&destRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&sourceAlpha); glGetIntegerv(GL_BLEND_DST_ALPHA,&destAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&equationRgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&equationAlpha);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
        depthTest=glIsEnabled(GL_DEPTH_TEST); blend=glIsEnabled(GL_BLEND); cull=glIsEnabled(GL_CULL_FACE);
    }
    ~GLState() {
        glBindVertexArray(GLuint(vao)); glBindBuffer(GL_ARRAY_BUFFER,GLuint(buffer)); glUseProgram(GLuint(program));
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,GLuint(texture)); glActiveTexture(GLenum(activeTexture));
        glDepthMask(depthWrite);
        if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glBlendFuncSeparate(GLenum(sourceRgb),GLenum(destRgb),GLenum(sourceAlpha),GLenum(destAlpha));
        glBlendEquationSeparate(GLenum(equationRgb),GLenum(equationAlpha));
    }
};
struct Instance { glm::vec4 centerSize; glm::vec2 rotationOpacity; };

// Generated once: a soft, irregular cloud mask shared by every burst size.
std::array<std::uint8_t,128*128> mistTexture() {
    std::array<std::uint8_t,128*128> pixels{};
    const std::array<glm::vec3,7> lobes{{{-.35f,.15f,8},{.22f,.28f,10},{.4f,-.22f,12},
        {-.2f,-.32f,12},{0,0,5},{-.55f,-.15f,24},{.1f,.55f,28}}};
    for (int y=0;y<128;++y) for (int x=0;x<128;++x) {
        const glm::vec2 p((x+.5f)/64-1,(y+.5f)/64-1);
        float cloud=0;
        for (const auto& lobe : lobes) {
            const auto d=p-glm::vec2(lobe);
            cloud+=std::exp(-glm::dot(d,d)*lobe.z);
        }
        const float rim=std::max(0.0f,1-glm::dot(p,p));
        const float wisps=.78f+.22f*std::sin(17*p.x+4*std::sin(13*p.y))*std::cos(19*p.y);
        pixels[std::size_t(y*128+x)]=std::uint8_t(255*std::clamp(cloud*.38f*rim*rim*wisps,0.0f,1.0f));
    }
    return pixels;
}
}
BloodMistRenderer::BloodMistRenderer(const std::filesystem::path& shaders) {
    const GLState state;
    try {
        program_=createShaderProgram(shaders/"blood_mist.vert",shaders/"blood_mist.frag");
        view_=glGetUniformLocation(program_,"uView"); projection_=glGetUniformLocation(program_,"uProjection");
        textureLocation_=glGetUniformLocation(program_,"uBloodMask"); color_=glGetUniformLocation(program_,"uColor");
        glGenVertexArrays(1,&vao_); glGenBuffers(1,&instances_);
        glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER,instances_);
        glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(BloodMist::capacity*sizeof(Instance)),nullptr,GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Instance),nullptr);
        glVertexAttribDivisor(0,1);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<const void*>(offsetof(Instance,rotationOpacity)));
        glVertexAttribDivisor(1,1);
        glGenTextures(1,&texture_); glBindTexture(GL_TEXTURE_2D,texture_);
        const auto pixels=mistTexture();
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,128,128,0,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    } catch (...) { release(); throw; }
}
BloodMistRenderer::~BloodMistRenderer() { release(); }
void BloodMistRenderer::release() noexcept {
    glDeleteTextures(1,&texture_); glDeleteBuffers(1,&instances_); glDeleteVertexArrays(1,&vao_);
    if (program_) glDeleteProgram(program_);
}
void BloodMistRenderer::draw(const Camera& camera, float aspect, std::vector<BloodMistBurst> bursts, glm::vec3 ambient) {
    if (bursts.empty()) return;
    const auto view=camera.view(), projection=camera.projection(aspect);
    const Frustum frustum(projection*view);
    const auto eye=camera.position();
    std::sort(bursts.begin(),bursts.end(),[&](const auto& a,const auto& b) {
        return glm::dot(a.position-eye,a.position-eye)>glm::dot(b.position-eye,b.position-eye);
    });
    std::vector<Instance> instances;
    for (const auto& burst : bursts) {
        if (burst.age<0 || burst.age>=BloodMist::duration) continue;
        const float t=burst.age/BloodMist::duration;
        const float width=burst.size==BloodMistSize::small ? .18f : burst.size==BloodMistSize::medium ? .35f : .55f;
        const float size=width*(.7f+.45f*t);
        if (!frustum.intersects(burst.position-glm::vec3(size),burst.position+glm::vec3(size))) continue;
        const float angle=float((burst.id*2654435761u)%65536)*(6.28318530718f/65536);
        instances.push_back({glm::vec4(burst.position,size),{angle,.85f*(1-t)*(1-t)}});
    }
    if (instances.empty()) return;
    const GLState state;
    glUseProgram(program_);
    glUniformMatrix4fv(view_,1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(projection_,1,GL_FALSE,glm::value_ptr(projection));
    const auto color=glm::vec3(.32f,.008f,.016f)*glm::clamp(ambient,glm::vec3(.15f),glm::vec3(1));
    glUniform3fv(color_,1,glm::value_ptr(color)); glUniform1i(textureLocation_,0);
    glBindTexture(GL_TEXTURE_2D,texture_);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER,instances_);
    glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(instances.size()*sizeof(Instance)),instances.data(),GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLES,0,6,GLsizei(instances.size()));
}
} // namespace viewer
