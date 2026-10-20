#pragma once

#include "opengl.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "camera.hpp"
#include "model.hpp"
#include "model_visibility.hpp"
#include "forest_tree_visibility.hpp"
#include "forest_volume_batch.hpp"
#include "forest_layer.hpp"
#include "forest_visibility.hpp"
#include "zombie_layer.hpp"
#include "zombie_palette_cache.hpp"
#include "zombie_visibility.hpp"
#include "sun_lighting.hpp"
#include "shadow_map.hpp"
#include "flashlight.hpp"
#include "muzzle_flash.hpp"
#include "grenades.hpp"
#include "atmosphere.hpp"
#include "night_lighting.hpp"
#include "day_night.hpp"
#include "tree_canopy.hpp"
#include "tree_billboard.hpp"
#include "tree_depth_order.hpp"
#include "building_batch.hpp"
#include "vegetation_blast.hpp"

namespace viewer {

class RainRenderer;
class DoorLayer;
class BoxLayer;

struct TreeRenderStats {
    std::size_t total = 0, visible = 0, drawCalls = 0;
    std::size_t queriedForestPatches = 0;
    std::size_t fullDetail = 0, sideBillboards = 0, topBillboards = 0, triangles = 0;
};

struct ZombieRenderStats {
    std::array<std::size_t, 4> cameraLevels{}, shadowLevels{}, flashlightLevels{};
    std::size_t cameraTriangles = 0, shadowTriangles = 0, flashlightTriangles = 0, distanceCulled = 0;
};

struct ZombieUpdateStats {
    std::size_t instances = 0, bounds = 0;
    bool full = false;
};

struct SceneRenderStats {
    std::size_t total = 0, visible = 0, drawCalls = 0;
};

struct ForestRenderStats { std::size_t patches = 0, volumes = 0, detailedPatches = 0, drawCalls = 0; };

class Renderer {
public:
    Renderer(const Model& model, const std::filesystem::path& shaderDirectory,
             const TreeLayer* trees = nullptr, const ZombieLayer* zombies = nullptr,
             const ForestLayer* forests = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(int width, int height);
    // Optional external opaque draws run with depth writes enabled before glass
    // and rain. They must restore the GL state they change before returning.
    void render(const Camera& camera, double animationSeconds = 0.0,
                const std::function<void()>& drawOpaque = {});
    // Fixed asset/population topology. Borrows this layer through render(); call
    // after owning API/physics changes and keep the layer alive during rendering.
    // Direct edits require ZombieLayer::markRenderChanged()/invalidateRenderData().
    void updateZombies(const ZombieLayer& zombies);
    void updateDoors(const DoorLayer& doors);
    // Refresh cached poses after physics or spawning. The source must outlive
    // this renderer; unchanged revisions retain meshes and visibility bounds.
    void updateBoxes(const BoxLayer& boxes);
    void loadGrenadeModel(const std::filesystem::path& path);
    void updateGrenades(const std::vector<GrenadePose>& poses) { grenadePoses_ = poses; }
    void updateVegetationBlasts(const std::vector<GrenadeExplosion>& explosions);
    void setExplosionLights(const std::vector<MuzzleFlashLight>& lights);
    void present(int width, int height);
    void writeScreenshot(const std::filesystem::path& path) const;
    void setSceneCulling(bool enabled) { sceneCulling_ = enabled; }
    void setRoofBatching(bool enabled) { roofBatching_ = enabled; }
    void setWallBatching(bool enabled) { wallBatching_ = enabled; }
    void setShadows(bool enabled) { shadows_ = enabled; }
    void setFlashlight(bool enabled) { flashlightEnabled_ = enabled; }
    void setFlashlightShadows(bool enabled) { flashlightShadows_ = enabled; }
    void setMuzzleFlash(const MuzzleFlashLight& light) { muzzleFlash_ = light; }
    void setTreeShadows(bool enabled) { treeShadows_ = enabled; }
    void setTreeCanopyShading(bool enabled) { treeCanopyShading_ = enabled; }
    // Three-dimensional root distance in meters; zero keeps authored cards.
    void setTreeBillboardDistance(float meters);
    void setTreeDepthOrdering(bool enabled) { treeDepthOrdering_ = enabled; }
    // Switch preuploaded index ranges; no image scan or buffer upload per frame.
    void setTreeCardSplitting(bool enabled) { treeCardSplitting_ = enabled; }
    // Horizontal radius inside old forest only; zero disables the cutoff.
    void setOldForestDistance(float meters);
    void setLighting(const SunLighting& lighting) { lighting_ = lighting; }
    void setNight(bool enabled) { night_ = enabled; dayNight_.reset(); }
    // Overrides the fixed lighting palette until the next setNight call.
    void setDayNightLighting(const DayNightLighting& lighting) { dayNight_ = lighting; }
    void setRain(bool enabled);
    void setHaze(bool enabled) { haze_ = enabled; }
    void setGroundFog(bool enabled) { groundFog_ = enabled; }
    void setAtmosphere(const Atmosphere& atmosphere) { atmosphere_ = atmosphere; }
    const SceneRenderStats& sceneStats() const { return sceneStats_; }
    void setTreeCulling(bool enabled) { treeCulling_ = enabled; }
    void setZombieDistanceDetail(bool enabled) { zombieDistanceDetail_ = enabled; }
    void setZombieDrawDistance(float meters) { zombieDrawDistance_ = meters; }
    void setZombieShadows(bool enabled) { zombieShadows_ = enabled; }
    void setZombieCulling(bool enabled) { zombieCulling_ = enabled; }
    const TreeRenderStats& treeStats() const { return treeStats_; }
    const ForestRenderStats& forestStats() const { return forestStats_; }
    std::size_t zombieCount() const { return zombieCount_; }
    std::size_t zombieVisibleCount() const { return zombieVisibleCount_; }
    std::size_t zombieDrawCalls() const { return zombieDrawCalls_; }
    const ZombieRenderStats& zombieRenderStats() const { return zombieRenderStats_; }
    const ZombieUpdateStats& zombieUpdateStats() const { return zombieUpdateStats_; }
    const ZombiePaletteStats& zombiePaletteStats() const { return zombiePaletteStats_; }
    const ZombieVisibilityStats& zombieVisibilityStats() const { return zombieVisibilityStats_; }
    static void checkErrors(const char* stage);

private:
    const HorizontalVisibilityRange* forestVisibilityRange() const {
        return forestViewRange_ ? &*forestViewRange_ : nullptr;
    }
    struct MeshGPU {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
        GLsizei splitCount = 0; // Replacement follows count original indices.
        int material = -1;
        glm::vec3 center{0};
    };
    struct DrawGPU {
        std::size_t primitive;
        glm::mat4 transform;
        glm::mat3 normalMatrix;
        glm::vec3 center;
        bool mirrored;
        bool buildingDetail = false;
    };
    struct ModelGPU {
        std::vector<MeshGPU> meshes;
        std::vector<GLuint> textures;
        std::vector<Material> materials;
    };
    struct TreeGPUInstance {
        glm::mat4 transform;
        TreeCanopyShade canopy;
    };
    struct TreeBatch {
        ModelGPU model;
        std::vector<DrawGPU> draws;
        GLuint instanceBuffer = 0;
        std::vector<TreeGPUInstance> visibleInstances;
        std::vector<TreeGPUInstance> sideInstances, topInstances;
        std::optional<TreeBillboardLayout> billboard;
    };
    struct InstanceGPU {
        std::size_t asset;
        TreeGPUInstance data;
    };
    struct ForestVolumeBatchGPU {
        std::vector<ForestVolumeRange> ranges;
        std::vector<GLsizei> counts;
        std::vector<const void*> offsets;
    };
    struct ZombieGPUInstance {
        glm::mat4 transform;
        float phase;
        float clipFirstFrame;
        float clipFrameCount; // Zero selects the uniform idle frame count.
        float animationFrame;
        float liveSkinning;
        float animationStep; // Nonzero bypasses interpolation and second-pose fetches.
        float boneBase; // Matrix offset into the batch's skinning palette, or -1.
    };
    static ZombieGPUInstance zombieGPUInstance(const AnimatedModel& asset, const ZombieInstance& instance,
                                               float boneBase = -1.0f);
    struct ZombieGeometry {
        ModelGPU model;
        GLuint instanceBuffer = 0;
        std::vector<GLuint> animationBuffers, animationTextures;
        std::vector<GLuint> skinningBuffers;
        std::vector<GLint> skinningEnabled;
        GLint skinningBoneCount = 0;
        std::vector<GLint> vertexCounts;
        GLsizei count = 0;
        GLint frameCount = 0;
        float duration = 0;
        std::vector<ZombieGPUInstance> cameraInstances, shadowInstances, flashlightInstances;
    };
    struct ZombieBatch : ZombieGeometry {
        std::vector<ZombieGeometry> lods;
        std::vector<std::size_t> sourceIndices;
        std::vector<ZombieGPUInstance> staging;
        std::unique_ptr<ZombiePaletteCache> palettes;
        VisibilityBounds animationBounds, bindBounds;
        std::vector<VisibilityBounds> instanceBounds;
        std::vector<VisibilityBounds> poseBounds;
        std::vector<std::uint64_t> poseVersions;
        std::vector<bool> livePoses;
        std::vector<bool> removedPoses;
    };
    void release() noexcept;
    static void releaseModel(ModelGPU& model) noexcept;
    void uploadModel(const Model& source, ModelGPU& destination,
                     const std::vector<Primitive>* geometry = nullptr);
    void uploadMeshes(const std::vector<Primitive>& source, ModelGPU& destination);
    void uploadRoofBatches(const Model& source);
    void uploadWallBatches(const Model& source);
    void beginTownPass(glm::vec3 eye, const glm::mat4& projection, bool sunShadow = false);
    void drawBuildingBatch(const ModelGPU& model, std::size_t batch);
    void drawBoxes(const glm::mat4& projectionView);
    // Returns false when a building's spatial batch was already drawn in this pass.
    bool drawTownSource(std::size_t index);
    std::vector<VisibilityBounds> uploadTrees(const TreeLayer& trees, const ForestLayer* forests);
    void drawTrees(const glm::mat4& projectionView, bool shadowPass = false);
    void uploadForests(const ForestLayer& forests);
    void prepareForestView(const Camera& camera);
    void drawForests(const glm::mat4& projectionView, bool shadowPass = false);
    void uploadZombies(const ZombieLayer& zombies);
    void updateZombieInstance(std::size_t id, bool bounds, bool full);
    void uploadZombieGeometry(const AnimatedModel& source, ZombieGeometry& geometry, std::size_t capacity, GLint maximumTexels);
    void prepareZombieDraws(const Frustum& cameraFrustum, const Frustum* lightFrustum,
                            const Camera& camera, const glm::mat4& projection, double seconds, const Frustum* flashlightFrustum);
    enum class ZombiePass { Camera, SunShadow, FlashlightShadow };
    void drawZombies(double seconds, ZombiePass pass = ZombiePass::Camera);
    void drawSunShadows(const SunShadowView& shadowView, double seconds);
    void drawFlashlightShadows(const FlashlightView& view, double seconds);
    void drawShadowCasters(const glm::mat4& view, const glm::mat4& projection, double seconds,
                           ZombiePass pass, glm::vec3 detailEye);
    DayNightLighting lightingForFrame() const;
    void applyLighting(const Camera& camera, const SunShadowView& shadowView,
                       const DayNightLighting& lighting, const FlashlightView& flashlight, bool flashlightCasting);
    void releaseZombies() noexcept;
    void applyMaterial(const ModelGPU& model, int index, bool mirrored);
    void draw(const DrawGPU& draw);
    void drawGrenades(const glm::mat4& projectionView);
    void gatherVisibleSceneDraws(const glm::mat4& projectionView, const glm::vec3& eye,
                                const glm::mat4& projection);
    const Material& material(const ModelGPU& model, int index) const;
    std::filesystem::path shaderDirectory_;
    GLuint program_ = 0, framebuffer_ = 0, colorBuffer_ = 0, depthBuffer_ = 0;
    int width_ = 0, height_ = 0;
    GLint modelLocation_, viewLocation_, projectionLocation_, normalLocation_;
    GLint colorLocation_, textureLocation_, hasTextureLocation_, unlitLocation_;
    GLint alphaLocation_, cutoffLocation_, instancedLocation_;
    GLint minifiedCutoffLocation_;
    GLint treeCanopyLocation_;
    GLint animatedLocation_, animationLocation_, animationFrameLocation_;
    GLint animationCountLocation_, animationVerticesLocation_;
    GLint skinningLocation_, skinningBonesLocation_, skinningBoneCountLocation_;
    GLint shadowPassLocation_, shadowsLocation_, shadowMapLocation_, sunMatrixLocation_;
    GLint eyeLocation_, sunDirectionLocation_, sunColorLocation_, skyAmbientLocation_, groundAmbientLocation_;
    GLint shadowDistanceLocation_, shadowFadeLocation_;
    GLint hazeColorLocation_, hazeDensityLocation_, hazeStartLocation_;
    GLint groundFogLocation_;
    GLint nightBlendLocation_, unlitLightLocation_;
    GLint flashlightPositionLocation_, flashlightShadowsLocation_, flashlightShadowMapLocation_;
    GLint flashlightMatrixLocation_, flashlightShadowDistancesLocation_;
    GLint flashlightDirectionLocation_, flashlightColorLocation_, flashlightConeLocation_;
    GLint muzzleFlashPositionLocation_, muzzleFlashColorLocation_, muzzleFlashRangeLocation_;
    MuzzleFlashLight muzzleFlash_;
    static constexpr std::size_t maxExplosionLights = 8; // Matches basic.frag.
    std::array<MuzzleFlashLight, maxExplosionLights> explosionLights_{};
    std::size_t explosionLightCount_ = 0;
    GLint explosionCountLocation_ = -1;
    std::array<GLint, maxExplosionLights> explosionPositionLocations_{}, explosionColorLocations_{}, explosionRangeLocations_{};
    Atmosphere atmosphere_;
    bool haze_ = true;
    bool groundFog_ = true;
    glm::vec3 backgroundSrgb_{0.10f, 0.14f, 0.20f};
    SunLighting lighting_;
    NightLighting nightLighting_;
    bool flashlightEnabled_ = true;
    bool night_ = false;
    std::optional<DayNightLighting> dayNight_;
    ShadowMap shadowMap_{SunLighting::shadowResolution};
    std::unique_ptr<ShadowMap> flashlightShadowMap_;
    bool flashlightShadows_ = false;
    bool shadows_ = true;
    std::vector<std::size_t> shadowScene_;
    ModelGPU model_;
    ModelGPU boxModel_;
    ModelGPU grenadeModel_;
    std::vector<DrawInstance> grenadeDraws_;
    std::vector<GrenadePose> grenadePoses_;
    GLuint boxInstanceBuffer_ = 0;
    std::vector<glm::mat4> boxTransforms_, visibleBoxTransforms_;
    std::vector<std::size_t> visibleBoxes_;
    VisibilityIndex boxVisibility_;
    const BoxLayer* boxSource_ = nullptr;
    std::uint64_t boxRevision_ = 0;
    ModelGPU roofModel_;
    std::vector<std::size_t> roofBatchForDraw_;
    std::vector<bool> roofBatchDrawn_;
    bool roofBatching_ = true;
    ModelGPU wallModel_;
    std::vector<std::size_t> wallBatchForDraw_;
    std::vector<BuildingBatchDetail> wallBatchDetails_;
    enum class WallBatchState { Unchecked, Individual, Ready, Drawn };
    std::vector<WallBatchState> wallBatchStates_;
    bool wallBatching_ = true, wallBatchSunShadow_ = false;
    glm::vec3 wallBatchEye_{0};
    glm::mat4 wallBatchProjection_{1};
    std::vector<DrawGPU> sceneDraws_;
    std::vector<std::size_t> visibleScene_, opaqueScene_, transparentScene_;
    ModelVisibility sceneVisibility_;
    SceneRenderStats sceneStats_;
    bool sceneCulling_ = true;
    std::vector<TreeBatch> treeBatches_;
    std::vector<InstanceGPU> treeInstances_;
    std::vector<VisibilityBounds> treeAuthoredBounds_;
    TreeDepthOrder treeDepthOrder_;
    bool treeDepthOrdering_ = true;
    bool treeCardSplitting_ = true;
    VegetationBlastMotion vegetationBlastMotion_;
    std::vector<VegetationBend> vegetationBends_;
    std::vector<std::size_t> visibleTrees_, shadowTrees_;
    ForestTreeVisibility treeVisibility_;
    ForestTreeVisibility treeAuthoredVisibility_;
    glm::vec3 treeViewEye_{0};
    glm::vec3 treeViewForward_{0,0,-1};
    bool treeBillboardsForFrame_ = true;
    float treeBillboardDistance_ = 100;
    GLint treeBillboardModeLocation_ = -1, treeBillboardPivotLocation_ = -1;
    TreeRenderStats treeStats_;
    bool treeCulling_ = true;
    bool treeShadows_ = true;
    bool treeCanopyShading_ = true;
    ModelGPU forestModel_;
    std::vector<ForestVolumeBatchGPU> forestBatches_;
    std::vector<ForestPatch> forestPatches_;
    OldForestVisibility oldForestVisibility_;
    float oldForestDistance_ = OldForestVisibility::distance;
    std::optional<HorizontalVisibilityRange> forestViewRange_;
    GLint forestVisibilityLocation_ = -1;
    std::vector<std::size_t> visibleForests_;
    std::vector<bool> forestDetail_, forestVolumeVisible_;
    VisibilityIndex forestVisibility_;
    ForestRenderStats forestStats_;
    float forestDetailDistance_ = 350;
    std::vector<ZombieBatch> zombieBatches_;
    // Shared groups across assets; bounds storage in batches stays fixed-size.
    ZombieVisibility zombieVisibility_;
    std::vector<std::size_t> zombieBatchLocals_;
    std::vector<std::size_t> zombieBatchAssets_;
    std::vector<ZombieVisibilityResult> visibleZombies_;
    ZombieVisibilityStats zombieVisibilityStats_;
    // Borrowed until the next update; the fixed population must outlive render().
    const ZombieLayer* zombieSource_ = nullptr;
    std::uint64_t zombieUpdateRevision_ = 0;
    ZombieUpdateStats zombieUpdateStats_;
    ZombiePaletteStats zombiePaletteStats_;
    std::vector<std::size_t> displayedZombieSources_;
    ZombieRenderStats zombieRenderStats_;
    bool zombieCulling_ = true;
    bool zombieDistanceDetail_ = true, zombieShadows_ = true;
    float zombieDrawDistance_ = 180;
    std::size_t zombieCount_ = 0, zombieVisibleCount_ = 0, zombieDrawCalls_ = 0;
    Material fallback_;
    std::unique_ptr<RainRenderer> rain_;
    bool rainEnabled_ = false;
};

} // namespace viewer
