#pragma once

#include "opengl.hpp"
#include "basic_shader_uniforms.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>

#include "camera.hpp"
#include "presentation_viewport.hpp"
#include "vehicle_lod.hpp"
#include <map>
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
#include "dropped_weapons.hpp"
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
class BloodMistRenderer;
class GpuTimer;
class DoorLayer;
class BoxLayer;
class VehicleLayer;
class DoodadLayer;

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

// The private base is the active program's location snapshot. It keeps shared
// draw helpers independent of whether the current basic program supports wounds.
class Renderer : private BasicShaderUniforms {
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
    // Upload each ground-detail mesh once; instances borrow the live layer.
    void setDoodads(const DoodadLayer& doodads, const std::array<Model,3>& models);
    void updateDoodads(const DoodadLayer& doodads);
    std::size_t doodadVisibleCount() const { return doodadVisibleCount_; }
    // Shared model resources; the physics layer must outlive rendering.
    void setDroppedWeapons(const DroppedWeapons& weapons, const std::array<Model,weaponIds.size()>& models);
    void loadGrenadeModel(const std::filesystem::path& path);
    void updateGrenades(const std::vector<GrenadePose>& poses) { grenadePoses_ = poses; }
    void updateVegetationBlasts(const std::vector<GrenadeExplosion>& explosions);
    void setExplosionLights(const std::vector<MuzzleFlashLight>& lights);

    void updateVehicles(const VehicleLayer& vehicles, std::optional<std::size_t> driver = {});
    // Upscale the scene into a color-only target at window-framebuffer resolution.
    // Binds it for GUI drawing and fits the viewport to the scene for the reticle.
    // The HUD can reset the viewport to the full window; its input then uses
    // window fractions directly. present()/writeScreenshot() include these draws.
    PresentationViewport beginOverlay(int width, int height);
    void present(int width, int height);
    void writeScreenshot(const std::filesystem::path& path) const;
    // Borrow an uploaded scene texture. Do not delete it or change its sampler
    // settings; consumers must stop using it before this renderer is destroyed.
    GLuint sceneTexture(std::size_t index) const { return model_.textures.at(index); }
    void setSceneCulling(bool enabled) { sceneCulling_ = enabled; }
    void setRoofBatching(bool enabled) { roofBatching_ = enabled; }
    void setWallBatching(bool enabled) { wallBatching_ = enabled; }
    void setShadows(bool enabled) { shadows_ = enabled; }
    // Reallocates the sunlight map between frames. Light coverage is unchanged;
    // projection snapping and PCF texels follow the actual map resolution.
    void setSunShadowResolution(int resolution);
    int sunShadowResolution() const { return shadowMap_->resolution(); }
    // Optional borrowed, non-overlapping GL_TIME_ELAPSED timer. Caller owns it.
    void setSunShadowTimer(GpuTimer* timer) { sunShadowTimer_ = timer; }
    double sunShadowCpuMilliseconds() const { return sunShadowCpuMs_; }
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
    // Nearest texels/mips by default, in both camera and shadow passes.
    // Disabling restores each vegetation asset's authored texture filters.
    void setTreeNearestFiltering(bool enabled);
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
    void setVehicleAssets(const std::filesystem::path& directory);
    void setVehicleInstancing(bool enabled) { vehicleInstancing_ = enabled; }
    void setVehicleDistanceDetail(bool enabled) { vehicleDistanceDetail_ = enabled; }
    // Draw/query counters describe the camera pass. Bounds update counts are
    // shared preparation; shadows retain independent frustum queries.
    struct VehicleStats {
        std::array<std::size_t, 4> levels{};
        std::size_t triangles = 0, drawCalls = 0, distanceCulled = 0;
        std::size_t instancedDrawCalls = 0, transparentDrawCalls = 0;
        std::size_t rangeCandidates = 0, testedNodes = 0, testedInstances = 0, boundsUpdated = 0;
    };
    const VehicleStats& vehicleStats() const { return vehicleStats_; }
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
        std::vector<std::array<GLint, 2>> textureFilters;
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
    struct VisibilityPassState {
        VisibilityQueryState scene;
        ForestTreeVisibilityQueryState billboardTrees, authoredTrees;
        VisibilityQueryState forests;
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
        float woundBase = 0; // Triangle-mask texel offset + 1; zero is intact.
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
        // Camera exteriors are partitioned while selecting visible instances.
        // Shadow lists retain their independent geometry and masking policy.
        std::vector<ZombieGPUInstance> cameraInstances, woundedCameraInstances;
        std::vector<ZombieGPUInstance> shadowInstances, flashlightInstances;
    };
    struct ZombieBatch : ZombieGeometry {
        struct WoundRow { std::size_t offset; std::uint64_t revision; int severedJoint; };
        GLuint woundBuffer = 0, woundTexture = 0;
        std::size_t woundStride = 0, woundLimit = 0;
        std::vector<std::uint8_t> woundBytes;
        std::unordered_map<std::size_t, WoundRow> woundRows;
        std::vector<std::size_t> freeWoundRows;
        std::vector<ZombieGeometry> lods;
        std::vector<ZombieGeometry> severed;
        // Original and severed anatomical caps are camera-only, uploaded on demand.
        std::vector<ZombieWoundGeometry> woundSurfaces;
        std::vector<ZombieGeometry> closures;
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
    void drawDoodads(const glm::mat4& projectionView, bool shadowPass = false);
    void releaseDoodads() noexcept;
    void drawVehicles(const glm::mat4& projectionView, bool shadowPass = false);
    void prepareVehicleDraws(const Camera& camera, const glm::mat4& projection);
    void releaseVehicleAssets() noexcept;
    void resetVehicleVisibility();
    void syncVehicleInstances();
    void loadVehicleRenderAsset(const VehicleConfig& config);
    void initializeVehicleInstances(ModelGPU& model, GLuint& buffer);
    void applyVehicleLights();

