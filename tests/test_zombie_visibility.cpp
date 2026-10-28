#include "zombie_visibility.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
using viewer::VisibilityBounds;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
VisibilityBounds box(glm::vec3 center, glm::vec3 half = glm::vec3(0.5f)) { return {center - half, center + half}; }

viewer::ZombieVisibilityStats compare(viewer::ZombieVisibility& groups, const std::vector<VisibilityBounds>& bounds,
                                     const std::vector<bool>& active, const viewer::Frustum& camera, const viewer::Frustum* light = nullptr,
                                     bool cameraCulling = true, const viewer::Frustum* flashlight = nullptr,
                                     const VisibilityBounds* cameraRegion = nullptr) {
    std::vector<viewer::ZombieVisibilityResult> actual, expected;
    const auto stats = groups.query(camera, light, cameraCulling, actual, flashlight, cameraRegion);
    // Independent full population query is the original rendering predicate.
    for (std::size_t id = 0; id < bounds.size(); ++id) {
        require(groups.contains(id) == active[id], "Membership must match independently recorded updates/removals");
        if (!active[id]) continue;
        const auto& b = bounds[id];
        bool inCamera = !cameraCulling || camera.intersects(b.minimum, b.maximum);
        if (cameraRegion) for (int axis = 0; axis < 3; ++axis)
            if (b.maximum[axis] < cameraRegion->minimum[axis] || b.minimum[axis] > cameraRegion->maximum[axis])
                inCamera = false;
        const bool inShadow = light && light->intersects(b.minimum, b.maximum);
        const bool inFlashlight = flashlight && flashlight->intersects(b.minimum, b.maximum);
        if (inCamera || inShadow || inFlashlight) expected.push_back({id, inCamera, inShadow, inFlashlight});
    }
    require(actual.size() == expected.size(), "Grouped culling must match the full-scan visible union");
    for (std::size_t i = 0; i < actual.size(); ++i)
        require(actual[i].instance == expected[i].instance && actual[i].camera == expected[i].camera &&
                actual[i].shadow == expected[i].shadow && actual[i].flashlight == expected[i].flashlight,
                "Grouped camera/shadow masks and source ordering must match individual tests");
    return stats;
}

void insideOutsideAndBoundaryGroups() {
    const viewer::Frustum camera(glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, -10.0f, 10.0f));
    const viewer::Frustum light(glm::ortho(80.0f, 110.0f, -10.0f, 10.0f, -10.0f, 10.0f));
    std::vector<VisibilityBounds> bounds{
        box({1, 1, 1}), box({2, 1, 2}), box({100, 1, 1}), box({101, 1, 2}), box({200, 1, 1})};
    std::vector<bool> active(bounds.size(), true);
    viewer::ZombieVisibility groups;
    groups.reset(bounds.size());
    for (std::size_t i = 0; i < bounds.size(); ++i) groups.update(i, bounds[i]);
    auto stats = compare(groups, bounds, active, camera, &light);
    require(stats.groups == 3 && stats.refittedGroups == 3, "Shared cells must refit once after initialization");
    require(stats.cameraInstanceTests == 0 && stats.shadowInstanceTests == 0,
            "Wholly inside/outside groups must bypass individual frustum tests");
    require(compare(groups, bounds, active, camera, &light).refittedGroups == 0,
            "Stationary and settled groups must retain their bounds");
    bounds[0] = box({9.8f, 1, 1}, {1, 0.5f, 0.5f});
    bounds[1] = box({15, 1, 2});
    groups.update(0, bounds[0]);
    groups.update(1, bounds[1]);
    stats = compare(groups, bounds, active, camera, &light);
    require(stats.refittedGroups == 1 && stats.cameraInstanceTests == 2,
            "Boundary groups must test members and refit only once for several movers");
    stats = compare(groups, bounds, active, camera, &light, false);
    require(stats.cameraGroupTests == 0 && stats.cameraInstanceTests == 0 && stats.shadowBlockTests > 0,
            "Disabling camera culling must retain independent shadow culling");
    const viewer::Frustum flashlight(glm::ortho(190.0f, 210.0f, -10.0f, 10.0f, -10.0f, 10.0f));
    stats = compare(groups, bounds, active, camera, &light, true, &flashlight);
    require(stats.flashlightBlockTests > 0 && stats.flashlightInstanceTests == 0,
            "A flashlight-only group must join the visible union without individual checks");
    compare(groups, bounds, active, camera); // No active lights: camera list only.
}

