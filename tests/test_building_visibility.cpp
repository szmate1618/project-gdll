#include "building_visibility.hpp"
#include "building_batch.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
viewer::Model fixture(const std::vector<float>& xs) {
    viewer::Model model;
    viewer::Primitive cube;
    for (int i = 0; i < 8; ++i) {
        viewer::Vertex v;
        v.position = {(i&1) ? .5f : -.5f, (i&2) ? .5f : -.5f, (i&4) ? .5f : -.5f};
        cube.vertices.push_back(v);
    }
    cube.indices = {0,1,2};
    model.primitives.push_back(cube);
    for (const float x : xs)
        model.draws.push_back({0, glm::translate(glm::mat4(1), {x,3,-20}), "building_test_roof"});
    return model;
}
glm::mat4 view(glm::vec3 eye, float fov = 60) {
    return glm::perspective(glm::radians(fov), 1.f, .1f, 2000.f) *
        glm::lookAt(eye, eye+glm::vec3(0,0,-1), glm::vec3(0,1,0));
}
viewer::BuildingVisibilityStats query(const viewer::BuildingVisibility& buildings,
    const glm::mat4& matrix, std::vector<std::size_t>& visible,
    const viewer::HorizontalVisibilityRange* range, bool culling, viewer::BuildingVisibilityState& history) {
    return buildings.query({matrix,glm::mat4(1),glm::vec3(0),range,culling,true,false},visible,history);
}
void compare(const viewer::ModelVisibility& original, const viewer::BuildingVisibility& roofs,
             const viewer::BuildingBatches& batches, const glm::mat4& matrix,
             const viewer::HorizontalVisibilityRange* range, bool culling,
             viewer::BuildingVisibilityState& history) {
    std::vector<std::size_t> sources, actual, expected;
    original.query(matrix, sources, range, culling);
    const auto stats = query(roofs,matrix,actual,range,culling,history);
    std::vector<std::size_t> first(batches.meshes.size(), viewer::noBuildingBatch);
    for (const auto id : sources) {
        const auto batch = batches.batchForDraw[id];
        if (batch == viewer::noBuildingBatch) expected.push_back(id);
        else first[batch] = std::min(first[batch], id);
    }
    for (const auto id : first) if (id != viewer::noBuildingBatch) expected.push_back(id);
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    require(actual == expected, "Direct roof query changed selected representatives/non-roof IDs");
    require(actual.size()+stats.extraSources == sources.size(), "Roof visibility changed exact source counts");
}
void orderingAndRange() {
    auto model = fixture({100,7,2,2});
    model.draws[2].name = "ordinary_surface";
    // A reflected/sheared roof crosses the nominal 128 m cell boundary.
    auto transform = glm::scale(glm::translate(glm::mat4(1), {127,3,-20}), {-20,2,1});
    transform[1].x = 3;
    model.draws.push_back({0, transform, "building_crossing_roof"});
    const viewer::ModelVisibility original(model);
    const auto batches = viewer::buildRoofBatches(model);
    const viewer::BuildingVisibility roofs(model,original,batches,viewer::buildWallBatches(model,original));
    viewer::BuildingVisibilityState history;
    for (int repeat = 0; repeat < 2; ++repeat) for (const bool culling : {true,false}) {
        for (const auto eye : {glm::vec3(2,3,0), glm::vec3(60,3,0), glm::vec3(140,3,0)}) {
            for (const float fov : {60.f,10.f,1.f}) {
                const auto matrix = view(eye,fov);
                compare(original,roofs,batches,matrix,nullptr,culling,history);
                for (const float radius : {1.f,15.f,200.f}) {
                    const viewer::HorizontalVisibilityRange range{{eye.x,-20},radius};
                    compare(original,roofs,batches,matrix,&range,culling,history);
                }
            }
        }
    }
    std::vector<std::size_t> visible;
    query(roofs,view({2,3,0},10),visible,nullptr,true,history);
    std::sort(visible.begin(),visible.end());
    require(visible == std::vector<std::size_t>({2,3}),
            "Scoped view must not substitute an offscreen lower-ID roof for the selected member");
}
void emptySpaceBetweenMembers() {
    const auto model = fixture({20,100});
    const viewer::ModelVisibility original(model);
    const auto batches = viewer::buildRoofBatches(model);
    const viewer::BuildingVisibility roofs(model,original,batches,viewer::buildWallBatches(model,original));
    require(batches.meshes.size() == 1, "Empty-space fixture must share one batch");
    viewer::BuildingVisibilityState history;
    std::vector<std::size_t> visible;
    const auto stats = query(roofs,view({60,3,0}),visible,nullptr,true,history);
    require(visible.empty() && stats.extraSources == 0 && stats.selectedBatches == 0,
            "Intersecting batch envelope must not invent visible member geometry");
    require(stats.testedMembers == 2, "Partial batch must resolve its member visibility");
}
void containedBatchSkipsMembers() {
    auto model = fixture({});
    // Every roof is in one material/cell and wholly inside this overhead view.
    for (int x = 0; x < 20; ++x) for (int z = 0; z < 20; ++z)
        model.draws.push_back({0,glm::translate(glm::mat4(1),{3.f+x*6,0,-3.f-z*6}),"building_grid_roof"});
    const viewer::ModelVisibility original(model);
    const auto batches = viewer::buildRoofBatches(model);
    const viewer::BuildingVisibility roofs(model,original,batches,viewer::buildWallBatches(model,original));
    require(batches.meshes.size() == 1,"Contained fixture must share one batch");
    const auto matrix = glm::perspective(glm::radians(60.f),1.f,.1f,1000.f) *
        glm::lookAt(glm::vec3(64,400,-64),glm::vec3(64,0,-64),glm::vec3(0,0,-1));
    viewer::BuildingVisibilityState history;
    std::vector<std::size_t> visible;
    const auto stats = query(roofs,matrix,visible,nullptr,true,history);
    require(visible == std::vector<std::size_t>({0}) && stats.extraSources == 399,
            "Contained batch must represent all 400 roofs at the earliest ID");
    require(stats.testedMembers == 0 && stats.coarse.testedNodes == 1 && stats.coarse.testedInstances == 0,
            "Contained roof batch must skip source-level tests");
    viewer::BuildingVisibility empty;
    visible = {12};
    query(empty,matrix,visible,nullptr,true,history);
    require(visible.empty(),"Empty roof visibility retained old IDs");
}

