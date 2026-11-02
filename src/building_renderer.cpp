#include "renderer.hpp"
#include "building_batch.hpp"

#include <utility>
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {

void Renderer::uploadBuildingBatches(const Model& source) {
    auto roofs = buildRoofBatches(source);
    auto walls = buildWallBatches(source, sceneVisibility_);
    buildingVisibility_ = BuildingVisibility(source, sceneVisibility_, roofs, walls);
    uploadMeshes(roofs.meshes, roofModel_);
    roofBatchForDraw_ = std::move(roofs.batchForDraw);
    roofBatchDrawn_.assign(roofs.meshes.size(), false);
    uploadMeshes(walls.meshes, wallModel_);
    wallBatchForDraw_ = std::move(walls.batchForDraw);
    wallBatchDetails_ = std::move(walls.details);
    wallBatchStates_.resize(walls.meshes.size());
}

void Renderer::beginTownPass(glm::vec3 eye, const glm::mat4& projection, bool sunShadow) {
    std::fill(roofBatchDrawn_.begin(), roofBatchDrawn_.end(), false);
    std::fill(wallBatchStates_.begin(), wallBatchStates_.end(), WallBatchState::Unchecked);
    wallBatchEye_ = eye;
    wallBatchProjection_ = projection;
    wallBatchSunShadow_ = sunShadow;
}

bool Renderer::drawTownSource(std::size_t index) {
    if (staticPass_ != StaticPass::All) {
        if (staticChunks_[index].buffer) return drawStaticChunks(index);
        if (!keepStaticDraw(index)) return false;
    }
    const auto wall = wallBatchForDraw_[index];
    if (wallBatching_ && wall != noBuildingBatch) {
        auto& state = wallBatchStates_[wall];
        if (state == WallBatchState::Unchecked) {
            const auto& detail = wallBatchDetails_[wall];
            // Sun always casts the simple shell. Camera and flashlight cells
            // must be wholly far, including offscreen members, to batch safely.
            state = wallBatchSunShadow_ || !buildingDetailsNear(detail.bounds, detail.distance,
                                                               wallBatchEye_, wallBatchProjection_)
                ? WallBatchState::Ready : WallBatchState::Individual;
        }
        if (state == WallBatchState::Drawn) return false;
        if (state == WallBatchState::Ready) {
            state = WallBatchState::Drawn;
            drawBuildingBatch(wallModel_, wall);
            return true;
        }
    }
    const auto batch = roofBatchForDraw_[index];
    if (!roofBatching_ || batch == noBuildingBatch) {
        draw(sceneDraws_[index]);
        return true;
    }
    if (roofBatchDrawn_[batch]) return false;
    roofBatchDrawn_[batch] = true;
    // Camera and light queries select occupied cells independently.
    // Draw the full cell when any member is visible; clip/depth tests handle the
    // conservative extra roofs. No per-frame geometry or index uploads needed.
    drawBuildingBatch(roofModel_, batch);
    return true;
}

void Renderer::drawBuildingBatch(const ModelGPU& model, std::size_t batch) {
    const auto& mesh = model.meshes[batch];
    applyMaterial(model_, mesh.material, false);
    const glm::mat4 identity(1);
    const glm::mat3 normalIdentity(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(normalIdentity));
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
}

} // namespace viewer
