#include "renderer.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 640, height = 480;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::Primitive quad(const std::array<glm::vec3, 4>& positions, glm::vec3 normal, int material) {
    viewer::Primitive result;
    result.material = material;
    const std::array<glm::vec2, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        viewer::Vertex vertex;
        vertex.position = positions[i];
        vertex.normal = normal;
        vertex.uv = uv[i];
        result.vertices.push_back(vertex);
    }
    result.indices = {0, 1, 2, 0, 2, 3};
    return result;
}

viewer::Model scene(bool masked, bool unlit = false) {
    viewer::Model model;
    viewer::Material ground;
    ground.unlit = unlit;
    model.materials.push_back(ground);
    viewer::Material wall;
    wall.doubleSided = true;
    if (masked) {
        wall.alphaMode = "MASK";
        wall.baseColorTexture = 0;
        viewer::ImageData image;
        image.width = 2;
        image.height = 1;
        image.rgba = {255, 255, 255, 0, 255, 255, 255, 255};
        model.images.push_back(image);
        viewer::Texture texture;
        texture.image = 0;
        texture.minFilter = GL_NEAREST;
        texture.magFilter = GL_NEAREST;
        model.textures.push_back(texture);
    }
    model.materials.push_back(wall);
    model.primitives.push_back(quad({{{-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10}}}, {0, 1, 0}, 0));
    // This elevated wall is behind the camera but casts onto the visible floor.
    model.primitives.push_back(quad({{{-2, 2, 3}, {2, 2, 3}, {2, 4, 3}, {-2, 4, 3}}}, {0, 0, 1}, 1));
    model.draws.push_back({0, glm::mat4(1), "ground"});
    model.draws.push_back({1, glm::mat4(1), "wall"});
    return model;
}

viewer::Camera cameraAt(glm::vec3 position) {
    viewer::Camera camera;
    camera.setPosition(position);
    camera.look(0, -400); // Look down at 60 degrees.
    return camera;
}

