#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {

void Renderer::drawSunShadows(const SunShadowView& shadowView, double seconds) {
    // Unbind before attaching for drawing: the depth target is only sampled
    // during the subsequent color pass.
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    shadowMap_.begin();
    drawShadowCasters(shadowView.view, shadowView.projection, seconds, ZombiePass::SunShadow, {});
}

void Renderer::drawFlashlightShadows(const FlashlightView& view, double seconds) {
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    if (!flashlightShadowMap_)
        flashlightShadowMap_ = std::make_unique<ShadowMap>(NightLighting::flashlightShadowResolution);
    flashlightShadowMap_->begin();
    drawShadowCasters(view.view, view.projection, seconds, ZombiePass::FlashlightShadow, view.position);
}

void Renderer::drawShadowCasters(const glm::mat4& view, const glm::mat4& projection, double seconds,
                                ZombiePass pass, glm::vec3 detailEye) {
    const bool detailedBuildings = pass == ZombiePass::FlashlightShadow;
    glDisable(GL_BLEND);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glUniform1i(shadowPassLocation_, GL_TRUE);
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniformMatrix4fv(viewLocation_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(projection));
    // Query the light volume independently of camera visibility. The existing
    // BVH conservatively includes full transformed geometry bounds.
    const auto projectionView = projection * view;
    sceneVisibility_.query(projectionView, shadowScene_, forestVisibilityRange());
    if (detailedBuildings) sceneVisibility_.filterDetails(detailEye, projection, shadowScene_);
    std::fill(roofBatchDrawn_.begin(), roofBatchDrawn_.end(), false);
    for (const auto index : shadowScene_) {
        const auto& caster = sceneDraws_[index];
        const auto& mesh = model_.meshes[caster.primitive];
        // Sun uses the original shell; nearby flashlight shadows retain doorway details.
        if ((detailedBuildings || !caster.buildingDetail) && material(model_, mesh.material).alphaMode != "BLEND") drawTownSource(index);
    }
    if (treeShadows_) {
        drawTrees(projectionView, true);
        drawForests(projectionView, true);
    }
    drawZombies(seconds, pass);
    drawBoxes(projectionView);
    drawGrenades(projectionView);
    glUniform1i(shadowPassLocation_, GL_FALSE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0, 0);
    glFrontFace(GL_CCW);
    glBindVertexArray(0);
}

DayNightLighting Renderer::lightingForFrame() const {
    if (dayNight_) return *dayNight_;
    DayNightLighting result;
    result.sun = lighting_;
    result.fogColor = atmosphere_.color;
    if (night_) {
        result.sun.color = glm::vec3(0);
        result.sun.skyAmbient = nightLighting_.skyAmbient;
        result.sun.groundAmbient = nightLighting_.groundAmbient;
        result.fogColor = nightLighting_.fogColor;
        result.unlitColor = nightLighting_.skyAmbient;
        result.nightBlend = 1.0f;
    }
    return result;
}

void Renderer::applyLighting(const Camera& camera, const SunShadowView& shadowView,
                             const DayNightLighting& lighting, const FlashlightView& flashlight, bool flashlightCasting) {
    glUniform1i(shadowPassLocation_, GL_FALSE);
    const bool sunlight = glm::any(glm::greaterThan(lighting.sun.color, glm::vec3(0)));
    glUniform1i(shadowsLocation_, shadows_ && sunlight && shadowPassLocation_ >= 0);
    const auto matrix = shadowView.projectionView();
    const auto eye = camera.position();
    const auto sun = glm::normalize(lighting.sun.direction);
    glUniformMatrix4fv(sunMatrixLocation_, 1, GL_FALSE, glm::value_ptr(matrix));
    glUniform3fv(eyeLocation_, 1, glm::value_ptr(eye));
    glUniform3fv(sunDirectionLocation_, 1, glm::value_ptr(sun));
    glUniform3fv(sunColorLocation_, 1, glm::value_ptr(lighting.sun.color));
    glUniform3fv(skyAmbientLocation_, 1, glm::value_ptr(lighting.sun.skyAmbient));
    glUniform3fv(groundAmbientLocation_, 1, glm::value_ptr(lighting.sun.groundAmbient));
    glUniform1f(nightBlendLocation_, lighting.nightBlend);
    glUniform3fv(unlitLightLocation_, 1, glm::value_ptr(lighting.unlitColor));
    glUniform1i(flashlightShadowsLocation_, flashlightCasting);
    glUniform1i(flashlightShadowMapLocation_, 4);
    const auto flashlightMatrix = flashlight.projectionView();
    glUniformMatrix4fv(flashlightMatrixLocation_, 1, GL_FALSE, glm::value_ptr(flashlightMatrix));
    glUniform3fv(flashlightPositionLocation_, 1, glm::value_ptr(flashlight.position));
    glUniform3fv(muzzleFlashPositionLocation_, 1, glm::value_ptr(muzzleFlash_.position));
    glUniform3fv(muzzleFlashColorLocation_, 1, glm::value_ptr(muzzleFlash_.color));
    glUniform1f(muzzleFlashRangeLocation_, muzzleFlash_.range);
    glUniform1i(explosionCountLocation_, static_cast<GLint>(explosionLightCount_));
    for (std::size_t i = 0; i < explosionLightCount_; ++i) {
        const auto& light = explosionLights_[i];
        glUniform3fv(explosionPositionLocations_[i], 1, glm::value_ptr(light.position));
        glUniform3fv(explosionColorLocations_[i], 1, glm::value_ptr(light.color));
        glUniform1f(explosionRangeLocations_[i], light.range);
    }
    glUniform3f(flashlightShadowDistancesLocation_, NightLighting::flashlightShadowNear,
                NightLighting::flashlightShadowFadeStart, NightLighting::flashlightShadowDistance);
    const auto direction = flashlight.direction;
    glUniform3fv(flashlightDirectionLocation_, 1, glm::value_ptr(direction));
    const auto flashlightColor = flashlightEnabled_ ? nightLighting_.flashlightColor : glm::vec3(0);
    glUniform3fv(flashlightColorLocation_, 1, glm::value_ptr(flashlightColor));
    glUniform3f(flashlightConeLocation_, nightLighting_.flashlightRange,
                std::cos(glm::radians(nightLighting_.innerAngle)),
                std::cos(glm::radians(nightLighting_.outerAngle)));
    glUniform1f(shadowDistanceLocation_, SunLighting::shadowDistance);
    glUniform1f(shadowFadeLocation_, SunLighting::shadowFadeStart);
    glUniform3fv(hazeColorLocation_, 1, glm::value_ptr(lighting.fogColor));
    glUniform1f(hazeDensityLocation_, haze_ ? atmosphere_.density : 0.0f);
    glUniform1f(hazeStartLocation_, atmosphere_.startDistance);
    const auto& fog = atmosphere_.groundFog;
    glUniform4f(groundFogLocation_, fog.baseHeight, 1.0f / glm::max(fog.height, 0.001f),
                groundFog_ ? fog.density : 0.0f, fog.startDistance);
    shadowMap_.bind(GL_TEXTURE3);
    // Keep all active shadow samplers bound to complete comparison textures,
    // even before the lazy flashlight target has been needed for the first time.
    if (flashlightShadowMap_) flashlightShadowMap_->bind(GL_TEXTURE4);
    else shadowMap_.bind(GL_TEXTURE4);
    glActiveTexture(GL_TEXTURE0);
}

} // namespace viewer
