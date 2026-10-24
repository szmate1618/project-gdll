#include "zombie_navigation.hpp"
#include "zombie_movement.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }

viewer::navmesh_path::Mesh grid(const std::vector<std::string>& cells) {
    using namespace viewer::navmesh_path;
    const auto h=cells.size(),w=cells.front().size();
    std::vector<Point> vertices;
    for(std::size_t z=0;z<=h;++z) for(std::size_t x=0;x<=w;++x)
        vertices.push_back({double(x),double(z)});
    std::vector<Polygon> polygons;
    std::vector<Edge> edges;
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::pair<std::uint32_t,std::uint32_t>> shared;
    for(std::size_t z=0;z<h;++z) for(std::size_t x=0;x<w;++x) if(cells[z][x]!='#') {
        const auto id=static_cast<std::uint32_t>(polygons.size());
        polygons.push_back({static_cast<std::uint32_t>(edges.size()),4});
        const auto a=static_cast<std::uint32_t>(z*(w+1)+x),b=a+1,c=b+static_cast<std::uint32_t>(w+1),d=c-1;
        const std::uint32_t ring[]={a,b,c,d};
        for(unsigned i=0;i<4;++i) {
            const auto index=static_cast<std::uint32_t>(edges.size());
            edges.push_back({ring[i],noPolygon});
            const auto key=std::minmax(ring[i],ring[(i+1)%4]);
            const auto previous=shared.find(key);
            if(previous==shared.end()) shared.emplace(key,std::make_pair(id,index));
            else {edges[index].neighbor=previous->second.first;edges[previous->second.second].neighbor=id;shared.erase(previous);}
        }
    }
    return Mesh(std::move(vertices),std::move(polygons),std::move(edges));
}

void routeAroundWall(bool mapCoordinates=false) {
    auto mesh=grid({"....#.....","....#.....","....#.....","....#.....",
                    "....#.....","....#.....","..........",".........."});
    const double offsetX=mapCoordinates?3060.119:0,offsetZ=mapCoordinates?-2540.137:0;
    const double scale=mapCoordinates?1.07:1;
    viewer::Model terrain;
    if(mapCoordinates) {
        auto vertices=mesh.vertices();
        for(auto& point:vertices) {point.x=point.x*scale+offsetX;point.z=point.z*scale+offsetZ;}
        mesh=viewer::navmesh_path::Mesh(std::move(vertices),mesh.polygons(),mesh.edges());
        // A nonempty world exercises the physical minimum-step cutoff as well
        // as nonrepresentable portal corners at kilometre-scale float positions.
        viewer::Primitive primitive;
        for(const auto p:{glm::vec3(-10000,0,-10000),glm::vec3(-10000,0,10000),
                          glm::vec3(10000,0,10000),glm::vec3(10000,0,-10000)}) {
            viewer::Vertex vertex;vertex.position=p;primitive.vertices.push_back(vertex);
        }
        primitive.indices={0,1,2,0,2,3};
        terrain.primitives.push_back(std::move(primitive));
        terrain.draws.push_back({0,glm::mat4(1),"terrain"});
    }
    viewer::ZombieNavigation navigation(std::move(mesh),2);
    const viewer::CollisionWorld world(terrain);
    glm::vec3 position{float(offsetX+1.5*scale),0,float(offsetZ+1.5*scale)},
              goal{float(offsetX+8.5*scale),0,float(offsetZ+1.5*scale)};
    require(navigation.plan(0,position,goal),"cold route planning failed");
    float maximumZ=position.z;
    for(int frame=0;frame<800 && glm::length(position-goal)>1e-4f;++frame) {
        const auto velocity=navigation.follow(0,position,2,.05f,world);
        const auto next=viewer::moveZombieBody(position,position+glm::vec3(velocity.x*.05f,0,velocity.y*.05f),world).feet;
        require(navigation.segmentClear(position,next),"wall route left the walkable mesh");
        navigation.recordMovement(0,position,next,.05f);position=next;maximumZ=std::max(maximumZ,position.z);
    }
    require(glm::length(position-goal)<=1e-4f,"route follower did not finish the gameplay target");
    require(maximumZ>float(offsetZ+5.99*scale),"navigation did not route around the wall");
    require(navigation.stats().requests==1 && navigation.stats().foundPaths==1,"static route was needlessly replanned");
    require(navigation.pathArenaBytes()==10'000'000 && navigation.pathUsedBytes()>0,"path arena contract changed");
    navigation.reset(0);
    require(!navigation.pathFor(0) && navigation.pathUsedBytes()==0,"reset did not release the route");
    navigation.resetPopulation(3);
    require(navigation.capacity()==3 && navigation.pathUsedBytes()==0,"population replacement retained stale paths");
}

