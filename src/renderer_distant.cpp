#include "renderer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
VisibilityBounds emptyBounds() {
    return {glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
}
void include(VisibilityBounds& target, const VisibilityBounds& bounds) {
    target.minimum = glm::min(target.minimum, bounds.minimum);
    target.maximum = glm::max(target.maximum, bounds.maximum);
}
}

void Renderer::setDistantSceneCache(bool enabled) {
    distantEnabled_ = enabled;
    if (distantCache_) distantCache_->invalidate();
}

void Renderer::prepareDistantScene(const Model& source) {
    roofCacheBounds_.assign(roofModel_.meshes.size(), emptyBounds());
    wallCacheBounds_.assign(wallModel_.meshes.size(), emptyBounds());
    staticChunks_.resize(source.draws.size());
    staticCacheable_.resize(source.draws.size());
    for (std::size_t i = 0; i < source.draws.size(); ++i) {
        const auto& draw = source.draws[i];
        const auto& bounds = sceneVisibility_.drawBounds(i);
        if (roofBatchForDraw_[i] != noBuildingBatch) include(roofCacheBounds_[roofBatchForDraw_[i]], bounds);
        if (wallBatchForDraw_[i] != noBuildingBatch) include(wallCacheBounds_[wallBatchForDraw_[i]], bounds);
        const auto& mesh = model_.meshes[draw.primitive];
        staticCacheable_[i] = draw.horizontalMotionRadius == 0 && material(model_, mesh.material).alphaMode != "BLEND";
        const auto extent = bounds.maximum - bounds.minimum;
        if (!staticCacheable_[i] || std::max(extent.x, extent.z) <= 256 ||
            roofBatchForDraw_[i] != noBuildingBatch || wallBatchForDraw_[i] != noBuildingBatch) continue;
        // Map-wide terrain/roads/fences cannot be assigned wholly near or far.
        // Partition their index lists once, sharing the original vertex buffer.
        // Triangles stay intact; conservative cell bounds decide cache residency.
        struct Cell { VisibilityBounds bounds = emptyBounds(); std::vector<std::uint32_t> indices; };
        std::map<std::pair<int,int>, Cell> cells;
        const auto& primitive = source.primitives[draw.primitive];
        for (std::size_t triangle = 0; triangle + 2 < primitive.indices.size(); triangle += 3) {
            glm::vec3 points[3];
            for (int v = 0; v < 3; ++v)
                points[v] = glm::vec3(draw.transform * glm::vec4(primitive.vertices[primitive.indices[triangle + v]].position, 1));
            const auto center = (points[0] + points[1] + points[2]) / 3.0f;
            auto& cell = cells[{static_cast<int>(std::floor(center.x / 128)), static_cast<int>(std::floor(center.z / 128))}];
            for (int v = 0; v < 3; ++v) {
                include(cell.bounds, {points[v], points[v]});
                cell.indices.push_back(primitive.indices[triangle + v]);
            }
        }
        auto& chunks = staticChunks_[i];
        std::vector<std::uint32_t> indices;
        std::vector<VisibilityBounds> chunkBounds;
        for (const auto& entry : cells) {
            const auto& cell = entry.second;
            chunks.chunks.push_back({cell.bounds, static_cast<GLsizei>(cell.indices.size()), indices.size() * sizeof(std::uint32_t)});
            chunkBounds.push_back(cell.bounds);
            indices.insert(indices.end(), cell.indices.begin(), cell.indices.end());
        }
        chunks.visibility = VisibilityIndex(chunkBounds);
        // GL_ARRAY_BUFFER avoids changing the source VAO's element binding.
        glGenBuffers(1, &chunks.buffer);
        glBindBuffer(GL_ARRAY_BUFFER, chunks.buffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer::releaseDistantScene() noexcept {
    distantCache_.reset();
    for (auto& chunks : staticChunks_) glDeleteBuffers(1, &chunks.buffer);
}

bool Renderer::keepStaticBounds(const VisibilityBounds& bounds) const {
    if (staticPass_ == StaticPass::All) return true;
    const bool far = DistantSceneCache::distant(bounds, distantCache_->origin(), 1000);
    return far == (staticPass_ == StaticPass::Far);
}

bool Renderer::hasDistantStatic(const Camera& camera, const glm::mat4& projection) {
    const auto eye = camera.position();
    const auto projectionView = projection * camera.view();
    // The ordinary camera list is already prepared. Probe the actual index
    // subsets, not their map-wide parent bounds, without another town query.
    for (const auto index : opaqueScene_) {
        if (!staticCacheable_[index]) continue;
        auto& chunks = staticChunks_[index];
        if (!chunks.buffer) {
            if (DistantSceneCache::distant(sceneVisibility_.drawBounds(index), eye, 1000)) return true;
            continue;
        }
        chunks.visibility.query(projectionView, chunks.visible, forestVisibilityRange(), true);
        for (const auto id : chunks.visible)
            if (DistantSceneCache::distant(chunks.chunks[id].bounds, eye, 1000)) return true;
    }
    // Only query foliage if town geometry supplied no cache candidates. Retain
    // spatial acceleration instead of scanning the full tree population.
    if (!forestPatches_.empty()) {
        forestVisibility_.query(projectionView, visibleForests_, forestVisibilityRange());
        for (const auto index : visibleForests_)
            if (!forestDetail_[index] && DistantSceneCache::distant(forestPatches_[index].bounds, eye, 1000)) return true;
    }
    if (!treeInstances_.empty()) {
        const auto& visibility = treeBillboardsForFrame_ ? treeVisibility_ : treeAuthoredVisibility_;
        visibility.query(projectionView, forestDetail_, true, visibleTrees_, forestVisibilityRange());
        for (const auto index : visibleTrees_)
            if (DistantSceneCache::distant(treeCacheBounds_[index], eye, 1000)) return true;
    }
    return false;
}

bool Renderer::keepStaticDraw(std::size_t index) const {
    if (staticPass_ == StaticPass::All) return true;
    if (!staticCacheable_[index]) return staticPass_ == StaticPass::Near;
    // Keep complete batches together. Otherwise a cell crossing 1 km could
    // accidentally bake nearby members into the cache or leave seam holes.
    const auto wall = wallBatchForDraw_[index], roof = roofBatchForDraw_[index];
    if (wallBatching_ && wall != noBuildingBatch &&
        !buildingDetailsNear(wallBatchDetails_[wall].bounds, wallBatchDetails_[wall].distance,
                             wallBatchEye_, wallBatchProjection_))
        return keepStaticBounds(wallCacheBounds_[wall]);
    if (roofBatching_ && roof != noBuildingBatch) return keepStaticBounds(roofCacheBounds_[roof]);
    return keepStaticBounds(sceneVisibility_.drawBounds(index));
}

bool Renderer::drawStaticChunks(std::size_t index) {
    auto& chunks = staticChunks_[index];
    chunks.visibility.query(staticProjectionView_, chunks.visible, forestVisibilityRange(), true);
    const auto& source = sceneDraws_[index];
    const auto& mesh = model_.meshes[source.primitive];
    applyMaterial(model_, mesh.material, source.mirrored);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(source.transform));
    glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(source.normalMatrix));
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunks.buffer);
    for (const auto id : chunks.visible) {
        const auto& chunk = chunks.chunks[id];
        if (!keepStaticBounds(chunk.bounds)) continue;
        glDrawElements(GL_TRIANGLES, chunk.count, GL_UNSIGNED_INT, reinterpret_cast<const void*>(chunk.offset));
        ++sceneStats_.drawCalls;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    return false; // Counts already include each submitted chunk.
}

void Renderer::drawStaticScene(const glm::mat4& view, const glm::mat4& projection) {
    forestStats_.volumes = forestStats_.drawCalls = 0;
    staticProjectionView_ = projection * view;
    glUniformMatrix4fv(viewLocation_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(animatedLocation_, GL_FALSE);
    const auto eye = staticPass_ == StaticPass::Far ? distantCache_->origin() : treeViewEye_;
    gatherVisibleSceneDraws(staticProjectionView_, eye, staticDetailProjection_);
    beginTownPass(eye, staticDetailProjection_);
    for (const auto index : opaqueScene_)
        if (drawTownSource(index)) ++sceneStats_.drawCalls;
    auto& temporal = staticPass_ == StaticPass::Far ? distantVisibility_ : cameraVisibility_;
    drawTrees(staticProjectionView_, false, temporal);
    drawForests(staticProjectionView_, false, temporal.forests);
}
} // namespace viewer
