#pragma once

#include "opengl.hpp"
#include <array>
#include <cstddef>
#include <filesystem>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace viewer {

// Samples the scene before HUD drawing and keeps each group's opacity on the
// GPU. Call inside the HUD renderer's GL state guard, with its draw target bound.
class HudBackdrop {
public:
    static constexpr std::size_t panelGroups = 15, sectorGroups = 3;
    static constexpr std::size_t maxGroups = panelGroups + sectorGroups;
    // Content rectangles in framebuffer pixels, with a top-left origin.
    // Nonpositive sizes disable a group. The final three rectangles describe
    // the danger/move/backup sector widgets; padding applies only to panels.
    using Bounds = std::array<glm::vec4, maxGroups>;

    explicit HudBackdrop(const std::filesystem::path& shaderDirectory);
    ~HudBackdrop();
    HudBackdrop(const HudBackdrop&) = delete;
    HudBackdrop& operator=(const HudBackdrop&) = delete;

    void draw(const Bounds& topLeftXYWidthHeight, int width, int height, float deltaSeconds,
              glm::vec3 encodedColor = glm::vec3(43.f / 255.f, 45.f / 255.f, 47.f / 255.f));
    void reset();

private:
    void release();
    GLuint updateProgram_ = 0, drawProgram_ = 0, vao_ = 0;
    GLuint sceneFramebuffer_ = 0, sceneTexture_ = 0;
    std::array<GLuint, 2> historyFramebuffers_{}, historyTextures_{};
    GLint updateBounds_ = -1, updateViewport_ = -1, updateDelta_ = -1, updateHistoryValid_ = -1;
    GLint drawBounds_ = -1, drawViewport_ = -1, drawPadding_ = -1, drawColor_ = -1, drawSectorPass_ = -1;
    int width_ = 0, height_ = 0;
    std::size_t historyIndex_ = 0;
    bool historyValid_ = false;
};

} // namespace viewer
