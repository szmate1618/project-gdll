#include "renderer.hpp"
#include "dog_layer.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> image(1200*800*3); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT,1); glReadPixels(0,0,1200,800,GL_RGB,GL_UNSIGNED_BYTE,image.data()); return image;
}
}
int main(int argc,char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(1200,800,"Dog rendering",nullptr,nullptr);
    if (!window) {glfwTerminate();return 77;}
    glfwMakeContextCurrent(window); int result=0;
    try {
        const std::filesystem::path output=argc>1 ? argv[1] : "build/dog-render-check";
        std::filesystem::create_directories(output);
        std::vector<glm::mat4> placements;
        for (int row=0;row<3;++row) for(int variant=0;variant<3;++variant)
            placements.push_back(glm::translate(glm::mat4(1),{float(variant-1)*2,0,float(-row)*2})*
                glm::rotate(glm::mat4(1),-1.57079633f,{0,1,0}));
        auto dogs=viewer::loadDogLayer(DOG_ASSET_DIR,placements);
        for(std::size_t i=0;i<dogs.instances.size();++i) {
            auto& dog=dogs.instances[i]; dog.phase=0;
            dog.walking=i>=3 && i<6; dog.running=dog.walking; dog.walkSeconds=.2;
            dog.attacking=i>=6; dog.attackSeconds=.3;
        }
        viewer::Renderer renderer(viewer::Model{},VIEWER_SHADER_DIR,nullptr,nullptr,nullptr,&dogs);
        dogs.releaseAnimationVertices();
        renderer.resize(1200,800); renderer.setHaze(false); renderer.setGroundFog(false);
        viewer::Camera camera; camera.setPosition({0,5,8}); camera.setOrientation(-90,-30);
        renderer.render(camera,.2); require(renderer.dogVisibleCount()==9,"Missing approved dog appearances");
        renderer.writeScreenshot(output/"variants-and-clips.ppm"); const auto first=pixels();
        viewer::CollisionWorld empty(viewer::Model{});
        std::optional<viewer::DogShotTrace> displayedHit;
        glm::vec3 shotOrigin{0};
        for(float y:{.2f,.4f,.6f}) {
            if(const auto hit=renderer.traceDogShot({0,y,6},{0,0,-1},empty)) {displayedHit=hit;shotOrigin={0,y,6};break;}
        }
        require(displayedHit.has_value(),"Displayed dog cannot be shot");
        const auto displayedPopulation=dogs.instances;
        for(auto& dog:dogs.instances) {dog.root[3].x+=1000;dog.transform[3].x+=1000;}
        const auto retained=renderer.traceDogShot(shotOrigin,{0,0,-1},empty);
        require(retained && retained->dog==displayedHit->dog && std::abs(retained->distance-displayedHit->distance)<.0001f,
            "Incoming dog motion overwrote the last displayed hit pose");
        dogs.instances=displayedPopulation;
        for(auto& dog:dogs.instances) {dog.walkSeconds=.6;dog.attackSeconds=.6;}
        renderer.render(camera,.6); const auto second=pixels();
        std::size_t changed=0;for(std::size_t i=0;i<first.size();++i) if(std::abs(int(first[i])-int(second[i]))>4) ++changed;
        require(changed>1000,"Dog animations did not change the rendered geometry");
        for(float distance:{25.f,50.f,100.f}) {
            camera.setPosition({0,1,distance});camera.setOrientation(-90,0);
            renderer.render(camera,.6);require(renderer.dogVisibleCount()==9,"A dog LoD disappeared");
            renderer.writeScreenshot(output/("lod-"+std::to_string(int(distance))+".ppm"));
        }
        // Dead and hidden states share neither the humanoid state nor buffers.
        dogs.damage(0,100); dogs.instances[1].removed=true;
        camera.setPosition({0,5,8});camera.setOrientation(-90,-30);
        renderer.render(camera,.6);require(renderer.dogVisibleCount()==8,"Hidden dog still rendered");
        renderer.writeScreenshot(output/"death-and-hidden.ppm");
        viewer::Renderer::checkErrors("dog rendering and LoDs");
        std::vector<glm::mat4> crowdPlacements;
        for(int id=0;id<1000;++id)
            crowdPlacements.push_back(glm::translate(glm::mat4(1),{float(id%40)*2-39,0,float(id/40)*2-24}));
        auto crowd=viewer::loadDogLayer(DOG_ASSET_DIR,crowdPlacements);
        viewer::Renderer crowdRenderer(viewer::Model{},VIEWER_SHADER_DIR,nullptr,nullptr,nullptr,&crowd);
        crowd.releaseAnimationVertices(); crowdRenderer.resize(1200,800);
        camera.frame({-41,0,-26},{41,1,26},1.5f);
        crowdRenderer.render(camera,.5);
        require(crowdRenderer.dogVisibleCount()==1000,"Expected 1000 dogs in independent render buffers");
        crowdRenderer.writeScreenshot(output/"population-1000.ppm");
        viewer::Renderer::checkErrors("1000 dog rendering");
        std::cout<<"Dog rendering passed on "<<glGetString(GL_RENDERER)<<"; changed channels "<<changed<<'\n';
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
