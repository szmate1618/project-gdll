#pragma once

#include <GL/glcorearb.h>
#include <filesystem>
#include <vector>

#include "camera.hpp"
#include "model.hpp"
#include "tree_visibility.hpp"

namespace viewer {

struct TreeRenderStats {
    std::size_t total = 0, visible = 0, drawCalls = 0;
};

class Renderer {
public:
    Renderer(const Model& model, const std::filesystem::path& shaderDirectory,
             const TreeLayer* trees = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(int width, int height);
    void render(const Camera& camera);
    void present(int width, int height);
    void writeScreenshot(const std::filesystem::path& path) const;
    void setTreeCulling(bool enabled) { treeCulling_ = enabled; }
    const TreeRenderStats& treeStats() const { return treeStats_; }
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
    void release() noexcept;
    static void releaseModel(ModelGPU& model) noexcept;
    void uploadModel(const Model& source, ModelGPU& destination);
    void uploadTrees(const TreeLayer& trees);
    void drawTrees(const glm::mat4& projectionView);
    void applyMaterial(const ModelGPU& model, int index, bool mirrored);
    void draw(const DrawGPU& draw);
    const Material& material(const ModelGPU& model, int index) const;
    GLuint program_ = 0, framebuffer_ = 0, colorBuffer_ = 0, depthBuffer_ = 0;
    int width_ = 0, height_ = 0;
    GLint modelLocation_, viewLocation_, projectionLocation_, normalLocation_;
    GLint colorLocation_, textureLocation_, hasTextureLocation_, unlitLocation_;
    GLint alphaLocation_, cutoffLocation_, instancedLocation_;
    ModelGPU model_;
    std::vector<DrawGPU> opaque_, transparent_;
    std::vector<TreeBatch> treeBatches_;
    std::vector<InstanceGPU> treeInstances_;
    std::vector<std::size_t> visibleTrees_;
    TreeVisibility treeVisibility_;
    TreeRenderStats treeStats_;
    bool treeCulling_ = true;
    Material fallback_;
};

} // namespace viewer
