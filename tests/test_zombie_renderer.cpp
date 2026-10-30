#include "zombie_shooting_fixture.hpp"
#include "renderer.hpp"
#include "zombie_animation.hpp"
#include "zombie_lod.hpp"
#include "box_layer.hpp"
#include "physics_world.hpp"
#include "collision_world.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int width = 640, height = 480;
constexpr std::size_t population = 24;
const std::array<glm::vec3, 4> colors{{{1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 1, 0}}};

glm::vec3 markerCenter(std::size_t primitive, std::size_t marker) {
    return {marker == 0 ? -0.38f : 0.38f, 0.8f * static_cast<float>(primitive), 0};
}

// A marker is a square rigidly attached to one bone. Two independently moving
// markers make a shifted palette visibly different from the intended pose.
viewer::AnimatedModel makeAsset(std::size_t primitiveCount, std::size_t boneCount) {
    viewer::AnimatedModel asset;
    asset.frameCount = 2;
    asset.duration = 1;
    asset.skeleton.resize(boneCount);
    viewer::Material material;
    material.unlit = true;
    material.doubleSided = true;
    asset.model.materials.push_back(material);
    for (std::size_t p = 0; p < primitiveCount; ++p) {
        viewer::Primitive primitive;
        viewer::AnimatedPrimitive animation;
        primitive.material = 0;
        for (std::size_t marker = 0; marker < 2; ++marker) {
            const auto base = static_cast<std::uint32_t>(primitive.vertices.size());
            for (const glm::vec2 corner : {glm::vec2(-1, -1), glm::vec2(1, -1),
                                          glm::vec2(1, 1), glm::vec2(-1, 1)}) {
                viewer::Vertex vertex;
                vertex.position = markerCenter(p, marker) + glm::vec3(corner * 0.12f, 0);
                vertex.normal = {0, 0, 1};
                vertex.color = glm::vec4(colors[p * 2 + marker], 1);
                primitive.vertices.push_back(vertex);
                viewer::SkinningVertex skin;
                // Exercise both groups of four joint/weight attributes.
                const std::size_t influence = marker == 0 ? 0 : 4;
                skin.joints[influence] = static_cast<std::uint32_t>(marker == 0 ? 0 : boneCount - 1);
                skin.weights[influence] = 1;
                animation.skinning.push_back(skin);
            }
            for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u})
                primitive.indices.push_back(base + index);
        }
        for (std::size_t frame = 0; frame < 2; ++frame) {
            for (const auto& vertex : primitive.vertices) {
                animation.frames.push_back({
                    glm::vec4(vertex.position + glm::vec3(0, frame == 0 ? 0.75f : 1.1f, 0), 1),
                    glm::vec4(vertex.normal, 0)});
            }
        }
        asset.model.primitives.push_back(std::move(primitive));
        asset.animation.push_back(std::move(animation));
    }
    asset.walkClip = viewer::BakedAnimationClip{"walk", 1.5f, 3, 2, {}};
    asset.runClip = viewer::BakedAnimationClip{"running", 2, 2, 5, {}};
    for (std::size_t p = 0; p < primitiveCount; ++p)
        for (const float height : {0.2f, 0.6f, 1.0f, -0.4f, 0.3f})
            for (const auto& vertex : asset.model.primitives[p].vertices)
                asset.animation[p].frames.push_back({glm::vec4(vertex.position + glm::vec3(0, height, 0), 1),
                                                     glm::vec4(vertex.normal, 0)});
    return asset;
}

glm::vec3 instancePosition(std::size_t index) {
    return {-6.25f + 2.5f * static_cast<float>(index % 6),
            -4.5f + 3.0f * static_cast<float>(index / 6), 0};
}

float boneHeight(std::size_t index, std::size_t marker, int pose) {
    const float variation = static_cast<float>(index / 3);
    return marker == 0 ? 0.22f + 0.035f * variation + 0.20f * static_cast<float>(pose)
                       : -0.27f - 0.025f * variation - 0.17f * static_cast<float>(pose);
}

void checkMarkers(const viewer::Camera& camera, const viewer::ZombieLayer& layer,
                  std::size_t primitiveCount, std::size_t activated, int pose) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    viewer::Renderer::checkErrors("zombie regression readback");
    const auto projectionView = camera.projection(static_cast<float>(width) / height) * camera.view();
    for (std::size_t index = 0; index < population; ++index) {
        for (std::size_t p = 0; p < primitiveCount; ++p) {
            for (std::size_t marker = 0; marker < 2; ++marker) {
                // Compute the expected geometry directly, independently of GPU
                // palette packing, asset batch order, and attribute layouts.
                auto center = markerCenter(p, marker);
                if (index >= activated) center.y += layer.instances[index].phase == 0 ? 0.75f : 1.1f;
                else if ((index / 3) % 3 == 0) center.y += boneHeight(index, marker, pose);
                // The first pose interpolates each clip's last frame back to
                // its own first frame; the second interpolates inside the clip.
                else if ((index / 3) % 3 == 1) center.y += pose == 0 ? 0.4f : 0.7f;
                else center.y += pose == 0 ? -0.225f : -0.05f;
                center = glm::vec3(layer.instances[index].transform * glm::vec4(center, 1));
                const glm::vec4 clip = projectionView * glm::vec4(center, 1);
                const glm::vec2 screen = (glm::vec2(clip) / clip.w * 0.5f + 0.5f) *
                                         glm::vec2(width, height);
                const int x = static_cast<int>(std::floor(screen.x));
                const int y = static_cast<int>(std::floor(screen.y));
                if (x < 1 || x >= width - 1 || y < 1 || y >= height - 1)
                    throw std::runtime_error("Test marker is outside the camera view");
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const auto offset = static_cast<std::size_t>(((y + dy) * width + x + dx) * 3);
                        for (std::size_t channel = 0; channel < 3; ++channel) {
                            const int expected = colors[p * 2 + marker][static_cast<int>(channel)] > 0 ? 255 : 0;
                            if (std::abs(static_cast<int>(pixels[offset + channel]) - expected) > 2)
                                throw std::runtime_error("Wrong rendered marker: primitives=" +
                                    std::to_string(primitiveCount) + ", activated=" + std::to_string(activated) +
                                    ", pose=" + std::to_string(pose) + ", instance=" + std::to_string(index) +
                                    ", primitive=" + std::to_string(p) + ", bone marker=" + std::to_string(marker));
                        }
                    }
                }
            }
        }
    }
}

void renderCrowd(std::size_t primitiveCount, const std::filesystem::path& screenshot) {
    viewer::ZombieLayer layer;
    for (std::size_t bones : {2u, 3u, 4u}) layer.assets.push_back(makeAsset(primitiveCount, bones));
    for (std::size_t index = 0; index < population; ++index) {
        viewer::ZombieInstance instance;
        instance.asset = index % layer.assets.size();
        instance.transform = glm::translate(glm::mat4(1), instancePosition(index));
        instance.phase = index % 2 == 0 ? 0 : 0.5f;
        layer.instances.push_back(instance);
    }
    viewer::Camera camera;
    camera.setPosition({0, 0.5f, 14});
    camera.look(0, 200); // Default pitch is -20 degrees; look straight at the markers.
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.setZombieDistanceDetail(false);
    renderer.setHaze(false); // Marker colors test geometry and palette lookups.
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    // Mix authored walk/run clips and ragdoll palettes with idle; cross the palette
    // boundary for every asset, then exceed the reported dozen activations.
    // Moving each pose twice detects retained palettes and stale transforms.
    for (std::size_t activated = 0; activated <= 18; ++activated) {
        for (int pose = 0; pose < 2; ++pose) {
            for (std::size_t index = 0; index < activated; ++index) {
                auto& instance = layer.instances[index];
                instance.ragdoll = (index / 3) % 3 == 0;
                instance.walking = !instance.ragdoll;
                instance.running = (index / 3) % 3 == 2;
                instance.phase = 0;
                layer.markRenderChanged(index);
                instance.walkSeconds = instance.running ? (pose == 0 ? 1.75 : 0.5) : (pose == 0 ? 1.375 : 0.625);
                instance.transform = glm::translate(glm::mat4(1), instancePosition(index) +
                    glm::vec3(0.08f * static_cast<float>(pose), 0, 0));
                if (instance.ragdoll) {
                    std::vector<glm::mat4> palette(layer.assets[instance.asset].skeleton.size(), glm::mat4(1));
                    palette.front() = glm::translate(glm::mat4(1), {0, boneHeight(index, 0, pose), 0});
                    palette.back() = glm::translate(glm::mat4(1), {0, boneHeight(index, 1, pose), 0});
                    layer.setRagdollPalette(index, std::move(palette));
                }
            }
            renderer.updateZombies(layer);
            renderer.render(camera, 0);
            viewer::Renderer::checkErrors("zombie regression render");
            checkMarkers(camera, layer, primitiveCount, activated, pose);
            renderer.updateZombies(layer);
            renderer.render(camera, 0);
            if (renderer.zombiePaletteStats().uploaded != 0)
                throw std::runtime_error("Unchanged palettes must remain on the GPU across both passes");
            checkMarkers(camera, layer, primitiveCount, activated, pose);
            if (renderer.zombieCount() != population || renderer.zombieDrawCalls() != 3 * primitiveCount)
                throw std::runtime_error("Zombie rendering must remain batched by asset and primitive");
        }
    }
    if (!screenshot.empty()) renderer.writeScreenshot(screenshot);
}

std::vector<unsigned char> colorPixels() {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    viewer::Renderer::checkErrors("frustum culling comparison readback");
    return pixels;
}

