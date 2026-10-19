#include "forest_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
double cross(glm::dvec2 a, glm::dvec2 b) { return a.x*b.y-a.y*b.x; }
double signedArea(const std::vector<glm::dvec2>& ring) {
    double area = 0;
    const auto origin = ring.front();
    for (std::size_t i=1; i<ring.size(); ++i) area += cross(ring[i-1]-origin,ring[i]-origin);
    return area*.5;
}
bool onSegment(glm::dvec2 p, glm::dvec2 a, glm::dvec2 b) {
    return std::abs(cross(p-a,b-a)) < 1e-8 && glm::dot(p-a,p-b) <= 1e-8;
}
bool intersects(glm::dvec2 a, glm::dvec2 b, glm::dvec2 c, glm::dvec2 d) {
    const double abC=cross(b-a,c-a), abD=cross(b-a,d-a);
    const double cdA=cross(d-c,a-c), cdB=cross(d-c,b-c);
    return (abC*abD<0 && cdA*cdB<0) || onSegment(a,c,d) ||
        onSegment(b,c,d) || onSegment(c,a,b) || onSegment(d,a,b);
}
bool inside(const std::vector<glm::dvec2>& ring, glm::dvec2 p) {
    bool result=false;
    for (std::size_t i=1; i<ring.size(); ++i) {
        const auto a=ring[i-1], b=ring[i];
        if (onSegment(p,a,b)) return false; // Shared borders never receive trees.
        if ((a.y>p.y)!=(b.y>p.y) && p.x<a.x+(p.y-a.y)*(b.x-a.x)/(b.y-a.y)) result=!result;
    }
    return result;
}
std::vector<const std::vector<glm::dvec2>*> rings(const ForestPolygon& polygon) {
    std::vector<const std::vector<glm::dvec2>*> result{&polygon.exterior};
    for (const auto& hole:polygon.holes) result.push_back(&hole);
    return result;
}
void quad(Primitive& mesh, const std::array<glm::vec3,4>& p,
          const std::array<glm::vec2,4>& uv, glm::vec3 normal) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t i=0;i<4;++i) {
        Vertex vertex; vertex.position=p[i]; vertex.normal=normal; vertex.uv=uv[i];
        mesh.vertices.push_back(vertex);
    }
    for (const auto index:{0u,1u,2u,0u,2u,3u}) mesh.indices.push_back(first+index);
}
}

void validateForestPolygon(const ForestPolygon& polygon) {
    const auto all=rings(polygon);
    if (all.size()>65) throw std::runtime_error("Too many forest holes");
    for (std::size_t r=0;r<all.size();++r) {
        const auto& ring=*all[r];
        if (ring.size()<4 || ring.size()>1024 || ring.front()!=ring.back())
            throw std::runtime_error("Forest rings must have 4..1024 vertices and be closed");
        for (std::size_t i=1;i<ring.size();++i) {
            const auto p=ring[i];
            if (!std::isfinite(p.x)||!std::isfinite(p.y)||std::max(std::abs(p.x),std::abs(p.y))>1e6 || p==ring[i-1])
                throw std::runtime_error("Invalid forest vertex or zero-length edge");
            for (std::size_t j=i+2;j<ring.size();++j) {
                if (i==1 && j==ring.size()-1) continue;
                if (intersects(ring[i-1],ring[i],ring[j-1],ring[j]))
                    throw std::runtime_error("Forest ring self-intersects");
            }
        }
        if ((r==0 && signedArea(ring)<=0) || (r>0 && signedArea(ring)>=0))
            throw std::runtime_error("Forest exterior must be CCW and holes CW");
        if (r>0 && !inside(polygon.exterior,ring.front()))
            throw std::runtime_error("Forest hole is outside exterior");
        for (std::size_t s=0;s<r;++s) {
            const auto& other=*all[s];
            for (std::size_t i=1;i<ring.size();++i)
                for (std::size_t j=1;j<other.size();++j)
                    if (intersects(ring[i-1],ring[i],other[j-1],other[j]))
                        throw std::runtime_error("Forest rings intersect");
            if (s>0 && (inside(other,ring.front()) || inside(ring,other.front())))
                throw std::runtime_error("Nested forest holes are unsupported");
        }
    }
}

bool forestContains(const ForestPolygon& polygon, glm::dvec2 p) {
    if (!inside(polygon.exterior,p)) return false;
    for (const auto& hole:polygon.holes) {
        if (inside(hole,p)) return false;
        for (std::size_t i=1;i<hole.size();++i) if (onSegment(p,hole[i-1],hole[i])) return false;
    }
    return true;
}
double forestArea(const ForestPolygon& polygon) {
    double result=signedArea(polygon.exterior);
    for (const auto& hole:polygon.holes) result+=signedArea(hole);
    return result;
}

