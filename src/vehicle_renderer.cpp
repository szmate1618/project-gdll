#include "renderer.hpp"
#include "vehicle_layer.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
void Renderer::updateVehicles(const VehicleLayer& vehicles, std::optional<std::size_t> driver) {
    vehicleSource_ = &vehicles;
    vehicleDriver_ = driver;
    if (vehicles.count() && vehicleModel_.meshes.empty()) uploadModel(VehicleLayer::geometry(), vehicleModel_);
}
void Renderer::drawVehicles(const glm::mat4& projectionView) {
    if (!vehicleSource_ || vehicleModel_.meshes.empty()) return;
    const Frustum frustum(projectionView);
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(treeCanopyLocation_, GL_FALSE);
    for (std::size_t i = 0; i < vehicleSource_->count(); ++i) {
        const auto pose = vehicleSource_->pose(i);
        // Conservative sphere includes the chassis, full suspension and wheels.
        if (!frustum.intersects(pose.chassis.center - glm::vec3(2.6f), pose.chassis.center + glm::vec3(2.6f))) continue;
        const auto part = [&](std::size_t meshId, const glm::mat4& transform) {
            const auto& mesh = vehicleModel_.meshes[meshId];
            applyMaterial(vehicleModel_, mesh.material, false);
            const auto normal = glm::transpose(glm::inverse(glm::mat3(transform)));
            glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(transform));
            glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(normal));
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
        };
        part(0, pose.chassis.transform());
        for (const auto& transform : pose.wheels) part(1, transform);
    }
    glFrontFace(GL_CCW);
}
void Renderer::applyVehicleLights() {
    // The prototype's single car keeps its lights on when parked at night.
    // Beams follow the rigid pose, independently of the driver's mouse look.
    const bool active = vehicleSource_ && vehicleSource_->count() > 0;
    glUniform1i(headlightsLocation_, active);
    if (!active) return;
    const auto box = vehicleSource_->pose(vehicleDriver_.value_or(0)).chassis;
    const auto direction = box.rotation * glm::normalize(glm::vec3(0, -.055f, -1));
    glUniform3fv(headlightDirectionLocation_, 1, glm::value_ptr(direction));
    for (int i = 0; i < 2; ++i) {
        const auto p = box.center + box.rotation * glm::vec3(i ? .64f : -.64f, .08f, -1.83f);
        glUniform3fv(headlightPositions_[i], 1, glm::value_ptr(p));
    }
}
} // namespace viewer