void movementRemovalAndReuse() {
    const viewer::Frustum camera(glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, -10.0f, 10.0f));
    std::vector<VisibilityBounds> bounds{box({101, 0, 0}), box({102, 0, 0}), box({103, 0, 0}), box({-20.01f, 0, 0})};
    std::vector<bool> active(bounds.size(), true);
    viewer::ZombieVisibility groups;
    groups.reset(bounds.size());
    for (std::size_t i = 0; i < bounds.size(); ++i) groups.update(i, bounds[i]);
    compare(groups, bounds, active, camera);
    // Remove a middle bucket entry, then move its swapped member.
    groups.erase(1);
    active[1] = false;
    bounds[2] = box({-0.01f, 0, 0});
    groups.update(2, bounds[2]);
    compare(groups, bounds, active, camera);
    // Free an entire group and reuse its storage before querying. Pending dirty
    // entries must describe the new group and must not contain duplicate IDs.
    groups.erase(0);
    bounds[0] = box({4, 0, 0});
    groups.update(0, bounds[0]);
    bounds[3] = box({20.01f, 0, 0});
    groups.update(3, bounds[3]);
    compare(groups, bounds, active, camera);
    // A fallen body's geometry reaches into view although its center is outside.
    bounds[3] = box({30, 0, 0}, {25, 1, 1});
    groups.update(3, bounds[3]);
    auto stats = compare(groups, bounds, active, camera);
    require(stats.refittedGroups == 1, "Pose expansion within a cell must invalidate its group");
    // Shrinking/removing the extremal member must shrink the group too.
    bounds[3] = box({30, 0, 0});
    groups.update(3, bounds[3]);
    compare(groups, bounds, active, camera);
    groups.erase(3);
    active[3] = false;
    compare(groups, bounds, active, camera);
    require(!groups.contains(1) && !groups.contains(3), "Removed corpses must leave group membership");
    for (std::size_t i = 0; i < bounds.size(); ++i) groups.erase(i);
    active.assign(bounds.size(), false);
    require(compare(groups, bounds, active, camera).groups == 0, "Empty groups must stop contributing to queries");
    groups.reset(1);
    bounds.resize(1);
    active.assign(1, true);
    bounds[0] = box({0, 0, 0});
    groups.update(0, bounds[0]);
    compare(groups, bounds, active, camera);
}

void cameraRegionRetainsIndependentLightsAndWideBounds() {
    const viewer::Frustum camera(glm::ortho(-500.f,500.f,-500.f,500.f,-500.f,500.f));
    const viewer::Frustum light(glm::ortho(80.f,110.f,-10.f,10.f,-10.f,10.f));
    const viewer::Frustum flashlight(glm::ortho(8.f,12.f,-10.f,10.f,-10.f,10.f));
    const auto region=box({0,0,0},{5,5,5});
    std::vector<VisibilityBounds> bounds{
        box({0,0,0}),box({10,0,0}),box({100,0,0}),box({101,0,0}),box({200,0,0},{199,1,1}),
        box({5.5f,0,0})}; // Touching the range boundary remains a candidate.
    std::vector<bool> active(bounds.size(),true);
    viewer::ZombieVisibility groups;groups.reset(bounds.size());
    for (std::size_t i=0;i<bounds.size();++i) groups.update(i,bounds[i]);
    const auto stats=compare(groups,bounds,active,camera,&light,true,&flashlight,&region);
    require(stats.cameraRegionRejectedGroups==1 && stats.cameraGroupTests<=2,
            "Far camera groups must skip frustum work while retaining sun/flashlight-only members");
    compare(groups,bounds,active,camera,&light,false,&flashlight,&region);
    compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
    bounds[4]=box({200,0,0});groups.update(4,bounds[4]);
    require(compare(groups,bounds,active,camera,nullptr,true,nullptr,&region).cameraRegionRejectedGroups==2,
            "Shrinking a wide bound must restore early range rejection after refitting");
}

