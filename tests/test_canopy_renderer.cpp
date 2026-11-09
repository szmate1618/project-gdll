#include "canopy_fixture.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(1000,750,"Canopy",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    try {
        viewer::initializeOpenGL();
        canopy_fixture::Shelter fixture;
        std::array<viewer::Model,viewer::weaponIds.size()> models;
        for (std::size_t s=0; s<models.size(); ++s)
            models[s]=viewer::loadModel(std::filesystem::path(VIEWER_WEAPON_ASSETS)/(std::string(viewer::weaponIds[s])+".glb"));
        fixture.items.setModels(models);
        fixture.plant(); fixture.drape();
        viewer::Renderer renderer(fixture.model,fixture.root/"shaders");
        renderer.resize(1000,750); renderer.setHaze(false); renderer.setGroundFog(false);
        renderer.setGroundItems(fixture.items,models);
        viewer::Camera camera; camera.setPosition({3,2.6f,4}); camera.setOrientation(-126,-18);
        renderer.setObjectShadows(false);
        renderer.render(camera);
        std::vector<unsigned char> unshadowed(1000*750*3),shadowed(unshadowed.size());
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,1000,750,GL_RGB,GL_UNSIGNED_BYTE,unshadowed.data());
        renderer.setObjectShadows(true);
        renderer.render(camera);
        glReadPixels(0,0,1000,750,GL_RGB,GL_UNSIGNED_BYTE,shadowed.data());
        std::size_t changed=0;
        for (std::size_t i=0; i<shadowed.size(); i+=3)
            if (std::abs(int(shadowed[i])-unshadowed[i])+std::abs(int(shadowed[i+1])-unshadowed[i+1])+
                std::abs(int(shadowed[i+2])-unshadowed[i+2])>12) ++changed;
        if (changed<500) throw std::runtime_error("Canopy must cast a visible shelter shadow");
        if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("Canopy rendering generated a GL error");
        if (argc>1) {
            renderer.writeScreenshot(std::string(argv[1])+".ppm");
            camera.setPosition({.35f,.85f,4.8f}); camera.setOrientation(-98,3);
            renderer.render(camera); renderer.writeScreenshot(std::string(argv[1])+"-low.ppm");
        }
        std::cout<<"Canopy rendering checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; glfwDestroyWindow(window); glfwTerminate(); return 1; }
    glfwDestroyWindow(window); glfwTerminate(); return 0;
}
