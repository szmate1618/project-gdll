#include "visibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using viewer::VisibilityBounds;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
bool overlap(const VisibilityBounds& a,const VisibilityBounds& b) {
    for(int axis=0;axis<3;++axis) if(a.maximum[axis]<b.minimum[axis] || a.minimum[axis]>b.maximum[axis]) return false;
    return true;
}
void compare(const viewer::VisibilityIndex& index,const std::vector<VisibilityBounds>& bounds,
             const VisibilityBounds& volume,const VisibilityBounds& region) {
    const auto matrix=glm::ortho(volume.minimum.x,volume.maximum.x,volume.minimum.y,volume.maximum.y,
                                 -volume.maximum.z,-volume.minimum.z);
    std::vector<std::size_t> actual,expected;
    index.query(matrix,region,actual);
    // Independent axis-aligned volume oracle; no production plane extraction.
    for(std::size_t i=0;i<bounds.size();++i) if(overlap(bounds[i],volume) && overlap(bounds[i],region)) expected.push_back(i);
    std::sort(actual.begin(),actual.end());
    require(actual==expected,"Refitted BVH or combined frustum/region query lost or duplicated bounds");
}
}
int main() {
    try {
        std::vector<VisibilityBounds> bounds;
        for(int i=0;i<1000;++i) bounds.push_back({{100.f+i*10,-1,-1},{101.f+i*10,1,1}});
        bounds[7]={{-2,-1,-1},{2,1,1}};
        bounds[90]={{9.9f,-1,-1},{20,1,1}};
        bounds[999]={{-20,-1,-1},{-9.9f,1,1}};
        viewer::VisibilityIndex index(bounds);
        const VisibilityBounds view{{-10,-10,-10},{10,10,10}}, region{{-5,-5,-5},{5,5,5}};
        compare(index,bounds,view,view);compare(index,bounds,view,region);
        std::vector<std::size_t> visible;
        const auto stats=index.query(glm::ortho(-10.f,10.f,-10.f,10.f,-10.f,10.f),region,visible);
        require(stats.testedInstances<100,"Spatial query scanned distant population");
        bounds[800]={{-1,-1,-1},{1,1,1}};
        require(index.update(800,bounds[800]),"Changed bound was not refitted");
        compare(index,bounds,view,region);
        bounds[7]={{500,-1,-1},{501,1,1}};index.update(7,bounds[7]);compare(index,bounds,view,view);
        bounds[999]={{0,0,0},{0,0,0}};index.update(999,bounds[999]);compare(index,bounds,view,region);
        require(!index.update(999,bounds[999]),"Unchanged bound invalidated the index");
        const VisibilityBounds light{{99,-5,-5},{102,5,5}};
        compare(index,bounds,light,light); // Light casters need not be in the camera frustum.
        auto invalid=bounds[800];invalid.minimum.x=std::numeric_limits<float>::infinity();
        bool rejected=false;try {index.update(800,invalid);} catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Invalid bounds entered the BVH");compare(index,bounds,view,region);
        std::vector<VisibilityBounds> inside;
        for(int i=0;i<64;++i) inside.push_back({{(i-32)*.2f-.01f,-1,-1},{(i-32)*.2f+.01f,1,1}});
        viewer::VisibilityIndex contained(inside);
        compare(contained,inside,view,{{-.05f,-5,-5},{.05f,5,5}});
        viewer::VisibilityIndex single({{{-1,-1,-1},{1,1,1}}});
        single.update(0,{{99,-1,-1},{101,1,1}});
        single.query(glm::ortho(-10.f,10.f,-10.f,10.f,-10.f,10.f),visible);
        require(visible.empty(),"Single-leaf root did not move out of view");
        viewer::VisibilityIndex empty;visible={1};empty.query(glm::mat4(1),region,visible);
        require(visible.empty(),"Empty combined query retained old visibility");
        std::cout<<"Visibility refit and combined region/frustum tests passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
