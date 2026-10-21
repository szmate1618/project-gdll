#include "navmesh_fixture.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace viewer::navmesh_path;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void integer(std::vector<unsigned char>& bytes, std::size_t at, std::uint64_t value, unsigned count = 8) {
    for (unsigned i = 0; i != count; ++i) bytes[at+i] = (value>>(8*i))&255;
}
void real(std::vector<unsigned char>& bytes, std::size_t at, double value) {
    std::uint64_t bits;
    std::memcpy(&bits,&value,8);
    integer(bytes,at,bits);
}
// This is the previous on-demand graph geometry, retained as a compatibility
// oracle. Exact equality detects arithmetic changes that can reorder A* ties.
Point uncachedCentroid(const Mesh& mesh, std::uint32_t index) {
    const auto& triangle = mesh.triangles()[index];
    const auto a = mesh.vertices()[triangle.vertex[0]], b = mesh.vertices()[triangle.vertex[1]], c = mesh.vertices()[triangle.vertex[2]];
    return {a.x+(b.x-a.x)/3+(c.x-a.x)/3, a.z+(b.z-a.z)/3+(c.z-a.z)/3};
}
void verifyGeometryCache(const Mesh& mesh) {
    require(mesh.geometryCacheBytes()==mesh.triangles().size()*40, "shared geometry cache memory accounting");
    for (std::uint32_t index=0; index<mesh.triangles().size(); ++index) {
        const auto center = uncachedCentroid(mesh,index);
        require(mesh.centroid(index)==center, "cached centroid changed graph geometry");
        for (unsigned edge=0; edge!=3; ++edge) {
            const auto neighbor = mesh.triangles()[index].neighbor[edge];
            double expected=0;
            if (neighbor!=noTriangle) {
                const auto next = uncachedCentroid(mesh,neighbor);
                expected=std::hypot(center.x-next.x,center.z-next.z);
            }
            require(mesh.neighborDistance(index,edge)==expected, "cached neighbor cost changed graph geometry");
        }
    }
}
void cachedGeometry() {
    const std::vector<Triangle> triangles{
        {{0,1,2},{noTriangle,noTriangle,1}},
        {{0,2,3},{0,noTriangle,noTriangle}}};
    const Mesh irregular({{-6,-3},{0,-3},{0,6},{-9,3}},triangles);
    require(irregular.centroid(0)==Point{-2,0} && irregular.centroid(1)==Point{-5,2},
            "irregular negative-coordinate triangle centers");
    require(irregular.neighborDistance(0,2)==std::hypot(3.,-2.) &&
            irregular.neighborDistance(1,0)==irregular.neighborDistance(0,2),
            "irregular triangle adjacency costs");
    verifyGeometryCache(irregular);
    const Mesh tiny({{-3000,-2000},{-3000+3e-9,-2000},
                     {-3000+3e-9,-2000+6e-9},{-3000,-2000+3e-9}},triangles);
    verifyGeometryCache(tiny);
    require(!(tiny.centroid(0)==tiny.centroid(1)) && tiny.neighborDistance(0,2)>0,
            "tiny triangle centers or costs collapsed");
    require(static_cast<float>(tiny.centroid(0).x)==static_cast<float>(tiny.centroid(1).x) &&
            static_cast<float>(tiny.centroid(0).z)==static_cast<float>(tiny.centroid(1).z),
            "tiny geometry fixture must require double precision");
    const Mesh empty;
    require(empty.geometryCacheBytes()==0 && empty.locate({0,0})==noTriangle,
            "default empty mesh cache");
}
void binary() {
    const auto path = std::filesystem::temp_directory_path() /
        ("navmesh-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::vector<unsigned char> valid(128+64+48);
    std::memcpy(valid.data(),"GDNAV01\0",8);
    integer(valid,8,4,4); integer(valid,12,2,4);
    real(valid,32,1); real(valid,40,1);
    real(valid,48,.3); real(valid,56,.002); real(valid,64,.05); real(valid,72,.005);
    integer(valid,80,64); integer(valid,88,48);
    const double coordinates[]{0,0,1,0,1,1,0,1};
    for (unsigned i = 0; i != 8; ++i) real(valid,128+8*i,coordinates[i]);
    const std::uint32_t records[]{0,1,2,noTriangle,noTriangle,1,0,2,3,0,noTriangle,noTriangle};
    for (unsigned i = 0; i != 12; ++i) integer(valid,192+4*i,records[i],4);
    const auto write = [&](const auto& bytes) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    };
    const auto rejects = [&](const auto& bytes) {
        write(bytes);
        try { (void)Mesh::load(path.string()); }
        catch (const std::exception&) { return; }
        throw std::runtime_error("malformed navmesh accepted");
    };
    try {
        write(valid);
        const auto mesh = Mesh::load(path.string());
        require(mesh.vertices().size()==4 && mesh.triangles().size()==2, "binary mesh counts");
        verifyGeometryCache(mesh);
        require(mesh.locate({.25,.5})!=noTriangle && mesh.locate({2,2})==noTriangle, "binary mesh location");
        require(mesh.segmentClear({0,0},{1,1}) && mesh.segmentClear({.2,.8},{.8,.2}), "binary mesh segment");
        require(mesh.sourceSha256()==std::string(64,'0') && mesh.numericMargin()==.005, "binary provenance");
        require(mesh.meshBytes()==112 && mesh.indexBytes()>0, "mesh/index memory accounting");
        auto changed = valid; changed[0]=0; rejects(changed);
        changed=valid; changed.resize(127); rejects(changed);
        changed=valid; changed.pop_back(); rejects(changed);
        changed=valid; changed.push_back(0); rejects(changed);
        changed=valid; integer(changed,8,0,4); rejects(changed);
        changed=valid; integer(changed,80,63); rejects(changed);
        changed=valid; real(changed,48,-1); rejects(changed);
        changed=valid; real(changed,128,std::numeric_limits<double>::quiet_NaN()); rejects(changed);
        changed=valid; integer(changed,192,5,4); rejects(changed);
        changed=valid; integer(changed,192+12,0,4); rejects(changed);
        changed=valid; integer(changed,192+24+12,noTriangle,4); rejects(changed);
        changed=valid; integer(changed,192+4,2,4); rejects(changed);
    } catch (...) { std::filesystem::remove(path); throw; }
    std::filesystem::remove(path);
}
bool crossesBox(Point a, Point b, unsigned x, unsigned z) {
    double low=0, high=1;
    const double starts[]{a.x,a.z}, delta[]{b.x-a.x,b.z-a.z};
    const double minimum[]{double(x),double(z)};
    for (unsigned axis=0;axis!=2;++axis) {
        if (delta[axis]==0) {
            if (starts[axis]<minimum[axis] || starts[axis]>minimum[axis]+1) return false;
        } else {
            double first=(minimum[axis]-starts[axis])/delta[axis], last=(minimum[axis]+1-starts[axis])/delta[axis];
            if (first>last) std::swap(first,last);
            low=std::max(low,first); high=std::min(high,last);
            if (low>high) return false;
        }
    }
    return true;
}
void queries() {
    const std::set<std::pair<unsigned,unsigned>> holes{{2,2},{3,2},{4,4},{5,4},{6,4},{1,6}};
    const auto mesh = test_navmesh::squares(8,8,holes);
    verifyGeometryCache(mesh);
    require(mesh.locate({2.5,2.5})==noTriangle && mesh.locate({-1,1})==noTriangle, "hole/outside location");
    require(mesh.locate({NAN,1})==noTriangle, "nonfinite location");
    require(mesh.segmentClear({0,0},{8,0}), "closed mesh boundary should be walkable");
    require(!mesh.segmentClear({1,2.5},{5,2.5}), "hole-crossing segment accepted");
    require(mesh.segmentClear({1,2},{5,2}), "triangle coverage should include hole boundary");
    require(mesh.segmentClear({1,1},{7,1}), "clear line through many triangles rejected");
    std::mt19937 random(0x4e4156);
    std::uniform_real_distribution<double> coordinate(.01,7.99);
    for (unsigned i=0;i!=5000;++i) {
        Point a{coordinate(random),coordinate(random)}, b{coordinate(random),coordinate(random)};
        bool expected=true;
        for (auto [x,z]:holes) if (crossesBox(a,b,x,z)) expected=false;
        require(mesh.segmentClear(a,b)==expected && mesh.segmentClear(b,a)==expected,
                "BVH triangle coverage differs from independent hole-box intersections");
    }
    require(mesh.segmentClear({2.5,1.5},{2.5,1.5}), "valid zero-length segment rejected");
}
}
int main() try {
    cachedGeometry(); binary(); queries();
    std::cout<<"Navmesh loading, point location and segment coverage tests passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