void followNeverSearches() {
    viewer::ZombieNavigation navigation(grid(std::vector<std::string>(10,"..........")),2);
    const viewer::CollisionWorld world(viewer::Model{});
    const glm::vec3 position{1,0,1},goal{8,0,1};
    require(navigation.follow(0,position,2,.1f,world)==glm::vec2(0) && navigation.stats().requests==0,
            "Following an unplanned actor started a search");
    require(navigation.plan(0,position,goal),"Cold route planning failed");
    const auto route=navigation.pathFor(0);
    // A physical obstruction can reject every step for several seconds.
    for (int frame=0;frame<100;++frame) {
        (void)navigation.follow(0,position,2,.1f,world);
        navigation.recordMovement(0,position,position,.1f);
    }
    require(navigation.stats().requests==1 && navigation.pathFor(0)==route,
            "A stalled scenario actor searched for a replacement path");
    require(navigation.follow(0,{-1,0,1},2,.1f,world)==glm::vec2(0) && !navigation.pathFor(0),
            "An uncovered position retained a stale path");
    for (int frame=0;frame<30;++frame) (void)navigation.follow(0,position,2,.1f,world);
    require(navigation.stats().requests==1 && navigation.pathUsedBytes()==0,
            "An invalidated path was automatically recreated");
    require(navigation.plan(1,position,goal),"Second cold route planning failed");
    require(navigation.follow(1,{2,0,1},2,.1f,world)==glm::vec2(0) && !navigation.pathFor(1) &&
                navigation.stats().requests==2,
            "A teleport triggered a runtime search instead of invalidating the route");
}

void movementValidationReuse() {
    viewer::ZombieNavigation navigation(grid({"....", ".##.", "...."}),2);
    const viewer::CollisionWorld world(viewer::Model{});
    const glm::vec3 start{.5f,0,.5f},goal{3.5f,0,.5f};
    require(navigation.plan(0,start,goal),"Validation fixture plan failed");
    const auto velocity=navigation.follow(0,start,1,.1f,world);
    const auto end=start+glm::vec3(velocity.x*.1f,0,velocity.y*.1f);
    require(end!=start && navigation.movementClear(0,start,end+glm::vec3(0,3,0)) &&
            navigation.stats().movementValidationReuses==1,
            "Unchanged horizontal movement did not reuse validation across ground height changes");
    require(!navigation.movementClear(0,start,{1.5f,0,1.5f}) &&
            !navigation.movementClear(0,{-1,0,0},end),
            "Changed collision/crowd movement reused unrelated validation");
    require(navigation.movementClear(0,start,(start+end)*.5f) &&
            navigation.stats().movementValidationReuses==1,
            "Shortened valid steps must receive their own coverage check");
    require(navigation.movementClear(1,start,end) && navigation.stats().movementValidationReuses==1,
            "Validation was reused for another actor");
    navigation.recordMovement(0,start,end,.1f);
    require(navigation.movementClear(0,start,end) && navigation.stats().movementValidationReuses==1,
            "Accepted movement retained an old validation cache");
    (void)navigation.follow(0,end,1,.1f,world);
    navigation.reset(0);
    require(navigation.movementClear(0,start,end) && navigation.stats().movementValidationReuses==1,
            "Reset retained an old validation cache");
    require(navigation.plan(0,start,goal),"Replan fixture failed");
    (void)navigation.follow(0,start,1,.1f,world);
    require(!navigation.plan(0,start,{-1,0,0}) && navigation.movementClear(0,start,end) &&
            navigation.stats().movementValidationReuses==1,
            "Failed planning retained an old validation cache");
}

