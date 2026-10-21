#pragma once
#include "zombie_navmesh.hpp"
#include <map>
#include <set>
#include <utility>

namespace test_navmesh {
inline viewer::navmesh_path::Mesh squares(unsigned width, unsigned height,
    const std::set<std::pair<unsigned,unsigned>>& holes = {}) {
    using namespace viewer::navmesh_path;
    std::vector<Point> points;
    std::vector<Triangle> triangles;
    for (unsigned z = 0; z <= height; ++z) for (unsigned x = 0; x <= width; ++x)
        points.push_back({double(x), double(z)});
    for (unsigned z = 0; z < height; ++z) for (unsigned x = 0; x < width; ++x) {
        if (holes.count({x,z})) continue;
        const auto a = z*(width+1)+x, b = a+1, c = a+width+2, d = a+width+1;
        triangles.push_back({{a,b,c}});
        triangles.push_back({{a,c,d}});
    }
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::pair<std::uint32_t,unsigned>> edges;
    for (std::uint32_t i = 0; i < triangles.size(); ++i) for (unsigned e = 0; e != 3; ++e) {
        auto a = triangles[i].vertex[e], b = triangles[i].vertex[(e+1)%3];
        if (b < a) std::swap(a,b);
        auto [entry, inserted] = edges.emplace(std::make_pair(a,b),std::make_pair(i,e));
        if (!inserted) {
            const auto [other, edge] = entry->second;
            triangles[i].neighbor[e] = other;
            triangles[other].neighbor[edge] = i;
        }
    }
    return Mesh(std::move(points),std::move(triangles));
}
// One convex quadrilateral per walkable square; the holes match squares().
inline viewer::navmesh_path::Mesh convexSquares(unsigned width, unsigned height,
    const std::set<std::pair<unsigned,unsigned>>& holes = {}) {
    using namespace viewer::navmesh_path;
    std::vector<Point> points;
    std::vector<Polygon> polygons;
    std::vector<Edge> polygonEdges;
    for (unsigned z=0;z<=height;++z) for (unsigned x=0;x<=width;++x)
        points.push_back({double(x),double(z)});
    for (unsigned z=0;z<height;++z) for (unsigned x=0;x<width;++x) {
        if (holes.count({x,z})) continue;
        const auto a=z*(width+1)+x, b=a+1, c=a+width+2, d=a+width+1;
        polygons.push_back({static_cast<std::uint32_t>(polygonEdges.size()),4});
        for (auto vertex : {a,b,c,d}) polygonEdges.push_back({vertex,noPolygon});
    }
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> portals;
    std::vector<std::uint32_t> owner(polygonEdges.size());
    for (std::uint32_t index=0;index<polygons.size();++index) {
        const auto polygon=polygons[index];
        for (unsigned edge=0;edge<polygon.edgeCount;++edge) {
            const auto flat=polygon.firstEdge+edge;
            owner[flat]=index;
            auto a=polygonEdges[flat].vertex, b=polygonEdges[polygon.firstEdge+(edge+1)%polygon.edgeCount].vertex;
            if (b<a) std::swap(a,b);
            auto [entry,inserted]=portals.emplace(std::make_pair(a,b),flat);
            if (!inserted) {
                polygonEdges[flat].neighbor=owner[entry->second];
                polygonEdges[entry->second].neighbor=index;
            }
        }
    }
    return Mesh(std::move(points),std::move(polygons),std::move(polygonEdges));
}

}
