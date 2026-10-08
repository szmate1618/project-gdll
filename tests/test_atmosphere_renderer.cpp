#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 640, height = 480;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::Model card(glm::vec3 color, bool masked = false) {
    viewer::Model model;
    viewer::Material material;
    material.unlit = true;
    material.doubleSided = true;
    if (masked) {
        material.alphaMode = "MASK";
        material.baseColorTexture = 0;
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
    model.materials.push_back(material);
    viewer::Primitive primitive;
    primitive.material = 0;
    const std::array<glm::vec2, 4> corners{{{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}}};
    for (const auto corner : corners) {
        viewer::Vertex vertex;
        vertex.position = glm::vec3(corner, 0);
        vertex.normal = {0, 0, 1};
        vertex.color = glm::vec4(color, 1);
        vertex.uv = corner + glm::vec2(0.5f);
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(primitive);
    model.draws.push_back({0, glm::mat4(1), "haze-card"});
    return model;
}

glm::mat4 transform(float distance) {
    return glm::scale(glm::translate(glm::mat4(1), {0, 1.7f, -distance}),
                      {distance * 0.2f, distance * 0.2f, 1});
}

glm::vec3 pixel(int x, int y) {
    std::array<unsigned char, 3> rgb{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return {rgb[0], rgb[1], rgb[2]};
}

viewer::Camera camera() {
    viewer::Camera result;
    result.setPosition({0, 1.7f, 0});
    result.look(0, 200);
    return result;
}

enum class Layer { Scene, Trees, Zombies };

glm::vec3 renderCard(Layer kind, float distance, bool haze) {
    const bool masked = kind == Layer::Trees;
    auto model = card({1, 0, 0}, masked);
    viewer::TreeLayer trees;
    viewer::ZombieLayer zombies;
    if (kind == Layer::Scene) model.draws[0].transform = transform(distance);
    if (kind == Layer::Trees) {
        viewer::TreeAsset asset;
        asset.model = model;
        trees.assets.push_back(asset);
        viewer::TreeInstance instance;
        instance.transform = transform(distance);
        instance.boundsMin = {-distance * 0.1f, 1.7f - distance * 0.1f, -distance};
        instance.boundsMax = {distance * 0.1f, 1.7f + distance * 0.1f, -distance};
        trees.instances.push_back(instance);
    }
    if (kind == Layer::Zombies) {
        viewer::AnimatedModel asset;
        asset.model = model;
        asset.frameCount = 2;
        asset.duration = 1;
        viewer::AnimatedPrimitive animation;
        for (int frame = 0; frame < 2; ++frame)
            for (const auto& vertex : model.primitives[0].vertices)
                animation.frames.push_back({glm::vec4(vertex.position, 1), glm::vec4(vertex.normal, 0)});
        asset.animation.push_back(animation);
        zombies.assets.push_back(asset);
        viewer::ZombieInstance instance;
        instance.transform = transform(distance);
        zombies.instances.push_back(instance);
    }
    viewer::Renderer renderer(kind == Layer::Scene ? model : viewer::Model{}, VIEWER_SHADER_DIR,
                               kind == Layer::Trees ? &trees : nullptr,
                               kind == Layer::Zombies ? &zombies : nullptr);
    renderer.setShadows(false);
    renderer.setHaze(haze);
    renderer.resize(width, height);
    renderer.render(camera());
    // Sample the opaque right half. The left half of a tree card is a hole.
    const auto result = pixel(width / 2 + 20, height / 2);
    if (masked) {
        require(glm::length(pixel(width / 2 - 20, height / 2) - pixel(0, 0)) < 2,
                "Haze must not fill tree alpha-mask holes");
        GLboolean depthWrite = GL_FALSE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        require(depthWrite == GL_TRUE, "Masked trees must retain depth writes");
    }
    viewer::Renderer::checkErrors("haze layer regression");
    return result;
}

void checkLayers() {
    for (const auto kind : {Layer::Scene, Layer::Trees, Layer::Zombies}) {
        const auto near = renderCard(kind, 10, true);
        const auto middle = renderCard(kind, 500, true);
        const auto far = renderCard(kind, 5000, true);
        const auto disabled = renderCard(kind, 5000, false);
        require(near.r > 253 && near.g < 2 && near.b < 2, "FPS foreground must remain clear");
        require(middle.r < near.r - 10 && middle.b > near.b + 40, "Distant surfaces must acquire haze");
        require(far.r < middle.r && far.b > middle.b + 20, "Haze must increase with distance");
        const auto sky = viewer::Atmosphere{}.backgroundSrgb() * 255.0f;
        require(glm::length(far - sky) < 8, "Very distant surfaces must converge toward the sky");
        require(glm::length(disabled - near) < 2, "Disabling haze must restore material colors at distance");
    }
}

void checkLinearBlend() {
    auto model = card({1, 1, 1});
    model.draws[0].transform = transform(100);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.setShadows(false);
    viewer::Atmosphere atmosphere;
    atmosphere.color = {0, 0, 0};
    atmosphere.startDistance = 0;
    atmosphere.density = 0.0069314718f; // Half-transmission distance is 100 m.
    renderer.setAtmosphere(atmosphere);
    renderer.resize(width, height);
    renderer.render(camera());
    const auto halfWhite = pixel(width / 2, height / 2);
    require(glm::all(glm::greaterThanEqual(halfWhite, glm::vec3(187))) &&
            glm::all(glm::lessThanEqual(halfWhite, glm::vec3(189))),
            "Half of linear white must encode near sRGB 188, not 128");
    // Alpha blending retains its original coverage while its RGB is hazed.
    model.materials[0].alphaMode = "BLEND";
    model.materials[0].baseColor.a = 0.5f;
    viewer::Renderer blended(model, VIEWER_SHADER_DIR);
    blended.setShadows(false);
    blended.setAtmosphere(atmosphere);
    blended.resize(width, height);
    blended.render(camera());
    const auto halfAlpha = pixel(width / 2, height / 2);
    require(glm::all(glm::greaterThanEqual(halfAlpha, glm::vec3(93))) &&
            glm::all(glm::lessThanEqual(halfAlpha, glm::vec3(95))),
            "Haze must preserve blended material alpha");
    viewer::Renderer::checkErrors("linear haze regression");
}

void checkEmptyCapture() {
    viewer::Renderer renderer(viewer::Model{}, VIEWER_SHADER_DIR);
    renderer.resize(width, height);
    renderer.render(camera());
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / ("viewer-haze-empty-" + std::to_string(suffix) + ".ppm");
    bool rejected = false;
    try { renderer.writeScreenshot(path); }
    catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("only background") != std::string::npos;
    }
    std::filesystem::remove(path);
    require(rejected, "Screenshot validation must still detect an empty hazy sky");
}
}

int main() {
    if (!glfwInit()) { std::cout << "SKIP: no display\n"; return 77; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Atmosphere regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        checkLayers();
        checkLinearBlend();
        checkEmptyCapture();
        std::cout << "Atmosphere rendering tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
