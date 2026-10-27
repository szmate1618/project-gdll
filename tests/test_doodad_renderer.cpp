#include "renderer.hpp"
#include "collision_world.hpp"
#include "doodad_layer.hpp"
#include "doodad_state.hpp"
#include "door_layer.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int width = 1000, height = 700;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

viewer::Model groundModel() {
    viewer::Model model;
    viewer::Material material;
    material.baseColor = {.28f,.32f,.20f,1};
    material.unlit = true;
    model.materials.push_back(material);
    viewer::Primitive ground;
    for (const auto p : {glm::vec3(-8,0,8), glm::vec3(8,0,8), glm::vec3(8,0,-8), glm::vec3(-8,0,-8)}) {
        viewer::Vertex vertex;
        vertex.position = p;
        vertex.normal = {0,1,0};
        ground.vertices.push_back(vertex);
    }
    ground.indices = {0,1,2,0,2,3};
    ground.material = 0;
    model.primitives.push_back(ground);
    viewer::DrawInstance draw;
    draw.name = "terrain";
    model.draws.push_back(draw);
    model.boundsMin = {-8,0,-8};
    model.boundsMax = {8,0,8};
    return model;
}

struct Pixels {
    std::vector<unsigned char> color = std::vector<unsigned char>(width*height*4);
    std::vector<float> depth = std::vector<float>(width*height);
    Pixels() {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,color.data());
        glReadPixels(0,0,width,height,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
    }
};

void requireVisibleSurface(const viewer::DoodadRenderInstance& instance,
                           const glm::mat4& projectionView, const Pixels& before, const Pixels& after) {
    glm::vec2 low(width,height), high(0);
    for (int corner=0; corner<8; ++corner) {
        const glm::vec3 p((corner&1) ? instance.maximum.x : instance.minimum.x,
                          (corner&2) ? instance.maximum.y : instance.minimum.y,
                          (corner&4) ? instance.maximum.z : instance.minimum.z);
        const auto clip = projectionView*glm::vec4(p,1);
        require(clip.w>0, "Doodad capture bounds must remain in front of the camera");
        const glm::vec2 pixel = (glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(width,height);
        low = glm::min(low,pixel);
        high = glm::max(high,pixel);
    }
    const int left = std::max(0,static_cast<int>(std::floor(low.x)));
    const int right = std::min(width-1,static_cast<int>(std::ceil(high.x)));
    const int bottom = std::max(0,static_cast<int>(std::floor(low.y)));
    const int top = std::min(height-1,static_cast<int>(std::ceil(high.y)));
    std::size_t opaquePixels = 0;
    for (int y=bottom; y<=top; ++y) for (int x=left; x<=right; ++x) {
        const auto index = static_cast<std::size_t>(y*width+x);
        int difference = 0;
        for (std::size_t channel=0; channel<3; ++channel)
            difference = std::max(difference,std::abs(int(after.color[index*4+channel])-int(before.color[index*4+channel])));
        if (difference>6 && after.depth[index]<before.depth[index]-1e-7f) ++opaquePixels;
    }
    require(opaquePixels>12, "Each actual doodad mesh must visibly cover and occlude its supporting ground");
}

void checkRendering(const std::filesystem::path& capture) {
    const auto ground = groundModel();
    viewer::CollisionWorld collision(ground);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics,collision,ground.boundsMin,ground.boundsMax);
    viewer::DoorLayer doors({},physics);
    viewer::ForestLayer forests;
    viewer::DoodadPlacementGenerator generator(forests,collision);
    viewer::DoodadState state("doodad-render-fixture");
    std::array<viewer::Model,viewer::doodadKindCount> models;
    constexpr std::array<const char*,viewer::doodadKindCount> names{"branch","stone","beer_bottle"};
    for (std::size_t i=0; i<models.size(); ++i) {
        models[i] = viewer::loadModel(std::filesystem::path(VIEWER_DOODAD_ASSETS)/(std::string(names[i])+".glb"));
        viewer::DoodadPlacement pose;
        pose.id = 100+i;
        pose.kind = static_cast<viewer::DoodadKind>(i);
        pose.position = {-.7f+.7f*static_cast<float>(i),.004f,0};
        state.set(pose);
    }
    viewer::DoodadLayer layer(physics,generator,state,models);
    viewer::Camera camera;
    camera.setPosition({1.2f,1.5f,2.7f});
    const auto direction = glm::normalize(glm::vec3(0,.04f,0)-camera.position());
    camera.setOrientation(glm::degrees(std::atan2(direction.z,direction.x)),glm::degrees(std::asin(direction.y)));
    layer.update(camera.position(),scene,doors);
    require(layer.instances().size()==3, "Moved doodads must stream into nonforest destination cells");
    viewer::Renderer renderer(ground,VIEWER_SHADER_DIR);
    renderer.resize(width,height);
    renderer.setShadows(false);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.render(camera);
    const Pixels before;
    renderer.setDoodads(layer,models);
    renderer.render(camera);
    require(renderer.doodadVisibleCount()==3, "All three doodad kinds must pass the camera visibility query");
    const Pixels after;
    const auto projectionView = camera.projection(float(width)/height)*camera.view();
    for (const auto& instance : layer.instances()) requireVisibleSurface(instance,projectionView,before,after);
    if (!capture.empty()) renderer.writeScreenshot(capture);
    renderer.setShadows(true);
    renderer.render(camera);
    require(renderer.doodadVisibleCount()==3, "Shadow rendering must retain the camera's doodad population");
    viewer::Renderer::checkErrors("doodad camera and shadow rendering");
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width,height,"doodad rendering regression",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        std::cout << "GL renderer: " << glGetString(GL_RENDERER) << '\n';
        checkRendering(argc>1 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
        std::cout << "Doodad camera, depth, nonforest destinations and shadow rendering passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
