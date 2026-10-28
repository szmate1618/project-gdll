#include "basic_shader_uniforms.hpp"

#include <string>

namespace viewer {

BasicShaderUniforms::BasicShaderUniforms(GLuint program) {
    modelLocation_ = glGetUniformLocation(program, "uModel");
    viewLocation_ = glGetUniformLocation(program, "uView");
    projectionLocation_ = glGetUniformLocation(program, "uProjection");
    normalLocation_ = glGetUniformLocation(program, "uNormalMatrix");
    colorLocation_ = glGetUniformLocation(program, "uBaseColor");
    textureLocation_ = glGetUniformLocation(program, "uBaseTexture");
    hasTextureLocation_ = glGetUniformLocation(program, "uHasTexture");
    unlitLocation_ = glGetUniformLocation(program, "uUnlit");
    alphaLocation_ = glGetUniformLocation(program, "uAlphaMode");
    cutoffLocation_ = glGetUniformLocation(program, "uAlphaCutoff");
    minifiedCutoffLocation_ = glGetUniformLocation(program, "uMinifiedAlphaCutoff");
    instancedLocation_ = glGetUniformLocation(program, "uInstanced");
    treeCanopyLocation_ = glGetUniformLocation(program, "uTreeCanopyShading");
    treeBillboardModeLocation_ = glGetUniformLocation(program, "uTreeBillboardMode");
    treeBillboardPivotLocation_ = glGetUniformLocation(program, "uTreeBillboardPivot");
    animatedLocation_ = glGetUniformLocation(program, "uAnimated");
    animationLocation_ = glGetUniformLocation(program, "uAnimation");
    animationFrameLocation_ = glGetUniformLocation(program, "uAnimationFrame");
    animationCountLocation_ = glGetUniformLocation(program, "uAnimationCount");
    animationVerticesLocation_ = glGetUniformLocation(program, "uAnimationVertices");
    skinningLocation_ = glGetUniformLocation(program, "uSkinningEnabled");
    skinningBonesLocation_ = glGetUniformLocation(program, "uSkinningBones");
    skinningBoneCountLocation_ = glGetUniformLocation(program, "uSkinningBoneCount");
    zombieWoundsLocation_ = glGetUniformLocation(program, "uZombieWounds");
    zombieTriangleOffsetLocation_ = glGetUniformLocation(program, "uZombieTriangleOffset");
    zombieCullBackfacesLocation_ = glGetUniformLocation(program, "uZombieCullBackfaces");
    shadowPassLocation_ = glGetUniformLocation(program, "uShadowPass");
    shadowsLocation_ = glGetUniformLocation(program, "uShadows");
    shadowMapLocation_ = glGetUniformLocation(program, "uShadowMap");
    sunMatrixLocation_ = glGetUniformLocation(program, "uSunMatrix");
    eyeLocation_ = glGetUniformLocation(program, "uEye");
    forestVisibilityLocation_ = glGetUniformLocation(program, "uForestVisibility");
    sunDirectionLocation_ = glGetUniformLocation(program, "uSunDirection");
    sunColorLocation_ = glGetUniformLocation(program, "uSunColor");
    skyAmbientLocation_ = glGetUniformLocation(program, "uSkyAmbient");
    groundAmbientLocation_ = glGetUniformLocation(program, "uGroundAmbient");
    shadowDistanceLocation_ = glGetUniformLocation(program, "uShadowDistance");
    shadowFadeLocation_ = glGetUniformLocation(program, "uShadowFadeStart");
    hazeColorLocation_ = glGetUniformLocation(program, "uHazeColor");
    hazeDensityLocation_ = glGetUniformLocation(program, "uHazeDensity");
    hazeStartLocation_ = glGetUniformLocation(program, "uHazeStart");
    groundFogLocation_ = glGetUniformLocation(program, "uGroundFog");
    nightBlendLocation_ = glGetUniformLocation(program, "uNightBlend");
    unlitLightLocation_ = glGetUniformLocation(program, "uUnlitLight");
    headlightsLocation_ = glGetUniformLocation(program, "uHeadlights");
    headlightDirectionLocation_ = glGetUniformLocation(program, "uHeadlightDirection");
    headlightPositions_[0] = glGetUniformLocation(program, "uHeadlightPosition[0]");
    headlightPositions_[1] = glGetUniformLocation(program, "uHeadlightPosition[1]");
    flashlightPositionLocation_ = glGetUniformLocation(program, "uFlashlightPosition");
    flashlightShadowsLocation_ = glGetUniformLocation(program, "uFlashlightShadows");
    flashlightShadowMapLocation_ = glGetUniformLocation(program, "uFlashlightShadowMap");
    flashlightMatrixLocation_ = glGetUniformLocation(program, "uFlashlightMatrix");
    flashlightShadowDistancesLocation_ = glGetUniformLocation(program, "uFlashlightShadowDistances");
    flashlightDirectionLocation_ = glGetUniformLocation(program, "uFlashlightDirection");
    flashlightColorLocation_ = glGetUniformLocation(program, "uFlashlightColor");
    flashlightConeLocation_ = glGetUniformLocation(program, "uFlashlightCone");
    muzzleFlashPositionLocation_ = glGetUniformLocation(program, "uMuzzleFlashPosition");
    muzzleFlashColorLocation_ = glGetUniformLocation(program, "uMuzzleFlashColor");
    muzzleFlashRangeLocation_ = glGetUniformLocation(program, "uMuzzleFlashRange");
    explosionCountLocation_ = glGetUniformLocation(program, "uExplosionCount");
    for (std::size_t i = 0; i < maxExplosionLights; ++i) {
        const auto index = "[" + std::to_string(i) + "]";
        explosionPositionLocations_[i] = glGetUniformLocation(program, ("uExplosionPositions" + index).c_str());
        explosionColorLocations_[i] = glGetUniformLocation(program, ("uExplosionColors" + index).c_str());
        explosionRangeLocations_[i] = glGetUniformLocation(program, ("uExplosionRanges" + index).c_str());
    }
}

} // namespace viewer
