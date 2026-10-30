#include "hud_render_interface.hpp"
#include "minimap_road_renderer.hpp"
#include "shader_program.hpp"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Vertex.h>
#include <cstddef>
#include <iostream>
#include <memory>
#include <array>
#include <algorithm>
#include <cmath>
#include <stb_image.h>

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
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &arrayTexture);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRowLength);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpackSkipPixels);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpackSkipRows);
        glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &unpackImageHeight);
        glGetIntegerv(GL_UNPACK_SKIP_IMAGES, &unpackSkipImages);
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
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
        glGetDoublev(GL_DEPTH_RANGE, depthRange);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        glGetIntegerv(GL_FRONT_FACE, &frontFace);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
        depth = glIsEnabled(GL_DEPTH_TEST); blend = glIsEnabled(GL_BLEND);
        cull = glIsEnabled(GL_CULL_FACE); stencil = glIsEnabled(GL_STENCIL_TEST);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        multisample = glIsEnabled(GL_MULTISAMPLE);
        sampleCoverage = glIsEnabled(GL_SAMPLE_COVERAGE);
        alphaToCoverage = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
        sampleMask = glIsEnabled(GL_SAMPLE_MASK);
    }
    ~GlState() {
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture); glBindTexture(GL_TEXTURE_2D_ARRAY, arrayTexture);
        glBindSampler(0, sampler);
        glActiveTexture(activeTexture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRowLength);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpackSkipRows);
        glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, unpackImageHeight);
        glPixelStorei(GL_UNPACK_SKIP_IMAGES, unpackSkipImages);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glBlendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
        glBlendEquationSeparate(equationRgb, equationAlpha);
        glDepthMask(depthWrite);
        glDepthFunc(depthFunction); glDepthRange(depthRange[0], depthRange[1]);
        glClearDepth(clearDepth);
        glFrontFace(frontFace); glCullFace(cullMode);
        enable(GL_DEPTH_TEST, depth); enable(GL_BLEND, blend);
        enable(GL_CULL_FACE, cull); enable(GL_STENCIL_TEST, stencil);
        enable(GL_SCISSOR_TEST, scissor);
        enable(GL_MULTISAMPLE, multisample); enable(GL_SAMPLE_COVERAGE, sampleCoverage);
        enable(GL_SAMPLE_ALPHA_TO_COVERAGE, alphaToCoverage); enable(GL_SAMPLE_MASK, sampleMask);
    }
private:
    static void enable(GLenum flag, GLboolean value) { if (value) glEnable(flag); else glDisable(flag); }
    GLint program{}, vao{}, arrayBuffer{}, activeTexture{}, texture{}, arrayTexture{}, sampler{};
    GLint drawFramebuffer{}, readFramebuffer{}, renderbuffer{};
    GLint unpackAlignment{}, unpackRowLength{}, unpackSkipPixels{}, unpackSkipRows{}, unpackBuffer{};
    GLint unpackImageHeight{}, unpackSkipImages{}, depthFunction{}, frontFace{}, cullMode{};
    GLdouble depthRange[2]{};
    GLdouble clearDepth{};
    GLint viewport[4]{}, scissorBox[4]{};
    GLint sourceRgb{}, destinationRgb{}, sourceAlpha{}, destinationAlpha{}, equationRgb{}, equationAlpha{};
    GLboolean depthWrite{}, depth{}, blend{}, cull{}, stencil{}, scissor{};
    GLboolean multisample{}, sampleCoverage{}, alphaToCoverage{}, sampleMask{};
};
} // namespace

