#include "renderer.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "house_polygon.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>

namespace viewer {
void Renderer::applyInteriorSunOcclusion(const Camera& camera) {
    glm::vec4 u(0), v(0);
    glm::vec2 heights(0);
    const std::vector<glm::vec2>* boundary = nullptr;
    if (floorplanSource_) {
        const auto eye = camera.position();
        for (const auto& entry : floorplanSource_->resident()) {
            const auto& plan = entry.second;
            if (eye.y < plan.floor || eye.y > plan.ceiling) continue;
            const auto& p = plan.envelope;
            const auto axisU = glm::normalize(p[3] - p[0]);
            const auto axisV = glm::normalize(p[1] - p[0]);
            const glm::vec2 eyeXZ(eye.x, eye.z);
            const float x = glm::dot(axisU, eyeXZ), z = glm::dot(axisV, eyeXZ);
            const glm::vec4 boundsU(axisU, glm::dot(axisU, p[0]), glm::dot(axisU, p[3]));
            const glm::vec4 boundsV(axisV, glm::dot(axisV, p[0]), glm::dot(axisV, p[1]));
            if (x < boundsU.z || x > boundsU.w || z < boundsV.z || z > boundsV.w) continue;
            if (!house_polygon::contains(plan.footprint, eyeXZ)) continue;
            // Sun shadows already use closed, low-detail building shells.
            // Exact interior containment prevents contact leaks on every wall
            // and floor edge, including where rays miss the top ceiling.
            u = boundsU; v = boundsV; heights = {plan.floor, plan.ceiling};
            if (plan.footprint.size() != 4 || !std::equal(plan.footprint.begin(), plan.footprint.end(), plan.envelope.begin())) boundary = &plan.footprint;
            break;
        }
    }
    glUniform4fv(interiorShellULocation_, 1, glm::value_ptr(u));
    glUniform4fv(interiorShellVLocation_, 1, glm::value_ptr(v));
    glUniform2fv(interiorShellHeightsLocation_, 1, glm::value_ptr(heights));
    glUniform1i(interiorShellVertexCountLocation_, boundary ? static_cast<GLint>(boundary->size()) : 0);
    if (boundary) glUniform2fv(interiorShellVerticesLocation_, static_cast<GLsizei>(boundary->size()), glm::value_ptr(boundary->front()));
}
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
