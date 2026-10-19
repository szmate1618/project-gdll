#include "renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <stdexcept>

namespace viewer {
void Renderer::loadGrenadeModel(const std::filesystem::path& path) {
    if (!grenadeModel_.meshes.empty()) throw std::logic_error("Grenade model already loaded");
    const auto model = loadModel(path);
    uploadModel(model, grenadeModel_);
    grenadeDraws_ = model.draws;
}
void Renderer::setExplosionLights(const std::vector<MuzzleFlashLight>& lights) {
    explosionLightCount_ = std::min(lights.size(), maxExplosionLights);
    std::copy_n(lights.begin(), explosionLightCount_, explosionLights_.begin());
}
void Renderer::drawGrenades(const glm::mat4& projectionView) {
    if (grenadePoses_.empty() || grenadeModel_.meshes.empty()) return;
    const Frustum frustum(projectionView);
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(treeCanopyLocation_, GL_FALSE);
    for (const auto& grenade : grenadePoses_) {
        if (!frustum.intersects(grenade.center - glm::vec3(.09f), grenade.center + glm::vec3(.09f))) continue;
        const auto body = glm::translate(glm::mat4(1), grenade.center) * glm::mat4_cast(grenade.rotation);
        for (const auto& part : grenadeDraws_) {
            const auto& mesh = grenadeModel_.meshes.at(part.primitive);
            const auto transform = body * part.transform;
            const auto normal = glm::inverseTranspose(glm::mat3(transform));
            applyMaterial(grenadeModel_, mesh.material, glm::determinant(glm::mat3(transform)) < 0);
            glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(transform));
            glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(normal));
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
        }
    }
    glFrontFace(GL_CCW);
}
} // namespace viewer