viewer::Model wallFixture(bool splitGroup) {
    auto model = fixture({2,8,2,8,2,2,80,140,140});
    model.materials.resize(3);
    auto glass = model.primitives[0]; glass.material = 1;
    model.primitives.push_back(glass);
    model.materials[1].alphaMode = "BLEND";
    for (const auto id : {0u,1u,2u,3u,4u,5u,7u,8u}) {
        auto& draw = model.draws[id];
        draw.detailDistance = 40;
        draw.detailGroup = (id == 1 || id == 3) ? "b" : "a";
        draw.lowDetail = id == 0 || id == 1 || id == 7;
        draw.name = draw.lowDetail ? "building_test_walls" : "building_test_detailed_walls";
    }
    model.draws[4].primitive = 1;
    model.draws[5].horizontalMotionRadius = 5;
    model.draws[6].name = "ordinary_surface";
    if (splitGroup) {
        auto mesh = model.primitives[0]; mesh.material = 2;
        model.primitives.push_back(mesh);
        auto draw = model.draws[0]; draw.primitive = 2;
        model.draws.push_back(draw);
    }
    // A zero-distance facade has no LoD alternative, even with a shared name.
    auto draw = model.draws[0];
    draw.detailDistance = 0; draw.detailGroup = "legacy";
    model.draws.push_back(draw);
    draw.lowDetail = false; draw.name = "building_legacy_detailed_walls";
    model.draws.push_back(draw);
    return model;
}

