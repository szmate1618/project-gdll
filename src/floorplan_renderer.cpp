#include "renderer.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace viewer {
void Renderer::updateFloorplans(const FloorplanLayer& layer) {
    if (floorplanSource_ != &layer || floorplanRevision_ != layer.revision()) {
        releaseModel(floorplanModel_); floorplanModel_ = {};
        uploadModel(layer.renderModel(), floorplanModel_);
        floorplanDraws_ = layer.renderModel().draws;
        floorplanSource_ = &layer; floorplanRevision_ = layer.revision();
    }
    for (const auto& entry : layer.doorDraws())
        floorplanDraws_.at(entry.first).transform = layer.doors().transform(entry.second);
}
void Renderer::drawFloorplans() {
    if (floorplanDraws_.empty()) return;
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(treeCanopyLocation_, GL_FALSE);
    for (const auto& draw : floorplanDraws_) {
        const auto& mesh = floorplanModel_.meshes.at(draw.primitive);
        applyMaterial(floorplanModel_, mesh.material, false);
        glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(draw.transform));
        const auto normal = glm::inverseTranspose(glm::mat3(draw.transform));
        glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(normal));
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
    }
    glFrontFace(GL_CCW);
}
} // namespace viewer