void renderHealthyAndWoundedBatches() {
    auto asset = makeAsset(2, 2);
    // Supply reference vertices for replicated masks without anatomical caps
    // on these deliberately planar marker squares.
    asset.ragdollBones[std::size_t(viewer::RagdollPart::torso)] = 0;
    asset.model.materials[0].unlit = false;
    asset.model.materials[0].baseColor = {.8f, .9f, .7f, 1};
    viewer::ZombieLayer layer;
    layer.assets.push_back(asset);
    for (const float x : {-2.f, 0.f, 2.f}) {
        viewer::ZombieInstance instance;
        instance.transform = glm::translate(glm::mat4(1), {x, 0, 0});
        layer.instances.push_back(instance);
    }
    layer.setRagdollPalette(2, std::vector<glm::mat4>(2, glm::translate(glm::mat4(1), {0, .75f, 0})));

    viewer::ZombieLayer reference;
    reference.assets.push_back(asset);
    // Independent pixel oracle: physically omit the red square's two index
    // triangles for the middle actor, with no wound mask or wound shader.
    auto missingSquare = asset;
    auto& indices = missingSquare.model.primitives[0].indices;
    indices.erase(indices.begin(), indices.begin()+6);
    reference.assets.push_back(std::move(missingSquare));
    reference.instances = layer.instances;
    reference.instances[1].asset = 1;

    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    boxes.add({3.3f, .8f, .4f}, {.8f, .8f, .6f});
    viewer::Camera camera;
    camera.setPosition({0, .8f, 10});
    camera.setOrientation(-90, 0);
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    viewer::Renderer oracle(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &reference);
    for (auto* current : {&renderer, &oracle}) {
        current->resize(width, height);
        current->setZombieDistanceDetail(false);
        current->setHaze(false);
        current->setGroundFog(false);
        current->setShadows(false);
        current->setFlashlight(false);
        // Boxes draw after zombies, exercising the restored program/material
        // state as well as the lit marker materials in both zombie groups.
        current->updateBoxes(boxes);
    }
    const auto requirePixels = [](const std::vector<unsigned char>& actual,
                                  const std::vector<unsigned char>& expected) {
        if (actual.size() != expected.size()) throw std::runtime_error("Pixel comparison size mismatch");
        for (std::size_t i=0; i<actual.size(); ++i)
            if (std::abs(int(actual[i])-int(expected[i])) > 2)
                throw std::runtime_error("Healthy/wounded grouping changed marker holes, lighting or the following prop");
    };
    const std::vector<std::uint8_t> intactMask(2, 0), removedSquare{0x0f, 0};
    std::vector<unsigned char> previousLighting;
    for (const bool night : {false, true, false}) {
        renderer.setNight(night);
        oracle.setNight(night);
        layer.setWoundMask(1, {});
        layer.instances[1].transform = glm::mat4(1);
        layer.markRenderChanged(1);
        reference.instances[1].transform = glm::mat4(1);
        reference.markRenderChanged(1);
        renderer.updateZombies(layer);
        renderer.render(camera, 0);
        const auto healthy = colorPixels();
        if (renderer.zombieDrawCalls() != 2 || renderer.zombieVisibleCount() != 3 ||
            renderer.zombieRenderStats().cameraLevels[0] != 3)
            throw std::runtime_error("Intact living actors and a live-skinned ragdoll must share one batch per primitive");
        if (!previousLighting.empty() && healthy == previousLighting)
            throw std::runtime_error("Lit marker and prop fixture must respond to day/night changes");
        previousLighting = healthy;

        layer.setWoundMask(1, intactMask);
        renderer.updateZombies(layer);
        renderer.render(camera, 0);
        requirePixels(colorPixels(), healthy);
        if (renderer.zombieDrawCalls() != 4 || renderer.zombieVisibleCount() != 3)
            throw std::runtime_error("One wounded actor must split its two primitives from same-asset healthy actors");

        layer.setWoundMask(1, removedSquare);
        renderer.updateZombies(layer);
        oracle.updateZombies(reference);
        oracle.render(camera, 0);
        const auto expectedHole = colorPixels();
        if (expectedHole == healthy) throw std::runtime_error("Missing-square oracle must visibly remove a marker");
        for (int repeat=0; repeat<2; ++repeat) {
            renderer.render(camera, 0);
            requirePixels(colorPixels(), expectedHole);
            if (renderer.zombieDrawCalls() != 4 || renderer.zombieVisibleCount() != 3)
                throw std::runtime_error("Repeated mixed draws must retain both shader groups and every visible actor");
        }

        layer.instances[1].transform = glm::translate(glm::mat4(1), {1000, 0, 0});
        reference.instances[1].transform = layer.instances[1].transform;
        layer.markRenderChanged(1);
        reference.markRenderChanged(1);
        renderer.updateZombies(layer);
        oracle.updateZombies(reference);
        oracle.render(camera, 0);
        const auto offscreen = colorPixels();
        renderer.render(camera, 0);
        requirePixels(colorPixels(), offscreen);
        if (renderer.zombieDrawCalls() != 2 || renderer.zombieVisibleCount() != 2)
            throw std::runtime_error("An offscreen wound must leave the visible healthy batch unsplit");

        layer.setWoundMask(1, {});
        layer.instances[1].transform = glm::mat4(1);
        layer.markRenderChanged(1);
        renderer.updateZombies(layer);
        renderer.render(camera, 0);
        requirePixels(colorPixels(), healthy);
        if (renderer.zombieDrawCalls() != 2)
            throw std::runtime_error("Removing the wound must merge the camera draw batches again");
    }
    viewer::Renderer::checkErrors("healthy and wounded shader grouping");
}

void renderFrustumCulling() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(2, 3));
    const std::array<glm::vec3, 11> positions{{
        {100, 0, 0}, {0, 0, 0}, {100, 0, 0}, {-2, 0, 0}, {0, 0, 30},
        {2, 0, 0}, {11.3f, 0, 0}, {12, 0, 0}, {0, 0, -20000}, {0, 100, 0}, {0, -9.2f, 0}
    }};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        viewer::ZombieInstance instance;
        instance.transform = glm::translate(glm::mat4(1), positions[i]);
        instance.ragdoll = i == 0 || i == 1 || i == 7;
        instance.walking = i == 2 || i == 3 || i == 4 || i == 5;
        instance.running = i == 4 || i == 5;
        instance.walkSeconds = 0.625;
        if (i == 10) instance.phase = 0.25f; // Idle frame one extends above the bind bounds.
        if (instance.ragdoll)
            instance.ragdollBones.assign(3, glm::translate(glm::mat4(1), {i == 7 ? -3.0f : 0.0f, 0.2f, 0}));
        if (i == 6) instance.transform = glm::scale(instance.transform, {2, 1, 1});
        layer.instances.push_back(instance);
    }
    viewer::Camera camera;
    camera.setPosition({0, 0.5f, 14});
    camera.look(0, 200);
    const viewer::Frustum frustum(camera.projection(static_cast<float>(width) / height) * camera.view());
    if (frustum.intersects(positions[6], positions[6]) || frustum.intersects(positions[7], positions[7]))
        throw std::runtime_error("Edge and ragdoll fixture origins must be outside the frustum");
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.setZombieDistanceDetail(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    renderer.updateZombies(layer);
    // A culled ragdoll precedes a visible one in the palette. Compaction must
    // retain palette offsets, clip metadata, and every primitive's attributes.
    for (const bool shadows : {true, false}) {
        renderer.setShadows(shadows);
        renderer.setZombieCulling(false);
        renderer.render(camera, 0.25);
        const auto reference = colorPixels();
        if (renderer.zombieVisibleCount() != positions.size())
            throw std::runtime_error("Disabling culling must submit the entire crowd");
        renderer.setZombieCulling(true);
        renderer.render(camera, 0.25);
        if (renderer.zombieCount() != positions.size() || renderer.zombieVisibleCount() != 6 ||
            renderer.zombieDrawCalls() != 2)
            throw std::runtime_error("Cull offscreen zombies while retaining crossing bounds and live ragdoll bones");
        if (colorPixels() != reference)
            throw std::runtime_error("Frustum culling changed visible animation or skinning");
    }
    layer.instances[2].transform = glm::translate(glm::mat4(1), {4, 0, 0});
    layer.markRenderChanged(2);
    renderer.updateZombies(layer);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibleCount() != 7)
        throw std::runtime_error("A moving zombie must reenter the camera batch");
    // Move the camera so every batch is empty, without changing the population.
    camera.setPosition({0, 0, 50});
    camera.look(1800, 0);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibleCount() != 0 || renderer.zombieDrawCalls() != 0)
        throw std::runtime_error("Empty camera batches must submit no draws and reset visible statistics");
    renderer.setZombieCulling(false);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibleCount() != positions.size() || renderer.zombieDrawCalls() != 2)
        throw std::runtime_error("Culling toggle must restore full batches after an empty view");
}

void paletteVisibilityAndDeferredChanges() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    for (const auto position : {glm::vec3(0), glm::vec3(0, 0, 30), glm::vec3(10000, 0, 0)}) {
        viewer::ZombieInstance instance;
        instance.transform = glm::translate(glm::mat4(1), position);
        layer.instances.push_back(instance);
        layer.setRagdollPalette(layer.instances.size() - 1, std::vector<glm::mat4>(2, glm::mat4(1)));
    }
    viewer::Camera camera;
    camera.setPosition({0, 0.5f, 14});
    camera.look(0, 200);
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.setZombieDistanceDetail(false);
    renderer.resize(width, height);
    renderer.render(camera);
    auto stats = renderer.zombiePaletteStats();
    if (renderer.zombieVisibleCount() != 1 || stats.needed != 2 || stats.uploaded != 2 || stats.resident != 2 ||
        stats.uploadCalls != 1 || stats.uploadBytes != 4 * sizeof(glm::mat4))
        throw std::runtime_error("Camera/shadow union must upload offscreen casters once, and exclude distant corpses");
    renderer.render(camera);
    if (renderer.zombiePaletteStats().uploaded != 0)
        throw std::runtime_error("Settled camera and shadow palettes must remain resident");
    renderer.setShadows(false);
    layer.setRagdollPalette(1, std::vector<glm::mat4>(2, glm::translate(glm::mat4(1), {0, 0.25f, 0})));
    renderer.updateZombies(layer);
    renderer.render(camera);
    if (renderer.zombiePaletteStats().needed != 1 || renderer.zombiePaletteStats().uploaded != 0)
        throw std::runtime_error("A changed offscreen non-caster must defer its palette upload");
    renderer.setNight(true);
    renderer.setShadows(true);
    renderer.render(camera);
    if (renderer.zombiePaletteStats().uploaded != 0 || renderer.zombiePaletteStats().needed != 1)
        throw std::runtime_error("Night must not request unused sunlight caster palettes");
    renderer.setNight(false);
    renderer.render(camera);
    if (renderer.zombiePaletteStats().uploaded != 1 || renderer.zombiePaletteStats().needed != 2)
        throw std::runtime_error("Reenabled shadows must upload only the deferred caster change");
    renderer.setZombieCulling(false);
    renderer.render(camera);
    if (renderer.zombiePaletteStats().uploaded != 1 || renderer.zombiePaletteStats().resident != 3)
        throw std::runtime_error("Disabling camera culling must request the previously unneeded corpse");
    layer.removeRagdoll(0);
    renderer.updateZombies(layer);
    renderer.render(camera);
    if (renderer.zombieCount() != 2 || renderer.zombieVisibleCount() != 2 ||
        renderer.zombiePaletteStats().resident != 2 || renderer.zombiePaletteStats().uploaded != 0)
        throw std::runtime_error("Removal must release its palette slot and exclude its stable source ID");
}

