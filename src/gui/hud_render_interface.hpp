#pragma once

#include <functional>

#include "opengl.hpp"
#include "minimap.hpp"
#include <filesystem>
#include <memory>
#include <RmlUi/Core/RenderInterface.h>

namespace Rml { class Context; }

namespace viewer {
class MinimapRoadRenderer;

// RmlUi backend for flat geometry, text and PNG icons, with rectangular clipping.
// File textures are owned; the minimap borrows the scene's terrain texture.
class HudRenderInterface final : public Rml::RenderInterface {
public:
    explicit HudRenderInterface(const std::filesystem::path& shaderDirectory);
    ~HudRenderInterface() override;
    void render(Rml::Context& context, int width, int height, const std::function<void()>& afterLayout = {});
    void setMinimap(const MinimapSource& source, GLuint texture);
    void setMinimapRoads(const Model& model);
    void setMinimapPosition(glm::vec2 position) { minimapPosition_ = position; }
    void drawMinimap(Rml::Vector2f offset, Rml::Vector2f size);
    void drawSatelliteMap(Rml::Vector2f offset, Rml::Vector2f size, glm::vec2 uvOrigin, glm::vec2 uvExtent);
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
    void drawSatelliteImage(Rml::Vector2f offset, Rml::Vector2f size, glm::vec2 origin,
                            glm::vec2 uvX, glm::vec2 uvY, float contrast);
    GLuint program_ = 0;
    GLint viewportLocation_ = -1, translationLocation_ = -1, texturedLocation_ = -1;
    int width_ = 0, height_ = 0;
    MinimapSource minimap_;
    glm::vec2 minimapPosition_{0};
    GLuint minimapTexture_ = 0, minimapSampler_ = 0; // Texture is borrowed; sampler is owned.
    Rml::CompiledGeometryHandle minimapQuad_ = 0;
    GLint minimapLocation_ = -1, minimapSizeLocation_ = -1;
    GLint minimapOriginLocation_ = -1, minimapXLocation_ = -1, minimapYLocation_ = -1;
    GLint roadsLocation_ = -1, centerLocation_ = -1, spanLocation_ = -1;
    GLint roadOverlayLocation_ = -1;
    GLint mapContrastLocation_ = -1;
    std::unique_ptr<MinimapRoadRenderer> roads_;
};

} // namespace viewer