void wallAlternatives() {
    for (const bool split : {false,true}) {
        const auto model = wallFixture(split);
        const viewer::ModelVisibility original(model);
        const auto roofs = viewer::buildRoofBatches(model);
        const auto walls = viewer::buildWallBatches(model,original);
        const viewer::BuildingVisibility buildings(model,original,roofs,walls);
        viewer::BuildingVisibilityState history;
        for (int repeat = 0; repeat < 2; ++repeat)
        for (const auto eye : {glm::vec3(2,3,0),glm::vec3(60,3,0),glm::vec3(4,3,160),glm::vec3(140,3,0)})
        for (const float fov : {60.f,10.f,1.f})
        for (const bool culling : {false,true}) for (const bool batching : {false,true})
        for (const float radius : {0.f,1.f,30.f,300.f}) {
            const auto projection = glm::perspective(glm::radians(fov),1.f,.1f,2000.f);
            const auto matrix = view(eye,fov);
            const viewer::HorizontalVisibilityRange range{{eye.x,eye.z},radius};
            viewer::BuildingVisibilityView request{matrix,projection,eye,radius ? &range : nullptr,culling,true,batching};
            std::vector<std::size_t> sources,actual,expected;
            original.query(matrix,sources,request.range,culling);
            original.filterDetails(eye,projection,sources);
            const auto stats = buildings.query(request,actual,history);
            original.filterDetails(eye,projection,actual);
            std::vector<std::size_t> first(walls.meshes.size(),viewer::noBuildingBatch);
            for (const auto id : sources) {
                const auto batch = walls.batchForDraw[id];
                if (!batching || batch == viewer::noBuildingBatch ||
                    viewer::buildingDetailsNear(walls.details[batch].bounds,walls.details[batch].distance,eye,projection))
                    expected.push_back(id);
                else first[batch] = std::min(first[batch],id);
            }
            for (const auto id : first) if (id != viewer::noBuildingBatch) expected.push_back(id);
            std::sort(actual.begin(),actual.end()); std::sort(expected.begin(),expected.end());
            require(actual == expected,"Wall query changed near/offscreen/scope/multi-material facade selection");
            require(actual.size()+stats.extraSources == sources.size(),"Wall query changed filtered source counts");
        }
    }
}

void distantWallsSkipDetails() {
    auto model = fixture({});
    for (int x = 0; x < 20; ++x) for (int z = 0; z < 20; ++z) {
        const auto transform = glm::translate(glm::mat4(1),{3.f+x*6,0,-3.f-z*6});
        const auto group = std::to_string(x*20+z);
        model.draws.push_back({0,transform,"building_grid_walls",10,true,group});
        model.draws.push_back({0,transform,"building_grid_detailed_walls",10,false,group});
    }
    const viewer::ModelVisibility original(model);
    const auto roofs = viewer::buildRoofBatches(model);
    const auto walls = viewer::buildWallBatches(model,original);
    const viewer::BuildingVisibility buildings(model,original,roofs,walls);
    const auto projection = glm::perspective(glm::radians(60.f),1.f,.1f,1000.f);
    const glm::vec3 eye(64,400,-64);
    const auto matrix = projection*glm::lookAt(eye,glm::vec3(64,0,-64),glm::vec3(0,0,-1));
    viewer::BuildingVisibilityState history;
    std::vector<std::size_t> visible;
    auto stats = buildings.query({matrix,projection,eye,nullptr,true,true,true},visible,history);
    require(visible == std::vector<std::size_t>({0}) && stats.extraSources == 399,
            "Contained distant walls must emit the earliest simple wall and exact count");
    require(stats.skippedDetails == 400 && stats.testedMembers == 0,
            "Contained distant walls must skip detailed alternatives and member tests");
    // Scoped near fallback expands the coarse list, requiring separate scratch.
    const auto scope = glm::perspective(glm::radians(1.f),1.f,.1f,1000.f);
    stats = buildings.query({matrix,scope,eye,nullptr,false,true,true},visible,history);
    require(visible.size() == 800 && stats.extraSources == 0 && stats.skippedDetails == 0,
            "Near batch failed to expand all original sources");
    original.filterDetails(eye,scope,visible);
    require(visible.size() == 400 && std::all_of(visible.begin(),visible.end(),[](auto id){return id%2 == 1;}),
            "Near fallback filled detailed facades with simple walls");
}
}
int main() {
    try { orderingAndRange(); emptySpaceBetweenMembers(); containedBatchSkipsMembers();
          wallAlternatives(); distantWallsSkipDetails(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "Building visibility preserves roof order, ranges, wall alternatives and scoped near fallback\n";
}