HudRenderInterface::HudRenderInterface(const std::filesystem::path& directory) {
    GlState restore;
    program_ = createShaderProgram(directory / "hud.vert", directory / "hud.frag");
    viewportLocation_ = glGetUniformLocation(program_, "uViewport");
    translationLocation_ = glGetUniformLocation(program_, "uTranslation");
    texturedLocation_ = glGetUniformLocation(program_, "uTextured");
    minimapLocation_ = glGetUniformLocation(program_, "uMinimap");
    minimapSizeLocation_ = glGetUniformLocation(program_, "uMinimapSize");
    minimapOriginLocation_ = glGetUniformLocation(program_, "uMinimapOrigin");
    minimapXLocation_ = glGetUniformLocation(program_, "uMinimapX");
    minimapYLocation_ = glGetUniformLocation(program_, "uMinimapY");
    roadsLocation_ = glGetUniformLocation(program_, "uMinimapRoads");
    centerLocation_ = glGetUniformLocation(program_, "uMinimapCenter");
    spanLocation_ = glGetUniformLocation(program_, "uMinimapSpan");
    roadOverlayLocation_ = glGetUniformLocation(program_, "uMinimapRoadOverlay");
    mapContrastLocation_ = glGetUniformLocation(program_, "uMapContrast");
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uTexture"), 0);
    glGenSamplers(1, &minimapSampler_);
    glSamplerParameteri(minimapSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(minimapSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(minimapSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(minimapSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

HudRenderInterface::~HudRenderInterface() {
    if (minimapQuad_) ReleaseGeometry(minimapQuad_);
    glDeleteSamplers(1, &minimapSampler_);
    glDeleteProgram(program_);
}
void HudRenderInterface::renderExternal(const std::function<void()>& draw) { GlState restore; draw(); }

void HudRenderInterface::setMinimap(const MinimapSource& source, GLuint texture) {
    minimap_ = source;
    minimapTexture_ = texture;
}
void HudRenderInterface::setMinimapRoads(const Model& model) {
    GlState restore;
    roads_ = std::make_unique<MinimapRoadRenderer>(model);
}
void HudRenderInterface::clearMinimap() {
    minimap_ = {};
    minimapTexture_ = 0;
    roads_.reset();
}

void HudRenderInterface::drawSatelliteImage(Rml::Vector2f offset, Rml::Vector2f size, glm::vec2 origin,
                                            glm::vec2 x, glm::vec2 y, float contrast) {
    if (!minimapTexture_ || size.x <= 0 || size.y <= 0) return;
    if (!minimapQuad_) {
        const std::array<Rml::Vertex, 4> vertices{{
            {{0, 0}, {255, 255, 255, 255}, {0, 0}}, {{1, 0}, {255, 255, 255, 255}, {1, 0}},
            {{1, 1}, {255, 255, 255, 255}, {1, 1}}, {{0, 1}, {255, 255, 255, 255}, {0, 1}}}};
        const std::array<int, 6> indices{{0, 1, 2, 0, 2, 3}};
        minimapQuad_ = CompileGeometry({vertices.data(), vertices.size()}, {indices.data(), indices.size()});
    }
    glUniform1i(minimapLocation_, 1);
    glUniform2f(minimapSizeLocation_, size.x, size.y);
    glUniform2f(minimapOriginLocation_, origin.x, origin.y);
    glUniform2f(minimapXLocation_, x.x, x.y);
    glUniform2f(minimapYLocation_, y.x, y.y);
    glUniform1f(mapContrastLocation_, contrast);
    glBindSampler(0, minimapSampler_);
    RenderGeometry(minimapQuad_, offset, minimapTexture_);
    glBindSampler(0, 0);
    glUniform1i(minimapLocation_, 0);
}

void HudRenderInterface::drawSatelliteMap(Rml::Vector2f offset, Rml::Vector2f size, glm::vec2 origin, glm::vec2 extent) {
    drawSatelliteImage(offset, size, origin, {extent.x, 0}, {0, extent.y}, 1.f);
}

void HudRenderInterface::drawMinimap(Rml::Vector2f offset, Rml::Vector2f size) {
    if (!minimapTexture_ || size.x <= 0 || size.y <= 0) return;
    // Top of the control is north (-Z). World coverage is independent of HUD
    // resolution; only uniforms change as the player moves.
    constexpr double spanMeters = 300;
    const auto x = glm::vec2(minimap_.uvFromWorld * glm::dvec3(spanMeters, 0, 0));
    const auto y = glm::vec2(minimap_.uvFromWorld * glm::dvec3(0, spanMeters, 0));
    const auto center = glm::vec2(minimap_.uvFromWorld * glm::dvec3(minimapPosition_, 1));
    drawSatelliteImage(offset, size, center - (x + y) * .5f, x, y, 1.15f);
    if (roads_) {
        const glm::ivec2 targetSize{int(std::ceil(size.x)), int(std::ceil(size.y))};
        glUniform1i(roadsLocation_, 1);
        glUniform2f(centerLocation_, minimapPosition_.x, minimapPosition_.y);
        glUniform1f(spanLocation_, float(spanMeters));
        glUniform2f(viewportLocation_, float(targetSize.x), float(targetSize.y));
        glUniform2f(minimapSizeLocation_, float(targetSize.x), float(targetSize.y));
        glUniform2f(translationLocation_, 0, 0);
        const auto overlay = roads_->render(minimapPosition_, float(spanMeters), targetSize);
        glUniform1i(roadsLocation_, 0);
        glUniform2f(viewportLocation_, float(width_), float(height_));
        glUniform2f(minimapSizeLocation_, size.x, size.y);
        if (overlay) {
            glUniform1i(minimapLocation_, 1);
            glUniform1i(roadOverlayLocation_, 1);
            RenderGeometry(minimapQuad_, offset, overlay);
            glUniform1i(roadOverlayLocation_, 0);
            glUniform1i(minimapLocation_, 0);
        }
    }
}

void HudRenderInterface::render(Rml::Context& context, int width, int height, const std::function<void()>& afterLayout) {
    GlState restore;
    width_ = width;
    height_ = height;
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_SAMPLE_COVERAGE); glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE); glDisable(GL_SAMPLE_MASK);
    glEnable(GL_BLEND);
    // RmlUi vertex colours and font atlases both use premultiplied alpha.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glBindSampler(0, 0);
    glUseProgram(program_);
    glUniform2f(viewportLocation_, float(width), float(height));
    glUniform1i(minimapLocation_, 0);
    glUniform1i(roadsLocation_, 0);
    glUniform1i(roadOverlayLocation_, 0);
    context.Update();
    // Hit testing needs current layout. Keep interaction-triggered geometry
    // updates inside the same GL state guard as normal RmlUi rendering.
    if (afterLayout) { afterLayout(); context.Update(); }
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
    int width = 0, height = 0, channels = 0;
    if (!stbi_info(source.c_str(), &width, &height, &channels) ||
        width < 1 || height < 1 || width > 4096 || height > 4096) {
        std::cerr << "Cannot read HUD image: " << source << '\n';
        return 0;
    }
    const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load(source.c_str(), &width, &height, &channels, 4), stbi_image_free);
    if (!pixels) {
        std::cerr << "Cannot decode HUD image: " << source << '\n';
        return 0;
    }
    const auto size = static_cast<std::size_t>(width) * height * 4;
    // Disk PNGs use straight alpha. Font atlases already arrive premultiplied
    // through GenerateTexture, so apply this conversion only to file images.
    for (std::size_t i = 0; i < size; i += 4)
        for (std::size_t channel = 0; channel < 3; ++channel)
            pixels.get()[i + channel] = static_cast<unsigned char>((pixels.get()[i + channel] * pixels.get()[i + 3] + 127) / 255);
    dimensions = {width, height};
    return GenerateTexture({pixels.get(), size}, dimensions);
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
