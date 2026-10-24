// Deterministic local differential corpus for collision/ground experiments.
// Writes explicit fields, excluding struct padding. Compare captures from two
// builds on the same platform; includes flat/sloped/steep faces, edges and large
// world coordinates. This is not a portable binary resource format.
#include "collision_world.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <tuple>

int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("Usage: capture_collision_queries OUTPUT.bin");
        std::ofstream out(argv[1],std::ios::binary);
        if(!out) throw std::runtime_error("Cannot write contact corpus");
        const auto write=[&](const auto& value) { out.write(reinterpret_cast<const char*>(&value),sizeof(value)); };
        const auto vector=[&](glm::vec3 p) { write(p.x);write(p.y);write(p.z); };
        std::mt19937 random(0x4c4c4f43);
        std::uniform_real_distribution<float> xy(-5,5),rise(-.6f,.6f),offset(-.8f,.8f);
        for(unsigned shape=0;shape<32;++shape) {
            viewer::Model model;
            viewer::Primitive primitive;
            const float grade=shape%4==0?0:shape%4==1?.2f:shape%4==2?1.5f:-.4f;
            for(const auto p:{glm::vec3(-4,-4*grade,-4),glm::vec3(-4,-4*grade,4),
                             glm::vec3(4,4*grade,4),glm::vec3(4,4*grade,-4)}) {
                viewer::Vertex v;v.position=p;primitive.vertices.push_back(v);
            }
            primitive.indices=shape%3==0?std::vector<std::uint32_t>{0,1,2}:std::vector<std::uint32_t>{0,1,2,0,2,3};
            const float shift=shape>=16?3000.f:0.f;
            const auto transform=glm::translate(glm::mat4(1),{shift,0,-shift});
            model.primitives.push_back(std::move(primitive));
            model.draws.push_back({0,transform,shape%2?"terrain":"platform"});
            const viewer::CollisionWorld world(model);
            viewer::WalkingGroundCache cache;
            for(unsigned query=0;query<1500;++query) {
                const float x=xy(random),z=xy(random),r=shape%3==0?.1f:.3f;
                const glm::vec3 feet{x+shift,x*grade+offset(random),z-shift};
                auto contacts=world.contacts(feet,r,1.8f,query%2?.002f:0,.05f);
                const auto key=[](const viewer::Contact& c) {
                    return std::make_tuple(c.depth,c.normal.x,c.normal.y,c.normal.z,c.point.x,c.point.y,c.point.z,
                                           c.terrain,c.terrainSupport);
                };
                std::sort(contacts.begin(),contacts.end(),[&](const auto& a,const auto& b){return key(a)<key(b);});
                write(std::uint32_t(contacts.size()));
                for(const auto& c:contacts) {
                    vector(c.normal);vector(c.point);write(c.depth);out.put(c.terrain);out.put(c.terrainSupport);
                }
                const float ceiling=feet.y+rise(random);
                for(bool cached:{false,true}) {
                    const auto ground=world.walkingGroundAt(feet.x,feet.z,-100,ceiling,cached?&cache:nullptr);
                    out.put(bool(ground));
                    if(ground) {write(ground->height);vector(ground->normal);out.put(ground->terrain);}
                }
            }
        }
        if(!out) throw std::runtime_error("Contact corpus write failed");
        std::cout<<"Captured 48000 capsule queries and 96000 ground queries\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
