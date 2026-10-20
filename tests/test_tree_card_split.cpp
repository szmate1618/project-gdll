#include "tree_card_split.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
viewer::Model fixture() {
    viewer::Model model;viewer::ImageData image;image.width=image.height=128;image.rgba.resize(128*128*4);
    const auto paint=[&](int x,int y,bool green) {
        const auto i=(y*128+x)*4;image.rgba[i]=green?40:90;image.rgba[i+1]=green?120:75;
        image.rgba[i+2]=40;image.rgba[i+3]=255;
    };
    for(int y=4;y<80;++y)for(int x=8;x<120;++x)paint(x,y,true);
    for(int y=80;y<124;++y)for(int x=60;x<68;++x)paint(x,y,false);
    // Asymmetric root flare: all source alpha must survive the narrower card.
    for(int y=120;y<124;++y)for(int x=48;x<72;++x)paint(x,y,false);
    model.images.push_back(image);viewer::Texture texture;texture.image=0;model.textures.push_back(texture);
    viewer::Material material;material.alphaMode="MASK";material.baseColorTexture=0;material.doubleSided=material.unlit=true;
    model.materials.push_back(material);viewer::Primitive mesh;mesh.material=0;
    for(const auto uv:{glm::vec2(0,0),glm::vec2(0,1),glm::vec2(1,1),glm::vec2(1,0)}) {
        viewer::Vertex vertex;vertex.uv=uv;vertex.position={uv.x*4-2,8*(1-uv.y),0};vertex.normal={0,0,1};mesh.vertices.push_back(vertex);
    }
    mesh.indices={0,1,2,0,2,3};model.primitives.push_back(mesh);return model;
}
void check() {
    auto model=fixture();const auto split=viewer::splitTreeCard(model,model.primitives[0]);
    require(bool(split),"Bare-trunk card not recognized");
    require(split->mesh.vertices.size()==12 && split->mesh.indices.size()==18,"Original and replacement ranges missing");
    require(split->canopyV>=80.f/128 && split->canopyV<90.f/128,"Wrong canopy boundary/image orientation");
    require(split->trunkU.x<=48.f/128 && split->trunkU.y>=72.f/128,"Root flare clipped");
    require(split->areaRatio<.8f,"Transparent trunk space not removed");
    for(int y=0;y<128;++y)for(int x=0;x<128;++x)if(model.images[0].rgba[(y*128+x)*4+3]) {
        const glm::vec2 uv((x+.5f)/128,(y+.5f)/128);
        require(uv.y<=split->canopyV || (uv.x>=split->trunkU.x && uv.x<=split->trunkU.y),"Visible texel clipped");
    }
    for(std::size_t i=6;i<split->mesh.indices.size();i+=3) {
        const auto a=split->mesh.vertices[split->mesh.indices[i]].position;
        const auto b=split->mesh.vertices[split->mesh.indices[i+1]].position;
        const auto c=split->mesh.vertices[split->mesh.indices[i+2]].position;
        require(glm::cross(b-a,c-a).z>0,"Replacement winding flipped");
    }
    for(const auto& v:split->mesh.vertices)
        require(glm::length(v.position-glm::vec3(v.uv.x*4-2,8*(1-v.uv.y),0))<1e-6f,"UV/position interpolation changed");
    // Almost invisible paint residue must not stretch the trunk to card width.
    model.images[0].rgba[(120*128+2)*4+3]=1;
    const auto residue=viewer::splitTreeCard(model,model.primitives[0]);
    require(residue && residue->trunkU==split->trunkU && residue->canopyV==split->canopyV,"Faint residue widened trunk");
    // A low-cutoff material can make that same source texel visible.
    model.materials[0].alphaCutoff=1.f/255;
    const auto lowCutoff=viewer::splitTreeCard(model,model.primitives[0]);
    require(!lowCutoff || lowCutoff->trunkU.x<=2.f/128,"Low-cutoff source pixel clipped");
    model.materials[0].alphaCutoff=.5f;
    // A low dangling leaf moves the split down instead of being cropped away.
    auto& pixels=model.images[0].rgba;const auto leaf=(92*128+64)*4;
    pixels[leaf]=20;pixels[leaf+1]=130;pixels[leaf+2]=40;pixels[leaf+3]=255;
    require(viewer::splitTreeCard(model,model.primitives[0])->canopyV>92.f/128,"Low leaf left in trunk region");
    auto horizontal=model.primitives[0];for(auto& v:horizontal.vertices)std::swap(v.position.y,v.position.z);
    require(!viewer::splitTreeCard(model,horizontal),"Horizontal card split");
    for(std::size_t i=0;i<pixels.size();i+=4)pixels[i+1]=pixels[i];
    require(!viewer::splitTreeCard(model,model.primitives[0]),"Bare asset split without foliage evidence");
}
}
int main(){try{check();std::cout<<"Tree card split regressions passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
