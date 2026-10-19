#include "renderer.hpp"
#include "tree_color.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstddef>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
std::vector<VisibilityBounds> Renderer::uploadTrees(const TreeLayer& trees, const ForestLayer* forests) {
    vegetationBlastMotion_ = VegetationBlastMotion(trees.instances);
    std::optional<glm::vec3> canopyColor;
    if (forests && forests->volumes.materials.size() > ForestLayer::canopyMaterialIndex)
        canopyColor = visibleTextureColor(forests->volumes,
            forests->volumes.materials[ForestLayer::canopyMaterialIndex]);
    if (!trees.instances.empty() && instancedLocation_ < 0)
        throw std::runtime_error("Tree rendering requires basic.vert with uInstanced and matrix attributes 4-7");
    if (trees.instances.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
        throw std::runtime_error("Too many tree instances for an OpenGL instanced draw");
    treeInstances_.reserve(trees.instances.size());
    visibleTrees_.reserve(trees.instances.size());
    shadowTrees_.reserve(trees.instances.size());
    std::vector<std::size_t> counts(trees.assets.size(), 0);
    const auto canopy = estimateTreeCanopyShade(trees.instances);
    for (std::size_t i = 0; i < trees.instances.size(); ++i) {
        const auto& instance = trees.instances[i];
        if (instance.asset >= trees.assets.size()) throw std::runtime_error("Tree instance references an unknown asset");
        treeInstances_.push_back({instance.asset, {instance.transform, canopy[i]}, vegetationBlastBounds(instance)});
        ++counts[instance.asset];
    }
    // Allocate ownership slots before making any GL objects, so constructor
    // cleanup covers partially uploaded models and instance buffers on failure.
    treeBatches_.resize(trees.assets.size());
    for (std::size_t index = 0; index < trees.assets.size(); ++index) {
        const auto& source = trees.assets[index].model;
        auto& batch = treeBatches_[index];
        if (treeBillboardModeLocation_ >= 0 && treeBillboardPivotLocation_ >= 0)
            batch.billboard = treeBillboardLayout(source);
        uploadModel(source, batch.model);
        if (canopyColor && trees.assets[index].matchCanopyColor)
            batch.model.materials = matchTreeTextureColors(source, *canopyColor);
        batch.visibleInstances.reserve(counts[index]);
        glGenBuffers(1, &batch.instanceBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, batch.instanceBuffer);
        for (const auto& mesh : batch.model.meshes) {
            const auto& mat = material(batch.model, mesh.material);
            if (!mat.doubleSided || !mat.unlit || mat.alphaMode != "MASK")
                throw std::runtime_error("Instanced tree materials must be double-sided, unlit alpha masks");
            glBindVertexArray(mesh.vao);
            for (GLuint column = 0; column < 4; ++column) {
                const GLuint location = 4 + column;
                glEnableVertexAttribArray(location);
                glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, sizeof(TreeGPUInstance),
                                      reinterpret_cast<const void*>(offsetof(TreeGPUInstance, transform) + column * sizeof(glm::vec4)));
                glVertexAttribDivisor(location, 1);
            }
            // Attribute 8 is animation info on zombie VAOs, canopy data on tree
            // VAOs. The uniform selects its meaning; both retain matrix slots 4-7.
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(TreeGPUInstance),
                                  reinterpret_cast<const void*>(offsetof(TreeGPUInstance, canopy)));
            glVertexAttribDivisor(8, 1);
        }
        for (const auto& draw : source.draws) {
            const auto& mesh = batch.model.meshes.at(draw.primitive);
            batch.draws.push_back({draw.primitive, draw.transform, glm::inverseTranspose(glm::mat3(draw.transform)),
                                   glm::vec3(draw.transform * glm::vec4(mesh.center, 1)),
                                   glm::determinant(glm::mat3(draw.transform)) < 0});
        }
    }
    treeStats_.total = treeInstances_.size();
    std::vector<VisibilityBounds> bounds;
    bounds.reserve(trees.instances.size());
    for (const auto& instance : trees.instances) {
        const auto& layout = treeBatches_[instance.asset].billboard;
        bounds.push_back(layout ? treeBillboardBounds(instance, trees.assets[instance.asset].model, *layout)
                                : vegetationBlastBounds(instance));
    }
    return bounds;
}

void Renderer::setTreeBillboardDistance(float meters) {
    if (!std::isfinite(meters) || meters < 0) throw std::invalid_argument("Tree billboard distance must be finite and nonnegative");
    treeBillboardDistance_ = meters;
}

void Renderer::updateVegetationBlasts(const std::vector<GrenadeExplosion>& explosions) {
    for (const auto& bend : vegetationBends_)
        treeInstances_[bend.instance].data.transform = bend.rest;
    vegetationBends_ = vegetationBlastMotion_.sample(explosions);
    for (const auto& bend : vegetationBends_)
        treeInstances_[bend.instance].data.transform = bend.transform;
}

