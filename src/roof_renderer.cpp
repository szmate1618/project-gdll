#include "renderer.hpp"
#include "roof_batch.hpp"

#include <utility>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {

void Renderer::uploadRoofBatches(const Model& source) {
    auto batches = buildRoofBatches(source);
    uploadMeshes(batches.meshes, roofModel_);
    roofBatchForDraw_ = std::move(batches.batchForDraw);
    roofBatchDrawn_.assign(batches.meshes.size(), false);
}

bool Renderer::drawTownSource(std::size_t index) {
    const auto batch = roofBatchForDraw_[index];
    if (!roofBatching_ || batch == noRoofBatch) {
        draw(sceneDraws_[index]);
        return true;
    }
    if (roofBatchDrawn_[batch]) return false;
    roofBatchDrawn_[batch] = true;
    // Source BVH queries select occupied cells separately for camera and light.
    // Draw the full cell when any member is visible; clip/depth tests handle the
    // conservative extra roofs. No per-frame geometry or index uploads needed.
    const auto& mesh = roofModel_.meshes[batch];
    applyMaterial(model_, mesh.material, false);
    const glm::mat4 identity(1);
    const glm::mat3 normalIdentity(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(normalIdentity));
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
    return true;
}

} // namespace viewer
