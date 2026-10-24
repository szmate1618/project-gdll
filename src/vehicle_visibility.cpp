#include "renderer.hpp"
#include "vehicle_layer.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace viewer {
void Renderer::resetVehicleVisibility() {
    vehicleInstances_.clear(); vehicleBounds_.clear();
    vehicleVisibility_ = {};
    vehicleRangeCandidates_.clear(); visibleVehicles_.clear();
}
void Renderer::updateVehicles(const VehicleLayer& vehicles, std::optional<std::size_t> driver) {
    if (vehicleSource_ != &vehicles || vehicleSourceIdentity_ != vehicles.instanceId()) resetVehicleVisibility();
    vehicleSource_ = &vehicles;
    vehicleSourceIdentity_ = vehicles.instanceId();
    vehicleDriver_ = driver;
}
void Renderer::syncVehicleInstances() {
    const auto count = vehicleSource_->count(), oldCount = vehicleInstances_.size();
    const bool rebuild = count != oldCount;
    vehicleInstances_.resize(count); vehicleBounds_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& instance = vehicleInstances_[i];
        if (i >= oldCount) {
            const auto& id = vehicleSource_->config(i).id;
            loadVehicleRenderAsset(id);
            instance.lods = &vehicleAssets_.at(id);
            for (const auto& lod : *instance.lods) instance.radius = std::max(instance.radius,lod.radius);
        }
        const auto revision = vehicleSource_->poseRevision(i);
        if (instance.revision == revision) continue;
        const auto pose = vehicleSource_->pose(i);
        instance.center = pose.chassis.center;
        instance.chassis = glm::translate(glm::mat4(1),instance.center) * glm::mat4_cast(pose.chassis.rotation);
        instance.wheels = pose.wheels;
        VisibilityBounds bounds{instance.center-glm::vec3(instance.radius),instance.center+glm::vec3(instance.radius)};
        if (instance.lods->empty()) {
            // Prototype geometry includes independently suspended wheels. Their
            // configured anchors can extend beyond the normal chassis sphere.
            instance.chassis = pose.chassis.transform();
            for (const auto& wheel : pose.wheels) {
                bounds.minimum = glm::min(bounds.minimum,glm::vec3(wheel[3])-glm::vec3(.5f));
                bounds.maximum = glm::max(bounds.maximum,glm::vec3(wheel[3])+glm::vec3(.5f));
            }
        }
        vehicleBounds_[i] = bounds;
        if (rebuild || vehicleVisibility_.update(i,bounds)) ++vehicleStats_.boundsUpdated;
        instance.revision = revision;
    }
    if (rebuild) vehicleVisibility_ = VisibilityIndex(vehicleBounds_);
}
void Renderer::prepareVehicleDraws(const Camera& camera, const glm::mat4& projection) {
    vehicleEye_ = camera.position();
    vehicleProjectionScale_ = projection[1][1];
    vehicleStats_ = {};
    if (!vehicleSource_) return;
    syncVehicleInstances();
    vehicleRangeLimited_ = vehicleDistanceDetail_ && std::isfinite(vehicleProjectionScale_) && vehicleProjectionScale_ > 0;
    if (vehicleRangeLimited_) {
        const float radius = 300 * vehicleProjectionScale_ / std::sqrt(3.0f);
        vehicleRange_ = {vehicleEye_-glm::vec3(radius),vehicleEye_+glm::vec3(radius)};
        vehicleVisibility_.query(vehicleRange_,vehicleRangeCandidates_);
        vehicleStats_.rangeCandidates = vehicleRangeCandidates_.size();
        vehicleStats_.distanceCulled = vehicleInstances_.size()-vehicleRangeCandidates_.size();
        for (auto i : vehicleRangeCandidates_) {
            auto& instance = vehicleInstances_[i];
            instance.detail = vehicleCameraDetail(glm::distance(instance.center,vehicleEye_),vehicleProjectionScale_,instance.detail.level);
            if (!instance.detail.visible) ++vehicleStats_.distanceCulled;
        }
        if (vehicleDriver_ && *vehicleDriver_ < vehicleInstances_.size()) {
            auto& occupied = vehicleInstances_[*vehicleDriver_];
            // Occupancy overrides the distance policy even for a diagnostic
            // camera placed outside the normal range. Frustum culling remains.
            if (std::find(vehicleRangeCandidates_.begin(),vehicleRangeCandidates_.end(),*vehicleDriver_) == vehicleRangeCandidates_.end() ||
                !occupied.detail.visible) --vehicleStats_.distanceCulled;
            occupied.detail = {};
        }
    } else vehicleStats_.rangeCandidates = vehicleInstances_.size();
}
} // namespace viewer