void blocksRetainIndependentLightsAndOverhangingBounds() {
    const viewer::Frustum camera(glm::ortho(-1000.f,1000.f,-10.f,10.f,-10.f,10.f));
    const viewer::Frustum light(glm::ortho(390.f,410.f,-10.f,10.f,-10.f,10.f));
    const viewer::Frustum flashlight(glm::ortho(790.f,810.f,-10.f,10.f,-10.f,10.f));
    const auto region=box({0,0,0},{5,5,5});
    std::vector<VisibilityBounds> bounds{
        box({0,0,0}),box({400,0,0}),box({800,0,0}),box({-400,0,0}),
        box({1600,0,0},{1595,1,1})}; // A distant center whose geometry touches the camera region.
    std::vector<bool> active(bounds.size(),true);
    viewer::ZombieVisibility groups;groups.reset(bounds.size());
    for (std::size_t i=0;i<bounds.size();++i) groups.update(i,bounds[i]);
    auto stats=compare(groups,bounds,active,camera,&light,true,&flashlight,&region);
    require(stats.blocks==5 && stats.refittedBlocks==5,
            "Disjoint occupied regions must initialize independent visibility blocks");
    compare(groups,bounds,active,camera,&light,false,&flashlight,&region);
    compare(groups,bounds,active,camera,nullptr,true,&flashlight,&region);
    compare(groups,bounds,active,camera,&light,true,nullptr,&region);
    stats=compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
    require(stats.refittedBlocks==0 && stats.visitedGroups<stats.groups,
            "Stationary blocks must retain bounds while irrelevant blocks skip their cells");
    // Expanding an existing member must expand its parent even without a cell move.
    bounds[3]=box({-400,0,0},{405,1,1});groups.update(3,bounds[3]);
    stats=compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
    require(stats.refittedGroups==1 && stats.refittedBlocks==1,
            "Geometry expansion must refit its existing cell and visibility block");
    bounds[3]=box({-400,0,0});groups.update(3,bounds[3]);
    bounds[4]=box({1600,0,0});groups.update(4,bounds[4]);
    compare(groups,bounds,active,camera,&light,true,&flashlight,&region);
}

void blockBoundaryMovesRemovalAndReset() {
    const viewer::Frustum camera(glm::ortho(-5.f,5.f,-5.f,5.f,-5.f,5.f));
    const auto region=box({0,0,0},{5,5,5});
    const std::vector<glm::vec3> positions{
        {-320,0,-320},{-160.01f,0,-160.01f},{-160,0,-160},{-.01f,0,-.01f},
        {0,0,0},{159.99f,0,159.99f},{160,0,160},{320,0,320}};
    std::vector<VisibilityBounds> bounds(positions.size());
    std::vector<bool> active(bounds.size(),true);
    viewer::ZombieVisibility groups;groups.reset(bounds.size());
    for (std::size_t i=0;i<bounds.size();++i) {
        bounds[i]=box(positions[i]);groups.update(i,bounds[i]);
    }
    compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
    for (std::size_t destination=0;destination<positions.size();++destination) {
        // Empty multiple source blocks, then reuse their slots before a query.
        for (std::size_t i=0;i<bounds.size();++i) groups.erase(i);
        active.assign(bounds.size(),false);
        for (std::size_t i=0;i<bounds.size();++i) {
            bounds[i]=box(positions[(i+destination)%positions.size()]);
            groups.update(i,bounds[i]);active[i]=true;
        }
        compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
        for (const auto& position:positions) {
            const viewer::Frustum local(glm::ortho(position.x-1,position.x+1,-5.f,5.f,
                                                  -position.z-1,-position.z+1));
            compare(groups,bounds,active,local);
        }
    }
    for (std::size_t i=0;i<bounds.size();++i) groups.erase(i);
    active.assign(bounds.size(),false);
    const auto empty=compare(groups,bounds,active,camera);
    require(empty.groups==0 && empty.blocks==0 && empty.visitedGroups==0,
            "Removing all members must retire their cells and blocks");
    // Reset with pending removals and a different capacity must discard old queues.
    groups.update(0,bounds[0]);groups.erase(0);groups.reset(1);
    bounds.assign(1,box({0,0,0}));active.assign(1,true);groups.update(0,bounds[0]);
    const auto reset=compare(groups,bounds,active,camera);
    require(reset.groups==1 && reset.blocks==1 && reset.refittedBlocks==1,
            "Reset must discard historical block membership and dirty entries");
}

