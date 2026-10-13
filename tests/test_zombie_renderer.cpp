#include "renderer.hpp"
#include "zombie_animation.hpp"

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
                instance.walkSeconds = instance.running ? (pose == 0 ? 1.75 : 0.5) : (pose == 0 ? 1.375 : 0.625);
                instance.transform = glm::translate(glm::mat4(1), instancePosition(index) +
                    glm::vec3(0.08f * static_cast<float>(pose), 0, 0));
                if (instance.ragdoll) {
                    instance.ragdollBones.assign(layer.assets[instance.asset].skeleton.size(), glm::mat4(1));
                    instance.ragdollBones.front() = glm::translate(glm::mat4(1), {0, boneHeight(index, 0, pose), 0});
                    instance.ragdollBones.back() = glm::translate(glm::mat4(1), {0, boneHeight(index, 1, pose), 0});
                }
            }
            renderer.updateZombies(layer);
            renderer.render(camera, 0);
            viewer::Renderer::checkErrors("zombie regression render");
            checkMarkers(camera, layer, primitiveCount, activated, pose);
            if (renderer.zombieCount() != population || renderer.zombieDrawCalls() != 3 * primitiveCount)
                throw std::runtime_error("Zombie rendering must remain batched by asset and primitive");
        }
    }
    if (!screenshot.empty()) renderer.writeScreenshot(screenshot);
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
}

} // namespace

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
        renderDistantHeadSurfaces();
        if (mode == "--assets") {
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