float brightness(const viewer::Camera& camera, glm::vec3 point) {
    const auto clip = camera.projection(static_cast<float>(width) / height) * camera.view() * glm::vec4(point, 1);
    const auto screen = (glm::vec2(clip) / clip.w * 0.5f + 0.5f) * glm::vec2(width, height);
    const int x = static_cast<int>(screen.x), y = static_cast<int>(screen.y);
    require(x > 1 && y > 1 && x < width - 2 && y < height - 2, "Shadow sample outside viewport");
    std::array<unsigned char, 27> pixels{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x - 1, y - 1, 3, 3, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    float sum = 0;
    for (const auto value : pixels) sum += value;
    return sum / static_cast<float>(pixels.size());
}

viewer::SunLighting lighting() {
    viewer::SunLighting light;
    light.direction = {0, 1, 1};
    return light;
}

void checkMovingSun() {
    viewer::Renderer renderer(scene(false), VIEWER_SHADER_DIR);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.setFlashlightShadows(false); // Isolate sunlight at night/horizon.
    renderer.resize(width, height);
    const auto camera = cameraAt({0, 6, 6});
    const auto sample = [&](double hour, bool shadows, glm::vec3 point) {
        renderer.setDayNightLighting(viewer::dayNightLighting(hour));
        renderer.setShadows(shadows);
        renderer.render(camera);
        return brightness(camera, point);
    };
    // This wall's shadow crosses the floor from west to east as the sun moves.
    const glm::vec3 west{-3, 0, 2}, east{3, 0, 2}, middle{0, 0, 2};
    require(sample(9, false, west) - sample(9, true, west) > 35,
            "Morning sunlight must cast the wall's shadow to the west");
    require(std::abs(sample(9, false, east) - sample(9, true, east)) < 3,
            "Morning shadow must leave the eastern receiver lit");
    require(sample(15, false, east) - sample(15, true, east) > 35,
            "Afternoon sunlight must move the wall's shadow to the east");
    require(std::abs(sample(15, false, west) - sample(15, true, west)) < 3,
            "Afternoon shadow must leave the western receiver lit");
    require(sample(12, false, middle) - sample(12, true, middle) > 35,
            "Noon must retain the nearby sun shadow pass");
    for (const auto hour : {0.0, 6.0, 18.0}) {
        require(std::abs(sample(hour, false, middle) - sample(hour, true, middle)) < 2,
                "Night and the horizon must not sample stale daylight shadows");
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        require(viewport[2] == width && viewport[3] == height && !glIsEnabled(GL_POLYGON_OFFSET_FILL),
                "Skipping sunlight must preserve the color pass GL state");
    }
    require(sample(9, false, west) - sample(9, true, west) > 35,
            "Sun shadows must resume after night");
    viewer::Renderer::checkErrors("moving sun regression");
}

void checkScene(bool masked, bool unlit, bool blended = false) {
    auto model = scene(masked, unlit);
    if (blended) model.materials[1].alphaMode = "BLEND";
    auto camera = cameraAt({0, 4, 2});
    const viewer::Frustum cameraFrustum(camera.projection(static_cast<float>(width) / height) * camera.view());
    require(!cameraFrustum.intersects({-2, 2, 3}, {2, 4, 3}), "Fixture caster must be outside the camera view");
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.setLighting(lighting());
    renderer.setHaze(false); // Isolate shadow brightness from distance haze.
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    renderer.setShadows(false);
    renderer.render(camera);
    const float baseline = brightness(camera, {1, 0, 0});
    renderer.setShadows(true);
    renderer.render(camera);
    const float shadow = brightness(camera, {1, 0, 0});
    const float left = brightness(camera, {-1, 0, 0});
    if (unlit || blended) require(std::abs(baseline - shadow) < 2,
        "Unlit receivers and blended casters must not add ground shadows");
    else {
        require(baseline - shadow > 50, "An offscreen caster must shadow the visible ground");
        require(shadow > 60, "Ambient light must remain in shadow");
        if (masked) require(left - shadow > 50, "Transparent alpha-mask pixels must not cast shadows");
        else require(std::abs(left - shadow) < 3, "Opaque wall should shadow both sides");
    }
    require(!glIsEnabled(GL_POLYGON_OFFSET_FILL), "Shadow pass leaked polygon offset into color rendering");
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    require(viewport[2] == width && viewport[3] == height, "Shadow pass leaked its viewport");
    if (!unlit && !blended) {
        camera = cameraAt({0, 90, 52});
        renderer.render(camera);
        const float faded = brightness(camera, {1, 0, 0});
        require(faded > shadow + 15 && faded < baseline - 15,
                "Receivers inside the fade band must have partial shadows");
    }
    // Move the same receiver past the fade range without moving the scene.
    camera = cameraAt({0, 140, 81});
    renderer.render(camera);
    const float farLit = brightness(camera, {1, 0, 0});
    require(std::abs(farLit - baseline) < 3, "Far receivers must remain lit beyond the shadow distance");
    renderer.resize(320, 240);
    renderer.render(camera);
    renderer.resize(width, height);
    renderer.render(camera);
    viewer::Renderer::checkErrors("shadow scene regression");
}

void checkTreeCaster() {
    auto model = scene(true);
    viewer::TreeLayer trees;
    viewer::TreeAsset asset;
    asset.model.materials.push_back(model.materials[1]);
    asset.model.materials[0].unlit = true;
    asset.model.images = std::move(model.images);
    asset.model.textures = std::move(model.textures);
    asset.model.primitives.push_back(model.primitives[1]);
    asset.model.primitives[0].material = 0;
    const auto node = glm::translate(glm::mat4(1), {2, 0, 0});
    asset.model.draws.push_back({0, node, "masked-tree-card"});
    trees.assets.push_back(std::move(asset));
    // Compose an asset node with an authoritative mirrored/sheared instance.
    // The combined card occupies X=[-2,2], Y=[2,4], Z=3; its opaque half is X<0.
    viewer::TreeInstance tree;
    tree.transform[0].x = -1;
    tree.transform[2].x = 0.3f;
    tree.transform[3].x = 1.1f;
    tree.boundsMin = {-2, 2, 3};
    tree.boundsMax = {2, 4, 3};
    trees.instances.push_back(tree);
    tree.transform[3].x += 1000;
    tree.boundsMin.x += 1000;
    tree.boundsMax.x += 1000;
    trees.instances.push_back(tree);
    model.draws.pop_back();
    model.primitives.pop_back();

    viewer::Renderer renderer(model, VIEWER_SHADER_DIR, &trees);
    renderer.setLighting(lighting());
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    const auto view = cameraAt({0, 4, 2});
    renderer.setTreeShadows(false);
    renderer.render(view);
    const float clear = brightness(view, {-1, 0, 0});
    renderer.setTreeShadows(true);
    renderer.render(view);
    const float shadow = brightness(view, {-1, 0, 0});
    require(clear - shadow > 50, "An offscreen instanced tree must cast onto visible ground");
    require(brightness(view, {1, 0, 0}) - shadow > 50,
            "Tree alpha-mask holes must stay open in the shadow map after node/instance transforms");
    require(renderer.treeStats().visible == 0 && renderer.treeStats().drawCalls == 0,
            "Tree shadow casters must not contaminate color visibility statistics");

    // A different color population must overwrite the shadow instance buffer.
    // Tree texels stay unlit, rather than receiving their own cast shadows.
    const auto treeView = cameraAt({0, 5, 6});
    renderer.render(treeView);
    const float treeColor = brightness(treeView, {-1, 2.5f, 3});
    require(treeColor > 250 && renderer.treeStats().visible == 1 && renderer.treeStats().drawCalls == 1,
            "Visible trees must retain unlit colors and asset batching after the shadow pass");
    renderer.setShadows(false);
    renderer.render(treeView);
    require(std::abs(brightness(treeView, {-1, 2.5f, 3}) - treeColor) < 2,
            "Trees must cast shadows without receiving them");
    renderer.setShadows(true);
    renderer.render(view);
    require(std::abs(brightness(view, {-1, 0, 0}) - shadow) < 3,
            "Camera movement must not publish color instances into the tree shadow pass");
    renderer.setTreeCulling(false);
    renderer.render(view);
    require(std::abs(brightness(view, {-1, 0, 0}) - shadow) < 3 &&
            renderer.treeStats().visible == 2 && renderer.treeStats().drawCalls == 1,
            "Disabling color tree culling must preserve independently culled shadows and batching");
    renderer.setTreeShadows(false);
    renderer.render(view);
    require(std::abs(brightness(view, {-1, 0, 0}) - clear) < 3,
            "The tree shadow comparison toggle must restore clear ground");
    GLboolean depthWrite = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    require(depthWrite && !glIsEnabled(GL_POLYGON_OFFSET_FILL),
            "Tree shadow rendering must retain color depth writes and restore polygon offset");
    viewer::Renderer::checkErrors("tree shadow regression");
}

void checkAnimatedCaster() {
    auto model = scene(false);
    model.draws.pop_back();
    model.primitives.pop_back();
    viewer::ZombieLayer layer;
    viewer::AnimatedModel asset;
    asset.model.primitives.push_back(quad({{{-2, 2, 3}, {2, 2, 3}, {2, 4, 3}, {-2, 4, 3}}}, {0, 0, 1}, 0));
    viewer::Material material;
    material.doubleSided = true;
    asset.model.materials.push_back(material);
    asset.duration = 1;
    asset.frameCount = 2;
    asset.skeleton.resize(1);
    viewer::AnimatedPrimitive animation;
    for (int frame = 0; frame < 2; ++frame)
        for (const auto& vertex : asset.model.primitives.front().vertices)
            animation.frames.push_back({glm::vec4(vertex.position + glm::vec3(frame * 6, 0, 0), 1),
                                        glm::vec4(vertex.normal, 0)});
    asset.walkClip = viewer::BakedAnimationClip{"walk", 1, 2, 2, {}};
    asset.runClip = viewer::BakedAnimationClip{"running", 1, 2, 4, {}};
    for (int frame : {0, 1, 1, 0})
        for (const auto& vertex : asset.model.primitives.front().vertices)
            animation.frames.push_back({glm::vec4(vertex.position + glm::vec3(frame * 6, 0, 0), 1),
                                        glm::vec4(vertex.normal, 0)});
    animation.skinning.resize(4);
    for (auto& skin : animation.skinning) skin.weights[0] = 1;
    asset.animation.push_back(animation);
    layer.assets.push_back(asset);
    layer.instances.emplace_back();
    // A distant instance is compacted out of the shadow pass while preserving
    // the asset's live-skinning palette offsets and full color population.
    layer.instances.emplace_back();
    layer.instances.back().transform = glm::translate(glm::mat4(1), {1000, 0, 0});
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.setLighting(lighting());
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    const auto camera = cameraAt({0, 4, 2});
    renderer.render(camera, 0);
    const float idleShadow = brightness(camera, {0, 0, 0});
    renderer.render(camera, 0.5);
    const float idleLit = brightness(camera, {0, 0, 0});
    require(idleLit - idleShadow > 50, "Baked animation must move its cast shadow with the visible pose");
    layer.instances[0].walking = true;
    renderer.updateZombies(layer);
    renderer.render(camera, 0.5);
    require(std::abs(brightness(camera, {0, 0, 0}) - idleShadow) < 3, "Walk shadow must use the selected clip and local clock");
    layer.instances[0].walkSeconds = 0.5;
    renderer.updateZombies(layer);
    renderer.render(camera, 0);
    require(std::abs(brightness(camera, {0, 0, 0}) - idleLit) < 3, "Authored walking must move its shadow independently of idle time");
    layer.instances[0].running = true;
    renderer.updateZombies(layer);
    renderer.render(camera, 0.5);
    require(std::abs(brightness(camera, {0, 0, 0}) - idleShadow) < 3, "Run shadow must use its own frame range");
    layer.instances[0].walking = false;
    layer.setRagdollPalette(0, {glm::mat4(1)});
    renderer.updateZombies(layer);
    renderer.render(camera, 0.5);
    require(std::abs(brightness(camera, {0, 0, 0}) - idleShadow) < 3, "Live ragdoll skinning must cast the live pose");
    require(renderer.zombiePaletteStats().needed == 1 && renderer.zombiePaletteStats().uploaded == 1,
            "A shadow-only ragdoll must upload its palette before the caster draw");
    renderer.render(camera, 0.5);
    require(renderer.zombiePaletteStats().uploaded == 0 &&
            std::abs(brightness(camera, {0, 0, 0}) - idleShadow) < 3,
            "A retained shadow-only palette must keep casting without another upload");
    layer.setRagdollPalette(0, {glm::translate(glm::mat4(1), {6, 0, 0})});
    renderer.updateZombies(layer);
    renderer.render(camera, 0);
    require(std::abs(brightness(camera, {0, 0, 0}) - idleLit) < 3, "Ragdoll shadow must follow the bone transforms");
    require(renderer.zombieCount() == 2 && renderer.zombieVisibleCount() == 0 && renderer.zombieDrawCalls() == 0,
            "Offscreen zombie casters must shadow the ground independently of camera culling");
    viewer::Renderer::checkErrors("animated shadow regression");
}

enum class FlashlightCaster { Opaque, Masked, Blended, Zombie, Door, Tree };
void checkFlashlight(FlashlightCaster kind, bool unlit = false, const std::string& capture = {}) {
    const bool masked = kind == FlashlightCaster::Masked || kind == FlashlightCaster::Tree;
    const bool blended = kind == FlashlightCaster::Blended;
    const bool zombie = kind == FlashlightCaster::Zombie;
    const bool tree = kind == FlashlightCaster::Tree;
    const bool door = kind == FlashlightCaster::Door;
    viewer::Camera camera;
    camera.setPosition({0, 1.7f, 0});
    camera.look(0, 200); // Horizontal, looking along -Z.
    viewer::Model model;
    viewer::Material receiver;
    receiver.baseColor = {0.15f, 0.15f, 0.15f, 1};
    receiver.unlit = unlit;
    model.materials.push_back(receiver);
    model.materials.push_back(scene(masked).materials[1]);
    if (masked) {
        auto mask = scene(true);
        model.images = std::move(mask.images);
        model.textures = std::move(mask.textures);
    }
    if (blended) model.materials[1].alphaMode = "BLEND";
    model.primitives.push_back(quad({{{-3, 0, -5}, {3, 0, -5}, {3, 4, -5}, {-3, 4, -5}}}, {0, 0, 1}, 0));
    model.draws.push_back({0, glm::mat4(1), "receiver"});
    const auto caster = quad({{{-.12f, 1.45f, -1}, {.12f, 1.45f, -1}, {.12f, 1.85f, -1}, {-.12f, 1.85f, -1}}}, {0, 0, 1}, 1);
    viewer::ZombieLayer layer;
    if (zombie) {
        viewer::AnimatedModel asset;
        asset.frameCount = 2;
        asset.duration = 1;
        asset.skeleton.resize(1);
        asset.model.materials.push_back(model.materials[1]);
        auto mesh = caster;
        mesh.material = 0;
        asset.model.primitives.push_back(mesh);
        viewer::AnimatedPrimitive animation;
        for (const auto& vertex : mesh.vertices) {
            animation.frames.push_back({glm::vec4(vertex.position, 1), glm::vec4(vertex.normal, 0)});
            viewer::SkinningVertex skin;
            skin.weights[0] = 1;
            animation.skinning.push_back(skin);
        }
        const auto frame = animation.frames;
        animation.frames.insert(animation.frames.end(), frame.begin(), frame.end());
        asset.animation.push_back(animation);
        layer.assets.push_back(asset);
        layer.instances.emplace_back();
        layer.setRagdollPalette(0, {glm::mat4(1)});
    } else if (!tree) {
        model.primitives.push_back(caster);
        model.draws.push_back({1, glm::mat4(1), door ? "building_test_doors" : "caster"});
        if (door) {
            model.draws.back().detailDistance = 120;
            model.draws.back().detailGroup = "building_test";
            // The original closed shell must not block light through an opened
            // detailed door. Sun shadows intentionally retain that shell.
            model.draws.push_back({1, glm::mat4(1), "building_test_walls", 120, true, "building_test"});
        }
    }
    viewer::TreeLayer trees;
    if (tree) {
        viewer::TreeAsset asset;
        asset.model.materials.push_back(model.materials[1]);
        asset.model.materials[0].unlit = true;
        asset.model.images = model.images;
        asset.model.textures = model.textures;
        auto mesh = caster;
        mesh.material = 0;
        asset.model.primitives.push_back(mesh);
        asset.model.draws.push_back({0, glm::mat4(1), "tree"});
        trees.assets.push_back(std::move(asset));
        viewer::TreeInstance instance;
        instance.boundsMin = {-.12f, 1.45f, -1};
        instance.boundsMax = {.12f, 1.85f, -1};
        trees.instances.push_back(instance);
    }
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    std::unique_ptr<viewer::DoorLayer> doors;
    if (door) doors = std::make_unique<viewer::DoorLayer>(viewer::prepareDoorPanels(model), physics);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR, tree ? &trees : nullptr, zombie ? &layer : nullptr);
    renderer.setNight(true);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    // The shadow lies to the left of the camera-visible caster because the
    // lamp is held to the right. Test a receiver the camera sees directly.
    const glm::vec3 opaque{-1.0f, 1.7f, -5}, hole{-1.6f, 1.7f, -5};
    // The initial baseline also verifies that flashlight shadows default off.
    renderer.render(camera);
    const auto clear = brightness(camera, opaque), clearHole = brightness(camera, hole);
    if (!capture.empty()) renderer.writeScreenshot(capture + ".off.ppm");
    renderer.setFlashlightShadows(true);
    renderer.render(camera);
    const auto shadow = brightness(camera, opaque);
    if (!capture.empty()) renderer.writeScreenshot(capture + ".on.ppm");
    if (blended) require(std::abs(clear - shadow) < 3, "Blended surfaces must not cast flashlight shadows");
    else {
        require(clear - shadow > 45, "Flashlight must cast visible offset shadows, including onto unlit receivers");
        if (masked) require(std::abs(clearHole - brightness(camera, hole)) < 3,
                            "Transparent mask texels must leave flashlight shadows open");
        else require(clearHole - brightness(camera, hole) > 25, "Opaque caster must shadow both halves");
    }
    if (zombie) {
        require(renderer.zombiePaletteStats().needed == 1 && renderer.zombiePaletteStats().uploaded == 0,
                "Camera and flashlight must share one retained ragdoll palette");
        auto twilight = viewer::dayNightLighting(12);
        twilight.nightBlend = .5f;
        renderer.setDayNightLighting(twilight);
        renderer.render(camera);
        require(renderer.zombieRenderStats().shadowTriangles == 2 && renderer.zombieRenderStats().flashlightTriangles == 2 &&
                renderer.zombiePaletteStats().needed == 1 && renderer.zombiePaletteStats().uploaded == 0,
                "Both lights and the camera must share a single palette during twilight");
        renderer.setNight(true);
        renderer.setZombieDrawDistance(0.01f);
        renderer.render(camera);
        require(renderer.zombieVisibleCount() == 0 && renderer.zombieRenderStats().flashlightTriangles == 2 &&
                renderer.zombiePaletteStats().needed == 1,
                "A camera-culled ragdoll must remain a flashlight caster");
        require(std::abs(shadow - brightness(camera, opaque)) < 3, "Shadow-only ragdoll must preserve its live pose");
        renderer.setZombieShadows(false);
        renderer.render(camera);
        require(renderer.zombiePaletteStats().needed == 0 && std::abs(clear - brightness(camera, opaque)) < 3,
                "Disabling zombie shadows must omit flashlight-only palettes");
    }
    if (tree) {
        renderer.setTreeShadows(false);
        renderer.render(camera);
        require(std::abs(clear - brightness(camera, opaque)) < 3, "Tree shadow toggle must include flashlight casting");
    }
    if (door) {
        const auto& panel = doors->panel(0);
        const auto freeEdge = panel.center + glm::cross(glm::vec3(0, 1, 0), panel.outward) * (panel.halfExtents.x * .9f);
        for (int step = 0; step < 180; ++step) {
            const auto point = glm::vec3(doors->transform(0) * glm::inverse(panel.closedTransform) * glm::vec4(freeEdge, 1));
            doors->applyForce(0, glm::cross(glm::vec3(0, -1, 0), point - panel.hinge) * 100.0f, point);
            physics->step(1.0f / 60);
        }
        require(doors->angle(0) > glm::radians(100.0f), "Fixture door must reach its open stop");
        renderer.updateDoors(*doors);
        renderer.render(camera);
        // The open panel now covers the first sample from the camera. Use
        // the other originally shadowed receiver, which remains camera-visible.
        require(std::abs(clearHole - brightness(camera, hole)) < 3,
                "Flashlight shadow must follow an opened door, excluding the original closed shell");
    }
    renderer.setShadows(false);
    renderer.render(camera);
    require(std::abs((door ? clearHole : clear) - brightness(camera, door ? hole : opaque)) < 3,
            "Global shadow toggle must disable flashlight shadows");
    require(!glIsEnabled(GL_POLYGON_OFFSET_FILL), "Flashlight must restore polygon offset state");
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    require(viewport[2] == width && viewport[3] == height, "Flashlight must restore the color viewport");
    viewer::Renderer::checkErrors("flashlight shadow regression");
}