void renderGroupMovementAndPoseChanges() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 3));
    layer.assets.push_back(makeAsset(1, 2));
    for (std::size_t i = 0; i < 40; ++i) {
        viewer::ZombieInstance instance;
        instance.asset = i % 2;
        instance.transform = glm::translate(glm::mat4(1), {100 + float(i % 5) * 0.2f, 0, 0});
        layer.instances.push_back(instance);
    }
    viewer::Camera camera;
    camera.setPosition({0, 0.5f, 14});
    camera.look(0, 200);
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.setZombieDistanceDetail(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.setShadows(false);
    renderer.resize(width, height);
    renderer.render(camera, 0.25);
    if (!renderer.zombieUpdateStats().full || renderer.zombieUpdateStats().instances != 40)
        throw std::runtime_error("Initial upload must initialize the entire population once");
    auto stats = renderer.zombieVisibilityStats();
    if (stats.groups != 1 || stats.blocks != 1 || stats.cameraBlockTests != 1 ||
        stats.visitedGroups != 0 || stats.cameraGroupTests != 0 || stats.cameraInstanceTests != 0 ||
        renderer.zombieVisibleCount() != 0)
        throw std::runtime_error("An offscreen block must reject all asset variants before visiting its groups");
    layer.instances[1].transform = glm::translate(glm::mat4(1), {1, 0, 0});
    layer.markRenderChanged(1);
    layer.instances[2].transform = glm::translate(glm::mat4(1), {2, 0, 0});
    layer.markRenderChanged(2);
    renderer.updateZombies(layer);
    if (renderer.zombieUpdateStats().instances != 2 || renderer.zombieUpdateStats().bounds != 2 || renderer.zombieUpdateStats().full)
        throw std::runtime_error("Moving two instances must prepare only those IDs and bounds");
    renderer.render(camera, 0.25);
    stats = renderer.zombieVisibilityStats();
    if (stats.groups != 2 || stats.refittedGroups != 2 || stats.cameraInstanceTests != 0 ||
        renderer.zombieVisibleCount() != 2)
        throw std::runtime_error("Cell crossings must refit both groups and accept fully visible groups without member tests");
    layer.instances[3].transform = glm::translate(glm::mat4(1), {12, 0, 0});
    layer.markRenderChanged(3);
    renderer.updateZombies(layer);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibleCount() != 2 || renderer.zombieVisibilityStats().cameraInstanceTests != 3)
        throw std::runtime_error("Boundary groups must retain original per-instance culling");
    layer.setRagdollPalette(3, std::vector<glm::mat4>(2, glm::translate(glm::mat4(1), {-3, 0, 0})));
    renderer.updateZombies(layer);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibleCount() != 3 || renderer.zombieVisibilityStats().refittedGroups != 1)
        throw std::runtime_error("Ragdoll geometry entering view without changing cells must refit the group");
    const auto grouped = colorPixels();
    renderer.setZombieCulling(false);
    renderer.render(camera, 0.25);
    if (colorPixels() != grouped)
        throw std::runtime_error("Group culling changed rendered animation, palettes, or stable asset ordering");
    renderer.setZombieCulling(true);
    renderer.updateZombies(layer);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibilityStats().refittedGroups != 0 || renderer.zombiePaletteStats().uploaded != 0)
        throw std::runtime_error("Unchanged settled poses must retain group bounds and GPU palettes");
    if (renderer.zombieUpdateStats().instances != 0 || renderer.zombieUpdateStats().bounds != 0)
        throw std::runtime_error("Unchanged instances must skip render preparation entirely");
    // An externally driven pose change exercises the same revision publication
    // used by physics wake-up, expanding the bounds and moving their center.
    layer.setRagdollPalette(3, std::vector<glm::mat4>(2, glm::translate(glm::mat4(1), {50, 0, 0})));
    renderer.updateZombies(layer);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibleCount() != 2)
        throw std::runtime_error("Changed corpse pose must update membership and stop drawing its old geometry");
    layer.removeRagdoll(3);
    for (const std::size_t id : {1u, 2u}) {
        layer.instances[id].transform = glm::translate(glm::mat4(1), {102, 0, 0});
        layer.markRenderChanged(id);
    }
    renderer.updateZombies(layer);
    renderer.render(camera, 0.25);
    if (renderer.zombieVisibleCount() != 0 || renderer.zombieCount() != 39 ||
        renderer.zombieVisibilityStats().groups != 1 || renderer.zombieVisibilityStats().cameraInstanceTests != 0)
        throw std::runtime_error("Removal and movement must release empty groups and repair swapped membership");
    viewer::Renderer::checkErrors("grouped zombie rendering regression");
}

