#include "renderer.hpp"
#include "vehicle_layer.hpp"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
void Renderer::initializeVehicleInstances(ModelGPU& model, GLuint& buffer) {
    glGenBuffers(1,&buffer);
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    const glm::mat4 identity(1);
    glBufferData(GL_ARRAY_BUFFER,sizeof(identity),glm::value_ptr(identity),GL_STREAM_DRAW);
    for (const auto& mesh : model.meshes) {
        glBindVertexArray(mesh.vao);
        for (GLuint column = 0; column < 4; ++column) {
            const GLuint location = 4+column;
            glEnableVertexAttribArray(location);
            glVertexAttribPointer(location,4,GL_FLOAT,GL_FALSE,sizeof(glm::mat4),
                                  reinterpret_cast<const void*>(column*sizeof(glm::vec4)));
            glVertexAttribDivisor(location,1);
        }
    }
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER,0);
}
void Renderer::releaseVehicleAssets() noexcept {
    resetVehicleVisibility();
    for (auto& item : vehicleAssets_) for (auto& lod : item.second) {
        releaseModel(lod.model); glDeleteBuffers(1,&lod.instanceBuffer);
    }
    vehicleAssets_.clear();
}
void Renderer::setVehicleAssets(const std::filesystem::path& directory) {
    releaseVehicleAssets();
    vehicleAssetDirectory_ = directory;
}
void Renderer::loadVehicleRenderAsset(const VehicleConfig& config) {
    const auto& id = config.id;
    if (vehicleModel_.meshes.empty()) {
        uploadModel(VehicleLayer::geometry(),vehicleModel_);
        initializeVehicleInstances(vehicleModel_,vehicleInstanceBuffer_);
    }
    if (vehicleAssets_.count(id)) return;
    auto& levels = vehicleAssets_[id];
    if (vehicleAssetDirectory_.empty()) return;
    auto sources = loadVehicleLods(vehicleAssetDirectory_,id,&config);
    levels.reserve(sources.size());
    try {
        for (const auto& source : sources) {
            levels.emplace_back();
            auto& level = levels.back();
            uploadModel(source,level.model);
            initializeVehicleInstances(level.model,level.instanceBuffer);
            level.draws = source.draws;
            level.radius = std::max(2.6f,glm::length(glm::max(glm::abs(source.boundsMin),glm::abs(source.boundsMax))));
        }
    } catch (...) {
        for (auto& level : levels) { releaseModel(level.model); glDeleteBuffers(1,&level.instanceBuffer); }
        levels.clear(); throw;
    }
}
void Renderer::drawVehicles(const glm::mat4& projectionView, bool shadowPass) {
    if (!vehicleSource_ || vehicleModel_.meshes.empty()) return;
    const auto query = vehicleRangeLimited_ ? vehicleVisibility_.query(projectionView,vehicleRange_,visibleVehicles_) :
                                             vehicleVisibility_.query(projectionView,visibleVehicles_);
    if (!shadowPass) { vehicleStats_.testedNodes = query.testedNodes; vehicleStats_.testedInstances = query.testedInstances; }
    if (vehicleRangeLimited_ && vehicleDriver_ && *vehicleDriver_ < vehicleInstances_.size()) {
        const auto id = *vehicleDriver_;
        if (std::find(visibleVehicles_.begin(),visibleVehicles_.end(),id) == visibleVehicles_.end() &&
            Frustum(projectionView).intersects(vehicleBounds_[id].minimum,vehicleBounds_[id].maximum)) visibleVehicles_.push_back(id);
    }
    for (auto& asset : vehicleAssets_) for (auto& level : asset.second) level.transforms.clear();
    vehicleChassisTransforms_.clear(); vehicleWheelTransforms_.clear();
    for (auto id : visibleVehicles_) {
        const auto& instance = vehicleInstances_[id];
        const auto detail = vehicleRangeLimited_ ? instance.detail : VehicleDetail{};
        if (!detail.visible) continue;
        const bool prepared = !instance.lods->empty();
        const auto level = prepared ? detail.level : 0;
        if (!shadowPass) ++vehicleStats_.levels[level];
        if (prepared) (*instance.lods)[level].transforms.push_back(instance.chassis);
        else {
            vehicleChassisTransforms_.push_back(instance.chassis);
            vehicleWheelTransforms_.insert(vehicleWheelTransforms_.end(),instance.wheels.begin(),instance.wheels.end());
        }
    }
    glUniform1i(animatedLocation_,GL_FALSE); glUniform1i(treeCanopyLocation_,GL_FALSE);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    const auto single = [&](const ModelGPU& model, std::size_t primitive, const glm::mat4& transform, bool transparent) {
        const auto& mesh = model.meshes[primitive];
        glUniform1i(instancedLocation_,GL_FALSE);
        applyMaterial(model,mesh.material,glm::determinant(glm::mat3(transform)) < 0);
        const auto normal = glm::transpose(glm::inverse(glm::mat3(transform)));
        glUniformMatrix4fv(modelLocation_,1,GL_FALSE,glm::value_ptr(transform));
        glUniformMatrix3fv(normalLocation_,1,GL_FALSE,glm::value_ptr(normal));
        glBindVertexArray(mesh.vao); glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);
        if (!shadowPass) {
            ++vehicleStats_.drawCalls; vehicleStats_.triangles += static_cast<std::size_t>(mesh.count)/3;
            if (transparent) ++vehicleStats_.transparentDrawCalls;
        }
    };
    const auto upload = [&](GLuint buffer,const std::vector<glm::mat4>& transforms) {
        if (!vehicleInstancing_ || transforms.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER,buffer);
        const auto size = static_cast<GLsizeiptr>(transforms.size()*sizeof(glm::mat4));
        // Orphan so a new pass/frame does not wait for an earlier GPU draw.
        glBufferData(GL_ARRAY_BUFFER,size,nullptr,GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER,0,size,transforms.data());
    };
    const auto batch = [&](const ModelGPU& model,std::size_t primitive,const glm::mat4& local,
                           const std::vector<glm::mat4>& transforms) {
        if (transforms.empty()) return;
        if (!vehicleInstancing_) { for (const auto& transform : transforms) single(model,primitive,transform*local,false); return; }
        const auto& mesh = model.meshes[primitive];
        glUniform1i(instancedLocation_,GL_TRUE);
        applyMaterial(model,mesh.material,glm::determinant(glm::mat3(local)) < 0);
        glUniformMatrix4fv(modelLocation_,1,GL_FALSE,glm::value_ptr(local));
        glBindVertexArray(mesh.vao);
        glDrawElementsInstanced(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(transforms.size()));
        if (!shadowPass) {
            ++vehicleStats_.drawCalls; ++vehicleStats_.instancedDrawCalls;
            vehicleStats_.triangles += static_cast<std::size_t>(mesh.count)/3*transforms.size();
        }
    };
    struct TransparentDraw { const VehicleLodGPU* lod; const DrawInstance* draw; glm::mat4 transform; float distance; };
    std::vector<TransparentDraw> transparent;
    for (auto& asset : vehicleAssets_) for (auto& lod : asset.second) {
        if (lod.transforms.empty()) continue;
        upload(lod.instanceBuffer,lod.transforms);
        for (const auto& draw : lod.draws) {
            const auto& mesh = lod.model.meshes[draw.primitive];
            if (material(lod.model,mesh.material).alphaMode == "BLEND") {
                if (!shadowPass) for (const auto& transform : lod.transforms) {
                    const auto world = transform*draw.transform;
                    const auto center = glm::vec3(world*glm::vec4(mesh.center,1));
                    transparent.push_back({&lod,&draw,world,glm::dot(center-vehicleEye_,center-vehicleEye_)});
                }
            } else batch(lod.model,draw.primitive,draw.transform,lod.transforms);
        }
    }
    upload(vehicleInstanceBuffer_,vehicleChassisTransforms_);
    batch(vehicleModel_,0,glm::mat4(1),vehicleChassisTransforms_);
    upload(vehicleInstanceBuffer_,vehicleWheelTransforms_);
    batch(vehicleModel_,1,glm::mat4(1),vehicleWheelTransforms_);
    if (!transparent.empty()) {
        std::stable_sort(transparent.begin(),transparent.end(),[](const auto& a,const auto& b) {return a.distance > b.distance;});
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        for (const auto& draw : transparent) single(draw.lod->model,draw.draw->primitive,draw.transform,true);
    }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glUniform1i(instancedLocation_,GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER,0); glFrontFace(GL_CCW);
}
void Renderer::applyVehicleLights() {
    const bool active = vehicleSource_ && vehicleSource_->count() > 0;
    glUniform1i(headlightsLocation_, active);
    if (!active) return;
    const auto box = vehicleSource_->pose(vehicleDriver_.value_or(0)).chassis;
    const auto direction = box.rotation * glm::normalize(glm::vec3(0, -.055f, -1));
    glUniform3fv(headlightDirectionLocation_, 1, glm::value_ptr(direction));
    for (int i = 0; i < 2; ++i) {
        const auto p = box.center + box.rotation * vehicleSource_->config(vehicleDriver_.value_or(0)).headlights[static_cast<std::size_t>(i)];
        glUniform3fv(headlightPositions_[i], 1, glm::value_ptr(p));
    }
}
} // namespace viewer
