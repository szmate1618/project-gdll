#include "renderer.hpp"
#include "inventory/inventory_config.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> data(800*600*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,800,600,GL_RGB,GL_UNSIGNED_BYTE,data.data());
    return data;
}
}
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(800,600,"Ground items",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    try {
        viewer::initializeOpenGL();
        const std::filesystem::path root(VIEWER_ITEM_ROOT);
        auto inventory=viewer::loadPlayerInventory(root);
        auto floor=viewer::BoxLayer::unitCube();
        floor.materials[0].baseColor={.55f,.55f,.55f,1};
        floor.draws.push_back({0,glm::scale(glm::mat4(1),glm::vec3(8,.1f,8)),"floor"});
        floor.boundsMin={-4,-.05f,-4}; floor.boundsMax={4,.05f,4};
        std::array<viewer::Model,viewer::weaponIds.size()> models;
        for (std::size_t i=0; i<models.size(); ++i)
            models[i]=viewer::loadModel(std::filesystem::path(VIEWER_WEAPON_ASSETS)/(std::string(viewer::weaponIds[i])+".glb"));
        viewer::GroundItems ground(std::make_shared<viewer::PhysicsWorld>());
        ground.setModels(models);
        viewer::Renderer renderer(floor,root/"shaders");
        renderer.resize(800,600); renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
        renderer.setFlashlight(false); renderer.setGroundItems(ground,models);
        viewer::Camera camera; camera.setPosition({0,.9f,1.2f}); camera.setOrientation(-90,-33);
        renderer.render(camera); const auto empty=pixels();
        ground.add(*inventory.find("pistol_rounds_1"),23,{}, {-.25f,.1f,0});
        renderer.render(camera); const auto ammo=pixels();
        ground.add(*inventory.find("pistol"),0,viewer::WeaponItem{0,7,0},{.25f,.15f,0});
        renderer.render(camera); const auto weapon=pixels();
        const auto changed=[](const auto& a,const auto& b) {
            std::size_t count=0;
            for (std::size_t i=0;i<a.size();i+=3)
                if (std::abs(int(a[i])-b[i])+std::abs(int(a[i+1])-b[i+1])+std::abs(int(a[i+2])-b[i+2])>12) ++count;
            return count;
        };
        require(changed(empty,ammo)>100,"Dropped ammo box did not render");
        require(changed(ammo,weapon)>100,"Dropped weapon model did not render");
        require(glGetError()==GL_NO_ERROR,"Ground item rendering generated a GL error");
        if (argc>1) renderer.writeScreenshot(argv[1]);
        std::cout<<"Ground item rendering checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; glfwDestroyWindow(window); glfwTerminate(); return 1; }
    glfwDestroyWindow(window); glfwTerminate(); return 0;
}
