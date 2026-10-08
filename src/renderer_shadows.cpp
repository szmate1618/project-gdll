#include "renderer.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace viewer {

void Renderer::drawSunShadows(const SunShadowView& shadowView, double seconds) {
    // Unbind before attaching for drawing: the depth target is only sampled
    // during the subsequent color pass.
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    shadowMap_.begin();
    glDisable(GL_BLEND);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glUniform1i(shadowPassLocation_, GL_TRUE);
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniformMatrix4fv(viewLocation_, 1, GL_FALSE, glm::value_ptr(shadowView.view));
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(shadowView.projection));
    // Query the light volume independently of camera visibility. The existing
    // BVH conservatively includes full transformed geometry bounds.
    const auto projectionView = shadowView.projectionView();
    sceneVisibility_.query(projectionView, shadowScene_);
    for (const auto index : shadowScene_) {
        const auto& caster = sceneDraws_[index];
        const auto& mesh = model_.meshes[caster.primitive];
        if (material(model_, mesh.material).alphaMode != "BLEND") draw(caster);
    }
    const Frustum lightFrustum(projectionView);
    drawZombies(seconds, &lightFrustum);
    glUniform1i(shadowPassLocation_, GL_FALSE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0, 0);
    glFrontFace(GL_CCW);
    glBindVertexArray(0);
}

void Renderer::applyLighting(const Camera& camera, const SunShadowView& shadowView) {
    glUniform1i(shadowPassLocation_, GL_FALSE);
    glUniform1i(shadowsLocation_, shadows_ && shadowPassLocation_ >= 0);
    const auto matrix = shadowView.projectionView();
    const auto eye = camera.position();
    const auto sun = glm::normalize(lighting_.direction);
    glUniformMatrix4fv(sunMatrixLocation_, 1, GL_FALSE, glm::value_ptr(matrix));
    glUniform3fv(eyeLocation_, 1, glm::value_ptr(eye));
    glUniform3fv(sunDirectionLocation_, 1, glm::value_ptr(sun));
    glUniform3fv(sunColorLocation_, 1, glm::value_ptr(lighting_.color));
    glUniform3fv(skyAmbientLocation_, 1, glm::value_ptr(lighting_.skyAmbient));
    glUniform3fv(groundAmbientLocation_, 1, glm::value_ptr(lighting_.groundAmbient));
    glUniform1f(shadowDistanceLocation_, SunLighting::shadowDistance);
    glUniform1f(shadowFadeLocation_, SunLighting::shadowFadeStart);
    glUniform3fv(hazeColorLocation_, 1, glm::value_ptr(atmosphere_.color));
    glUniform1f(hazeDensityLocation_, haze_ ? atmosphere_.density : 0.0f);
    glUniform1f(hazeStartLocation_, atmosphere_.startDistance);
    const auto& fog = atmosphere_.groundFog;
    glUniform4f(groundFogLocation_, fog.baseHeight, 1.0f / glm::max(fog.height, 0.001f),
                groundFog_ ? fog.density : 0.0f, fog.startDistance);
    shadowMap_.bind(GL_TEXTURE3);
    glActiveTexture(GL_TEXTURE0);
}

} // namespace viewer
