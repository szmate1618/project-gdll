#include "shrub_cover.hpp"
#include "tree_layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace viewer {
namespace {
constexpr std::size_t noImage = std::numeric_limits<std::size_t>::max();
struct Bounds {
    glm::vec3 low{std::numeric_limits<float>::max()};
    glm::vec3 high{std::numeric_limits<float>::lowest()};
    void include(glm::vec3 p) { low=glm::min(low,p); high=glm::max(high,p); }
};
bool finite(glm::vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
void validateTransform(const glm::mat4& transform) {
    for (int i=0;i<4;++i) for (int j=0;j<4;++j)
        if (!std::isfinite(transform[i][j])) throw std::runtime_error("Shrub cover requires finite transforms");
    const auto determinant=glm::determinant(glm::mat3(transform));
    if (!std::isfinite(determinant)||std::abs(determinant)<1e-12f||
        transform[0][3]!=0||transform[1][3]!=0||transform[2][3]!=0||transform[3][3]!=1)
        throw std::runtime_error("Shrub cover requires nondegenerate affine transforms");
}
bool intersects(const Bounds& bounds,glm::vec3 start,glm::vec3 delta) {
    float first=0,last=1;
    for (int axis=0;axis<3;++axis) {
        if (delta[axis]==0) {
            if (start[axis]<bounds.low[axis]||start[axis]>bounds.high[axis]) return false;
        } else {
            const float a=(bounds.low[axis]-start[axis])/delta[axis];
            const float b=(bounds.high[axis]-start[axis])/delta[axis];
            first=std::max(first,std::min(a,b)); last=std::min(last,std::max(a,b));
            if (first>last) return false;
        }
    }
    return true;
}
struct AlphaImage {
    int width=0,height=0;
    std::vector<std::uint8_t> pixels;
};
// Normalize first to keep conversion to texel indices bounded even for repeated
// UVs. Source texels keep authority independent of camera mip/filter settings.
float coordinate(float uv,int wrap) {
    if (wrap==10497) return uv-std::floor(uv); // REPEAT
    if (wrap==33648) { // MIRRORED_REPEAT
        float mirrored=std::fmod(uv,2.f);
        if (mirrored<0) mirrored+=2;
        return mirrored<=1?mirrored:2-mirrored;
    }
    return std::clamp(uv,0.f,1.f); // CLAMP_TO_EDGE
}
float sampleAlpha(const AlphaImage& image,glm::vec2 uv,int wrapS,int wrapT) {
    const int x=std::min(static_cast<int>(coordinate(uv.x,wrapS)*image.width),image.width-1);
    const int y=std::min(static_cast<int>(coordinate(uv.y,wrapT)*image.height),image.height-1);
    return image.pixels[static_cast<std::size_t>(y)*image.width+x]/255.f;
}
struct Triangle {
    std::array<glm::vec3,3> positions;
    std::array<glm::vec2,3> uv;
    glm::vec3 alpha{1};
    float baseAlpha=1,cutoff=0;
    std::size_t image=noImage;
    int wrapS=10497,wrapT=10497;
};
struct Asset {
    std::vector<Triangle> triangles;
    std::vector<AlphaImage> images;
    Bounds bounds;
};
Asset prepare(const Model& model) {
    Asset result;
    std::vector<std::size_t> images(model.images.size(),noImage);
    for (const auto& draw:model.draws) {
        validateTransform(draw.transform);
        const auto& primitive=model.primitives.at(draw.primitive);
        Material fallback;
        const auto& material=primitive.material<0?fallback:model.materials.at(static_cast<std::size_t>(primitive.material));
        Triangle triangle;
        if (material.alphaMode=="MASK") {
            triangle.cutoff=material.alphaCutoff;
            triangle.baseAlpha=material.baseColor.a;
            if (!std::isfinite(triangle.cutoff)||!std::isfinite(triangle.baseAlpha))
                throw std::runtime_error("Shrub cover requires finite material alpha");
            if (material.baseColorTexture>=0) {
                const auto& texture=model.textures.at(static_cast<std::size_t>(material.baseColorTexture));
                const auto image=static_cast<std::size_t>(texture.image);
                const auto& source=model.images.at(image);
                if (source.width<=0||source.height<=0||
                    source.rgba.size()!=static_cast<std::size_t>(source.width)*source.height*4)
                    throw std::runtime_error("Shrub cover requires a valid RGBA alpha mask");
                if (images[image]==noImage) {
                    AlphaImage mask; mask.width=source.width; mask.height=source.height;
                    mask.pixels.reserve(source.rgba.size()/4);
                    for (std::size_t pixel=3;pixel<source.rgba.size();pixel+=4) mask.pixels.push_back(source.rgba[pixel]);
                    images[image]=result.images.size(); result.images.push_back(std::move(mask));
                }
                triangle.image=images[image]; triangle.wrapS=texture.wrapS; triangle.wrapT=texture.wrapT;
            }
        }
        for (std::size_t i=0;i+2<primitive.indices.size();i+=3) {
            for (std::size_t j=0;j<3;++j) {
                const auto& vertex=primitive.vertices.at(primitive.indices[i+j]);
                triangle.positions[j]=glm::vec3(draw.transform*glm::vec4(vertex.position,1));
                triangle.uv[j]=vertex.uv;
                triangle.alpha[static_cast<int>(j)]=vertex.color.a;
                if (!finite(triangle.positions[j])||!std::isfinite(vertex.uv.x)||
                    !std::isfinite(vertex.uv.y)||!std::isfinite(vertex.color.a))
                    throw std::runtime_error("Shrub cover requires finite vertices and UVs");
                result.bounds.include(triangle.positions[j]);
            }
            result.triangles.push_back(triangle);
        }
    }
    return result;
}
bool blocks(const Asset& asset,glm::vec3 start,glm::vec3 delta) {
    for (const auto& triangle:asset.triangles) {
        const auto edge1=triangle.positions[1]-triangle.positions[0];
        const auto edge2=triangle.positions[2]-triangle.positions[0];
        const auto cross=glm::cross(delta,edge2);
        const double determinant=glm::dot(edge1,cross);
        // A coplanar ray sees a card edge-on; only actual crossings occlude.
        if (determinant==0) continue;
        const auto offset=start-triangle.positions[0];
        const double u=glm::dot(offset,cross)/determinant;
        if (u<0||u>1) continue;
        const auto q=glm::cross(offset,edge1);
        const double v=glm::dot(delta,q)/determinant;
        if (v<0||u+v>1) continue;
        const double t=glm::dot(edge2,q)/determinant;
        if (t<0||t>1) continue;
        if (triangle.cutoff<=0) return true;
        const glm::vec3 weights(1-u-v,u,v);
        float alpha=triangle.baseAlpha*glm::dot(weights,triangle.alpha);
        if (triangle.image!=noImage) {
            const auto uv=triangle.uv[0]*weights.x+triangle.uv[1]*weights.y+triangle.uv[2]*weights.z;
            alpha*=sampleAlpha(asset.images[triangle.image],uv,triangle.wrapS,triangle.wrapT);
        }
        if (alpha>=triangle.cutoff) return true;
    }
    return false;
}
} // namespace

struct ShrubCover::Impl {
    struct Instance { std::size_t asset; glm::mat4 inverse; Bounds bounds; };
    struct Node { Bounds bounds; std::size_t first=0,count=0,left=0,right=0; };
    std::vector<Asset> assets;
    std::vector<Instance> instances;
    std::vector<std::size_t> order;
    std::vector<Node> nodes;