void Renderer::drawTrees(const glm::mat4& projectionView, bool shadowPass) {
    if (!shadowPass) {
        treeStats_.visible = 0;
        treeStats_.drawCalls = 0;
        treeStats_.queriedForestPatches = 0;
        treeStats_.fullDetail = treeStats_.sideBillboards = treeStats_.topBillboards = treeStats_.triangles = 0;
    }
    if (treeInstances_.empty()) return;
    auto& selected = shadowPass ? shadowTrees_ : visibleTrees_;
    // Casters use the light volume independently of color visibility and the
    // --no-tree-culling flag. Always keep caster selection spatially bounded.
    const bool billboards = !shadowPass && treeBillboardsForFrame_;
    const auto& visibilityIndex = billboards ? treeVisibility_ : treeAuthoredVisibility_;
    const auto* range = forestVisibilityRange();
    const auto visibility = visibilityIndex.query(projectionView, forestDetail_,
                                                 shadowPass || treeCulling_, selected, range);
    if (!shadowPass) treeStats_.queriedForestPatches = visibility.queriedPatches;
    for (auto& batch : treeBatches_) {
        batch.visibleInstances.clear(); batch.sideInstances.clear(); batch.topInstances.clear();
    }
    const Frustum frustum(projectionView);
    std::size_t drawn = 0;
    for (const auto index : selected) {
        const auto& instance = treeInstances_[index];
        auto& batch = treeBatches_[instance.asset];
        const auto mode = billboards && batch.billboard
            ? treeBillboardMode(glm::vec3(instance.data.transform[3]), treeViewEye_, treeBillboardDistance_)
            : TreeBillboardMode::Full;
        // The broad query covers every rotated pose. A nearby tree still uses
        // its authored bounds, avoiding extra full-card draws at the view edge.
        if (billboards && batch.billboard && mode == TreeBillboardMode::Full) {
            const auto& bounds = instance.authoredBounds;
            if ((treeCulling_ && !frustum.intersects(bounds.minimum, bounds.maximum)) ||
                (range && !range->intersects(bounds))) continue;
        }
        ++drawn;
        if (mode == TreeBillboardMode::Side) {
            batch.sideInstances.push_back(instance.data); ++treeStats_.sideBillboards;
        } else if (mode == TreeBillboardMode::Top) {
            batch.topInstances.push_back(instance.data); ++treeStats_.topBillboards;
        } else {
            batch.visibleInstances.push_back(instance.data);
            if (!shadowPass) ++treeStats_.fullDetail;
        }
    }
    if (!shadowPass) treeStats_.visible = drawn;
    glUniform1i(instancedLocation_, GL_TRUE);
    glUniform1i(treeCanopyLocation_, !shadowPass && treeCanopyShading_);
    for (const auto& batch : treeBatches_) {
        const auto submit = [&](const std::vector<TreeGPUInstance>& instances, const DrawGPU* single, int mode) {
            if (instances.empty()) return;
            const auto size = static_cast<GLsizeiptr>(instances.size() * sizeof(TreeGPUInstance));
            glBindBuffer(GL_ARRAY_BUFFER, batch.instanceBuffer);
            // Each LoD group uploads once, shared by its selected card draws.
            glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, size, instances.data());
            glUniform1i(treeBillboardModeLocation_, mode);
            const auto pivot = batch.billboard ? batch.billboard->topPivot : glm::vec3(0);
            glUniform3fv(treeBillboardPivotLocation_, 1, glm::value_ptr(pivot));
            const auto drawCard = [&](const DrawGPU& draw) {
                const auto& mesh = batch.model.meshes[draw.primitive];
                applyMaterial(batch.model, mesh.material, draw.mirrored);
                if (mode) {
                    // Shader rotation faces the actual winding toward the eye,
                    // including reflected instances and reflected asset nodes.
                    glEnable(GL_CULL_FACE); glFrontFace(GL_CCW);
                }
                glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(draw.transform));
                glBindVertexArray(mesh.vao);
                glDrawElementsInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr,
                                       static_cast<GLsizei>(instances.size()));
                if (!shadowPass) { ++treeStats_.drawCalls; treeStats_.triangles += mesh.count/3 * instances.size(); }
            };
            if (single) drawCard(*single);
            else for (const auto& draw : batch.draws) drawCard(draw);
        };
        submit(batch.visibleInstances, nullptr, 0);
        if (batch.billboard) {
            submit(batch.sideInstances, &batch.draws[batch.billboard->sideDraw], 1);
            submit(batch.topInstances, &batch.draws[batch.billboard->topDraw], 2);
        }
    }
    glUniform1i(treeBillboardModeLocation_, 0);
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(treeCanopyLocation_, GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);
}

} // namespace viewer