void renderIncrementalAndFullRefresh() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    layer.instances.resize(2);
    layer.instances[1].transform = glm::translate(glm::mat4(1), {2, 0, 0});
    viewer::Camera camera;
    camera.setPosition({0, .8f, 10});
    camera.look(0, 200);
    viewer::Renderer first(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    viewer::Renderer second(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    for (auto* renderer : {&first, &second}) {
        renderer->resize(width, height);
        renderer->setHaze(false);
        renderer->setGroundFog(false);
        renderer->setShadows(false);
    }
    first.updateZombies(layer);
    first.render(camera, 0);
    const auto idle = colorPixels();
    first.render(camera, .5);
    if (colorPixels() == idle || first.zombieUpdateStats().instances != 0)
        throw std::runtime_error("Idle animation must advance with zero instance preparation");
    layer.instances[0].walking = true;
    layer.instances[0].walkSeconds = .625;
    layer.markRenderChanged(0, false);
    first.updateZombies(layer);
    if (first.zombieUpdateStats().instances != 1 || first.zombieUpdateStats().bounds != 0)
        throw std::runtime_error("Animation state must update staging without recomputing conservative bounds");
    first.render(camera, .25);
    const auto incremental = colorPixels();
    second.updateZombies(layer);
    if (!second.zombieUpdateStats().full) throw std::runtime_error("A consumer that missed changes must refresh");
    second.render(camera, .25);
    if (colorPixels() != incremental) throw std::runtime_error("Incremental and full preparation must produce identical poses");
    first.updateZombies(layer);
    if (first.zombieUpdateStats().instances != 0) throw std::runtime_error("Other consumers must not create false updates");
    layer.instances[0].phase = .4f;
    layer.instances[0].running = true;
    layer.instances[0].walkSeconds = 1.75;
    layer.instances[0].transform = glm::translate(glm::mat4(1), {-2, 0, 0});
    layer.markRenderChanged(0);
    first.updateZombies(layer);
    first.render(camera, .25);
    const auto moved = colorPixels();
    layer.invalidateRenderData();
    first.updateZombies(layer);
    first.render(camera, .25);
    if (colorPixels() != moved || first.zombieUpdateStats().instances != 2)
        throw std::runtime_error("Bulk invalidation must match incremental transform and clip changes");
    // Replace the borrowed layer at the same address, preserving GPU topology.
    viewer::ZombieLayer replacement;
    replacement.assets.push_back(makeAsset(1, 2));
    replacement.instances.resize(2);
    replacement.instances[0].transform = glm::translate(glm::mat4(1), {100, 0, 0});
    replacement.instances[1].transform = glm::translate(glm::mat4(1), {-1, 0, 0});
    replacement.instances[1].walking = replacement.instances[1].running = true;
    replacement.instances[1].phase = .125f;
    replacement.instances[1].walkSeconds = 1;
    layer = std::move(replacement);
    first.updateZombies(layer);
    first.render(camera, .25);
    if (!first.zombieUpdateStats().full || first.zombieVisibleCount() != 1)
        throw std::runtime_error("Layer replacement must discard cached placements at the same address");
    if (layer.instances[1].displayedAnimationFrame != 1.25f || layer.instances[1].displayedAnimationClip != 2)
        throw std::runtime_error("Layer replacement must also replace cached clip, clock and phase at the same address");
    viewer::Renderer::checkErrors("incremental zombie render updates");
}

void renderHiddenLocomotionChanges() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    layer.instances.emplace_back();
    auto& instance = layer.instances.front();
    instance.walking = true;
    instance.walkSeconds = 1.375;
    viewer::Camera camera;
    camera.setPosition({0, .8f, 10});
    camera.setOrientation(-90, 0);
    const viewer::Model scene;
    viewer::Renderer renderer(scene, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(width, height);
    renderer.setZombieDistanceDetail(false);
    renderer.setShadows(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.render(camera, .125);
    const auto walking = colorPixels();
    if (instance.displayedAnimationFrame != 2.75f || instance.displayedAnimationClip != 1)
        throw std::runtime_error("Displayed walk pose must use locomotion time, independently of idle time");
    instance.transform = glm::translate(glm::mat4(1), {100, 0, 0});
    layer.markRenderChanged(0);
    renderer.updateZombies(layer);
    renderer.render(camera, .125);
    if (renderer.zombieVisibleCount() != 0 || instance.displayedAnimationFrame >= 0 || instance.displayedAnimationClip != 1 ||
        renderer.zombieRenderStats().poseRetirements != 1 || renderer.zombieRenderStats().posePublications != 0)
        throw std::runtime_error("Hiding a walker must retire its previously displayed pose");
    instance.running = true;
    instance.walkSeconds = 1.75;
    layer.markRenderChanged(0, false);
    renderer.updateZombies(layer);
    renderer.render(camera, .125);
    if (renderer.zombieVisibleCount() != 0 || instance.displayedAnimationFrame >= 0 || instance.displayedAnimationClip != 1 ||
        renderer.zombieRenderStats().poseRetirements != 0 || renderer.zombieRenderStats().posePublications != 0)
        throw std::runtime_error("Hidden locomotion changes must not publish an undrawn camera pose");
    // Reveal through camera movement alone: an instance update on reentry
    // would mask a stale animation cache left by the bounds=false notification.
    camera.setPosition({100, .8f, 10});
    renderer.render(camera, .125);
    if (renderer.zombieVisibleCount() != 1 || instance.displayedAnimationFrame != 1.75f ||
        instance.displayedAnimationClip != 2 || colorPixels() == walking)
        throw std::runtime_error("Camera-only reentry must retain a hidden actor's latest clip and clock");
    camera.setPosition({0, .8f, 10});
    renderer.render(camera, .125);
    if (renderer.zombieVisibleCount() != 0 || instance.displayedAnimationFrame >= 0 || instance.displayedAnimationClip != 2)
        throw std::runtime_error("Hiding the revealed actor must retire its newly published pose");
    instance.transform = glm::mat4(1);
    layer.markRenderChanged(0);
    renderer.updateZombies(layer);
    renderer.render(camera, .125);
    const auto running = colorPixels();
    if (renderer.zombieVisibleCount() != 1 || instance.displayedAnimationFrame != 1.75f ||
        instance.displayedAnimationClip != 2 || running == walking)
        throw std::runtime_error("A revealed walker must display and publish its latest run pose");
    renderer.render(camera, 1.125);
    if (colorPixels() != running || instance.displayedAnimationFrame != 1.75f)
        throw std::runtime_error("Idle time must not advance a stopped locomotion clip");
    const auto refreshAnimation = [&] {
        layer.markRenderChanged(0, false);
        renderer.updateZombies(layer);
        if (renderer.zombieUpdateStats().instances != 1 || renderer.zombieUpdateStats().bounds != 0 ||
            renderer.zombieUpdateStats().full)
            throw std::runtime_error("Animation-only notifications must refresh state without touching bounds");
    };
    const auto requireFrame = [&](double idleSeconds, float frame, int clip) {
        renderer.render(camera, idleSeconds);
        if (instance.displayedAnimationFrame != frame || instance.displayedAnimationClip != clip ||
            renderer.zombieVisibilityStats().refittedGroups != 0)
            throw std::runtime_error("Cached animation fields must update independently of spatial bounds");
    };
    instance.walkSeconds = .5;
    instance.phase = .25f;
    refreshAnimation();
    requireFrame(50, 1, 2);
    if (colorPixels() == running) throw std::runtime_error("Changed run clock and phase must move the rendered pose");
    instance.walking = false; // A retained running flag must not select locomotion.
    refreshAnimation();
    requireFrame(.125, .75f, 0);
    const auto idle = colorPixels();
    renderer.updateZombies(layer);
    if (renderer.zombieUpdateStats().instances != 0)
        throw std::runtime_error("Unchanged cached animation state must skip preparation");
    requireFrame(.5, 1.5f, 0);
    if (colorPixels() == idle) throw std::runtime_error("Idle rendering must advance without a source-state notification");
    instance.walking = true;
    instance.running = false;
    instance.walkSeconds = .375;
    instance.phase = .5f;
    refreshAnimation();
    requireFrame(100, 2.25f, 1);
    instance.walkSeconds = .875;
    refreshAnimation();
    requireFrame(200, .25f, 1);
    viewer::Renderer::checkErrors("hidden locomotion pose changes");
}

void renderExplicitIdleClock() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    auto& asset = layer.assets.front();
    for (const float height : {.75f, 1.1f})
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
            asset.boneFrames.push_back(glm::translate(glm::mat4(1), {0, height, 0}));
    for (const float height : {.2f, .6f, 1.f})
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
            asset.walkClip->boneFrames.push_back(glm::translate(glm::mat4(1), {0, height, 0}));
    layer.instances.emplace_back();
    auto& instance = layer.instances.front();
    instance.idleSeconds = .25;
    if (std::abs(viewer::sampleZombiePose(asset, instance, 0)[0][3].y - .925f) > .0001f)
        throw std::runtime_error("CPU idle sampling must honor an actor's explicit clock before its first draw");
    viewer::Camera camera;
    camera.setPosition({0, .8f, 10}); camera.setOrientation(-90, 0);
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(width, height); renderer.setZombieDistanceDetail(false);
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.render(camera, 0);
    const auto predictedIdle = colorPixels();
    if (instance.displayedAnimationFrame != .5f || instance.displayedAnimationClip != 0 ||
        std::abs(viewer::sampleZombiePose(asset, instance, 0)[0][3].y - .925f) > .0001f)
        throw std::runtime_error("GPU and CPU idle poses must agree on the actor's explicit clock");
    renderer.render(camera, .75);
    if (colorPixels() != predictedIdle || instance.displayedAnimationFrame != .5f)
        throw std::runtime_error("The shared world clock must not advance an explicitly timed idle pose");
    instance.idleSeconds = .5;
    layer.markRenderChanged(0, false); renderer.updateZombies(layer);
    renderer.render(camera, .75);
    if (instance.displayedAnimationFrame != 1 || colorPixels() == predictedIdle)
        throw std::runtime_error("An idle-clock update must refresh the rendered pose without changing bounds");
    instance.idleSeconds = -1;
    layer.markRenderChanged(0, false); renderer.updateZombies(layer);
    renderer.render(camera, .25);
    if (colorPixels() != predictedIdle || instance.displayedAnimationFrame != .5f)
        throw std::runtime_error("Removing an explicit idle clock must restore the shared animation clock");
    instance.walking = true; instance.walkSeconds = .375; instance.idleSeconds = .9;
    instance.displayedAnimationFrame = -1;
    if (std::abs(viewer::sampleZombiePose(asset, instance, 0)[0][3].y - .5f) > .0001f)
        throw std::runtime_error("An explicit idle clock must not change CPU locomotion sampling");
    layer.markRenderChanged(0, false); renderer.updateZombies(layer);
    renderer.render(camera, 0);
    const auto walking = colorPixels();
    if (instance.displayedAnimationFrame != .75f || instance.displayedAnimationClip != 1)
        throw std::runtime_error("An explicit idle clock must not change the displayed locomotion pose");
    instance.idleSeconds = 0;
    layer.markRenderChanged(0, false); renderer.updateZombies(layer);
    renderer.render(camera, .75);
    if (colorPixels() != walking)
        throw std::runtime_error("Changing either idle clock must leave a stopped locomotion clock unchanged");
    viewer::Renderer::checkErrors("explicit zombie idle clocks");
}

void traceLastRenderedZombiePose() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    auto& asset = layer.assets.front();
    for (const float height : {.75f, 1.1f})
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
            asset.boneFrames.push_back(glm::translate(glm::mat4(1), {0, height, 0}));
    for (const float height : {.2f, .6f, 1.f})
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
            asset.walkClip->boneFrames.push_back(glm::translate(glm::mat4(1), {0, height, 0}));
    layer.capsuleAssets.resize(1);
    viewer::ZombieBoneCapsule capsule;
    capsule.bone = 0; capsule.a = capsule.b = markerCenter(0, 0); capsule.radius = .12f;
    layer.capsuleAssets[0].capsules.push_back(capsule);
    layer.instances.resize(3);
    layer.instances[0].transform = layer.instances[0].restTransform = layer.instances[0].restRoot =
        glm::translate(glm::mat4(1), {0, 0, -3});
    layer.instances[2].transform = layer.instances[2].restTransform = layer.instances[2].restRoot =
        glm::translate(glm::mat4(1), {1000, 0, 0});
    viewer::Camera camera;
    camera.setPosition({0, .925f, 10}); camera.setOrientation(-90, 0);
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(width, height); renderer.setZombieDistanceDetail(false); renderer.setShadows(false);
    renderer.setHaze(false); renderer.setGroundFog(false);
    const viewer::CollisionWorld empty(viewer::Model{});
    const glm::vec3 origin(-.38f, .925f, 10), direction(0, 0, -1);
    if (renderer.traceZombieShot(origin, direction, empty))
        throw std::runtime_error("A zombie cannot be claimed before its first camera draw");
    renderer.render(camera, .25);
    auto hit = renderer.traceZombieShot(origin, direction * 3.f, empty);
    if (!hit || hit->claim.zombie != 1 || std::abs(hit->distance - 9.88f) > .001f)
        throw std::runtime_error("Displayed hit query must select the nearest actor rather than the lowest stable ID");
    // Snapshot playback can replace the source before this renderer is updated.
    // Both cached placement and the exact displayed frame must survive that gap.
    auto& moved = layer.instances[1];
    moved.transform = moved.restTransform = moved.restRoot = glm::translate(glm::mat4(1), {100, 0, 0});
    moved.walking = true; moved.walkSeconds = .375; moved.displayedAnimationFrame = -1;
    hit = renderer.traceZombieShot(origin, direction, empty);
    if (!hit || hit->claim.zombie != 1 || std::abs(hit->claim.point.y) > .001f ||
        std::abs(hit->distance - 9.88f) > .001f || moved.displayedAnimationFrame != -1 ||
        layer.damage().injury(1))
        throw std::runtime_error("Read-only hit query must use the last rendered pose after source playback advances");
    viewer::Model wall;
    wall.materials.emplace_back();
    auto& primitive = wall.primitives.emplace_back();
    for (const auto position : {glm::vec3(-2, -1, 5), glm::vec3(2, -1, 5),
                                glm::vec3(2, 3, 5), glm::vec3(-2, 3, 5)}) {
        viewer::Vertex vertex; vertex.position = position; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    wall.draws.push_back({0, glm::mat4(1), "wall"});
    const viewer::CollisionWorld blocked(wall);
    if (renderer.traceZombieShot(origin, direction, blocked))
        throw std::runtime_error("Client hit query must reject a displayed zombie behind scene geometry");
    if (renderer.traceZombieShot({999.62f, .925f, 10}, direction, empty))
        throw std::runtime_error("Actors outside the last camera draw must not receive client hit claims");
    camera.setOrientation(90, 0);
    renderer.render(camera, .25);
    if (renderer.traceZombieShot(origin, direction, empty))
        throw std::runtime_error("An actor leaving the camera draw must retire its cached hit pose");
    viewer::Renderer::checkErrors("last-rendered zombie hit queries");
}

void traceLastRenderedWoundMask() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    auto& asset = layer.assets.front();
    asset.ragdollBones[std::size_t(viewer::RagdollPart::torso)] = 0;
    asset.skeleton[0].name = "torso";
    for (const float height : {.75f, 1.1f})
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
            asset.boneFrames.push_back(glm::translate(glm::mat4(1), {0, height, 0}));
    layer.capsuleAssets.resize(1);
    viewer::ZombieBoneCapsule capsule;
    capsule.bone = 0; capsule.a = capsule.b = markerCenter(0, 0); capsule.radius = .12f;
    layer.capsuleAssets[0].capsules.push_back(capsule);
    layer.instances.emplace_back();
    viewer::Camera camera;
    camera.setPosition({0, .925f, 10}); camera.setOrientation(-90, 0);
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(width, height); renderer.setZombieDistanceDetail(false); renderer.setShadows(false);
    renderer.setHaze(false); renderer.setGroundFog(false);
    const viewer::CollisionWorld empty(viewer::Model{});
    const glm::vec3 origin(-.38f, .925f, 10), direction(0, 0, -1);
    const auto hits = [&] { return renderer.traceZombieShot(origin, direction, empty).has_value(); };
    const auto draw = [&] { renderer.updateZombies(layer); renderer.render(camera, .25); };
    draw();
    if (!hits()) throw std::runtime_error("Intact displayed torso must accept its surface hit");
    layer.setWoundMask(0, {0x0f});
    if (!hits()) throw std::runtime_error("An unrendered new wound must not remove the previously displayed surface");
    draw();
    if (hits()) throw std::runtime_error("A displayed wound must remove its hit surface");
    layer.setWoundMask(0, {0});
    if (hits()) throw std::runtime_error("An unrendered wound revision must not replace the cached displayed hole");
    draw();
    if (!hits()) throw std::runtime_error("Updating an existing sparse wound row must restore its displayed surface");
    layer.setWoundMask(0, {0x0f});
    if (!hits()) throw std::runtime_error("A sparse displayed mask must retain its own values after source mutation");
    draw();
    layer.setWoundMask(0, {});
    if (hits()) throw std::runtime_error("Removing the source wound must retain the displayed hole until its next draw");
    draw();
    if (!hits()) throw std::runtime_error("Retiring the sparse wound row must restore intact hit geometry");
    viewer::Renderer::checkErrors("last-rendered zombie wound masks");
}

