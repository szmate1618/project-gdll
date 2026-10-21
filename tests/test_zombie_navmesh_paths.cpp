#include "navmesh_fixture.hpp"
#include "zombie_navmesh_paths.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
std::atomic<std::size_t> allocations{0};
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
}
void* operator new(std::size_t size) {
    ++allocations;
    if (void* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

int main() try {
    using namespace viewer::navmesh_path;
    const auto mesh=test_navmesh::squares(8,8,{{3,3},{3,4},{3,5}});
    PathManager manager(mesh,2), one(mesh,1), limited(mesh,2,1);
    require(manager.capacityBytes()==10'000'000 && manager.nodeLimit()==50'000 && manager.heuristicWeight()==2,
            "manager construction contracts");
    const auto before=allocations.load();
    const auto first=manager.request(0,{1,4.5},{7,4.5});
    require(first && manager.pathFor(0)==first, "zombie ID zero mapping");
    const auto path=manager.view(first);
    require(path.size>2 && path[0]==Point{1,4.5} && path[path.size-1]==Point{7,4.5}, "detour and exact endpoints");
    for (std::size_t i=1;i<path.size;++i) require(mesh.segmentClear(path[i-1],path[i]), "invalid manager route");
    const auto second=manager.request(UINT64_MAX,{1,1},{7,1});
    require(second && manager.view(first).points==path.points, "other requests preserve borrowed paths");
    require(!manager.request(3,{1,1},{2,1}) && manager.lastStatus()==Status::bufferFull, "metadata capacity");
    require(!manager.request(0,{1,1},{3.5,3.5}) && manager.pathFor(0)==first, "failed replacement preserves path");
    const auto replacement=manager.request(0,{1,1},{2,1});
    require(replacement && replacement!=first && !manager.view(first).size, "replacement handle lifetime");
    require(manager.releaseZombie(UINT64_MAX) && !manager.release(second), "release removes ownership");
    for (std::uint64_t i=0;i<200;++i) {
        const auto id=manager.request(i+1000,{1,1},{7,1});
        require(id && manager.release(id), "reusable mapping tombstones and spans");
        require(one.request(i,{1,1},{7,1}) && one.request(i,{1,1},{6,1}) && one.releaseZombie(i),
                "single-slot replacement span coalescing");
    }
    require(!limited.request(0,{1,1},{7,7}) && limited.lastStatus()==Status::searchLimit, "manager search cap");
    manager.clear();
    require(!manager.usedBytes() && !manager.pathFor(0) && !manager.view(replacement).size, "clear ownership and arena");
    const auto after=manager.request(0,{1,1},{2,1});
    require(after && after!=replacement && manager.usedBytes()==manager.view(after).size*sizeof(Point), "clear never aliases handles");
    require(allocations.load()==before, "manager requests, releases and clears must not allocate");
    std::cout<<"Navmesh path-manager ownership, arena and allocation tests passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