    std::size_t build(std::size_t first,std::size_t count) {
        const auto index=nodes.size(); nodes.emplace_back();
        Bounds bounds;
        for (std::size_t i=first;i<first+count;++i) {
            bounds.include(instances[order[i]].bounds.low); bounds.include(instances[order[i]].bounds.high);
        }
        nodes[index].bounds=bounds;
        if (count<=8) { nodes[index].first=first; nodes[index].count=count; return index; }
        const auto extent=bounds.high-bounds.low;
        int axis=extent.y>extent.x?1:0;
        if (extent.z>extent[axis]) axis=2;
        const auto middle=first+count/2;
        std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+first+count,[&](std::size_t a,std::size_t b) {
            const auto& aa=instances[a].bounds; const auto& bb=instances[b].bounds;
            const float ca=aa.low[axis]+aa.high[axis],cb=bb.low[axis]+bb.high[axis];
            return ca!=cb?ca<cb:a<b;
        });
        const auto left=build(first,middle-first),right=build(middle,first+count-middle);
        nodes[index].left=left; nodes[index].right=right;
        return index;
    }
    bool visit(std::size_t index,glm::vec3 start,glm::vec3 delta) const {
        const auto& node=nodes[index];
        if (!intersects(node.bounds,start,delta)) return false;
        if (!node.count) return visit(node.left,start,delta)||visit(node.right,start,delta);
        for (std::size_t i=node.first;i<node.first+node.count;++i) {
            const auto& instance=instances[order[i]];
            if (!intersects(instance.bounds,start,delta)) continue;
            const auto localStart=glm::vec3(instance.inverse*glm::vec4(start,1));
            const auto localDelta=glm::vec3(instance.inverse*glm::vec4(delta,0));
            if (blocks(assets[instance.asset],localStart,localDelta)) return true;
        }
        return false;
    }
};

