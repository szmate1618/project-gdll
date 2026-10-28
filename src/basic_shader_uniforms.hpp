#pragma once

#include "opengl.hpp"

#include <array>
#include <cstddef>

namespace viewer {

// Locations belong to one linked basic program and are queried once at creation.
struct BasicShaderUniforms {
    BasicShaderUniforms() = default;
    explicit BasicShaderUniforms(GLuint program);

    GLint modelLocation_ = -1, viewLocation_ = -1, projectionLocation_ = -1, normalLocation_ = -1;
    GLint colorLocation_ = -1, textureLocation_ = -1, hasTextureLocation_ = -1, unlitLocation_ = -1;
    GLint alphaLocation_ = -1, cutoffLocation_ = -1, instancedLocation_ = -1;
    GLint minifiedCutoffLocation_ = -1;
    GLint treeCanopyLocation_ = -1, treeBillboardModeLocation_ = -1, treeBillboardPivotLocation_ = -1;
    GLint animatedLocation_ = -1, animationLocation_ = -1, animationFrameLocation_ = -1;
    GLint animationCountLocation_ = -1, animationVerticesLocation_ = -1;
    GLint skinningLocation_ = -1, skinningBonesLocation_ = -1, skinningBoneCountLocation_ = -1;
    GLint zombieWoundsLocation_ = -1, zombieTriangleOffsetLocation_ = -1, zombieCullBackfacesLocation_ = -1;
    GLint shadowPassLocation_ = -1, shadowsLocation_ = -1, shadowMapLocation_ = -1, sunMatrixLocation_ = -1;
    GLint eyeLocation_ = -1, sunDirectionLocation_ = -1, sunColorLocation_ = -1;
    GLint skyAmbientLocation_ = -1, groundAmbientLocation_ = -1;
    GLint shadowDistanceLocation_ = -1, shadowFadeLocation_ = -1;
    GLint hazeColorLocation_ = -1, hazeDensityLocation_ = -1, hazeStartLocation_ = -1;
    GLint groundFogLocation_ = -1, forestVisibilityLocation_ = -1;
    GLint nightBlendLocation_ = -1, unlitLightLocation_ = -1;
    GLint headlightsLocation_ = -1, headlightDirectionLocation_ = -1;
    std::array<GLint, 2> headlightPositions_{};
    GLint flashlightPositionLocation_ = -1, flashlightShadowsLocation_ = -1, flashlightShadowMapLocation_ = -1;
    GLint flashlightMatrixLocation_ = -1, flashlightShadowDistancesLocation_ = -1;
    GLint flashlightDirectionLocation_ = -1, flashlightColorLocation_ = -1, flashlightConeLocation_ = -1;
    GLint muzzleFlashPositionLocation_ = -1, muzzleFlashColorLocation_ = -1, muzzleFlashRangeLocation_ = -1;
    static constexpr std::size_t maxExplosionLights = 8; // Matches basic.frag.
    GLint explosionCountLocation_ = -1;
    std::array<GLint, maxExplosionLights> explosionPositionLocations_{}, explosionColorLocations_{}, explosionRangeLocations_{};
};

} // namespace viewer