void coverageRegionReuse() {
    viewer::ZombieNavigation navigation(grid({"..........","..........",".........."}),1);
    const viewer::CollisionWorld empty(viewer::Model{});
    glm::vec3 position{.2f,0,1.5f};
    require(navigation.plan(0,position,{9.8f,0,1.5f}),"Coverage-region fixture failed to plan");
    for(int frame=0;frame<300;++frame) {
        const auto velocity=navigation.follow(0,position,1.2f,1.f/60,empty);
        const auto next=position+glm::vec3(velocity.x/60,0,velocity.y/60);
        require(navigation.segmentClear(position,next),"Reused region allowed uncovered movement");
        navigation.recordMovement(0,position,next,1.f/60);position=next;
    }
    require(position.x>6 && navigation.stats().coverageRegionReuses>0 &&
            navigation.stats().coverageRegionBuilds>0,"Straight travel did not reuse certified coverage");
    require(!navigation.movementClear(0,position,{-1,0,1.5f}),"A changed segment escaped its coverage region");
    require(navigation.stats().requests==1,"Coverage reuse started a runtime path search");
}

void disconnectedAndOverflow() {
    const viewer::CollisionWorld empty(viewer::Model{});
    viewer::ZombieNavigation disconnected(grid({"...#...","...#...","...#..."}),1);
    require(!disconnected.plan(0,{1,0,1},{5,0,1}) &&
            disconnected.follow(0,{1,0,1},2,.05f,empty)==glm::vec2(0),
            "disconnected target produced motion");
    require(disconnected.lastStatus()==viewer::navmesh_path::Status::noPath && disconnected.stats().requests==0,
            "component precheck started a search");
    const std::vector<viewer::TrunkCollider> trunks{{{2,0,2},.15f,5},{{2,0,3},.15f,5},{{2,0,4},.15f,5}};
    const viewer::CollisionWorld forest(viewer::Model{},trunks);
    viewer::ZombieNavigation bounded(grid(std::vector<std::string>(10,"..........")),1,2);
    require(bounded.plan(0,{1,0,1},{8,0,1}),"Overflow fixture cold plan failed");
    const auto velocity=bounded.follow(0,{1,0,1},2,.05f,forest);
    require(glm::length(velocity)==0 && bounded.stats().trunkOverflows==1,"overflow silently dropped local trunks");
    viewer::ZombieNavigation nobody(grid({"..",".."}),0);
    require(nobody.capacity()==0 && glm::length(nobody.follow(0,{0,0,0},2,.05f,empty))==0,
            "empty population navigation is unsafe");
}

void treeBypass() {
    const glm::vec2 center{9,5};
    const viewer::CollisionWorld world(viewer::Model{},{{{center.x,0,center.y},.6f,5}});
    viewer::ZombieNavigation navigation(grid(std::vector<std::string>(10,"....................")),1);
    glm::vec3 position{1,0,5},goal{18,0,5};
    require(navigation.plan(0,position,goal),"cold tree route planning failed");
    float excursion=0;
    for(int frame=0;frame<1200 && glm::length(position-goal)>1e-4f;++frame) {
        const auto v=navigation.follow(0,position,2,.05f,world);
        const auto next=viewer::moveZombieBody(position,position+glm::vec3(v.x*.05f,0,v.y*.05f),world).feet;
        const glm::vec2 a{position.x,position.z},b{next.x,next.z},d=b-a;
        const float t=glm::dot(d,d)>0?std::clamp(glm::dot(center-a,d)/glm::dot(d,d),0.f,1.f):0;
        require(glm::length(a+d*t-center)>=.9f-.0003f,"tree bypass penetrated the body/trunk sweep");
        require(navigation.segmentClear(position,next),"tree steering left the navmesh");
        navigation.recordMovement(0,position,next,.05f);position=next;
        excursion=std::max(excursion,std::abs(position.z-5));
    }
    require(glm::length(position-goal)<=1e-4f && excursion>.9f,"Detour did not bypass the centered trunk");
    require(navigation.stats().velocitySamples>0 && navigation.stats().obstacleTests>0,"tree sampler was not used");
}
}

int main() {
    try {routeAroundWall();routeAroundWall(true);followNeverSearches();movementValidationReuse();coverageRegionReuse();disconnectedAndOverflow();treeBypass();
        std::cout<<"scenario route/no-runtime-search/disconnection/overflow/trunk tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
