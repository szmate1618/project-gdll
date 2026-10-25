#include "hud_render_interface.hpp"
#include "shader_program.hpp"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Vertex.h>
#include <cstddef>
#include <iostream>
#include <memory>

namespace viewer {
namespace {
struct Geometry {
    GLuint vao = 0, vertices = 0, indices = 0;
    GLsizei count = 0;
};

// RmlUi can compile geometry and create font atlases during Update/Render.
// Preserve bindings around the entire operation, including the first frame.
class GlState {
public:
    GlState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRowLength);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpackSkipPixels);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpackSkipRows);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetIntegerv(GL_BLEND_SRC_RGB, &sourceRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &destinationRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &destinationAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        depth = glIsEnabled(GL_DEPTH_TEST); blend = glIsEnabled(GL_BLEND);
        cull = glIsEnabled(GL_CULL_FACE); stencil = glIsEnabled(GL_STENCIL_TEST);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
    }
    ~GlState() {
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindSampler(0, sampler);
        glActiveTexture(activeTexture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRowLength);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpackSkipRows);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glBlendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
        glBlendEquationSeparate(equationRgb, equationAlpha);
        glDepthMask(depthWrite);
        enable(GL_DEPTH_TEST, depth); enable(GL_BLEND, blend);
        enable(GL_CULL_FACE, cull); enable(GL_STENCIL_TEST, stencil);
        enable(GL_SCISSOR_TEST, scissor);
    }
private:
    static void enable(GLenum flag, GLboolean value) { if (value) glEnable(flag); else glDisable(flag); }
    GLint program{}, vao{}, arrayBuffer{}, activeTexture{}, texture{}, sampler{};
    GLint unpackAlignment{}, unpackRowLength{}, unpackSkipPixels{}, unpackSkipRows{}, unpackBuffer{};
    GLint viewport[4]{}, scissorBox[4]{};
    GLint sourceRgb{}, destinationRgb{}, sourceAlpha{}, destinationAlpha{}, equationRgb{}, equationAlpha{};
    GLboolean depthWrite{}, depth{}, blend{}, cull{}, stencil{}, scissor{};
};
} // namespace

HudRenderInterface::HudRenderInterface(const std::filesystem::path& directory) {
    GlState restore;
    program_ = createShaderProgram(directory / "hud.vert", directory / "hud.frag");
    viewportLocation_ = glGetUniformLocation(program_, "uViewport");
    translationLocation_ = glGetUniformLocation(program_, "uTranslation");
    texturedLocation_ = glGetUniformLocation(program_, "uTextured");
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uTexture"), 0);
}

HudRenderInterface::~HudRenderInterface() { glDeleteProgram(program_); }

void HudRenderInterface::render(Rml::Context& context, int width, int height) {
    GlState restore;
    height_ = height;
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    // RmlUi vertex colours and font atlases both use premultiplied alpha.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glBindSampler(0, 0);
    glUseProgram(program_);
    glUniform2f(viewportLocation_, float(width), float(height));
    context.Update();
    context.Render();
}

Rml::CompiledGeometryHandle HudRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                               Rml::Span<const int> indices) {
    auto geometry = std::make_unique<Geometry>();
    geometry->count = static_cast<GLsizei>(indices.size());
    glGenVertexArrays(1, &geometry->vao);
    glGenBuffers(1, &geometry->vertices);
    glGenBuffers(1, &geometry->indices);
    glBindVertexArray(geometry->vao);
    glBindBuffer(GL_ARRAY_BUFFER, geometry->vertices);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Rml::Vertex), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(int), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, colour)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, tex_coord)));
    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
}

void HudRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                                      Rml::TextureHandle texture) {
    const auto& geometry = *reinterpret_cast<const Geometry*>(handle);
    glUniform2f(translationLocation_, translation.x, translation.y);
    glUniform1i(texturedLocation_, texture ? 1 : 0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glBindVertexArray(geometry.vao);
    glDrawElements(GL_TRIANGLES, geometry.count, GL_UNSIGNED_INT, nullptr);
}

void HudRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
    const std::unique_ptr<Geometry> geometry(reinterpret_cast<Geometry*>(handle));
    glDeleteVertexArrays(1, &geometry->vao);
    glDeleteBuffers(1, &geometry->vertices);
    glDeleteBuffers(1, &geometry->indices);
}

Rml::TextureHandle HudRenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
    dimensions = {};
    std::cerr << "HUD image loading is disabled: " << source << '\n';
    return 0;
}

Rml::TextureHandle HudRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i dimensions) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dimensions.x, dimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

void HudRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
    const GLuint name = static_cast<GLuint>(texture);
    glDeleteTextures(1, &name);
}

void HudRenderInterface::EnableScissorRegion(bool enable) {
    if (enable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

void HudRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
    glScissor(region.Left(), height_ - region.Bottom(), region.Width(), region.Height());
}
} // namespace viewer