void smallViewSkipsDistantCells() {
    // A sparse town-sized population with many occupied fine cells in each region.
    std::vector<VisibilityBounds> bounds(40000);
    std::vector<bool> active(bounds.size(),true);
    viewer::ZombieVisibility groups;groups.reset(bounds.size());
    for (std::size_t i=0;i<bounds.size();++i) {
        bounds[i]=box({float(i%200)*20-2000,0,float(i/200)*20-2000});
        groups.update(i,bounds[i]);
    }
    const viewer::Frustum camera(glm::ortho(-10.f,10.f,-10.f,10.f,-10.f,10.f));
    const auto region=box({0,0,0},{10,10,10});
    const auto stats=compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
    require(stats.groups==bounds.size() && stats.blocks<stats.groups/10 && stats.visitedGroups<500,
            "A small view must skip distant fine cells rather than visit the whole occupied grid");
    const auto unchanged=compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
    require(unchanged.refittedGroups==0 && unchanged.refittedBlocks==0,
            "Repeated small views must not refit the stationary population");
    // Move a far actor into view and back, exercising both parents.
    bounds.back()=box({0,0,0});groups.update(bounds.size()-1,bounds.back());
    compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
    bounds.back()=box({4000,0,4000});groups.update(bounds.size()-1,bounds.back());
    compare(groups,bounds,active,camera,nullptr,true,nullptr,&region);
}

void distantBlockFacesRetainTouchingMembers() {
    const viewer::Frustum nearOrigin(glm::ortho(-5.f,5.f,-5.f,5.f,-5.f,5.f));
    for (const int axis:{0,2}) for (const float origin:{10000000.f,-10000160.f}) {
        std::vector<VisibilityBounds> bounds(2);
        bounds[0].minimum[axis]=bounds[0].maximum[axis]=origin;
        bounds[1].minimum[axis]=origin+157;
        bounds[1].maximum[axis]=origin+161;
        const std::vector<bool> active(bounds.size(),true);
        viewer::ZombieVisibility groups;groups.reset(bounds.size());
        for (std::size_t i=0;i<bounds.size();++i) groups.update(i,bounds[i]);
        const float edge=origin+161;
        const viewer::Frustum touching(axis==0 ? glm::ortho(edge,edge+4,-5.f,5.f,-5.f,5.f) :
                                                glm::ortho(-5.f,5.f,-5.f,5.f,-edge-4,-edge));
        require(touching.intersects(bounds[1].minimum,bounds[1].maximum),
                "The individual distant member must survive at the touching clip plane");
        // Enclosing center/half-size arithmetic has different float rounding
        // from the small member. Parent rejection must remain conservative.
        const auto stats=compare(groups,bounds,active,touching);
        require(stats.blocks==1 && stats.groups==2,
                "The numerical boundary fixture must exercise two cells under one parent");
        compare(groups,bounds,active,nearOrigin,&touching);
        compare(groups,bounds,active,nearOrigin,nullptr,true,&touching);
    }
}