void renderPosePublicationChanges() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1,2));
    layer.lodAssets.resize(1);
    for (int level=0;level<3;++level) layer.lodAssets[0].push_back(layer.assets[0]);
    for (std::size_t id=0;id<4;++id) {
        viewer::ZombieInstance instance;
        instance.phase=float(id)*.25f;
        instance.transform=glm::translate(glm::mat4(1),{id==3 ? 1000.f : -3.f+3.f*float(id),0,0});
        layer.instances.push_back(instance);
    }
    viewer::Camera camera;
    camera.setPosition({0,.8f,100});camera.setOrientation(-90,0);
    const auto configure=[](viewer::Renderer& renderer) {
        renderer.resize(width,height);renderer.setShadows(false);
        renderer.setHaze(false);renderer.setGroundFog(false);
    };
    const auto requireCounts=[](const viewer::Renderer& renderer,std::size_t publications,std::size_t retirements) {
        const auto& stats=renderer.zombieRenderStats();
        if (stats.posePublications!=publications || stats.poseRetirements!=retirements)
            throw std::runtime_error("Pose publication must count only actors whose authoritative fields changed");
    };
    viewer::Renderer first(viewer::Model{},VIEWER_SHADER_DIR,nullptr,&layer);
    configure(first);first.render(camera,0);
    if (first.zombieVisibleCount()!=3 || first.zombieRenderStats().cameraLevels[3]!=3 ||
        layer.instances[0].displayedAnimationFrame!=0 || layer.instances[1].displayedAnimationFrame!=0 ||
        layer.instances[2].displayedAnimationFrame!=0 || layer.instances[3].displayedAnimationFrame!=-1)
        throw std::runtime_error("Publication fixture must show three independently phased actors at far LOD");
    requireCounts(first,3,0);
    const auto initial=colorPixels();
    first.render(camera,0);requireCounts(first,0,0);
    if (colorPixels()!=initial) throw std::runtime_error("Skipping unchanged publication must preserve rendered poses");
    // At 5 Hz with this two-frame idle clip, only the phase-.5 actor enters
    // the second baked frame. Advancing inside that same step changes none.
    first.render(camera,.11);requireCounts(first,1,0);
    if (layer.instances[0].displayedAnimationFrame!=0 || layer.instances[1].displayedAnimationFrame!=0 ||
        layer.instances[2].displayedAnimationFrame!=1)
        throw std::runtime_error("Far animation must publish only the actor whose stepped pose advanced");
    first.render(camera,.12);requireCounts(first,0,0);
    layer.instances[0].transform=glm::translate(glm::mat4(1),{1000,0,0});
    layer.instances[3].transform=glm::translate(glm::mat4(1),{-3,0,0});
    layer.markRenderChanged(0);layer.markRenderChanged(3);first.updateZombies(layer);
    if (layer.instances[0].displayedAnimationFrame!=0 || layer.instances[3].displayedAnimationFrame!=-1)
        throw std::runtime_error("Renderer preparation must preserve the last displayed pose until rendering");
    first.render(camera,.12);requireCounts(first,1,1);
    if (layer.instances[0].displayedAnimationFrame!=-1 || layer.instances[3].displayedAnimationFrame!=1)
        throw std::runtime_error("Partial camera exit and entry must retire and publish only their own source IDs");
    const auto crossed=colorPixels();
    first.setZombieCulling(false);first.render(camera,.12);requireCounts(first,0,0);
    if (colorPixels()!=crossed) throw std::runtime_error("Incremental publication must preserve exact culled/unculled pixels");
    first.setZombieCulling(true);

    viewer::Renderer second(viewer::Model{},VIEWER_SHADER_DIR,nullptr,&layer);
    configure(second);
    auto otherCamera=camera;otherCamera.setPosition({78,.8f,100});
    second.render(otherCamera,.81);requireCounts(second,1,0);
    if (second.zombieVisibleCount()!=1 || layer.instances[2].displayedAnimationFrame!=0)
        throw std::runtime_error("Second renderer must publish its different time for the one overlapping actor");
    second.render(otherCamera,.81);requireCounts(second,0,0);
    first.render(camera,.12);requireCounts(first,1,0);
    if (layer.instances[2].displayedAnimationFrame!=1)
        throw std::runtime_error("A renderer must restore the actual pose changed by another renderer");
    otherCamera.setPosition({-78,.8f,100});
    second.render(otherCamera,.81);requireCounts(second,1,1);
    if (second.zombieVisibleCount()!=1 || layer.instances[2].displayedAnimationFrame!=-1 ||
        layer.instances[3].displayedAnimationFrame!=0)
        throw std::runtime_error("A second camera must merge its own entry/exit IDs without stale pose caching");
    first.render(camera,.12);requireCounts(first,2,0);
    if (layer.instances[2].displayedAnimationFrame!=1 || layer.instances[3].displayedAnimationFrame!=1)
        throw std::runtime_error("Last writer must restore both a retired pose and another renderer's changed pose");
    viewer::Renderer::checkErrors("incremental displayed zombie pose publication");
}

void paletteGrowthAndReuse() {
    viewer::ZombiePaletteCache cache(20, 2, 160);
    std::vector<glm::mat4> original(2, glm::translate(glm::mat4(1), {1, 2, 3}));
    cache.beginFrame();
    const auto first = cache.request(0, 1, original);
    cache.flush();
    const auto initialBytes = cache.stats().storageBytes;
    cache.beginFrame();
    std::vector<glm::mat4> changed(2, glm::translate(glm::mat4(1), {7, 8, 9}));
    // The first palette is hidden while enough others arrive to grow storage.
    for (std::size_t i = 1; i <= 9; ++i) cache.request(i, 1, changed);
    cache.flush();
    if (cache.stats().storageBytes <= initialBytes || cache.stats().uploaded != 9 || cache.stats().uploadCalls != 1)
        throw std::runtime_error("Adjacent dirty slots must share one upload during GPU storage growth");
    cache.beginFrame();
    cache.request(0, 1, original);
    cache.flush();
    if (cache.stats().uploaded != 0) throw std::runtime_error("Growing must preserve a hidden settled palette");
    const auto read = [&](float offset) {
        glBindTexture(GL_TEXTURE_BUFFER, cache.texture());
        GLint buffer = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_BUFFER, 0, GL_TEXTURE_BUFFER_DATA_STORE_BINDING, &buffer);
        glBindBuffer(GL_TEXTURE_BUFFER, static_cast<GLuint>(buffer));
        std::vector<glm::mat4> palette(2);
        glGetBufferSubData(GL_TEXTURE_BUFFER, static_cast<GLintptr>(offset * sizeof(glm::mat4)),
                          static_cast<GLsizeiptr>(palette.size() * sizeof(glm::mat4)), palette.data());
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
        viewer::Renderer::checkErrors("resident palette readback");
        return palette;
    };
    if (read(first) != original) throw std::runtime_error("GPU copy must preserve actual hidden palette contents");
    cache.release(0);
    cache.beginFrame();
    const auto reused = cache.request(10, 1, changed); // Same revision, different owner.
    cache.flush();
    if (reused != first || cache.stats().uploaded != 1 || read(reused) != changed)
        throw std::runtime_error("Reused slots must overwrite the previous corpse regardless of revision equality");
    cache.beginFrame();
    cache.request(1, 2, original);
    cache.request(2, 2, original);
    cache.request(4, 2, original);
    cache.flush();
    if (cache.stats().uploaded != 3 || cache.stats().uploadCalls != 2)
        throw std::runtime_error("Merge only adjacent changed slots, retaining gaps in GPU storage");
    viewer::ZombiePaletteCache limited(2, 2, 8); // Only one palette fits.
    limited.beginFrame();
    limited.request(0, 1, original);
    limited.flush();
    limited.beginFrame();
    bool rejected = false;
    try { limited.request(1, 1, changed); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Slots must respect the hardware texture-buffer capacity");
    limited.release(0);
    limited.request(1, 1, changed);
    limited.flush();
}