ForestPolygon oldForestPolygon(const ForestPolygon& polygon) {
    validateForestPolygon(polygon);
    glm::dvec2 center(0);
    for (std::size_t i=0;i+1<polygon.exterior.size();++i) center+=polygon.exterior[i];
    center/=static_cast<double>(polygon.exterior.size()-1);
    ForestPolygon result;
    for (const auto p:polygon.exterior) result.exterior.push_back(center+(p-center)*.5);
    return result;
}

std::pair<Primitive, Primitive> forestVolume(const ForestPolygon& polygon,
    const ForestGround& ground, float height, float tileMeters) {
    validateForestPolygon(polygon);
    if (!std::isfinite(height)||height<=0||!std::isfinite(tileMeters)||tileMeters<=0)
        throw std::runtime_error("Forest volume requires positive height and texture scale");
    Primitive walls,top; walls.material=0; top.material=1;
    const auto position=[&](glm::dvec2 p,float offset) {
        const auto y=ground(p);
        if (!y || !std::isfinite(*y)) throw std::runtime_error("Forest polygon has no published terrain support");
        return glm::vec3(static_cast<float>(p.x),*y+offset,static_cast<float>(-p.y));
    };
    const auto all=rings(polygon);
    std::vector<double> cuts;
    for (const auto* ring:all) {
        float along=0;
        for (std::size_t i=1;i<ring->size();++i) {
            const auto a=(*ring)[i-1], b=(*ring)[i];
            cuts.push_back(a.x);
            const auto length=glm::length(b-a);
            const int pieces=std::max(1,static_cast<int>(std::ceil(length/20)));
            for (int j=0;j<pieces;++j) {
                const auto p=glm::mix(a,b,static_cast<double>(j)/pieces);
                const auto q=glm::mix(a,b,static_cast<double>(j+1)/pieces);
                const float u0=(along+static_cast<float>(length*j/pieces))/tileMeters;
                const float u1=(along+static_cast<float>(length*(j+1)/pieces))/tileMeters;
                const auto edge=glm::vec3(q.x-p.x,0,p.y-q.y);
                const auto normal=glm::normalize(glm::cross(edge,glm::vec3(0,1,0)));
                quad(walls,{{position(p,-.3f),position(q,-.3f),position(q,height),position(p,height)}},
                     {{{u0,1},{u1,1},{u1,0},{u0,0}}},normal);
            }
            along+=static_cast<float>(length);
        }
    }
    std::sort(cuts.begin(),cuts.end());
    cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    struct Edge { glm::dvec2 a,b; double y(double x) const { return a.y+(x-a.x)*(b.y-a.y)/(b.x-a.x); } };
    for (std::size_t c=1;c<cuts.size();++c) {
        const double left=cuts[c-1],right=cuts[c],mid=(left+right)*.5;
        std::vector<Edge> edges;
        for (const auto* ring:all) for (std::size_t i=1;i<ring->size();++i) {
            const auto a=(*ring)[i-1],b=(*ring)[i];
            if (std::min(a.x,b.x)<mid && mid<std::max(a.x,b.x)) edges.push_back({a,b});
        }
        std::sort(edges.begin(),edges.end(),[&](const auto& a,const auto& b){return a.y(mid)<b.y(mid);});
        if (edges.size()%2) throw std::runtime_error("Forest top decomposition has unpaired edges");
        const int columns=std::max(1,static_cast<int>(std::ceil((right-left)/20)));
        for (std::size_t e=0;e<edges.size();e+=2) {
            const auto& lower=edges[e]; const auto& upper=edges[e+1];
            const double span=std::max(upper.y(left)-lower.y(left),upper.y(right)-lower.y(right));
            const int rows=std::max(1,static_cast<int>(std::ceil(span/20)));
            for (int x=0;x<columns;++x) for (int y=0;y<rows;++y) {
                const double x0=glm::mix(left,right,static_cast<double>(x)/columns);
                const double x1=glm::mix(left,right,static_cast<double>(x+1)/columns);
                const auto p=[&](double xx,int yy){return glm::dvec2(xx,glm::mix(lower.y(xx),upper.y(xx),static_cast<double>(yy)/rows));};
                const std::array<glm::dvec2,4> points{{p(x0,y),p(x1,y),p(x1,y+1),p(x0,y+1)}};
                std::array<glm::vec3,4> positions;
                std::array<glm::vec2,4> uvs;
                for (std::size_t i=0;i<4;++i) {
                    positions[i]=position(points[i],height);
                    uvs[i]=glm::vec2(points[i].x/tileMeters,-points[i].y/tileMeters);
                }
                quad(top,positions,uvs,{0,1,0});
            }
        }
    }
    return {std::move(walls),std::move(top)};
}
} // namespace viewer