void randomViewsMatchFullScan() {
    std::mt19937 random(731);
    std::uniform_real_distribution<float> coordinate(-250, 250), extent(0.02f, 15);
    std::vector<VisibilityBounds> bounds(1000);
    std::vector<bool> active(bounds.size(), true);
    viewer::ZombieVisibility groups;
    groups.reset(bounds.size());
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        bounds[i] = box({coordinate(random), coordinate(random) * 0.2f, coordinate(random)},
                        {extent(random), extent(random), extent(random)});
        groups.update(i, bounds[i]);
    }
    for (int view = 0; view < 100; ++view) {
        for (int move = 0; move < 15; ++move) {
            const auto id = std::size_t(random()) % bounds.size();
            if (random() % 5 == 0) {
                groups.erase(id);
                active[id] = false;
            } else {
                active[id] = true;
                bounds[id] = box({coordinate(random), coordinate(random) * 0.2f, coordinate(random)},
                                 {extent(random), extent(random), extent(random)});
                groups.update(id, bounds[id]);
            }
        }
        const glm::vec3 eye(coordinate(random), 80, coordinate(random));
        const viewer::Frustum camera(glm::perspective(glm::radians(60.0f), 1.3f, 0.05f, 400.0f) *
                                     glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0)));
        const viewer::Frustum light(glm::ortho(-120.0f, 120.0f, -120.0f, 120.0f, 0.1f, 600.0f) *
                                    glm::lookAt(eye + glm::vec3(-100, 250, 100), eye, glm::vec3(0, 1, 0)));
        const viewer::Frustum flashlight(glm::perspective(glm::radians(44.0f), 1.0f, 0.05f, 25.0f) *
                                        glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0)));
        compare(groups, bounds, active, camera, view % 3 == 0 ? nullptr : &light, view % 4 != 0,
                view % 2 == 0 ? nullptr : &flashlight);
        const auto region=box({eye.x,0,eye.z},{55,1000,55});
        compare(groups, bounds, active, camera, &light, true, &flashlight, &region);
    }
}

void orderingRetainsMasksAcrossDigitBoundaries() {
    const viewer::Frustum broad(glm::ortho(-1000.f,1000.f,-100.f,100.f,-100.f,100.f));
    const viewer::Frustum camera(glm::ortho(-230.f,-170.f,-100.f,100.f,-100.f,100.f));
    const viewer::Frustum sun(glm::ortho(-30.f,230.f,-100.f,100.f,-100.f,100.f));
    const viewer::Frustum flashlight(glm::ortho(170.f,430.f,-100.f,100.f,-100.f,100.f));
    const auto emptyRegion=box({1000,0,0},{1,100,100});
    std::mt19937 random(913);
    viewer::ZombieVisibility groups;
    std::vector<VisibilityBounds> bounds;
    std::vector<bool> active;
    // Large visible lists cross two- and three-byte ID ranges. The larger
    // population is sparse, so result count cannot bound the largest key.
    for (const std::size_t population:{256u,257u,512u,66064u}) {
        groups.reset(population);
        bounds.assign(population,box({1000,0,0}));
        active.assign(population,false);
        std::vector<std::size_t> ids;
        if (population<=512) {
            for (std::size_t id=0;id<population;++id) ids.push_back(id);
        } else {
            for (std::size_t i=0;i<520;++i) ids.push_back(i*127);
            for (const std::size_t id:{255u,256u,65535u,65536u,66063u}) ids.push_back(id);
        }
        std::shuffle(ids.begin(),ids.end(),random);
        for (const auto id:ids) {
            bounds[id]=box({float(id%5)*200-200,0,float((id/5)%3)*10});
            active[id]=true;groups.update(id,bounds[id]);
        }
        compare(groups,bounds,active,broad,&sun,true,&flashlight);
        // Independent light-only results carry different masks through sorting.
        compare(groups,bounds,active,camera,&sun,true,&flashlight);
        compare(groups,bounds,active,camera); // Small list after a large query.
        compare(groups,bounds,active,broad,nullptr,true,nullptr,&emptyRegion);
        compare(groups,bounds,active,broad,&sun,true,&flashlight);

        for (std::size_t i=0;i<ids.size();i+=3) {
            groups.erase(ids[i]);active[ids[i]]=false;
        }
        compare(groups,bounds,active,broad,&sun,true,&flashlight);
        for (std::size_t i=ids.size();i-- > 0;) if (!active[ids[i]]) {
            const auto id=ids[i];
            bounds[id]=box({600-float(id%5)*200,0,-float((id/5)%3)*10});
            active[id]=true;groups.update(id,bounds[id]);
        }
        compare(groups,bounds,active,camera,&sun,true,&flashlight);
        compare(groups,bounds,active,broad,&sun,true,&flashlight);
    }
    // A smaller population after a large sorted result must discard old keys,
    // including when reset interrupts pending removal/refit work.
    groups.erase(65536);
    groups.reset(1);
    bounds.assign(1,box({0,0,0}));active.assign(1,true);
    groups.update(0,bounds[0]);
    compare(groups,bounds,active,broad,&sun,true,&flashlight);
    groups.erase(0);active[0]=false;
    compare(groups,bounds,active,broad,&sun,true,&flashlight);
}