void renderDistantHeadSurfaces() {
    viewer::ZombieLayer layer;
    auto asset = makeAsset(1, 2);
    asset.walkClip.reset();
    asset.runClip.reset();
    auto& primitive = asset.model.primitives[0];
    // Emulate a face overlay 3 mm in front of a skull at sniper distance.
    // Equal quantized depths must not merge the two surfaces.
    for (std::size_t v = 0; v < primitive.vertices.size(); ++v) {
        auto& vertex = primitive.vertices[v];
        vertex.position -= markerCenter(0, v / 4);
        vertex.position.y += 1.7f;
        vertex.position.z = v < 4 ? 0.003f : 0;
    }
    auto& animation = asset.animation[0];
    animation.skinning.clear();
    animation.frames.clear();
    for (int frame = 0; frame < 2; ++frame)
        for (const auto& vertex : primitive.vertices)
            animation.frames.push_back({glm::vec4(vertex.position, 1), glm::vec4(vertex.normal, 0)});
    // Draw the skull first, then the face. The face must pass the depth test.
    std::rotate(primitive.indices.begin(), primitive.indices.begin() + 6, primitive.indices.end());
    layer.assets.push_back(std::move(asset));
    layer.instances.emplace_back();
    viewer::Camera camera;
    camera.look(0, 200);
    camera.cycleViewMode();
    camera.cycleViewMode();
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.setZombieDistanceDetail(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.resize(width, height);
    for (const float x : {-0.05f, 0.0f, 0.05f}) {
        camera.setPosition({x, 1.7f, 300});
        renderer.render(camera, 0);
        std::array<unsigned char, 3 * 3 * 3> pixels{};
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(width / 2 - 1, height / 2 - 1, 3, 3, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        viewer::Renderer::checkErrors("distant head surface readback");
        for (std::size_t pixel = 0; pixel < 9; ++pixel)
            if (pixels[pixel * 3] != 255 || pixels[pixel * 3 + 1] != 0 || pixels[pixel * 3 + 2] != 0)
                throw std::runtime_error("Zoomed sight must resolve the face ahead of the skull at 300 m");
    }
}

void renderDistanceLods() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    auto& base = layer.assets[0];
    // Match the shipped rigs: bind vertices in centimeters, baked frames in
    // meters. A raw bind box must never contaminate living visibility/LOD.
    for (auto& vertex : base.model.primitives[0].vertices) vertex.position *= 100;
    base.boneFrames.assign(4, glm::scale(glm::mat4(1), glm::vec3(.01f)));
    for (std::size_t bone = 0; bone < 2; ++bone) {
        base.boneFrames[bone][3].y = .75f;
        base.boneFrames[2 + bone][3].y = 1.1f;
    }
    layer.lodAssets.resize(1);
    for (int level = 1; level <= 3; ++level) {
        auto lod = base;
        auto& indices = lod.model.primitives[0].indices;
        if (level == 2) indices.erase(indices.begin(), indices.begin() + 6); // Blue marker only.
        else indices.resize(level == 3 ? 3 : 6); // Red marker only.
        layer.lodAssets[0].push_back(std::move(lod));
    }
    layer.instances.resize(1);
    viewer::Camera camera;
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(width, height);
    renderer.setHaze(false); renderer.setGroundFog(false); renderer.setShadows(false);
    const auto renderAt = [&](float distance) {
        camera.setPosition(glm::vec3(0, 1, 0) - camera.direction() * distance);
        renderer.updateZombies(layer);
        renderer.render(camera, .25);
    };
    const auto markerPixels = [&](std::size_t channel) {
        std::vector<unsigned char> pixels(width * height * 3);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::size_t count = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 3)
            if (pixels[i + channel] == 255 && pixels[i + (channel + 1) % 3] == 0 &&
                pixels[i + (channel + 2) % 3] == 0) ++count;
        return count;
    };
    renderAt(20);
    if (renderer.zombieRenderStats().cameraLevels[1] != 1 || markerPixels(0) == 0 || markerPixels(2) != 0)
        throw std::runtime_error("LOD1 must draw its red marker mesh");
    if (layer.instances[0].displayedAnimationFrame != .5f ||
        std::abs(viewer::sampleZombiePose(base, layer.instances[0], .25)[0][3].y - .925f) > .0001f)
        throw std::runtime_error("Smooth LOD1 must publish the interpolated hit/ragdoll pose");
    renderer.render(camera, .26);
    if (std::abs(layer.instances[0].displayedAnimationFrame - .52f) > .0001f)
        throw std::runtime_error("Smooth LOD1 animation must advance on consecutive render frames");
    renderAt(60);
    if (renderer.zombieRenderStats().cameraLevels[2] != 1 || markerPixels(2) == 0 || markerPixels(0) != 0)
        throw std::runtime_error("LOD2 must draw its blue marker mesh");
    if (layer.instances[0].displayedAnimationFrame != 0)
        throw std::runtime_error("Smooth LOD1 must preserve stepped LOD2 animation");
    renderAt(120);
    if (renderer.zombieRenderStats().cameraLevels[3] != 1 || renderer.zombieRenderStats().cameraTriangles != 1)
        throw std::runtime_error("Far camera must draw the lowest mesh");
    if (layer.instances[0].displayedAnimationFrame != 0)
        throw std::runtime_error("Smooth LOD1 must preserve stepped LOD3 animation");
    renderAt(200);
    if (renderer.zombieVisibleCount() != 0 || renderer.zombieVisibilityStats().cameraRegionRejectedGroups != 1 ||
        renderer.zombieVisibilityStats().cameraGroupTests != 0)
        throw std::runtime_error("Far camera must reject the group before camera frustum work");
    camera.cycleViewMode();
    renderAt(200);
    if (renderer.zombieVisibleCount()!=1)
        throw std::runtime_error("Scope projection must extend the early camera range");
    camera.cycleViewMode();camera.cycleViewMode();
    // A cut-off ragdoll remains a caster, sharing one palette across mesh LODs.
    auto& corpse = layer.instances[0];
    corpse.ragdoll = true;
    corpse.ragdollBones.assign(2, glm::scale(glm::mat4(1), glm::vec3(.01f)));
    corpse.ragdollPoseVersion = 1;
    layer.markRenderChanged(0);
    renderer.setShadows(true); renderer.setZombieDrawDistance(1);
    renderAt(20);
    if (renderer.zombieVisibleCount() != 0 || renderer.zombieRenderStats().shadowLevels[3] != 1 ||
        renderer.zombiePaletteStats().uploaded != 1)
        throw std::runtime_error("Distance-culled corpse must retain its LOD3 shadow and initial palette");
    renderer.render(camera, .25);
    if (renderer.zombiePaletteStats().uploaded != 0)
        throw std::runtime_error("LOD shadow must reuse a settled palette");
    renderer.setZombieDrawDistance(0);
    renderAt(20);
    if (renderer.zombieRenderStats().cameraLevels[1] != 1 || renderer.zombiePaletteStats().needed != 1 ||
        renderer.zombiePaletteStats().uploaded != 0)
        throw std::runtime_error("Camera/shadow LODs must share one persistent palette");
    viewer::Renderer::checkErrors("distance LOD rendering");
}

void renderOutlyingLodBounds() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1, 2));
    auto& base = layer.assets.front();
    // As in authored assets, bind vertices use centimeters and VAT uses meters.
    for (auto& vertex : base.model.primitives[0].vertices) vertex.position *= 100;
    layer.lodAssets.resize(1);
    layer.lodAssets[0] = {base, base, base};
    auto& lod = layer.lodAssets[0].back();
    auto& primitive = lod.model.primitives[0];
    for (std::size_t frameVertex = 0; frameVertex < lod.animation[0].frames.size(); ++frameVertex) {
        const auto vertex = frameVertex % primitive.vertices.size();
        // Enlarge the markers so the far-LOD capture resolves them clearly.
        const auto offset = primitive.vertices[vertex].position * .04f + glm::vec3(-100, 0, 0);
        lod.animation[0].frames[frameVertex].position += glm::vec4(offset, 0);
    }
    for (auto& vertex : primitive.vertices)
        vertex.position = vertex.position * 5.f + glm::vec3(-10000, 0, 0);
    layer.instances.emplace_back();
    layer.instances[0].transform = glm::translate(glm::mat4(1), {100, 0, 0});
    viewer::Camera camera;
    camera.setPosition({0, .75f, 100});
    camera.setOrientation(-90, 0);
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(width, height);
    renderer.setZombieDrawDistance(0);
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    const auto checkVisible = [&] {
        renderer.render(camera, 0);
        if (renderer.zombieVisibleCount() != 1 || renderer.zombieRenderStats().cameraLevels[3] != 1)
            throw std::runtime_error("An outlying LOD must remain visible when the original mesh is outside the frustum");
        const auto pixels = colorPixels();
        std::size_t red = 0, blue = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 3) {
            red += pixels[i] == 255 && pixels[i + 1] == 0 && pixels[i + 2] == 0;
            blue += pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 255;
        }
        if (red < 4 || blue < 4)
            throw std::runtime_error("Outlying LOD markers must actually reach the capture");
        renderer.setZombieCulling(false);
        renderer.render(camera, 0);
        if (colorPixels() != pixels)
            throw std::runtime_error("LOD bounds culling must match drawing without culling");
        renderer.setZombieCulling(true);
    };
    checkVisible();

    // Now both original and LOD baked bounds are outside the view. Only the
    // live palette applied to the outlying LOD bind vertices brings it back.
    auto& instance = layer.instances[0];
    instance.transform = glm::translate(glm::mat4(1), {300, 0, 0});
    auto bone = glm::scale(glm::mat4(1), glm::vec3(.01f));
    bone[3] = {-200, .75f, 0, 1};
    layer.setRagdollPalette(0, std::vector<glm::mat4>(base.skeleton.size(), bone));
    renderer.updateZombies(layer);
    checkVisible();
    viewer::Renderer::checkErrors("outlying zombie LOD bounds");
}

