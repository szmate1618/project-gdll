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
    float maximumZ=position.z;
    for(int frame=0;frame<800 && glm::length(position-goal)>1e-4f;++frame) {
        const auto velocity=navigation.desiredVelocity(0,position,goal,2,.05f,world);
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

void movingAndBlockedGoal() {
    viewer::ZombieNavigation navigation(grid(std::vector<std::string>(10,"..........")),2);
    const viewer::CollisionWorld world(viewer::Model{});
    glm::vec3 p{1,0,1},goal{8,0,1};
    auto advance=[&](glm::vec3 target) {
        const auto v=navigation.desiredVelocity(0,p,target,2,.1f,world);
        const auto next=p+glm::vec3(v.x*.1f,0,v.y*.1f);
        navigation.recordMovement(0,p,next,.1f);p=next;
    };
    advance(goal);
    const auto old=navigation.pathFor(0);
    goal.z=3;
    for(int i=0;i<4;++i) advance(goal);
    require(navigation.stats().requests==1,"moving goal ignored the replan cooldown");
    advance(goal);
    require(navigation.stats().requests==2 && navigation.pathFor(0)!=old,"moving goal did not replace its route");
    const auto velocity=navigation.desiredVelocity(0,p,{-1,0,1},2,.1f,world);
    require(glm::length(velocity)==0 && !navigation.pathFor(0),"blocked changed goal retained a stale route");
    require(navigation.lastStatus()==viewer::navmesh_path::Status::blockedEndpoint,"blocked endpoint status lost");
    require(navigation.pathUsedBytes()==0,"failed goal leaked old path storage");
}

void disconnectedAndOverflow() {
    const viewer::CollisionWorld empty(viewer::Model{});
    viewer::ZombieNavigation disconnected(grid({"...#...","...#...","...#..."}),1);
    require(glm::length(disconnected.desiredVelocity(0,{1,0,1},{5,0,1},2,.05f,empty))==0,
            "disconnected target produced motion");
    require(disconnected.lastStatus()==viewer::navmesh_path::Status::noPath && disconnected.stats().requests==0,
            "component precheck started a search");
    const std::vector<viewer::TrunkCollider> trunks{{{2,0,2},.15f,5},{{2,0,3},.15f,5},{{2,0,4},.15f,5}};
    const viewer::CollisionWorld forest(viewer::Model{},trunks);
    viewer::ZombieNavigation bounded(grid(std::vector<std::string>(10,"..........")),1,2);
    const auto velocity=bounded.desiredVelocity(0,{1,0,1},{8,0,1},2,.05f,forest);
    require(glm::length(velocity)==0 && bounded.stats().trunkOverflows==1,"overflow silently dropped local trunks");
    viewer::ZombieNavigation nobody(grid({"..",".."}),0);
    require(nobody.capacity()==0 && glm::length(nobody.desiredVelocity(0,{0,0,0},{1,0,1},2,.05f,empty))==0,
            "empty population navigation is unsafe");
}

void treeBypass() {
    const glm::vec2 center{9,5};
    const viewer::CollisionWorld world(viewer::Model{},{{{center.x,0,center.y},.6f,5}});
    viewer::ZombieNavigation navigation(grid(std::vector<std::string>(10,"....................")),1);
    glm::vec3 position{1,0,5},goal{18,0,5};
    float excursion=0;
    for(int frame=0;frame<1200 && glm::length(position-goal)>1e-4f;++frame) {
        const auto v=navigation.desiredVelocity(0,position,goal,2,.05f,world);
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
    try {routeAroundWall();routeAroundWall(true);movingAndBlockedGoal();disconnectedAndOverflow();treeBypass();
        std::cout<<"navigation route/replan/disconnection/overflow/trunk tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
