#include "forest_bush.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace viewer {
namespace {
struct PixelBounds { int left, top, right, bottom; };
int atlasSplit(const ImageData& image) {
    // Image generation can shift the transparent divider a little. Prefer
    // its nearest empty column so one view cannot leak into the other's UVs.
    const int middle=image.width/2;
    for (int offset=0;offset<=image.width/8;++offset) for (const int x:{middle+offset,middle-offset}) {
        bool empty=true;
        for (int y=0;y<image.height&&empty;++y)
            empty=image.rgba[(static_cast<std::size_t>(y)*image.width+x)*4+3]<90;
        if (empty) return x;
    }
    return middle;
}
PixelBounds silhouette(const ImageData& image,int first,int last) {
    PixelBounds bounds{last,image.height,first,0};
    for (int y=0;y<image.height;++y) for (int x=first;x<last;++x) {
        // Same threshold as the material, so padding does not change size.
        if (image.rgba[(static_cast<std::size_t>(y)*image.width+x)*4+3]<90) continue;
        bounds.left=std::min(bounds.left,x); bounds.right=std::max(bounds.right,x+1);
        bounds.top=std::min(bounds.top,y); bounds.bottom=std::max(bounds.bottom,y+1);
    }
    if (bounds.left>=bounds.right||bounds.top>=bounds.bottom)
        throw std::runtime_error("Bush atlas requires visible side and top silhouettes");
    return bounds;
}
void card(Primitive& mesh,const std::array<glm::vec3,4>& positions,
          glm::vec3 normal,PixelBounds pixels,int width,int height) {
    const float left=static_cast<float>(pixels.left)/width,right=static_cast<float>(pixels.right)/width;
    const float top=static_cast<float>(pixels.top)/height,bottom=static_cast<float>(pixels.bottom)/height;
    const std::array<glm::vec2,4> uv{{{left,top},{left,bottom},{right,bottom},{right,top}}};
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t i=0;i<4;++i) {
        Vertex vertex; vertex.position=positions[i]; vertex.normal=normal; vertex.uv=uv[i];
        mesh.vertices.push_back(vertex);
    }
    for (const auto i:{0u,1u,2u,0u,2u,3u}) mesh.indices.push_back(first+i);
}
}

Model forestBushModel(ImageData atlas) {
    if (atlas.width<2||atlas.width%2||atlas.height<1||
        atlas.rgba.size()!=static_cast<std::size_t>(atlas.width)*atlas.height*4)
        throw std::runtime_error("Bush atlas must contain two equal-width RGBA cells");
    const auto split=atlasSplit(atlas);
    const auto side=silhouette(atlas,0,split);
    const auto top=silhouette(atlas,split,atlas.width);
    Model model;
    Texture texture; texture.image=0; texture.wrapS=texture.wrapT=33071; // Clamp to edge.
    model.textures.push_back(texture);
    Material material; material.baseColorTexture=0; material.unlit=true;
    material.doubleSided=true; material.alphaMode="MASK"; material.alphaCutoff=.35f;
    model.materials.push_back(material);
    Primitive mesh; mesh.material=0;
    card(mesh,{{{-2,1,0},{-2,0,0},{2,0,0},{2,1,0}}},{0,0,1},side,atlas.width,atlas.height);
    card(mesh,{{{0,1,2},{0,0,2},{0,0,-2},{0,1,-2}}},{1,0,0},side,atlas.width,atlas.height);
    card(mesh,{{{-2,.5f,-2},{-2,.5f,2},{2,.5f,2},{2,.5f,-2}}},{0,1,0},top,atlas.width,atlas.height);
    model.primitives.push_back(std::move(mesh));
    model.draws.push_back({0,glm::mat4(1),"forest_bush"});
    model.boundsMin={-2,0,-2}; model.boundsMax={2,1,2};
    model.images.push_back(std::move(atlas));
    return model;
}
} // namespace viewer