void renderHalfRoundedAnimationBounds() {
    auto base=makeAsset(1,2);
    const auto vertices=base.model.primitives[0].vertices.size();
    for (std::size_t index=0;index<base.animation[0].frames.size();++index) {
        const auto corner=index%4, marker=(index%vertices)/4, frame=index/vertices;
        base.animation[0].frames[index].position={32, (corner<2 ? -1.f : 1.f)+float(marker)*3+float(frame%2)*.5f,0,1};
    }
    auto rounded=base;
    for (std::size_t index=0;index<rounded.animation[0].frames.size();++index)
        rounded.animation[0].frames[index].position.x=index%4==0 || index%4==3 ? 32.01f : 32.02f;
    // Amplify half rounding: original world X ends at 20, stored half X at
    // 31.25. The camera's left face lies near 25, leaving a visible clipped strip.
    const auto transform=glm::scale(glm::translate(glm::mat4(1),{-32000,0,0}),{1000,1,1});
    viewer::Camera camera;
    camera.setPosition({102,1,100});camera.setOrientation(-90,0);
    const viewer::VisibilityBounds original{{0,-1,0},{20.01f,4.5f,0}}, stored{{0,-1,0},{31.25f,4.5f,0}};
    const viewer::Frustum frustum(camera.projection(float(width)/height)*camera.view());
    if (frustum.intersects(original.minimum,original.maximum) || !frustum.intersects(stored.minimum,stored.maximum))
        throw std::runtime_error("Half-bounds fixture must cross the camera face only after rounding");
    for (const bool lodOnly:{true,false}) {
        viewer::ZombieLayer layer;
        layer.assets.push_back(lodOnly ? base : rounded);
        if (lodOnly) layer.lodAssets={{base,base,rounded}};
        layer.instances.emplace_back();layer.instances[0].transform=transform;
        viewer::Renderer renderer(viewer::Model{},VIEWER_SHADER_DIR,nullptr,&layer);
        renderer.resize(width,height);renderer.setZombieDrawDistance(0);renderer.setZombieDistanceDetail(lodOnly);
        renderer.setShadows(false);renderer.setHaze(false);renderer.setGroundFog(false);
        // LOD3 tests its independently expanded bounds. Full-detail playback
        // additionally tests both frame endpoints and their interpolated pose.
        for (const double seconds:{0.,.25,.5,.75}) {
            renderer.render(camera,seconds);
            if (renderer.zombieVisibleCount()!=1 || renderer.zombieRenderStats().cameraLevels[lodOnly ? 3 : 0]!=1)
                throw std::runtime_error("Decoded half positions must expand original and LOD rendering bounds");
            const auto pixels=colorPixels();
            std::size_t red=0,blue=0;
            for (std::size_t index=0;index<pixels.size();index+=3) {
                red+=pixels[index]==255 && pixels[index+1]==0 && pixels[index+2]==0;
                blue+=pixels[index]==0 && pixels[index+1]==0 && pixels[index+2]==255;
            }
            if (red<4 || blue<4) throw std::runtime_error("Half-rounded geometry must actually reach the camera capture");
            renderer.setZombieCulling(false);renderer.render(camera,seconds);
            if (colorPixels()!=pixels) throw std::runtime_error("Half bounds culling changed decoded camera geometry");
            renderer.setZombieCulling(true);
        }
        if (lodOnly) {
            viewer::SunLighting sunlight;sunlight.direction={0,1,1};
            auto shadowCamera=camera;
            shadowCamera.setPosition({147,1,50});shadowCamera.setOrientation(90,0);
            const viewer::Frustum light(viewer::sunShadowView(shadowCamera.position(),sunlight.direction).projectionView());
            if (light.intersects(original.minimum,original.maximum) || !light.intersects(stored.minimum,stored.maximum))
                throw std::runtime_error("Half-bounds fixture must cross the independent light face only after rounding");
            renderer.setLighting(sunlight);renderer.setShadows(true);renderer.render(shadowCamera,0);
            if (renderer.zombieVisibleCount()!=0 || renderer.zombieRenderStats().shadowLevels[3]!=1)
                throw std::runtime_error("Rounded LOD bounds must retain an offscreen sunlight caster");
        }
        viewer::Renderer::checkErrors("half-rounded zombie animation bounds");
    }
}

void renderAnimationFloatFallbackAndLivePalette() {
    viewer::ZombieLayer layer;
    layer.assets.push_back(makeAsset(1,2));
    for (auto& sample:layer.assets[0].animation[0].frames) sample.position.x+=70000;
    layer.instances.emplace_back();
    layer.instances[0].transform=glm::translate(glm::mat4(1),{-70000,0,0});
    viewer::Camera camera;camera.setPosition({0,.8f,10});camera.setOrientation(-90,0);
    viewer::Renderer renderer(viewer::Model{},VIEWER_SHADER_DIR,nullptr,&layer);
    renderer.resize(width,height);renderer.setZombieDistanceDetail(false);renderer.setShadows(false);
    renderer.setHaze(false);renderer.setGroundFog(false);renderer.render(camera,0);
    const auto baked=colorPixels();
    std::size_t red=0,blue=0;
    for (std::size_t index=0;index<baked.size();index+=3) {
        red+=baked[index]==255 && baked[index+1]==0 && baked[index+2]==0;
        blue+=baked[index]==0 && baked[index+1]==0 && baked[index+2]==255;
    }
    if (renderer.zombieVisibleCount()!=1 || red<4 || blue<4)
        throw std::runtime_error("Finite animation outside half range must retain visible float32 geometry");
    layer.setRagdollPalette(0,std::vector<glm::mat4>(2,glm::translate(glm::mat4(1),{70000,.75f,0})));
    renderer.updateZombies(layer);renderer.render(camera,0);
    if (colorPixels()!=baked || renderer.zombiePaletteStats().uploadBytes!=2*sizeof(glm::mat4))
        throw std::runtime_error("Live bone palettes must retain full float32 coordinates and matrix storage");
    renderer.render(camera,0);
    if (renderer.zombiePaletteStats().uploaded!=0 || colorPixels()!=baked)
        throw std::runtime_error("Unchanged full-precision palettes must remain resident");
    viewer::Renderer::checkErrors("float32 animation fallback and live palettes");
}

void renderAuthoredAssets(const std::filesystem::path& directory, const std::filesystem::path& screenshot) {
    viewer::ZombieLayer layer;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".glb") paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    if (paths.empty()) throw std::runtime_error("No authored assets found");
    for (const auto& path : paths) {
        auto asset = viewer::loadZombieModel(path);
        if (!asset.walkClip || !asset.runClip) throw std::runtime_error("Expected authored walk and run clips in " + path.string());
        const auto low = asset.model.boundsMin, high = asset.model.boundsMax;
        const auto normalization = glm::scale(glm::mat4(1), glm::vec3(1.8f / (high.y - low.y))) *
            glm::translate(glm::mat4(1), -glm::vec3((low.x + high.x) * 0.5f, low.y, (low.z + high.z) * 0.5f));
        const auto index = layer.assets.size();
        for (int state = 0; state < 3; ++state) {
            viewer::ZombieInstance instance;
            instance.asset = index;
            instance.restRoot = glm::translate(glm::mat4(1),
                {2.5f * (static_cast<float>(index) - static_cast<float>(paths.size() - 1) * 0.5f), 0, -3.0f * state});
            instance.transform = instance.restTransform = instance.restRoot * normalization;
            instance.walking = state != 0;
            instance.running = state == 2;
            instance.walkSeconds = 0.5;
            layer.instances.push_back(instance);
            // Independently compare sampled skinning against baked displayed
            // vertices for each real rig/clip, including wrap interpolation.
            const auto* clip = viewer::zombieAnimationClip(asset, instance);
            const auto count = clip ? clip->frameCount : asset.frameCount;
            const auto offset = clip ? clip->firstFrame : 0;
            const auto duration = clip ? clip->duration : asset.duration;
            const double seconds = duration * (1.0 - 0.25 / static_cast<double>(count));
            const auto pose = viewer::sampleAnimatedPose(asset, seconds, 0, clip);
            for (std::size_t p = 0; p < asset.animation.size(); ++p) {
                const auto& primitive = asset.model.primitives[p];
                const auto& animation = asset.animation[p];
                for (std::size_t v = 0; v < animation.skinning.size(); ++v) {
                    glm::vec4 skinned(0);
                    const auto& skin = animation.skinning[v];
                    for (std::size_t influence = 0; influence < 8; ++influence)
                        skinned += pose[skin.joints[influence]] * asset.skeleton[skin.joints[influence]].inverseBind *
                            glm::vec4(primitive.vertices[v].position, 1) * skin.weights[influence];
                    const auto baked = animation.frames[(offset + count - 1) * primitive.vertices.size() + v].position * 0.25f +
                                       animation.frames[offset * primitive.vertices.size() + v].position * 0.75f;
                    if (glm::length(skinned - baked) > 2e-4f)
                        throw std::runtime_error("Authored pose and displayed vertices disagree in " + path.string());
                }
            }
        }
        layer.lodAssets.push_back(viewer::loadZombieLods(path, asset));
        layer.assets.push_back(std::move(asset));
    }
    viewer::Camera camera;
    camera.setPosition({0, 12, 20});
    camera.look(0, -150);
    viewer::Model ground;
    viewer::Primitive plane;
    for (const glm::vec3 position : {glm::vec3(-18, 0, 5), glm::vec3(18, 0, 5),
                                   glm::vec3(18, 0, -12), glm::vec3(-18, 0, -12)}) {
        viewer::Vertex vertex;
        vertex.position = position;
        vertex.normal = {0, 1, 0};
        vertex.color = {0.4f, 0.45f, 0.35f, 1};
        plane.vertices.push_back(vertex);
    }
    plane.indices = {0, 1, 2, 0, 2, 3};
    ground.primitives.push_back(std::move(plane));
    ground.draws.push_back({0, glm::mat4(1), "terrain"});
    viewer::Renderer renderer(ground, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.resize(width * 2, height * 2);
    renderer.updateZombies(layer);
    renderer.render(camera, 0.5);
    viewer::Renderer::checkErrors("authored locomotion asset render");
    if (!screenshot.empty()) renderer.writeScreenshot(screenshot);
    std::cout << "Verified " << layer.assets.size() << " authored rigs in idle/walk/run\n";
    for (std::size_t asset = 0; asset < layer.assets.size(); ++asset) {
        const auto feet = glm::vec3(layer.instances[asset * 3].restRoot[3]);
        if (!viewer::test::shootToKill(layer, feet + glm::vec3(0, 0.9f, 5), {0, 0, -1}, 0.5))
            throw std::runtime_error("Authored rig must activate its physics-owned palette");
    }
    for (int frame = 0; frame < 180; ++frame) layer.update(1.0f / 60);
    renderer.updateZombies(layer);
    renderer.render(camera, 0.5);
    if (renderer.zombiePaletteStats().uploaded != layer.assets.size())
        throw std::runtime_error("Every authored ragdoll must upload its initial visible palette");
    layer.synchronizePhysics();
    renderer.updateZombies(layer);
    renderer.render(camera, 0.5);
    if (renderer.zombiePaletteStats().uploaded != 0)
        throw std::runtime_error("Authored ragdoll palettes must persist across render frames");
    if (!screenshot.empty()) {
        const auto corpses = screenshot.parent_path() / (screenshot.stem().string() + "-ragdolls.ppm");
        renderer.writeScreenshot(corpses);
    }
}

