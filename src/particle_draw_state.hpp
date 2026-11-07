#pragma once
#include "opengl.hpp"

namespace viewer {
// Restore the OpenGL state touched by billboard particle passes.
struct ParticleDrawState {
    GLint program, vao, buffer, activeTexture, texture;
    GLint sourceRgb, destRgb, sourceAlpha, destAlpha, equationRgb, equationAlpha;
    GLboolean depthWrite, depthTest, blend, cull;
    ParticleDrawState() {
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
    ~ParticleDrawState() {
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
} // namespace viewer
