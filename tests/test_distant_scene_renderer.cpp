#include "renderer.hpp"
#include <GLFW/glfw3.h>
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
float depth(int x, int y) {
    float value = 0;
    glReadPixels(x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&value);
    return value;
}
void depthState(viewer::SceneDepthMode mode) {
    GLint function = 0;
    GLboolean write = GL_FALSE;
    GLdouble clear = -1;
    glGetIntegerv(GL_DEPTH_FUNC,&function);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE,&clear);
    const bool reversed = mode == viewer::SceneDepthMode::Reversed32;
    require(function == (reversed ? GL_GREATER : GL_LESS) && write == GL_TRUE,
            "Cache composition must restore foreground depth comparison and writes");
    require(clear == (reversed ? 0 : 1), "Cache capture must preserve the scene depth clear value");
}
void compare(viewer::Renderer& renderer, const viewer::Camera& camera, double seconds, int width, int height) {
    renderer.setDistantSceneCache(true);
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
    require(errors < static_cast<std::size_t>(width*height/50), "A fresh rectangle must match direct static rendering");
    renderer.setDistantSceneCache(true);
}
void run(viewer::SceneDepthMode mode) {
    viewer::Model model;
    quad(model, {0,0,-1500}, {650,0,0}, {0,350,0}, {1,0,0,1});
    quad(model, {0,0,-1700}, {800,0,0}, {0,400,0}, {1,0,1,1});
    quad(model, {1700,0,0}, {0,0,600}, {0,350,0}, {0,0,1,1});
    quad(model, {0,0,1800}, {650,0,0}, {0,350,0}, {1,1,0,1});
    quad(model, {0,0,-500}, {60,0,0}, {0,60,0}, {0,1,0,1});
    quad(model, {0,1700,0}, {650,0,0}, {0,0,650}, {1,1,1,1});
    // A transformed map-wide surface spans both sides of the 1 km boundary.
    quad(model, {0,-200,0}, {2400,0,0}, {0,0,2400}, {0,1,1,1});
    model.draws.back().transform[3] = {80,0,-70,1};
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.setSceneDepthMode(mode);
    renderer.resize(320,180); renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    renderer.setDistantSceneCache(true);
    viewer::Camera camera; camera.setPosition({0,0,0}); camera.setOrientation(-90,0);
    renderer.render(camera,0,[&] { depthState(mode); },0);
    require(renderer.distantCacheStats().refreshed && renderer.distantCacheStats().active, "First draw must populate cache");
    const auto center = pixels(320,180);
    const auto index = static_cast<std::size_t>((90*320+160)*3);
    require(center[index] == 0 && center[index+1] == 255, "Near geometry must occlude cached geometry");
    const auto farIndex = static_cast<std::size_t>((90*320+200)*3);
    require(center[farIndex] == 255 && center[farIndex+1] == 0 && center[farIndex+2] == 0,
            "Cache capture must keep the nearer of overlapping distant surfaces");
    const float clear = mode == viewer::SceneDepthMode::Reversed32 ? 0 : 1;
    require(depth(200,90) == clear, "Cached scenery must not publish its proxy depth into the scene");
    require(std::abs(depth(160,90)-clear) > 1e-7f, "Following foreground geometry must write scene depth");
    renderer.render(camera,.001,{},.001);
    require(!renderer.distantCacheStats().refreshed,
            "Stationary reuse must avoid distant redraws");
    camera.setOrientation(0,0); renderer.render(camera,.002,{},.002);
    require(!renderer.distantCacheStats().refreshed && renderer.distantCacheStats().active,
            "A turn must reuse the rectangle and accept gaps until the next refresh");
    camera.setOrientation(-90,0); renderer.render(camera,.003,{},.003);
    require(!renderer.distantCacheStats().refreshed, "Returning must reuse the same captured rectangle");
    for (const float yaw : {-90.0f,45.0f}) {
        camera.setOrientation(yaw,-5); compare(renderer,camera,.004,320,180);
    }
    camera.setOrientation(45,89); compare(renderer,camera,.005,320,180);
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
    for (int scope = 0; scope < 2; ++scope) {
        camera.cycleViewMode(); renderer.render(camera,.52 + scope*.001,{},.52 + scope*.001);
        require(!renderer.distantCacheStats().active, "Magnified scopes must use direct geometry");
        compare(renderer,camera,.53 + scope*.001,400,240);
    }
    {
        viewer::Model nearOnly;
        quad(nearOnly,{0,0,-500},{60,0,0},{0,60,0},{0,1,0,1});
        viewer::Renderer emptyCache(nearOnly,VIEWER_SHADER_DIR);
        emptyCache.setSceneDepthMode(mode);
        emptyCache.resize(320,180); emptyCache.setShadows(false);
        emptyCache.setDistantSceneCache(true);
        viewer::Camera walking; walking.setPosition({0,0,0}); walking.setOrientation(-90,0);
        emptyCache.render(walking,0,{},0);
        require(!emptyCache.distantCacheStats().active && emptyCache.sceneStats().drawCalls == 1,
                "A near-only view must bypass cache allocation and composition");
        walking.setPosition({0,0,2000}); emptyCache.render(walking,.01,{},.01);
        require(emptyCache.distantCacheStats().refreshed && emptyCache.distantCacheStats().active,
                "Moving into a view with distant work must activate the cache");
        walking.setPosition({0,0,0}); emptyCache.render(walking,.02,{},.02);
        require(!emptyCache.distantCacheStats().active && emptyCache.sceneStats().drawCalls == 1,
                "Returning to a near-only view must restore direct rendering");
    }
    if (mode == viewer::SceneDepthMode::Reversed32) {
        viewer::Model closeSurfaces;
        quad(closeSurfaces,{0,0,-1500.001f},{650,0,0},{0,350,0},{1,0,1,1});
        quad(closeSurfaces,{0,0,-1500},{650,0,0},{0,350,0},{1,0,0,1});
        viewer::Renderer precise(closeSurfaces,VIEWER_SHADER_DIR);
        precise.setSceneDepthMode(mode); precise.resize(320,180); precise.setShadows(false);
        precise.setHaze(false); precise.setGroundFog(false); precise.setDistantSceneCache(true);
        viewer::Camera view; view.setPosition({0,0,0}); view.setOrientation(-90,0);
        precise.render(view,0,{},0);
        const auto image = pixels(320,180);
        require(image[index] == 255 && image[index+1] == 0 && image[index+2] == 0,
                "Reversed cache capture must resolve distant surfaces separated by one millimeter");
        compare(precise,view,.001,320,180);
        // Change the convention while the rectangle is reusable and its size is
        // unchanged. Private capture depth must be reallocated and refreshed.
        precise.setSceneDepthMode(viewer::SceneDepthMode::Conventional24);
        precise.render(view,.002,[&] { depthState(viewer::SceneDepthMode::Conventional24); },.002);
        require(precise.distantCacheStats().refreshed, "Changing capture depth convention must refresh the cache");
        precise.setSceneDepthMode(mode);
        precise.render(view,.003,[&] { depthState(mode); },.003);
        require(precise.distantCacheStats().refreshed, "Returning to reversed depth must refresh the cache");
        const auto restored = pixels(320,180);
        require(restored[index] == 255 && restored[index+1] == 0 && restored[index+2] == 0,
                "Reallocated reversed cache must retain millimeter depth precision");
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
    try {
        const bool reversed = viewer::sceneClipControl() != nullptr;
        for (const auto mode : {viewer::SceneDepthMode::Conventional24, viewer::SceneDepthMode::Conventional32,
                               viewer::SceneDepthMode::Reversed32}) {
            if (mode == viewer::SceneDepthMode::Reversed32 && !reversed) continue;
            run(mode);
        }
        std::cout << "Distant cache rendering passed (" << glGetString(GL_RENDERER) << ")\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
