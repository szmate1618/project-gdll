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
    const auto polygon = mesh.polygons()[index];
    const auto a = mesh.vertices()[mesh.edges()[polygon.firstEdge].vertex];
    const auto b = mesh.vertices()[mesh.edges()[polygon.firstEdge+1].vertex];
    const auto c = mesh.vertices()[mesh.edges()[polygon.firstEdge+2].vertex];
    return {a.x+(b.x-a.x)/3+(c.x-a.x)/3, a.z+(b.z-a.z)/3+(c.z-a.z)/3};
}
void verifyGeometryCache(const Mesh& mesh) {
    require(mesh.geometryCacheBytes()==mesh.polygons().size()*16+mesh.edges().size()*8, "shared geometry cache memory accounting");
    for (std::uint32_t index=0; index<mesh.polygons().size(); ++index) {
        const auto polygon = mesh.polygons()[index];
        const auto center = mesh.centroid(index);
        if (polygon.edgeCount==3)
            require(center==uncachedCentroid(mesh,index), "cached centroid changed legacy graph geometry");
        for (unsigned edge=0; edge!=polygon.edgeCount; ++edge) {
            const auto neighbor = mesh.edges()[polygon.firstEdge+edge].neighbor;
            double expected=0;
            if (neighbor!=noTriangle) {
                const auto next = mesh.centroid(neighbor);
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
    require(empty.componentCount()==0 && empty.connectivityBytes()==0, "default empty mesh connectivity");
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
        require(mesh.vertices().size()==4 && mesh.polygons().size()==2, "binary mesh counts");
        verifyGeometryCache(mesh);
        require(mesh.locate({.25,.5})!=noTriangle && mesh.locate({2,2})==noTriangle, "binary mesh location");
        require(mesh.segmentClear({0,0},{1,1}) && mesh.segmentClear({.2,.8},{.8,.2}), "binary mesh segment");
        require(mesh.sourceSha256()==std::string(64,'0') && mesh.numericMargin()==.005, "binary provenance");
        require(mesh.meshBytes()==128 && mesh.indexBytes()>0, "mesh/index memory accounting");
        require(mesh.componentCount()==1 && mesh.component(0)==mesh.component(1) && mesh.connectivityBytes()==8,
                "loaded v1 reciprocal triangles must share a component");
        auto polygonVersion=valid;
        polygonVersion.resize(valid.size()+8);
        std::memcpy(polygonVersion.data(),"GDNAV02\0",8);
        integer(polygonVersion,88,56);
        integer(polygonVersion,192,3,4);
        std::memcpy(polygonVersion.data()+196,valid.data()+192,24);
        integer(polygonVersion,220,3,4);
        std::memcpy(polygonVersion.data()+224,valid.data()+216,24);
        write(polygonVersion);
        const auto v2=Mesh::load(path.string());
        require(v2.componentCount()==mesh.componentCount() && v2.connectivityBytes()==8 &&
                v2.component(0)==mesh.component(0) && v2.component(1)==mesh.component(1),
                "same portal graph loaded as v1 and v2 must have consistent component labels");
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
// A separately encoded v2 square exercises the variable-size disk records.
void binaryPolygons() {
    const auto path = std::filesystem::temp_directory_path() /
        ("navmesh-polygons-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::vector<unsigned char> valid(128+64+36);
    std::memcpy(valid.data(),"GDNAV02\0",8);
    integer(valid,8,4,4); integer(valid,12,1,4);
    real(valid,32,1); real(valid,40,1);
    real(valid,48,.3); real(valid,56,.002); real(valid,64,.05); real(valid,72,.005);
    integer(valid,80,64); integer(valid,88,36);
    const double coordinates[]{0,0,1,0,1,1,0,1};
    for (unsigned i=0;i!=8;++i) real(valid,128+8*i,coordinates[i]);
    integer(valid,192,4,4);
    for (unsigned edge=0;edge!=4;++edge) {
        integer(valid,196+4*edge,edge,4);
        integer(valid,212+4*edge,noPolygon,4);
    }
    const auto write = [&](const auto& bytes) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    };
    const auto rejects = [&](const auto& bytes) {
        write(bytes);
        try { (void)Mesh::load(path.string()); }
        catch (const std::exception&) { return; }
        throw std::runtime_error("malformed polygon navmesh accepted");
    };
    try {
        write(valid);
        const auto mesh = Mesh::load(path.string());
        require(mesh.polygons().size()==1 && mesh.edges().size()==4 && mesh.polygons()[0].edgeCount==4,
                "v2 polygon counts");
        require(mesh.centroid(0)==Point{.5,.5} && mesh.locate({.5,.5})==0 && mesh.locate({1.1,.5})==noPolygon,
                "v2 polygon center and location");
        require(mesh.segmentClear({0,1},{1,0}) && mesh.segmentClear({0,.5},{1,.5}), "v2 square coverage");
        require(mesh.meshBytes()==104 && mesh.geometryCacheBytes()==48, "v2 compact memory accounting");
        verifyGeometryCache(mesh);
        require(mesh.componentCount()==1 && mesh.component(0)==0 && mesh.connectivityBytes()==4,
                "loaded v2 quadrilateral component label");
        auto changed=valid; changed.pop_back(); rejects(changed);
        changed=valid; changed.push_back(0); rejects(changed);
        changed=valid; integer(changed,88,35); rejects(changed);
        changed=valid; integer(changed,192,2,4); rejects(changed);
        changed=valid; integer(changed,192,7,4); rejects(changed);
        changed=valid; integer(changed,196,9,4); rejects(changed);
        changed=valid; integer(changed,200,3,4); integer(changed,208,1,4); rejects(changed); // CW
        changed=valid; integer(changed,200,0,4); rejects(changed); // duplicate ID
        changed=valid; real(changed,144,0); real(changed,152,0); rejects(changed); // duplicate location
        changed=valid; real(changed,160,.2); real(changed,168,.2); rejects(changed); // concave
        changed=valid; integer(changed,212,0,4); rejects(changed); // self neighbor
        changed=valid; integer(changed,212,1,4); rejects(changed); // outside neighbor
        changed=valid; real(changed,128,std::numeric_limits<double>::infinity()); rejects(changed);
    } catch (...) { std::filesystem::remove(path); throw; }
    std::filesystem::remove(path);
}
void convexValidation() {
    const std::vector<Point> points{{0,0},{1,0},{2,0},{2,1},{1,2},{0,1}};
    std::vector<Edge> edges;
    for (std::uint32_t index=0;index<points.size();++index) edges.push_back({index,noPolygon});
    const Mesh hexagon(points, {{0,6}}, edges);
    require(hexagon.locate({1,1})==0 && hexagon.locate({.1,1.8})==noPolygon,
            "six-edge polygon location");
    require(hexagon.segmentClear({.1,.1},{1,1.9}), "six-edge polygon interval");
    require(!hexagon.segmentClear({.1,.1},{.1,1.8}), "six-edge polygon excludes outside endpoint");
    const Mesh pentagon({{0,0},{2,0},{3,1},{1,3},{0,1}}, {{0,5}},
                        {{0,noPolygon},{1,noPolygon},{2,noPolygon},{3,noPolygon},{4,noPolygon}});
    require(pentagon.locate({1,1})==0 && pentagon.segmentClear({1,.1},{1,2.9}),
            "five-edge polygon coverage");
    const auto rejects = [&](auto polygons, auto malformedEdges) {
        try { (void)Mesh(points, std::move(polygons), std::move(malformedEdges)); }
        catch (const std::exception&) { return; }
        throw std::runtime_error("invalid polygon constructor accepted");
    };
    rejects(std::vector<Polygon>{{1,6}},edges);
    rejects(std::vector<Polygon>{{0,5}},edges); // unused edge
    auto changed=edges; std::swap(changed[1],changed[3]); // self-crossing
    rejects(std::vector<Polygon>{{0,6}},changed);
    changed=edges; std::swap(changed[1],changed[2]); // backwards collinear edge
    rejects(std::vector<Polygon>{{0,6}},changed);
    const auto adjacent = test_navmesh::convexSquares(2,1);
    require(adjacent.polygons().size()==2 && adjacent.edges().size()==8 &&
            adjacent.segmentClear({.1,.5},{1.9,.5}), "reciprocal quadrilateral portal");
    auto malformed=adjacent.edges();
    const auto polygon=adjacent.polygons()[1];
    for (unsigned edge=0;edge<polygon.edgeCount;++edge)
        if (malformed[polygon.firstEdge+edge].neighbor==0) malformed[polygon.firstEdge+edge].neighbor=noPolygon;
    bool rejected=false;
    try { (void)Mesh(adjacent.vertices(),adjacent.polygons(),std::move(malformed)); }
    catch (const std::exception&) { rejected=true; }
    require(rejected,"one-way polygon portal accepted");
}
void connectivity() {
    const auto connected=test_navmesh::convexSquares(3,2);
    require(connected.componentCount()==1 && connected.connectivityBytes()==24,
            "connected polygon graph component count or memory");
    for (std::uint32_t polygon=1;polygon<connected.polygons().size();++polygon)
        require(connected.component(polygon)==connected.component(0), "portal-connected polygons separated");
    const auto isolated=test_navmesh::convexSquares(3,1,{{1,0}});
    require(isolated.componentCount()==2 && isolated.component(0)!=isolated.component(1),
            "isolated polygons must be different components");
    const auto touching=test_navmesh::convexSquares(2,2,{{1,0},{0,1}});
    require(touching.componentCount()==2 && touching.component(0)!=touching.component(1),
            "vertex-only contact must not connect polygon components");
    const auto triangleGraph=test_navmesh::squares(3,1,{{1,0}});
    require(triangleGraph.componentCount()==2 && triangleGraph.component(0)==triangleGraph.component(1) &&
            triangleGraph.component(2)==triangleGraph.component(3) && triangleGraph.component(0)!=triangleGraph.component(2),
            "legacy triangle constructor connectivity");
    // A 50,001-triangle fan forms a single long portal chain. Mesh connectivity
    // must cover it completely, independent of the per-request 50k search cap.
    constexpr std::uint32_t count=50'001;
    std::vector<Point> vertices;
    vertices.reserve(count+2);
    vertices.push_back({0,0});
    for (std::uint32_t vertex=0;vertex<=count;++vertex) vertices.push_back({double(vertex),1});
    std::vector<Triangle> triangles;
    triangles.reserve(count);
    for (std::uint32_t index=0;index<count;++index)
        triangles.push_back({{0,index+2,index+1},
            {index+1<count ? index+1 : noTriangle,noTriangle,index ? index-1 : noTriangle}});
    const Mesh longGraph(std::move(vertices),std::move(triangles));
    require(longGraph.polygons().size()>50'000 && longGraph.componentCount()==1 &&
            longGraph.component(0)==longGraph.component(count-1) && longGraph.connectivityBytes()==count*4,
            "component preprocessing must cover graphs larger than the search budget");
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
void queries(bool convexPolygons = false) {
    const std::set<std::pair<unsigned,unsigned>> holes{{2,2},{3,2},{4,4},{5,4},{6,4},{1,6}};
    const auto mesh = convexPolygons ? test_navmesh::convexSquares(8,8,holes) : test_navmesh::squares(8,8,holes);
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
    cachedGeometry(); binary(); binaryPolygons(); convexValidation(); connectivity(); queries(); queries(true);
    std::cout<<"Navmesh loading, point location and segment coverage tests passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
