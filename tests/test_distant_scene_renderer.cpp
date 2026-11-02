#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void quad(viewer::Model& model, glm::vec3 center, glm::vec3 right, glm::vec3 up, glm::vec4 color) {
    viewer::Material material; material.baseColor = color; material.unlit = true; material.doubleSided = true;
    viewer::Primitive primitive; primitive.material = static_cast<int>(model.materials.size());
    for (const auto offset : {-right-up, right-up, right+up, -right+up}) {
        viewer::Vertex vertex; vertex.position = center + offset; vertex.normal = {0,0,1};
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0,1,2,0,2,3};
    viewer::DrawInstance draw; draw.primitive = model.primitives.size();
    model.draws.push_back(draw); model.materials.push_back(material); model.primitives.push_back(primitive);
}
std::vector<unsigned char> pixels(int width, int height) {
    std::vector<unsigned char> result(static_cast<std::size_t>(width * height * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,result.data());
    return result;
}
void compare(viewer::Renderer& renderer, const viewer::Camera& camera, double seconds, int width, int height) {
    renderer.render(camera,seconds,{},seconds);
    const auto cached = pixels(width, height);
    renderer.setDistantSceneCache(false);
    renderer.render(camera,seconds,{},seconds);
    const auto reference = pixels(width, height);
    std::size_t errors = 0, foreground = 0;
    for (std::size_t i = 0; i < cached.size(); i += 3) {
        if (reference[i] == 255 || reference[i+1] == 255 || reference[i+2] == 255) ++foreground;
        if (std::abs(int(cached[i])-reference[i]) + std::abs(int(cached[i+1])-reference[i+1]) +
            std::abs(int(cached[i+2])-reference[i+2]) > 30) ++errors;
    }
    require(foreground > 100, "Reference must contain useful colored geometry");
    require(errors < static_cast<std::size_t>(width*height/50), "Cache must match direct rendering apart from silhouette sampling");
    renderer.setDistantSceneCache(true);
}
void run() {
    viewer::Model model;
    quad(model, {0,0,-1500}, {650,0,0}, {0,350,0}, {1,0,0,1});
    quad(model, {1700,0,0}, {0,0,600}, {0,350,0}, {0,0,1,1});
    quad(model, {0,0,1800}, {650,0,0}, {0,350,0}, {1,1,0,1});
    quad(model, {-1600,0,0}, {0,0,600}, {0,350,0}, {1,0,1,1});
    quad(model, {0,0,-500}, {60,0,0}, {0,60,0}, {0,1,0,1});
    quad(model, {300,0,-1700}, {80,0,0}, {0,80,0}, {0,0,1,1});
    model.draws.back().horizontalMotionRadius = 30; // Full-rate object behind cached red geometry.
    quad(model, {0,1700,0}, {650,0,0}, {0,0,650}, {1,1,1,1});
    quad(model, {0,-1700,0}, {650,0,0}, {0,0,650}, {1,1,1,1});
    // A transformed map-wide surface spans both sides of the 1 km boundary.
    quad(model, {0,-200,0}, {2400,0,0}, {0,0,2400}, {0,1,1,1});
    model.draws.back().transform[3] = {80,0,-70,1};
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(320,180); renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.setDistantSceneCache(true);
    viewer::Camera camera; camera.setPosition({0,0,0}); camera.setOrientation(-90,0);
    renderer.render(camera,0,{},0);
    require(renderer.distantCacheStats().refreshed && renderer.distantCacheStats().patches > 0, "First draw must populate cache");
    const auto center = pixels(320,180);
    const auto index = static_cast<std::size_t>((90*320+160)*3);
    require(center[index] == 0 && center[index+1] == 255, "Near geometry must occlude cached geometry");
    renderer.render(camera,.001,{},.001);
    require(!renderer.distantCacheStats().refreshed && renderer.distantCacheStats().patches == 0,
            "Stationary reuse must submit no distant tiles");
    camera.setOrientation(0,0); renderer.render(camera,.002,{},.002);
    require(!renderer.distantCacheStats().refreshed && renderer.distantCacheStats().patches > 0,
            "Turn must patch new directions without full refresh");
    camera.setOrientation(-90,0); renderer.render(camera,.003,{},.003);
    require(renderer.distantCacheStats().patches == 0, "Returning to a cached direction must reuse its tiles");
    for (const float yaw : {-179.0f,-135.0f,-90.0f,-45.0f,0.0f,45.0f,90.0f,135.0f,179.0f}) {
        camera.setOrientation(yaw,-5); compare(renderer,camera,.004,320,180);
    }
    for (const float pitch : {-89.0f,89.0f}) {
        camera.setOrientation(45,pitch); compare(renderer,camera,.005,320,180);
    }
    camera.setOrientation(-90,0); renderer.render(camera,1,{},1);
    camera.setPosition({.1f,0,0}); renderer.render(camera,1.03,{},1.03);
    require(!renderer.distantCacheStats().refreshed, "Small walking movement must wait for scheduled refresh");
    renderer.render(camera,1.067,{},1.067);
    require(renderer.distantCacheStats().refreshed, "15 Hz deadline must refresh position");
    renderer.render(camera,0,{},1.135);
    require(renderer.distantCacheStats().refreshed, "Frozen animation must not freeze the independent scenery clock");
    renderer.render(camera,.5,{},.5);
    require(renderer.distantCacheStats().refreshed, "A reset presentation clock must invalidate cache");
    camera.setPosition({100,0,0}); renderer.render(camera,.501,{},.501);
    require(renderer.distantCacheStats().refreshed, "Teleport must invalidate cache immediately");
    renderer.resize(400,240); compare(renderer,camera,.51,400,240);
    camera.cycleViewMode(); renderer.render(camera,.52,{},.52);
    require(renderer.distantCacheStats().tiles == 0, "Magnified scopes must use direct geometry");
    {
        viewer::Model nearOnly;
        quad(nearOnly,{0,0,-500},{60,0,0},{0,60,0},{0,1,0,1});
        viewer::Renderer emptyCache(nearOnly,VIEWER_SHADER_DIR);
        emptyCache.resize(320,180); emptyCache.setShadows(false);
        emptyCache.setDistantSceneCache(true);
        viewer::Camera walking; walking.setPosition({0,0,0}); walking.setOrientation(-90,0);
        emptyCache.render(walking,0,{},0);
        require(emptyCache.distantCacheStats().tiles == 0 && emptyCache.sceneStats().drawCalls == 1,
                "A near-only view must bypass cache allocation, patches and composition");
        walking.setPosition({0,0,2000}); emptyCache.render(walking,.01,{},.01);
        require(emptyCache.distantCacheStats().refreshed && emptyCache.distantCacheStats().patches > 0,
                "Moving into a view with distant work must activate the cache");
        walking.setPosition({0,0,0}); emptyCache.render(walking,.02,{},.02);
        require(emptyCache.distantCacheStats().tiles == 0 && emptyCache.sceneStats().drawCalls == 1,
                "Returning to a near-only view must restore direct rendering");
    }
    viewer::Renderer::checkErrors("distant cache regression");
}
}
int main() {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(400,240,"Distant cache regression",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window); viewer::initializeOpenGL();
    int result = 0;
    try { run(); std::cout << "Distant cache rendering passed (" << glGetString(GL_RENDERER) << ")\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
