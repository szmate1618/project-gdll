#pragma once

#include <GL/glcorearb.h>
#include <filesystem>
#include <vector>

#include "camera.hpp"
#include "model.hpp"
#include "model_visibility.hpp"
#include "tree_visibility.hpp"
#include "zombie_layer.hpp"

namespace viewer {

struct TreeRenderStats {
    std::size_t total = 0, visible = 0, drawCalls = 0;
};

struct SceneRenderStats {
    std::size_t total = 0, visible = 0, drawCalls = 0;
};

class Renderer {
public:
    Renderer(const Model& model, const std::filesystem::path& shaderDirectory,
             const TreeLayer* trees = nullptr, const ZombieLayer* zombies = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(int width, int height);
    void render(const Camera& camera, double animationSeconds = 0.0);
    void present(int width, int height);
    void writeScreenshot(const std::filesystem::path& path) const;
    void setSceneCulling(bool enabled) { sceneCulling_ = enabled; }
    const SceneRenderStats& sceneStats() const { return sceneStats_; }
    void setTreeCulling(bool enabled) { treeCulling_ = enabled; }
    const TreeRenderStats& treeStats() const { return treeStats_; }
    std::size_t zombieCount() const { return zombieCount_; }
    std::size_t zombieDrawCalls() const { return zombieDrawCalls_; }
    static void checkErrors(const char* stage);

private:
    struct MeshGPU {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
        int material = -1;
        glm::vec3 center{0};
    };
    struct DrawGPU {
        std::size_t primitive;
        glm::mat4 transform;
        glm::mat3 normalMatrix;
        glm::vec3 center;
        bool mirrored;
    };
    struct ModelGPU {
        std::vector<MeshGPU> meshes;
        std::vector<GLuint> textures;
        std::vector<Material> materials;
    };
    struct TreeBatch {
        ModelGPU model;
        std::vector<DrawGPU> draws;
        GLuint instanceBuffer = 0;
        std::vector<glm::mat4> visibleTransforms;
    };
    struct InstanceGPU { std::size_t asset; glm::mat4 transform; };
    struct ZombieBatch {
        ModelGPU model;
        GLuint instanceBuffer = 0;
        std::vector<GLuint> animationBuffers, animationTextures;
        std::vector<GLint> vertexCounts;
        GLsizei count = 0;
        GLint frameCount = 0;
        float duration = 0;
    };
    void release() noexcept;
    static void releaseModel(ModelGPU& model) noexcept;
    void uploadModel(const Model& source, ModelGPU& destination);
    void uploadTrees(const TreeLayer& trees);
    void drawTrees(const glm::mat4& projectionView);
    void uploadZombies(const ZombieLayer& zombies);
    void drawZombies(double seconds);
    void releaseZombies() noexcept;
    void applyMaterial(const ModelGPU& model, int index, bool mirrored);
    void draw(const DrawGPU& draw);
    void gatherVisibleSceneDraws(const glm::mat4& projectionView, const glm::vec3& eye);
    const Material& material(const ModelGPU& model, int index) const;
    GLuint program_ = 0, framebuffer_ = 0, colorBuffer_ = 0, depthBuffer_ = 0;
    int width_ = 0, height_ = 0;
    GLint modelLocation_, viewLocation_, projectionLocation_, normalLocation_;
    GLint colorLocation_, textureLocation_, hasTextureLocation_, unlitLocation_;
    GLint alphaLocation_, cutoffLocation_, instancedLocation_;
    GLint animatedLocation_, animationLocation_, animationFrameLocation_;
    GLint animationCountLocation_, animationVerticesLocation_;
    ModelGPU model_;
    std::vector<DrawGPU> sceneDraws_;
    std::vector<std::size_t> visibleScene_, opaqueScene_, transparentScene_;
    ModelVisibility sceneVisibility_;
    SceneRenderStats sceneStats_;
    bool sceneCulling_ = true;
    std::vector<TreeBatch> treeBatches_;
    std::vector<InstanceGPU> treeInstances_;
    std::vector<std::size_t> visibleTrees_;
    TreeVisibility treeVisibility_;
    TreeRenderStats treeStats_;
    bool treeCulling_ = true;
    std::vector<ZombieBatch> zombieBatches_;
    std::size_t zombieCount_ = 0, zombieDrawCalls_ = 0;
    Material fallback_;
};

} // namespace viewer