void checkFlashlightRange() {
    viewer::Camera camera;
    camera.setPosition({0, 1.7f, 0});
    camera.look(0, 200);
    const auto light = viewer::flashlightView(camera);
    const glm::vec3 casterCenter{0, 1.7f, -1};
    std::array<float, 3> ratios{};
    std::size_t sample = 0;
    for (const float distance : {18.0f, 22.5f, 28.0f}) {
        // Follow one blocked light ray to receivers before, within, and beyond
        // the fade band. Their projected shadows stay visible beside the caster.
        const auto point = light.position + glm::normalize(casterCenter - light.position) * distance;
        viewer::Model model;
        viewer::Material material;
        material.baseColor = {.4f, .4f, .4f, 1};
        material.doubleSided = true;
        model.materials.push_back(material);
        model.primitives.push_back(quad({{point + glm::vec3(-2, -2, 0), point + glm::vec3(2, -2, 0),
                                          point + glm::vec3(2, 2, 0), point + glm::vec3(-2, 2, 0)}}, {0, 0, 1}, 0));
        model.primitives.push_back(quad({{{-.12f, 1.45f, -1}, {.12f, 1.45f, -1}, {.12f, 1.85f, -1}, {-.12f, 1.85f, -1}}}, {0, 0, 1}, 0));
        model.draws = {{0, glm::mat4(1), "receiver"}, {1, glm::mat4(1), "caster"}};
        viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
        renderer.setNight(true);
        renderer.setHaze(false);
        renderer.setGroundFog(false);
        renderer.resize(width, height);
        renderer.setFlashlightShadows(false);
        renderer.render(camera);
        const float clear = brightness(camera, point);
        renderer.setFlashlightShadows(true);
        renderer.render(camera);
        ratios[sample++] = brightness(camera, point) / clear;
        viewer::Renderer::checkErrors("flashlight range regression");
    }
    require(ratios[0] < .3f && ratios[1] > .5f && ratios[1] < .95f && std::abs(ratios[2] - 1) < .02f,
            "Flashlight shadows must fade between 20 and 25 m and stop beyond 25 m");
}
}

int main(int argc, char** argv) {
    if (!glfwInit()) { std::cout << "SKIP: no display\n"; return 77; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Shadow regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        checkScene(false, false);
        checkScene(true, false);
        checkScene(false, true);
        checkScene(false, false, true);
        checkTreeCaster();
        checkAnimatedCaster();
        checkMovingSun();
        checkFlashlight(FlashlightCaster::Opaque, false, argc > 1 ? argv[1] : "");
        checkFlashlight(FlashlightCaster::Masked);
        checkFlashlight(FlashlightCaster::Opaque, true);
        checkFlashlight(FlashlightCaster::Blended);
        checkFlashlight(FlashlightCaster::Zombie);
        checkFlashlight(FlashlightCaster::Tree);
        checkFlashlight(FlashlightCaster::Door);
        checkFlashlightRange();
        std::cout << "Shadow rendering tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
