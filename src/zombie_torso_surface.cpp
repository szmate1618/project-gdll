#include "zombie_torso_surface.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace viewer {
namespace {
using Source = ZombieTorsoGeometry::Vertex;
// Ear clipping in the loop's dominant plane also handles concave clothing rims.
void triangulate(std::vector<int> loop, const std::vector<glm::vec3>& points,
                 const std::vector<Source>& sources, ZombieTorsoGeometry& geometry) {
    glm::vec3 normal(0);
    for (std::size_t i=0; i<loop.size(); ++i)
        normal += glm::cross(points[loop[i]], points[loop[(i+1)%loop.size()]]);
    const auto magnitude = glm::abs(normal);
    const int axis = magnitude.x > magnitude.y && magnitude.x > magnitude.z ? 0 : magnitude.y > magnitude.z ? 1 : 2;
    const auto project = [&](int id) { return glm::dvec2(points[id][(axis+1)%3], points[id][(axis+2)%3]); };
    const auto cross = [](glm::dvec2 a, glm::dvec2 b) { return a.x*b.y-a.y*b.x; };
    const double sign = normal[axis] >= 0 ? 1 : -1;
    while (loop.size() > 2) {
        bool clipped = false;
        for (std::size_t i=0; i<loop.size(); ++i) {
            const auto previous = (i+loop.size()-1)%loop.size(), next = (i+1)%loop.size();
            const auto a = project(loop[previous]), b = project(loop[i]), c = project(loop[next]);
            if (sign*cross(b-a,c-b) <= 1e-12) continue;
            bool occupied = false;
            for (std::size_t j=0; j<loop.size(); ++j) {
                if (j==previous || j==i || j==next) continue;
                const auto p = project(loop[j]);
                if (sign*cross(b-a,p-a) >= -1e-12 && sign*cross(c-b,p-b) >= -1e-12 &&
                    sign*cross(a-c,p-c) >= -1e-12) { occupied = true; break; }
            }
            if (occupied) continue;
            // Reverse the shell boundary for outward cap winding.
            geometry.closures.push_back({sources[loop[next]], sources[loop[i]], sources[loop[previous]]});
            loop.erase(loop.begin()+i);
            clipped = true;
            break;
        }
        if (!clipped) {
            // These authored rims can fold out of the projection plane. Finish
            // their remaining polygon in 3D rather than leaving an opening.
            for (std::size_t i=1; i+1<loop.size(); ++i)
                geometry.closures.push_back({sources[loop[i+1]],sources[loop[i]],sources[loop[0]]});
            break;
        }
    }
}
}
void closeZombieTorso(const AnimatedModel& asset, ZombieTorsoGeometry& geometry) {
    geometry.closures.clear();
    if (geometry.bone < 0) return;
    using Key = std::pair<std::array<long,3>,std::array<long,16>>;
    std::map<Key,int> welded;
    std::vector<glm::vec3> points;
    std::vector<Source> sources;
    struct Edge { int a, b, count; };
    std::map<std::pair<int,int>,Edge> edges;
    std::size_t bitOffset = 0;
    for (std::size_t p=0; p<asset.model.primitives.size(); ++p) {
        const auto& primitive = asset.model.primitives[p];
        for (std::size_t t=0; t<primitive.indices.size(); t+=3) {
            if (!geometry.torsoTriangles[geometry.triangleOffsets[p]+t/3]) continue;
            std::array<int,3> ids{};
            for (int i=0; i<3; ++i) {
                const auto v = primitive.indices[t+i];
                const auto point = geometry.referenceVertices[p][v];
                Key key{{std::lround(point.x*100000),std::lround(point.y*100000),std::lround(point.z*100000)}, {}};
                // Opposite sides of an authored cut coincide in the reference
                // pose but belong to different bodies. Never weld those sides.
                std::map<std::uint32_t,long> influences;
                const auto& skin = asset.animation[p].skinning[v];
                for (std::size_t influence=0; influence<skin.weights.size(); ++influence)
                    if (skin.weights[influence]>0)
                        influences[skin.joints[influence]] += std::lround(skin.weights[influence]*100000);
                std::size_t influence=0;
                for (const auto& entry : influences) {
                    key.second[influence++] = entry.first+1;
                    key.second[influence++] = entry.second;
                }
                const auto inserted = welded.emplace(key, int(points.size()));
                if (inserted.second) { points.push_back(point); sources.push_back({p,v,bitOffset+v}); }
                ids[i] = inserted.first->second;
            }
            if (ids[0]==ids[1] || ids[1]==ids[2] || ids[2]==ids[0]) continue;
            for (int i=0; i<3; ++i) {
                const int a=ids[i], b=ids[(i+1)%3];
                auto edge=edges.emplace(std::minmax(a,b), Edge{a,b,0});
                ++edge.first->second.count;
            }
        }
        bitOffset += primitive.vertices.size();
    }
    std::multimap<int,int> boundary;
    for (const auto& edge : edges) if (edge.second.count==1) boundary.emplace(edge.second.a,edge.second.b);
    while (!boundary.empty()) {
        std::vector<int> loop;
        const int start=boundary.begin()->first;
        int current=start;
        do {
            const auto edge=boundary.find(current);
            if (edge==boundary.end()) break;
            loop.push_back(current); current=edge->second; boundary.erase(edge);
        } while (current!=start);
        if (current==start && loop.size()>=3) triangulate(std::move(loop),points,sources,geometry);
    }
}
bool zombieTorsoClosureRemoved(const std::array<Source,3>& triangle, const ZombieTorsoWound& wound) {
    return std::any_of(triangle.begin(),triangle.end(),[&](const auto& source) {
        return source.bit/8 < wound.vertexMask.size() && (wound.vertexMask[source.bit/8] & (1u << (source.bit%8)));
    });
}
AnimatedModel zombieTorsoClosureModel(const AnimatedModel& asset, const ZombieTorsoGeometry& geometry) {
    AnimatedModel result;
    if (geometry.closures.empty()) return result;
    result.skeleton = asset.skeleton;
    result.frameCount = asset.frameCount;
    result.duration = asset.duration;
    const auto clip = [](const auto& source) -> std::optional<BakedAnimationClip> {
        if (!source) return {};
        return BakedAnimationClip{source->name,source->duration,source->frameCount,source->firstFrame,{}};
    };
    result.walkClip = clip(asset.walkClip);
    result.runClip = clip(asset.runClip);
    Material material;
    material.baseColor = {.231f,.00792f,.0132f,1};
    material.doubleSided = true;
    result.model.materials.push_back(material);
    Primitive primitive;
    primitive.material = 0;
    AnimatedPrimitive animation;
    for (const auto& triangle : geometry.closures)
        for (const auto& source : triangle) {
            primitive.indices.push_back(static_cast<std::uint32_t>(primitive.vertices.size()));
            auto vertex = asset.model.primitives[source.primitive].vertices[source.vertex];
            vertex.color = glm::vec4(1);
            primitive.vertices.push_back(vertex);
            animation.skinning.push_back(asset.animation[source.primitive].skinning[source.vertex]);
        }
    for (std::size_t i=0; i<primitive.vertices.size(); i+=3) {
        const auto normal = glm::cross(primitive.vertices[i+1].position-primitive.vertices[i].position,
                                       primitive.vertices[i+2].position-primitive.vertices[i].position);
        if (glm::dot(normal,normal)>1e-16f)
            for (std::size_t v=i; v<i+3; ++v) primitive.vertices[v].normal = glm::normalize(normal);
    }
    for (std::size_t frame=0; frame<asset.totalFrameCount(); ++frame)
        for (const auto& triangle : geometry.closures) {
            std::array<AnimationVertex,3> vertices;
            for (int i=0; i<3; ++i) {
                const auto& source = triangle[i];
                vertices[i] = asset.animation[source.primitive].frames[frame*asset.model.primitives[source.primitive].vertices.size()+source.vertex];
            }
            const auto normal = glm::cross(glm::vec3(vertices[1].position-vertices[0].position),glm::vec3(vertices[2].position-vertices[0].position));
            for (auto vertex : vertices) {
                if (glm::dot(normal,normal)>1e-16f) vertex.normal = glm::vec4(glm::normalize(normal),0);
                animation.frames.push_back(vertex);
            }
        }
    result.model.primitives.push_back(std::move(primitive));
    result.animation.push_back(std::move(animation));
    return result;
}
} // namespace viewer