void largeCrowdRejectsGroups() {
    std::vector<VisibilityBounds> bounds(10000);
    std::vector<bool> active(bounds.size(), true);
    viewer::ZombieVisibility groups;
    groups.reset(bounds.size());
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        bounds[i] = box({float(i % 100) * 3 - 150, 1, float(i / 100) * 3 - 150});
        groups.update(i, bounds[i]);
    }
    const viewer::Frustum camera(glm::ortho(-15.0f, 15.0f, -10.0f, 10.0f, -15.0f, 15.0f));
    const auto stats = compare(groups, bounds, active, camera);
    require(stats.groups < 300 && stats.cameraInstanceTests < 500,
            "Small views of large crowds must avoid population-wide individual culling");
    require(compare(groups, bounds, active, camera).refittedGroups == 0, "Repeated views must not rebuild stationary groups");
    std::cout << "10,000 zombies: " << stats.cameraGroupTests << " group tests, " << stats.cameraInstanceTests
              << " individual tests\n";
    const auto maximum = std::numeric_limits<float>::max();
    bounds[0] = box({maximum, 0, maximum}, glm::vec3(0));
    groups.update(0, bounds[0]); // Double cell keys must not overflow integer coordinates.
    compare(groups, bounds, active, camera);
    // A large crowd gathers, then disperses. Refitting shrinks oversized member
    // storage; removal slot repairs must survive that buffer replacement.
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        bounds[i] = box({1, 1, 1});
        groups.update(i, bounds[i]);
    }
    require(compare(groups, bounds, active, camera).groups == 1, "Gathering must release historical occupied cells");
    for (std::size_t i = 1; i < bounds.size(); ++i) {
        groups.erase(i);
        active[i] = false;
    }
    compare(groups, bounds, active, camera);
    groups.update(1, bounds[1]);
    active[1] = true;
    bounds[0] = box({100, 1, 1});
    groups.update(0, bounds[0]);
    compare(groups, bounds, active, camera);
}
}

int main() {
    try {
        insideOutsideAndBoundaryGroups();
        movementRemovalAndReuse();
        cameraRegionRetainsIndependentLightsAndWideBounds();
        blocksRetainIndependentLightsAndOverhangingBounds();
        blockBoundaryMovesRemovalAndReset();
        smallViewSkipsDistantCells();
        distantBlockFacesRetainTouchingMembers();
        randomViewsMatchFullScan();
        orderingRetainsMasksAcrossDigitBoundaries();
        largeCrowdRejectsGroups();
        std::cout << "Zombie visibility tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie visibility test failed: " << error.what() << '\n';
        return 1;
    }
}
