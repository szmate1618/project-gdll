// Headless scenario hot-path timing: navigation, physical movement and final
// coverage validation. Excludes route setup, perception, spatial/render updates,
// crowd, trees and live props. No timing thresholds; run an optimized build.
#include "collision_world.hpp"
#include "zombie_movement.hpp"
#include "zombie_navigation.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr unsigned width=128, population=1600, frames=120;
constexpr float dt=1.f/60;

viewer::navmesh_path::Mesh navigationGrid() {
    using namespace viewer::navmesh_path;
    std::vector<Point> vertices;
    std::vector<Polygon> polygons;
    std::vector<Edge> edges;
    for (unsigned z=0;z<=width;++z) for (unsigned x=0;x<=width;++x)
        vertices.push_back({double(x),double(z)});
    for (unsigned z=0;z<width;++z) for (unsigned x=0;x<width;++x) {
        const auto id=z*width+x, a=z*(width+1)+x, b=a+1, c=b+width+1, d=c-1;
        polygons.push_back({static_cast<std::uint32_t>(edges.size()),4});
        edges.push_back({a,z>0?id-width:noPolygon});
        edges.push_back({b,x+1<width?id+1:noPolygon});
        edges.push_back({c,z+1<width?id+width:noPolygon});
        edges.push_back({d,x>0?id-1:noPolygon});
    }
    return Mesh(std::move(vertices),std::move(polygons),std::move(edges));
}

viewer::Model slopingTerrain() {
    viewer::Model model;
    viewer::Primitive terrain;
    for (unsigned z=0;z<=width;++z) for (unsigned x=0;x<=width;++x) {
        viewer::Vertex vertex; vertex.position={float(x),.025f*x,float(z)};
        terrain.vertices.push_back(vertex);
    }
    for (unsigned z=0;z<width;++z) for (unsigned x=0;x<width;++x) {
        const auto a=z*(width+1)+x, b=a+1, c=b+width+1, d=c-1;
        terrain.indices.insert(terrain.indices.end(),{a,d,c,a,c,b});
    }
    model.primitives.push_back(std::move(terrain));
    model.draws.push_back({0,glm::mat4(1),"terrain"});
    return model;
}
}

int main() {
    try {
        const viewer::CollisionWorld world(slopingTerrain());
        for (unsigned trial=0;trial<5;++trial) {
            viewer::ZombieNavigation navigation(navigationGrid(),population);
            std::vector<glm::vec3> positions;
            std::vector<viewer::WalkingGroundCache> ground(population);
            for (unsigned id=0;id<population;++id) {
                const float x=1.25f+(id%40)*2, z=1.25f+(id/40)*2;
                positions.push_back({x,.025f*x,z});
                if (!navigation.plan(id,positions.back(),positions.back()+glm::vec3(30,0,0)))
                    throw std::runtime_error("Cannot prepare benchmark routes");
            }
            const auto begin=std::chrono::steady_clock::now();
            for (unsigned frame=0;frame<frames;++frame) for (unsigned id=0;id<population;++id) {
                const auto start=positions[id];
                const auto velocity=navigation.follow(id,start,1.2f,dt,world);
                const auto desired=start+glm::vec3(velocity.x*dt,0,velocity.y*dt);
                auto movement=viewer::moveZombieBody(start,desired,world,nullptr,id,&ground[id]);
                if (!navigation.movementClear(id,start,movement.feet)) movement={start};
                navigation.recordMovement(id,start,movement.feet,dt);
                positions[id]=movement.feet;
            }
            const double milliseconds=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-begin).count()/frames;
            double checksum=0;
            for (const auto p:positions) checksum+=p.x+p.y+p.z;
            if (navigation.stats().requests!=population)
                throw std::runtime_error("Benchmark started runtime path searches");
            std::cout<<"trial "<<trial+1<<": "<<milliseconds<<" ms/step; "
                     <<navigation.stats().movementValidationReuses<<" reused segment checks; "
                     <<"position checksum "<<checksum<<'\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
