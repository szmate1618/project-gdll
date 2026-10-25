#pragma once

#include "opengl.hpp"
#include <filesystem>
#include <RmlUi/Core/RenderInterface.h>

namespace Rml { class Context; }

namespace viewer {

// Basic RmlUi backend for flat rectangles and text, with rectangular clipping.
// File textures are deliberately rejected. Generated textures are font atlases.
class HudRenderInterface final : public Rml::RenderInterface {
public:
    explicit HudRenderInterface(const std::filesystem::path& shaderDirectory);
    ~HudRenderInterface() override;
    void render(Rml::Context& context, int width, int height);
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                               Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
private:
    GLuint program_ = 0;
    GLint viewportLocation_ = -1, translationLocation_ = -1, texturedLocation_ = -1;
    int height_ = 0;
};

} // namespace viewer