void renderAuthoredAttacks(const std::filesystem::path& path, const std::filesystem::path& screenshot) {
    constexpr std::array<double, 5> times{0, .6, .9, 1.25, 2.3333333};
    std::vector<glm::mat4> placements;
    for (int row = 0; row < 2; ++row) for (int column = 0; column < 5; ++column)
        placements.push_back(glm::translate(glm::mat4(1), {float(column - 2) * 2.2f, 0, -float(row) * 3}));
    auto layer = viewer::loadZombieLayer(path, placements);
    const auto& asset = layer.assets[0];
    if (!asset.attackLeftClip || !asset.attackRightClip) throw std::runtime_error("Expected both authored attacks");
    for (std::size_t id = 0; id < layer.instances.size(); ++id) {
        auto& zombie = layer.instances[id];
        zombie.attackClip = id < 5 ? 1 : 2;
        zombie.attackSeconds = times[id % 5];
        zombie.phase = .73f;
    }
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(1280, 720); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.setZombieDistanceDetail(false);
    viewer::Camera camera; camera.setPosition({0, 5.8f, 10}); camera.setOrientation(-90, -22);
    renderer.updateZombies(layer); renderer.render(camera, 100);
    if (renderer.zombieVisibleCount() != 10) throw std::runtime_error("Attack preview omitted actors");
    for (std::size_t id = 0; id < layer.instances.size(); ++id) {
        auto& zombie = layer.instances[id];
        const auto& clip = id < 5 ? *asset.attackLeftClip : *asset.attackRightClip;
        const auto expected = viewer::clampedAnimationFrame(clip.duration, clip.frameCount, times[id % 5]);
        if (zombie.displayedAnimationClip != (id < 5 ? 3 : 4) || std::abs(zombie.displayedAnimationFrame - expected) > .0001f)
            throw std::runtime_error("Rendered attack used the wrong clip, clock or random phase");
        const auto displayed = viewer::sampleZombiePose(asset, zombie, 100);
        const auto reference = viewer::sampleAnimatedPose(asset, times[id % 5], 0, &clip);
        for (std::size_t b = 0; b < displayed.size(); ++b)
            if (glm::distance(glm::vec3(displayed[b][3]), glm::vec3(reference[b][3])) > .0002f)
                throw std::runtime_error("Rendered attack and hit-test skeleton disagree");
    }
    if (!screenshot.empty()) renderer.writeScreenshot(screenshot);
    for (std::size_t id = 0; id < layer.instances.size(); ++id) {
        layer.instances[id].attackSeconds = 100;
        layer.markRenderChanged(id, false);
    }
    renderer.updateZombies(layer); renderer.render(camera, 200);
    for (const auto& zombie : layer.instances) {
        const auto& clip = zombie.attackClip == 1 ? *asset.attackLeftClip : *asset.attackRightClip;
        if (zombie.displayedAnimationFrame != float(clip.frameCount - 1))
            throw std::runtime_error("Completed attack wrapped to its first frame");
    }
    viewer::Renderer::checkErrors("authored zombie attack rendering");
}

} // namespace

// Exercises the runtime path using already-prepared local assets. This does not
// generate or modify those assets.
void renderJointSeparations(const std::filesystem::path& path, const std::filesystem::path& screenshot) {
    std::vector<glm::mat4> placements;
    for (int j = 0; j < 10; ++j)
        placements.push_back(glm::translate(glm::mat4(1), {float(j % 5 - 2) * 2.6f, 0, -float(j / 5) * 4}));
    auto layer = viewer::loadZombieLayer(path, placements);
    if (layer.assets.size() != 1 || layer.dismembermentAssets[0].joints.size() != 10)
        throw std::runtime_error("Expected one prepared character with ten joint definitions");
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR, nullptr, &layer);
    renderer.resize(1280, 960); renderer.setHaze(false); renderer.setGroundFog(false);
    viewer::Camera camera; camera.setPosition({0, 7, 11}); camera.setOrientation(-90, -28);
    for (std::size_t j = 0; j < 10; ++j) {
        auto& instance = layer.instances[j];
        instance.phase = 0;
        const auto& asset = layer.assets[0];
        const auto& joints = layer.dismembermentAssets[0].joints;
        const auto pose = viewer::sampleZombiePose(asset, instance, .5);
        const auto center = glm::vec3(instance.transform * viewer::zombieJointTransform(joints[j], pose) *
                                      glm::vec4(joints[j].center, 1));
        bool hit = false;
        for (int attempt = 0; attempt < 32 && !hit; ++attempt) {
            const float angle = float(attempt) * .618033989f * 6.283185307f;
            const float y = -.8f + 1.6f * float(attempt % 5) / 4;
            const auto outward = glm::normalize(glm::vec3(std::cos(angle), y, std::sin(angle)));
            const auto origin = center + outward * .4f;
            float closest = 100; std::size_t selected = joints.size();
            for (std::size_t k = 0; k < joints.size(); ++k) {
                float distance = 0;
                if (viewer::zombieJointRay(origin, -outward, instance.transform, joints[k], pose, distance) && distance < closest) {
                    closest = distance; selected = k;
                }
            }
            if (selected != j) continue;
            hit = layer.shoot(origin, -outward, .5);
        }
        if (!hit || instance.severedJoint != int(j) || !instance.physicsRagdoll || layer.damage().injury(j))
            throw std::runtime_error("Prepared joint hit did not activate the matching separation: " + joints[j].name);
    }
    renderer.updateZombies(layer); renderer.render(camera, .5);
    if (renderer.zombieVisibleCount() != 10) throw std::runtime_error("Not all severed variants rendered");
    if (!screenshot.empty()) {
        auto activation = screenshot.parent_path() / (screenshot.stem().string() + "-activation.ppm");
        renderer.writeScreenshot(activation);
    }
    for (int step = 0; step < 120; ++step) layer.update(1.0f / 120);
    renderer.updateZombies(layer); renderer.render(camera, .5);
    viewer::Renderer::checkErrors("severed joint asset rendering");
    if (!screenshot.empty()) {
        renderer.writeScreenshot(screenshot);
        camera.setPosition({-2.6f, 2.3f, 3.2f}); camera.setOrientation(-90, -20);
        renderer.render(camera, .5);
        renderer.writeScreenshot(screenshot.parent_path() / (screenshot.stem().string() + "-neck.ppm"));
    }
}

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cout << "SKIP: GLFW cannot access a display\n";
        return 77;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(width, height, "Zombie rendering regression", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cout << "SKIP: OpenGL 3.3 context is unavailable\n";
        return 77;
    }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        renderDistanceLods();
        renderOutlyingLodBounds();
        renderHalfRoundedAnimationBounds();
        renderAnimationFloatFallbackAndLivePalette();
        renderHealthyAndWoundedBatches();
        renderDistantHeadSurfaces();
        renderFrustumCulling();
        paletteVisibilityAndDeferredChanges();
        renderGroupMovementAndPoseChanges();
        renderIncrementalAndFullRefresh();
        renderHiddenLocomotionChanges();
        renderExplicitIdleClock();
        traceLastRenderedZombiePose();
        traceLastRenderedWoundMask();
        renderPosePublicationChanges();
        paletteGrowthAndReuse();
        if (mode == "--attacks") {
            if (argc < 3) throw std::runtime_error("--attacks requires a prepared character GLB");
            renderAuthoredAttacks(argv[2], argc > 3 ? argv[3] : "");
        } else if (mode == "--dismemberment") {
            if (argc < 3) throw std::runtime_error("--dismemberment requires one prepared character GLB");
            renderJointSeparations(argv[2], argc > 3 ? argv[3] : "");
        } else if (mode == "--assets") {
            if (argc < 3) throw std::runtime_error("--assets requires a model directory");
            renderAuthoredAssets(argv[2], argc > 3 ? argv[3] : "");
        } else {
            const std::filesystem::path screenshot = argc > 2 ? argv[2] : "";
            if (mode != "--multiple-primitives") renderCrowd(1, mode == "--single-primitive" ? screenshot : "");
            if (mode != "--single-primitive") renderCrowd(2, screenshot);
        }
        std::cout << "Zombie rendering tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << "Zombie rendering test failed: " << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
