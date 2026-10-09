#pragma once

#include <GL/glcorearb.h>
#include <filesystem>
#include <memory>
#include <vector>

#include "camera.hpp"
#include "model.hpp"
#include "model_visibility.hpp"
#include "tree_visibility.hpp"
#include "zombie_layer.hpp"
#include "sun_lighting.hpp"
#include "sun_shadow_map.hpp"
#include "atmosphere.hpp"
#include "night_lighting.hpp"

namespace viewer {

class RainRenderer;

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
    void updateZombies(const ZombieLayer& zombies);
    void present(int width, int height);
    void writeScreenshot(const std::filesystem::path& path) const;
    void setSceneCulling(bool enabled) { sceneCulling_ = enabled; }
    void setShadows(bool enabled) { shadows_ = enabled; }
    void setTreeShadows(bool enabled) { treeShadows_ = enabled; }
    void setLighting(const SunLighting& lighting) { lighting_ = lighting; }
    void setNight(bool enabled) { night_ = enabled; }
    void setRain(bool enabled);
    void setHaze(bool enabled) { haze_ = enabled; }
    void setGroundFog(bool enabled) { groundFog_ = enabled; }
    void setAtmosphere(const Atmosphere& atmosphere) { atmosphere_ = atmosphere; }
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
    struct ZombieGPUInstance {
        glm::mat4 transform;
        float phase;
        float animationFrame;
        float liveSkinning;
        float boneBase; // Matrix offset into the batch's skinning palette, or -1.
    };
    struct ZombieBatch {
        ModelGPU model;
        GLuint instanceBuffer = 0;
        std::vector<std::size_t> sourceIndices;
        std::vector<ZombieGPUInstance> staging;
        std::vector<GLuint> animationBuffers, animationTextures;
        std::vector<GLuint> skinningBuffers;
        std::vector<GLint> skinningEnabled;
        GLuint skinningTexture = 0;
        GLuint skinningBuffer = 0;
        std::vector<glm::mat4> skinningMatrices;
        GLint skinningBoneCount = 0;
        std::vector<GLint> vertexCounts;
        GLsizei count = 0;
        GLint frameCount = 0;
        float duration = 0;
        VisibilityBounds animationBounds, bindBounds;
        std::vector<VisibilityBounds> instanceBounds;
        std::vector<ZombieGPUInstance> shadowInstances;
        bool shadowInstancesUploaded = false;
    };
    void release() noexcept;
    static void releaseModel(ModelGPU& model) noexcept;
    void uploadModel(const Model& source, ModelGPU& destination);
    void uploadTrees(const TreeLayer& trees);
    void drawTrees(const glm::mat4& projectionView, bool shadowPass = false);
    void uploadZombies(const ZombieLayer& zombies);
    void drawZombies(double seconds, const Frustum* shadowFrustum = nullptr);
    void drawSunShadows(const SunShadowView& shadowView, double seconds);
    void applyLighting(const Camera& camera, const SunShadowView& shadowView);
    void releaseZombies() noexcept;
    void applyMaterial(const ModelGPU& model, int index, bool mirrored);
    void draw(const DrawGPU& draw);
    void gatherVisibleSceneDraws(const glm::mat4& projectionView, const glm::vec3& eye);
    const Material& material(const ModelGPU& model, int index) const;
    std::filesystem::path shaderDirectory_;
    GLuint program_ = 0, framebuffer_ = 0, colorBuffer_ = 0, depthBuffer_ = 0;
    int width_ = 0, height_ = 0;
    GLint modelLocation_, viewLocation_, projectionLocation_, normalLocation_;
    GLint colorLocation_, textureLocation_, hasTextureLocation_, unlitLocation_;
    GLint alphaLocation_, cutoffLocation_, instancedLocation_;
    GLint animatedLocation_, animationLocation_, animationFrameLocation_;
    GLint animationCountLocation_, animationVerticesLocation_;
    GLint skinningLocation_, skinningBonesLocation_, skinningBoneCountLocation_;
    GLint shadowPassLocation_, shadowsLocation_, shadowMapLocation_, sunMatrixLocation_;
    GLint eyeLocation_, sunDirectionLocation_, sunColorLocation_, skyAmbientLocation_, groundAmbientLocation_;
    GLint shadowDistanceLocation_, shadowFadeLocation_;
    GLint hazeColorLocation_, hazeDensityLocation_, hazeStartLocation_;
    GLint groundFogLocation_;
    GLint nightLocation_, flashlightDirectionLocation_, flashlightColorLocation_, flashlightConeLocation_;
    Atmosphere atmosphere_;
    bool haze_ = true;
    bool groundFog_ = true;
    glm::vec3 backgroundSrgb_{0.10f, 0.14f, 0.20f};
    SunLighting lighting_;
    NightLighting nightLighting_;
    bool night_ = false;
    SunShadowMap shadowMap_;
    bool shadows_ = true;
    std::vector<std::size_t> shadowScene_;
    ModelGPU model_;
    std::vector<DrawGPU> sceneDraws_;
    std::vector<std::size_t> visibleScene_, opaqueScene_, transparentScene_;
    ModelVisibility sceneVisibility_;
    SceneRenderStats sceneStats_;
    bool sceneCulling_ = true;
    std::vector<TreeBatch> treeBatches_;
    std::vector<InstanceGPU> treeInstances_;
    std::vector<std::size_t> visibleTrees_, shadowTrees_;
    TreeVisibility treeVisibility_;
    TreeRenderStats treeStats_;
    bool treeCulling_ = true;
    bool treeShadows_ = true;
    std::vector<ZombieBatch> zombieBatches_;
    std::size_t zombieCount_ = 0, zombieDrawCalls_ = 0;
    Material fallback_;
    std::unique_ptr<RainRenderer> rain_;
    bool rainEnabled_ = false;
};

} // namespace viewer