ShrubCover::ShrubCover(const TreeLayer& trees):impl_(std::make_unique<Impl>()) {
    std::vector<std::size_t> assets(trees.assets.size(),noImage);
    for (const auto& shrub:trees.instances) {
        if (shrub.undergrowth==UndergrowthKind::none) continue;
        const auto& source=trees.assets.at(shrub.asset);
        validateTransform(shrub.transform);
        if (assets[shrub.asset]==noImage) {
            assets[shrub.asset]=impl_->assets.size(); impl_->assets.push_back(prepare(source.model));
        }
        const auto asset=assets[shrub.asset];
        if (impl_->assets[asset].triangles.empty()) continue;
        Bounds bounds;
        const auto& local=impl_->assets[asset].bounds;
        for (int corner=0;corner<8;++corner) {
            const glm::vec3 p(corner&1?local.high.x:local.low.x,corner&2?local.high.y:local.low.y,
                              corner&4?local.high.z:local.low.z);
            const auto world=glm::vec3(shrub.transform*glm::vec4(p,1));
            if (!finite(world)) throw std::runtime_error("Shrub cover world bounds must be finite");
            bounds.include(world);
        }
        impl_->instances.push_back({asset,glm::inverse(shrub.transform),bounds});
    }
    impl_->order.resize(impl_->instances.size());
    std::iota(impl_->order.begin(),impl_->order.end(),std::size_t{0});
    if (!impl_->instances.empty()) impl_->build(0,impl_->instances.size());
}
ShrubCover::~ShrubCover()=default;
ShrubCover::ShrubCover(ShrubCover&&) noexcept=default;
ShrubCover& ShrubCover::operator=(ShrubCover&&) noexcept=default;
std::size_t ShrubCover::size() const { return impl_?impl_->instances.size():0; }
bool ShrubCover::blocksSight(glm::vec3 origin,glm::vec3 target) const {
    if (!impl_||impl_->nodes.empty()||!finite(origin)||!finite(target)) return false;
    auto delta=target-origin;
    const float length=glm::length(delta);
    if (!std::isfinite(length)||length<=.002f) return false;
    const auto inset=delta*(.001f/length);
    origin+=inset; delta-=2.f*inset;
    return impl_->visit(0,origin,delta);
}
} // namespace viewer