    void setupInstanceMatrices(ModelGPU& model, GLuint& buffer);
    void drawDroppedWeapons(const glm::mat4& projectionView);
    void releaseDroppedWeapons() noexcept;
    // Returns false when a building's spatial batch was already drawn in this pass.
    bool drawTownSource(std::size_t index);
    std::vector<VisibilityBounds> uploadTrees(const TreeLayer& trees, const ForestLayer* forests);
    void drawTrees(const glm::mat4& projectionView, bool shadowPass, VisibilityPassState& temporal);
    void uploadForests(const ForestLayer& forests);
    void prepareForestView(const Camera& camera);
    void drawForests(const glm::mat4& projectionView, bool shadowPass, VisibilityQueryState& temporal);
    void uploadZombies(const ZombieLayer& zombies);
    void updateZombieInstance(std::size_t id, bool bounds, bool full);
    void updateZombieWound(std::size_t id, ZombieBatch& batch, ZombieGPUInstance& instance);
    void drawWoundContents(const glm::mat4& projectionView);
    void releaseWoundContents() noexcept;
    void uploadZombieGeometry(const AnimatedModel& source, ZombieGeometry& geometry, std::size_t capacity, GLint maximumTexels);
    void prepareZombieDraws(const Frustum& cameraFrustum, const Frustum* lightFrustum,
                            const Camera& camera, const glm::mat4& projection, double seconds, const Frustum* flashlightFrustum);
    enum class ZombiePass { Camera, SunShadow, FlashlightShadow };
    void drawZombies(double seconds, ZombiePass pass = ZombiePass::Camera);
    void drawZombieGroup(double seconds, ZombiePass pass, bool wounds);
    void drawSunShadows(const SunShadowView& shadowView, double seconds);
    void drawFlashlightShadows(const FlashlightView& view, double seconds);
    void drawShadowCasters(const glm::mat4& view, const glm::mat4& projection, double seconds,
                           ZombiePass pass, glm::vec3 detailEye);
    DayNightLighting lightingForFrame() const;
    void applyLighting(const Camera& camera, const SunShadowView& shadowView,
                       const DayNightLighting& lighting, const FlashlightView& flashlight, bool flashlightCasting);
    void releaseZombies() noexcept;
    struct MaterialState {
        glm::vec4 baseColor;
        GLuint texture;
        int alphaMode;
        float alphaCutoff, minifiedAlphaCutoff;
        bool unlit;
    };
    // Material uniforms belong to the active program; texture is the unit-0 2D binding.
    // Culling/winding remain explicit because individual layers override them.
    std::optional<MaterialState> materialState_;
    void selectBasicShader(bool wounds);
    void applyBasicSamplersAndVisibility();
    void applyMaterial(const ModelGPU& model, int index, bool mirrored);
    void draw(const DrawGPU& draw);
    void drawGrenades(const glm::mat4& projectionView);
    void gatherVisibleSceneDraws(const glm::mat4& projectionView, const glm::vec3& eye,
                                const glm::mat4& projection);
    void sortOpaqueScene();
    const Material& material(const ModelGPU& model, int index) const;
    std::filesystem::path shaderDirectory_;
    GLuint program_ = 0, framebuffer_ = 0, colorBuffer_ = 0, depthBuffer_ = 0;
    GLuint healthyZombieProgram_ = 0, activeBasicProgram_ = 0;
    BasicShaderUniforms woundUniforms_, healthyZombieUniforms_;
    int width_ = 0, height_ = 0;
    GLuint overlayFramebuffer_ = 0, overlayColor_ = 0;
    int overlayWidth_ = 0, overlayHeight_ = 0;
    bool overlayActive_ = false; // Reset by render(); other callers can present the scene directly.
    MuzzleFlashLight muzzleFlash_;
    std::array<MuzzleFlashLight, maxExplosionLights> explosionLights_{};
    std::size_t explosionLightCount_ = 0;
    Atmosphere atmosphere_;
    bool haze_ = true;
    bool groundFog_ = true;
    glm::vec3 backgroundSrgb_{0.10f, 0.14f, 0.20f};
    SunLighting lighting_;
    NightLighting nightLighting_;
    bool flashlightEnabled_ = true;
    bool night_ = false;
    std::optional<DayNightLighting> dayNight_;
    std::unique_ptr<ShadowMap> shadowMap_ = std::make_unique<ShadowMap>(SunLighting::shadowResolution);
    GpuTimer* sunShadowTimer_ = nullptr;
    double sunShadowCpuMs_ = 0;
    std::unique_ptr<ShadowMap> flashlightShadowMap_;
    bool flashlightShadows_ = false;
    bool shadows_ = true;
    std::vector<std::size_t> shadowScene_;
    ModelGPU model_;
    ModelGPU vehicleModel_; // Procedural fallback retains independently posed wheels.
    struct VehicleLodGPU {
        ModelGPU model;
        std::vector<DrawInstance> draws;
        float radius = 2.6f;
        GLuint instanceBuffer = 0;
        std::vector<glm::mat4> transforms;
    };
    struct VehicleRenderInstance {
        std::vector<VehicleLodGPU>* lods = nullptr;
        std::uint64_t revision = 0;
        glm::vec3 center{0};
        glm::mat4 chassis{1};
        std::array<glm::mat4,4> wheels;
        VehicleDetail detail;
        float radius = 2.6f;
    };
    std::filesystem::path vehicleAssetDirectory_;
    std::map<std::string, std::vector<VehicleLodGPU>> vehicleAssets_;
    std::vector<VehicleRenderInstance> vehicleInstances_;
    std::vector<VisibilityBounds> vehicleBounds_;
    VisibilityIndex vehicleVisibility_;
    std::vector<std::size_t> vehicleRangeCandidates_, visibleVehicles_;
    VisibilityBounds vehicleRange_;
    std::uint64_t vehicleSourceIdentity_ = 0;
    bool vehicleRangeLimited_ = false, vehicleInstancing_ = true;
    float vehicleProjectionScale_ = 1;
    GLuint vehicleInstanceBuffer_ = 0;
    std::vector<glm::mat4> vehicleChassisTransforms_, vehicleWheelTransforms_;
    glm::vec3 vehicleEye_{0};
    bool vehicleDistanceDetail_ = true;
    VehicleStats vehicleStats_;
    const VehicleLayer* vehicleSource_ = nullptr;
    std::optional<std::size_t> vehicleDriver_;
    ModelGPU boxModel_;
    ModelGPU grenadeModel_;
    ModelGPU woundContentsModel_;
    GLuint woundContentsBuffer_ = 0;
    std::vector<DrawInstance> grenadeDraws_;
    std::vector<GrenadePose> grenadePoses_;
    GLuint boxInstanceBuffer_ = 0;
    std::vector<glm::mat4> boxTransforms_, visibleBoxTransforms_;
    std::vector<std::size_t> visibleBoxes_;
    VisibilityIndex boxVisibility_;
    const BoxLayer* boxSource_ = nullptr;
    std::uint64_t boxRevision_ = 0;
    struct DoodadModelGPU {
        ModelGPU model;
        std::vector<DrawInstance> draws;
        GLuint instances = 0;
        std::vector<glm::mat4> visible;
    };
    std::array<DoodadModelGPU,3> doodadModels_;
    const DoodadLayer* doodadSource_ = nullptr;
    VisibilityIndex doodadVisibility_;
    std::vector<std::size_t> visibleDoodads_;
    std::uint64_t doodadRevision_ = 0;
    std::size_t doodadVisibleCount_ = 0;
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
    // Render-thread scratch owned for the fixed scene's lifetime, reused when
    // radix-ordering the opaque source IDs.
    std::vector<std::size_t> opaqueSortScratch_;
    ModelVisibility sceneVisibility_;
    // Plane-order hints belong to each view; alternating camera/light volumes
    // and billboard/authored bounds must not overwrite one another's history.
    VisibilityPassState cameraVisibility_, sunShadowVisibility_, flashlightVisibility_;
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
    std::unique_ptr<BloodMistRenderer> bloodMist_;
    struct DroppedModelGPU {
        ModelGPU model;
        std::vector<DrawInstance> draws;
        GLuint instances = 0;
    };
    std::array<DroppedModelGPU,weaponIds.size()> droppedModels_;
    const DroppedWeapons* droppedSource_ = nullptr;
    bool rainEnabled_ = false;
};

} // namespace viewer
