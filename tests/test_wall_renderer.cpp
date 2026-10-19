#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
constexpr int width=640,height=480;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void card(viewer::Model& model,glm::vec3 center,glm::vec2 size,int material,const std::string& name,
          float range=0,bool low=false,const std::string& group="",glm::mat4 transform=glm::mat4(1)){
    viewer::Primitive mesh;mesh.material=material;
    for(const auto xy:std::array<glm::vec2,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}}){
        viewer::Vertex v;v.position=center+glm::vec3(xy*size*.5f,0);v.normal={0,0,1};v.uv=xy*.5f+.5f;mesh.vertices.push_back(v);
    }
    mesh.indices={0,1,2,0,2,3};model.draws.push_back({model.primitives.size(),transform,name,range,low,group});model.primitives.push_back(mesh);
}
viewer::Model fixture(){
    viewer::Model model;model.materials.resize(5);
    for(auto& mat:model.materials)mat.unlit=true;
    model.materials[0].baseColor={1,0,0,1};model.materials[1].baseColor={0,0,1,1};model.materials[2].baseColor={0,1,0,1};
    model.materials[3].baseColor={.1f,.2f,.6f,.25f};model.materials[3].alphaMode="BLEND";model.materials[3].doubleSided=true;
    model.materials[4].unlit=false;
    const auto floor=glm::rotate(glm::translate(glm::mat4(1),{0,-1,-40}),glm::radians(-90.f),glm::vec3(1,0,0));
    card(model,{0,0,0},{220,300},4,"ground",0,false,"",floor);
    card(model,{4,3,-20},{8,6},0,"building_a_walls",40,true,"a");
    for(const auto panel:std::array<glm::vec4,4>{{{1.5f,3,3,6},{6.5f,3,3,6},{4,1,2,2},{4,5,2,2}}})
        card(model,{panel.x,panel.y,-20},{panel.z,panel.w},1,"building_a_detailed_walls",40,false,"a");
    card(model,{4,3,-21},{8,6},2,"background");
    card(model,{4,3,-20},{2,2},3,"building_a_windows",40,false,"a");
    const auto reflected=glm::scale(glm::translate(glm::mat4(1),{16,0,-90}),{-1.25f,1.5f,1});
    card(model,{0,3,0},{8,6},0,"building_b_walls",40,true,"b",reflected);
    card(model,{0,3,0},{8,6},1,"building_b_detailed_walls",40,false,"b",reflected);
    return model;
}
viewer::Camera view(glm::vec3 eye,int scopes=0){viewer::Camera camera;camera.setPosition(eye);camera.setOrientation(-90,0);for(int i=0;i<scopes;++i)camera.cycleViewMode();return camera;}
std::vector<unsigned char> pixels(){std::vector<unsigned char> result(width*height*3);glReadBuffer(GL_COLOR_ATTACHMENT0);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,result.data());return result;}
void compare(){
    viewer::Renderer renderer(fixture(),VIEWER_SHADER_DIR);renderer.resize(width,height);renderer.setHaze(false);renderer.setGroundFog(false);
    viewer::SunLighting sun;sun.direction={-1,1,1};renderer.setLighting(sun);
    std::vector<unsigned char> lit,shadowed;
    const std::array<viewer::Camera,5> views{{view({4,3,0}),view({4,3,160}),view({4,3,160},1),view({4,3,160},2),view({4,3,-40})}};
    for(int lighting=0;lighting<3;++lighting){
        renderer.setShadows(lighting!=0);renderer.setNight(lighting==2);renderer.setFlashlight(lighting==2);renderer.setFlashlightShadows(true);
        for(bool culling:{true,false})for(std::size_t i=0;i<views.size();++i){
            renderer.setSceneCulling(culling);renderer.setWallBatching(false);renderer.render(views[i]);const auto before=pixels();const auto original=renderer.sceneStats();
            renderer.setWallBatching(true);renderer.render(views[i]);const auto after=pixels();const auto batched=renderer.sceneStats();
            require(original.visible==batched.visible,"Wall batching changed source visibility");
            std::size_t changed=0;for(std::size_t p=0;p<before.size();++p)if(std::abs(int(before[p])-int(after[p]))>3)++changed;
            require(changed<before.size()/1000,"Wall batching changed openings, glass, reflection, scope or shadow pixels");
            if(i==1)require(batched.drawCalls<original.drawCalls,"Wholly far walls did not reduce draw calls");
            if(i==0||i==4)require(batched.drawCalls==original.drawCalls,"Cell with a near or offscreen near member must use individual walls");
            if(i==0&&lighting==0){const auto p=(height/2*width+width/2)*3;require(after[p+1]>after[p]+30,"Detailed opening was filled by a simple wall");}
            if(i==0&&culling&&lighting==0)lit=after;
            if(i==0&&culling&&lighting==1)shadowed=after;
            viewer::Renderer::checkErrors("wall batching pixel comparison");
        }
    }
    std::size_t darker=0;for(std::size_t p=0;p<lit.size();++p)if(int(lit[p])-int(shadowed[p])>20)++darker;
    require(darker>100,"Wall fixture did not exercise visible sun shadows");
    std::cout<<"Wall batching preserves pixels through mixed cells, scopes, culling and sun/flashlight passes\n";
}
}
int main(){if(!glfwInit())return 77;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);auto* window=glfwCreateWindow(width,height,"wall batching",nullptr,nullptr);if(!window){glfwTerminate();return 77;}glfwMakeContextCurrent(window);int code=0;try{viewer::initializeOpenGL();compare();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';code=1;}glfwDestroyWindow(window);glfwTerminate();return code;}
