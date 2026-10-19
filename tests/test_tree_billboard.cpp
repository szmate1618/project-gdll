#include "tree_billboard.hpp"
#include "forest_tree_visibility.hpp"
#include "tree_billboard_fixture.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void selection() {
    using Mode=viewer::TreeBillboardMode;
    require(viewer::treeBillboardMode({0,0,0},{0,0,99},100)==Mode::Full,"Near tree lost authored cards");
    require(viewer::treeBillboardMode({0,0,0},{100,0,0},100)==Mode::Side,"Horizontal distance boundary selected wrong LoD");
    require(viewer::treeBillboardMode({0,0,0},{0,101,0},100)==Mode::Top,"Distance ignored height above tree");
    require(viewer::treeBillboardMode({0,0,0},{0,-101,0},100)==Mode::Top,"Below-tree angle did not use the requested top texture");
    require(viewer::treeBillboardMode({0,0,0},{0,100,100},100)==Mode::Side,"45-degree boundary must keep the vertical card");
    require(viewer::treeBillboardMode({0,0,0},{0,101,100},100)==Mode::Top,"Above 45 degrees did not select top card");
    require(viewer::treeBillboardMode({0,0,0},{0,0,1000},0)==Mode::Full,"Zero must disable billboard LoD");
    auto model=billboard_test::asset();const auto layout=viewer::treeBillboardLayout(model);
    require(layout&&layout->sideDraw==0&&layout->topDraw==2,"Layout did not recognize the front and mid-canopy cards");
    std::swap(model.draws[0],model.draws[3]);
    require(viewer::treeBillboardLayout(model)->sideDraw==3,"Layout assumed fixed draw order");
    model.draws.pop_back();require(!viewer::treeBillboardLayout(model),"Unsupported asset lost authored geometry");
}
void bounds() {
    auto model=billboard_test::asset();
    for(auto& draw:model.draws)draw.transform=glm::translate(glm::mat4(1),{.3f,.4f,-.2f})*draw.transform;
    const auto layout=*viewer::treeBillboardLayout(model);
    auto transform=glm::rotate(glm::translate(glm::mat4(1),{11,7,-19}),.7f,glm::vec3(0,1,0));
    transform=glm::scale(transform,{-2,3,4});transform[1].x+=.6f;transform[0].y+=.3f;
    const auto tree=billboard_test::instance(model,transform);
    const auto bounds=viewer::treeBillboardBounds(tree,model,layout);
    const auto include=[&](glm::vec3 p){require(glm::all(glm::greaterThanEqual(p,bounds.minimum-glm::vec3(.001f)))&&
        glm::all(glm::lessThanEqual(p,bounds.maximum+glm::vec3(.001f))),"Rotated card escaped conservative bounds");};
    const glm::vec3 root(transform[3]);
    const auto& side=model.draws[layout.sideDraw];
    const auto& top=model.draws[layout.topDraw];
    const glm::vec3 pivot(transform*top.transform*glm::vec4(layout.topPivot,1));
    for(int i=0;i<36;++i){
        const float angle=i*glm::radians(10.f);
        for(const auto& v:model.primitives[side.primitive].vertices){
            const glm::vec3 p(transform*side.transform*glm::vec4(v.position,1));
            include(root+glm::vec3(glm::rotate(glm::mat4(1),angle,glm::vec3(0,1,0))*glm::vec4(p-root,0)));
        }
        for(const auto axis:{glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,0,1),glm::normalize(glm::vec3(1,2,3))})
            for(const auto& v:model.primitives[top.primitive].vertices){
                const glm::vec3 p(transform*top.transform*glm::vec4(v.position,1));
                include(pivot+glm::vec3(glm::rotate(glm::mat4(1),angle,axis)*glm::vec4(p-pivot,0)));
            }
    }
    auto edgeModel=billboard_test::asset();const auto edgeLayout=*viewer::treeBillboardLayout(edgeModel);
    const auto edge=billboard_test::instance(edgeModel,glm::translate(glm::mat4(1),{2.2f,0,0}));
    std::vector<std::size_t> visible;
    viewer::ForestTreeVisibility({edge},{}).query(glm::mat4(1),{},true,visible);
    require(visible.empty(),"Culling fixture must have its authored cards outside the view");
    viewer::ForestTreeVisibility({edge},{},{viewer::treeBillboardBounds(edge,edgeModel,edgeLayout)}).query(glm::mat4(1),{},true,visible);
    require(visible.size()==1,"Camera index omitted a billboard that can turn into the view");
}
}
int main(){try{selection();bounds();std::cout<<"Tree billboard layout, selection and bounds passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
