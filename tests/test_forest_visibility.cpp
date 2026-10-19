#include "forest_visibility.hpp"
#include "forest_tree_visibility.hpp"
#include "model_visibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::ForestPatch area() {
    viewer::ForestPatch patch;
    patch.bounds = {{0,0,-200},{200,30,0}};
    patch.oldForest = {{{50,50},{150,50},{150,150},{50,150},{50,50}}, {}};
    return patch;
}
void membership() {
    auto patch = area();
    const viewer::OldForestVisibility forest({patch});
    const auto range = forest.rangeAt({100,5,-100});
    require(range && range->radius == 100 && range->center == glm::vec2(100,-100),
            "Old-forest membership lost meters or east/north -> X/-Z conversion");
    require(!forest.rangeAt({20,5,-100}) && !forest.rangeAt({50,5,-100}) &&
            !forest.rangeAt({100,5,100}), "Outside/border camera incorrectly entered old forest");
    require(!forest.rangeAt({100,31,-100}) && !forest.rangeAt({100,-1,-100}),
            "Above/below-forest free flight enabled the cutoff");
    require(!viewer::OldForestVisibility().rangeAt({100,5,-100}), "Empty forest enabled a cutoff");
    patch.oldForest.holes = {{{90,90},{90,110},{110,110},{110,90},{90,90}}};
    const viewer::OldForestVisibility holed({patch});
    require(!holed.rangeAt({100,5,-100}) && holed.rangeAt({120,5,-100}),
            "Old-forest hole membership is incorrect");
    // The old region is authoritative, independent of surrounding forest bounds.
    patch = area(); patch.bounds.minimum.x = 1000; patch.bounds.maximum.x = 1200;
    require(viewer::OldForestVisibility({patch}).rangeAt({100,5,-100}).has_value(),
            "Visibility assumed old forest must be contained in another forest region");
    patch.oldForest = {{{50,50},{150,50},{150,150},{110,150},{110,90},{90,90},{90,150},{50,150},{50,50}}, {}};
    const viewer::OldForestVisibility concave({patch});
    require(!concave.rangeAt({100,5,-120}) && concave.rangeAt({80,5,-120}),
            "Membership used a bounding box instead of the concave old-forest polygon");
}
void indexedRange() {
    std::vector<viewer::VisibilityBounds> bounds;
    std::vector<std::size_t> expected;
    for (int x=-100;x<=100;++x) for (int z=-100;z<=100;++z) {
        const glm::vec3 point(x*10.f,1000,z*10.f);
        if (std::hypot(point.x,point.z)<=50) expected.push_back(bounds.size());
        bounds.push_back({point,point});
    }
    const viewer::VisibilityIndex index(bounds);
    const viewer::HorizontalVisibilityRange range{{0,0},50};
    const auto projection=glm::ortho(-2000.f,2000.f,-2000.f,2000.f,-2000.f,2000.f);
    std::vector<std::size_t> visible;
    const auto stats=index.query(projection,visible,&range);
    std::sort(visible.begin(),visible.end());
    require(visible==expected,"Range query retained far corners or discarded tall nearby geometry");
    require(stats.testedInstances<1000,"Range query scanned the entire world");
    index.query(glm::mat4(1),visible,&range,false);
    std::sort(visible.begin(),visible.end());
    require(visible==expected,"Disabling frustum culling bypassed the distance cutoff");
    const viewer::VisibilityBounds crown{{49,0,-2},{60,20,2}}, far{{51,0,-2},{60,20,2}};
    require(range.intersects(crown)&&!range.contains(crown)&&!range.intersects(far),
            "Geometry crossing the distance boundary was incorrectly discarded");
    require(!range.intersects({{49,0,49},{51,10,51}}),"Square corners escaped the circular cutoff");
}
void groupedAndModelRange() {
    std::vector<viewer::TreeInstance> trees(3);
    trees[0].boundsMin={-1,0,-21}; trees[0].boundsMax={1,10,-19};
    trees[1].boundsMin={-1,0,-81}; trees[1].boundsMax={1,10,-79};
    trees[2].boundsMin={-1,0,19}; trees[2].boundsMax={1,10,21};
    viewer::ForestPatch patch; patch.firstTree=0;patch.treeCount=2;
    const viewer::ForestTreeVisibility index(trees,{patch});
    const viewer::HorizontalVisibilityRange range{{0,0},50};
    const auto projection=glm::perspective(glm::radians(60.f),1.f,.1f,1000.f);
    std::vector<std::size_t> visible;
    index.query(projection,{true},true,visible,&range);
    require(visible==std::vector<std::size_t>{0},"Tree group query ignored the range");
    const auto reverse=projection*glm::lookAt(glm::vec3(0),glm::vec3(0,0,1),glm::vec3(0,1,0));
    index.query(reverse,{true},true,visible,&range);
    require(visible==std::vector<std::size_t>{2},"Range-limited shadows reused camera visibility");
    index.query(projection,{true},false,visible,&range);std::sort(visible.begin(),visible.end());
    require(visible==std::vector<std::size_t>({0,2}),"Tree culling bypass ignored the hard distance policy");
    viewer::Model model;viewer::Primitive mesh;
    for(const auto position:{glm::vec3(-1),glm::vec3(1)}){viewer::Vertex v;v.position=position;mesh.vertices.push_back(v);}
    model.primitives.push_back(mesh);
    model.draws.push_back({0,glm::translate(glm::mat4(1),{0,0,-20}),"near"});
    model.draws.push_back({0,glm::translate(glm::mat4(1),{0,0,-80}),"far"});
    model.draws.push_back({0,glm::scale(glm::translate(glm::mat4(1),{0,0,-60}),{1,1,12}),"crossing"});
    viewer::ModelVisibility(model).query(projection,visible,&range);std::sort(visible.begin(),visible.end());
    require(visible==std::vector<std::size_t>({0,2}),"Town query discarded crossing geometry or retained distant draws");
}
}
int main() {
    try {membership();indexedRange();groupedAndModelRange();std::cout<<"Old forest membership and range queries passed\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
