#include "renderer.hpp"

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

viewer::Model card() {
    viewer::Model model;
    viewer::Material material;
    material.baseColor = {.35f,.35f,.35f,1};
    material.doubleSided = true;
    material.unlit = true;
    material.alphaMode = "MASK";
    model.materials = {material};
    viewer::Primitive mesh;
    mesh.material = 0;
    for (auto position : {glm::vec3(-3,0,0), glm::vec3(3,0,0), glm::vec3(3,12,0), glm::vec3(-3,12,0)}) {
        viewer::Vertex vertex;
        vertex.position = position;
        vertex.normal = {0,0,1};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0,1,2,0,2,3};
    model.primitives = {mesh};
    // Include an asset-node translation, and compensate it in the instances.
    model.draws = {{0,glm::translate(glm::mat4(1), {1,0,0}),"card"}};
    return model;
}

float brightness(const viewer::Camera& camera, glm::vec3 point) {
    const auto clip = camera.projection(float(width) / height) * camera.view() * glm::vec4(point,1);
    const glm::vec2 screen = (glm::vec2(clip) / clip.w * .5f + .5f) * glm::vec2(width,height);
    const int x = static_cast<int>(screen.x), y = static_cast<int>(screen.y);
    require(x >= 0 && x < width && y >= 0 && y < height, "Canopy sample outside viewport");
    std::array<unsigned char,3> pixel{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(x,y,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel.data());
    return (pixel[0] + pixel[1] + pixel[2]) / 3.0f;
}

void checkRendering(const std::string& capture) {
    viewer::TreeLayer trees;
    trees.assets.push_back({"card",{},card()});
    for (auto position : {glm::vec3(0,0,-24), glm::vec3(-8,0,-26), glm::vec3(8,0,-26),
                          glm::vec3(-8,0,-36), glm::vec3(8,0,-36), glm::vec3(0,0,-36)}) {
        viewer::TreeInstance instance;
        instance.transform = glm::translate(glm::mat4(1), position + glm::vec3(1,0,0));
        instance.transform[0].x = -1;
        instance.transform[2].x = .2f;
        instance.boundsMin = position + glm::vec3(-3,0,-.01f);
        instance.boundsMax = position + glm::vec3(3,12,.01f);
        instance.dimensions = {10,12,10};
        trees.instances.push_back(instance);
    }
    // A nearby unlit town card must not inherit the last tree's shading state.
    auto town = card();
    town.draws[0].transform = glm::translate(glm::mat4(1), {-14,0,-24});
    viewer::Renderer renderer(town, VIEWER_SHADER_DIR, &trees);
    renderer.resize(width,height);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.setShadows(false);
    viewer::Camera camera;
    camera.setPosition({0,6,0});
    camera.look(0,200); // Level the default -20 degree pitch.
    const glm::vec3 lower(0,2.5f,-24), upper(0,11,-24), townPoint(-14,6,-24);
    renderer.setTreeCanopyShading(false);
    renderer.render(camera);
    const float baseline = brightness(camera,lower);
    const float townBaseline = brightness(camera,townPoint);
    const auto stats = renderer.treeStats();
    if (!capture.empty()) renderer.writeScreenshot(capture + ".off.ppm");
    renderer.setTreeCanopyShading(true);
    renderer.render(camera);
    const float dark = brightness(camera,lower);
    const float crown = brightness(camera,upper);
    require(baseline - dark > 20 && crown - dark > 20 && std::abs(crown - baseline) < 8,
            "Canopy shade did not darken lower foliage while preserving exposed crowns");
    require(renderer.treeStats().visible == stats.visible && renderer.treeStats().drawCalls == stats.drawCalls &&
            stats.drawCalls == 1, "Canopy shade split an asset batch or changed visibility");
    require(std::abs(brightness(camera,townPoint) - townBaseline) < 2,
            "Tree shading leaked into an unlit town material");
    if (!capture.empty()) renderer.writeScreenshot(capture + ".on.ppm");
    renderer.setTreeCulling(false);
    renderer.render(camera);
    require(std::abs(brightness(camera,lower) - dark) < 2 && renderer.treeStats().drawCalls == 1,
            "Culling bypass changed canopy cover or instancing");
    renderer.setShadows(true);
    renderer.render(camera);
    require(std::abs(brightness(camera,lower) - dark) < 2,
            "Shadow-pass instance uploads corrupted canopy data");
    renderer.setNight(true);
    renderer.render(camera);
    const float night = brightness(camera,lower);
    renderer.setTreeCanopyShading(false);
    renderer.render(camera);
    require(std::abs(brightness(camera,lower) - night) < 2, "Canopy shade reduced nighttime flashlight illumination");

    // At twilight, compare flashlight increments in linear RGB, before sRGB
    // encoding. The same beam must be added to shaded and unshaded trees.
    auto twilight = viewer::dayNightLighting(12);
    twilight.unlitColor = {.15f,.15f,.15f};
    twilight.nightBlend = .5f;
    renderer.setShadows(false);
    const auto linear = [](float srgb) {
        const float value = srgb / 255;
        return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
    };
    float beam[2]{};
    for (int shading = 0; shading < 2; ++shading) {
        renderer.setTreeCanopyShading(shading != 0);
        camera.setPosition({0,2.5f,0});
        renderer.setDayNightLighting(twilight);
        renderer.render(camera);
        const float lit = linear(brightness(camera,lower));
        // Move the beam off the sample while keeping it visible.
        camera.look(300,0);
        renderer.render(camera);
        const float unlit = linear(brightness(camera,lower));
        beam[shading] = lit - unlit;
        camera.look(-300,0);
    }
    require(beam[0] > .03f && std::abs(beam[0] - beam[1]) < .01f,
            "Canopy shade multiplied the flashlight contribution at twilight");
    viewer::Renderer::checkErrors("tree canopy regression");
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width,height,"canopy regression",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        checkRendering(argc > 1 ? argv[1] : "");
        std::cout << "Tree canopy shading, batching and flashlight regressions passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
