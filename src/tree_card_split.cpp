#include "tree_card_split.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace viewer {
std::optional<TreeCardSplit> splitTreeCard(const Model& model, const Primitive& card) {
    if (card.vertices.size()!=4 || card.indices.size()!=6 || card.material<0 ||
        static_cast<std::size_t>(card.material)>=model.materials.size()) return std::nullopt;
    const auto& material=model.materials[card.material];
    if (material.alphaMode!="MASK" || material.baseColorTexture<0 ||
        static_cast<std::size_t>(material.baseColorTexture)>=model.textures.size()) return std::nullopt;
    const auto imageIndex=model.textures[material.baseColorTexture].image;
    if (imageIndex<0 || static_cast<std::size_t>(imageIndex)>=model.images.size()) return std::nullopt;
    const auto& image=model.images[imageIndex];
    if (image.width<32 || image.height<32 ||
        image.rgba.size()!=static_cast<std::size_t>(image.width)*image.height*4) return std::nullopt;
    std::array<const Vertex*,4> corners{}; // UV (0,0), (1,0), (0,1), (1,1)
    for (const auto& v:card.vertices) {
        const int u=v.uv.x>.5f, t=v.uv.y>.5f;
        if (glm::length(v.uv-glm::vec2(u,t))>1e-5f || corners[u+2*t]) return std::nullopt;
        corners[u+2*t]=&v;
    }
    for (const auto* corner:corners) if (!corner) return std::nullopt;
    const auto origin=corners[0]->position;
    const auto du=corners[1]->position-origin, dv=corners[2]->position-origin;
    if (std::abs(du.y)>1e-5f || dv.y>=-1e-5f ||
        std::abs(dv.x)+std::abs(dv.z)>1e-5f || glm::length(du)<1e-5f ||
        glm::length(corners[3]->position-origin-du-dv)>1e-5f) return std::nullopt;
    // Keep affine attributes: retriangulation must not change color/normal.
    for (const auto* v:corners)
        if (glm::length(v->normal-corners[0]->normal)>1e-5f ||
            glm::length(v->color-corners[0]->color)>1e-5f) return std::nullopt;
    const float opacity=material.baseColor.a*corners[0]->color.a;
    if (!std::isfinite(opacity) || opacity<=0 || material.alphaCutoff<=0) return std::nullopt;
    // Ignore isolated compression/paint residue far below the alpha test. Never
    // exclude a source texel that can itself pass the material cutoff.
    const int inkAlpha=static_cast<int>(std::clamp(std::ceil(material.alphaCutoff/opacity*255.f),1.f,16.f));
    const auto cross=glm::cross(card.vertices.at(card.indices[1]).position-card.vertices.at(card.indices[0]).position,
                                card.vertices.at(card.indices[2]).position-card.vertices.at(card.indices[0]).position);
    if (glm::length(cross)<1e-8f) return std::nullopt;
    std::vector<int> left(image.height,image.width),right(image.height,-1);
    int lastFoliage=-1, overallLeft=image.width,overallRight=-1;
    for (int y=0;y<image.height;++y) for (int x=0;x<image.width;++x) {
        const auto offset=(static_cast<std::size_t>(y)*image.width+x)*4;
        const auto* pixel=&image.rgba[offset];
        if (pixel[3]<inkAlpha) continue;
        left[y]=std::min(left[y],x); right[y]=std::max(right[y],x);
        overallLeft=std::min(overallLeft,x);overallRight=std::max(overallRight,x);
        // Strong foliage green, rather than weak green/moss on the root bark.
        if (pixel[3]>=16 && pixel[1]>pixel[0]*1.15f && pixel[1]>pixel[2]*1.15f &&
            int(pixel[1])-pixel[0]>=10) lastFoliage=y;
    }
    if (lastFoliage<0 || overallRight<=overallLeft) return std::nullopt;
    int lastWide=-1;
    for (int y=0;y<image.height;++y)
        if (right[y]-left[y]+1>(overallRight-overallLeft+1)*.25f) lastWide=y;
    // Four source texels protect bilinear sampling and the canopy/trunk seam.
    constexpr int guard=4;
    const int split=std::max(lastFoliage,lastWide)+1+guard;
    if (split<image.height/3 || split>=image.height*95/100) return std::nullopt;
    int trunkLeft=image.width,trunkRight=-1;
    for (int y=split-guard;y<image.height;++y) {
        trunkLeft=std::min(trunkLeft,left[y]);trunkRight=std::max(trunkRight,right[y]);
    }
    if (trunkRight<trunkLeft) return std::nullopt;
    const float u0=std::max(0,trunkLeft-guard)/float(image.width);
    const float u1=std::min(image.width,trunkRight+1+guard)/float(image.width);
    const float v=split/float(image.height);
    const float ratio=v+(1-v)*(u1-u0);
    if (ratio>.98f) return std::nullopt;
    TreeCardSplit result{card,v,{u0,u1},ratio};
    const bool forward=glm::dot(glm::cross(du,dv),cross)>0;
    const auto quad=[&](float a,float b,float c,float d) {
        const auto first=static_cast<std::uint32_t>(result.mesh.vertices.size());
        for (const auto uv: {glm::vec2(a,c),glm::vec2(b,c),glm::vec2(b,d),glm::vec2(a,d)}) {
            auto vertex=*corners[0];vertex.uv=uv;vertex.position=origin+du*uv.x+dv*uv.y;
            result.mesh.vertices.push_back(vertex);
        }
        const std::array<std::uint32_t,6> indices=forward
            ? std::array<std::uint32_t,6>{0,1,2,0,2,3} : std::array<std::uint32_t,6>{0,2,1,0,3,2};
        for (const auto index:indices) result.mesh.indices.push_back(first+index);
    };
    quad(0,1,0,v);quad(u0,u1,v,1);
    return result;
}
} // namespace viewer
