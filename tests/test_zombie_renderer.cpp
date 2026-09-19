#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

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
                auto center = instancePosition(index) + markerCenter(p, marker);
                center.y += index < activated ? boneHeight(index, marker, pose)
                    : (layer.instances[index].phase == 0 ? 0.75f : 1.1f);
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
    renderer.resize(width, height);
    // Leave six instances idle, cross the first/second palette boundary for
    // every asset, then exceed the reported dozen activations. Moving each pose
    // twice also detects palettes accidentally retained from an earlier update.
    for (std::size_t activated = 0; activated <= 18; ++activated) {
        for (int pose = 0; pose < 2; ++pose) {
            for (std::size_t index = 0; index < activated; ++index) {
                auto& instance = layer.instances[index];
                instance.ragdoll = true;
                instance.ragdollBones.assign(layer.assets[instance.asset].skeleton.size(), glm::mat4(1));
                instance.ragdollBones.front() = glm::translate(glm::mat4(1), {0, boneHeight(index, 0, pose), 0});
                instance.ragdollBones.back() = glm::translate(glm::mat4(1), {0, boneHeight(index, 1, pose), 0});
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
        const std::filesystem::path screenshot = argc > 2 ? argv[2] : "";
        if (mode != "--multiple-primitives") renderCrowd(1, mode == "--single-primitive" ? screenshot : "");
        if (mode != "--single-primitive") renderCrowd(2, screenshot);
        std::cout << "Zombie rendering tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << "Zombie rendering test failed: " << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
