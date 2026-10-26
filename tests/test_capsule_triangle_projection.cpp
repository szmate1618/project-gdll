// Public collision regressions plus an optional deterministic differential
// capture. Build this SAME source against the saved original and changed
// collision libraries, then compare captures; no reference kernel is copied.
#include "collision_world.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void triangle(viewer::Model& model,glm::vec3 a,glm::vec3 b,glm::vec3 c,
              const char* name="platform",glm::mat4 transform=glm::mat4(1)) {
    viewer::Primitive primitive;
    for (auto position:{a,b,c}) {viewer::Vertex vertex;vertex.position=position;primitive.vertices.push_back(vertex);}
    primitive.indices={0,1,2};
    model.draws.push_back({model.primitives.size(),transform,name});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model floorTriangle(bool reverse=false,glm::mat4 transform=glm::mat4(1)) {
    viewer::Model model;
    triangle(model,{0,0,0},reverse?glm::vec3(2,0,0):glm::vec3(0,0,2),
             reverse?glm::vec3(0,0,2):glm::vec3(2,0,0),"terrain",transform);
    return model;
}
viewer::Model wall(const char* name="building_projection_walls",glm::mat4 transform=glm::mat4(1)) {
    viewer::Model model;triangle(model,{0,-2,-2},{0,4,-2},{0,-2,4},name,transform);return model;
}
bool same(const viewer::Contact& a,const viewer::Contact& b) {
    return a.normal==b.normal && a.point==b.point && a.depth==b.depth &&
           a.terrain==b.terrain && a.terrainSupport==b.terrainSupport;
}
void expectContact(const std::vector<viewer::Contact>& contacts,glm::vec3 normal,glm::vec3 point,float depth) {
    require(contacts.size()==1,"Expected exactly one triangle contact");
    const auto& c=contacts.front();
    require(glm::distance(c.normal,normal)<2e-5f && glm::distance(c.point,point)<2e-5f &&
            std::abs(c.depth-depth)<2e-5f,"Triangle contact normal, point or depth changed");
}
auto key(const viewer::Contact& c) {
    return std::make_tuple(c.depth,c.normal.x,c.normal.y,c.normal.z,c.point.x,c.point.y,c.point.z,
                           c.terrain,c.terrainSupport);
}
void exactOrder(const std::vector<viewer::Contact>& a,const std::vector<viewer::Contact>& b) {
    require(a.size()==b.size(),"Cached/uncached contact count changed");
    for (std::size_t i=0;i<a.size();++i) require(same(a[i],b[i]),"Cached/uncached visitor contact order or fields changed");
}
std::vector<viewer::Contact> visit(const viewer::CollisionWorld& world,glm::vec3 feet,float radius,float height,
                                 float skin,float clearance,viewer::ContactCandidates* cache,
                                 viewer::ContactPolicy policy) {
    std::vector<viewer::Contact> contacts;
    world.visitContacts(feet,radius,height,skin,clearance,{&contacts,[](void* context,const viewer::Contact& c) {
        static_cast<std::vector<viewer::Contact>*>(context)->push_back(c);return true;
    }},cache,policy);
    return contacts;
}
std::optional<viewer::Contact> select(const viewer::CollisionWorld& world,glm::vec3 feet,float radius,float height,
                                     float skin,float clearance,viewer::ContactCandidates* cache,
                                     viewer::ContactPolicy policy,bool first) {
    struct Selection { bool first;std::optional<viewer::Contact> contact; } selected{first,{}};
    world.visitContacts(feet,radius,height,skin,clearance,{&selected,[](void* context,const viewer::Contact& c) {
        auto& s=*static_cast<Selection*>(context);
        if (!s.contact || key(c)>key(*s.contact)) s.contact=c;
        return !s.first;
    }},cache,policy);
    return selected.contact;
}

void expectedGeometry() {
    const float diagonal=std::sqrt(.5f),edgeDepth=.3f-std::sqrt(.02f);
    for (bool reverse:{false,true}) {
        const viewer::CollisionWorld world(floorTriangle(reverse));
        expectContact(world.contacts({.5f,-.1f,.5f},.3f,1.8f),{0,1,0},{.5f,0,.5f},.1f);
        expectContact(world.contacts({1.1f,-.3f,1.1f},.3f,1.8f),{diagonal,0,diagonal},{1,0,1},edgeDepth);
        expectContact(world.contacts({-.1f,-.3f,-.1f},.3f,1.8f),{-diagonal,0,-diagonal},{0,0,0},edgeDepth);
        require(world.contacts({1.4f,-.3f,1.4f},.3f,1.8f).empty(),
                "A capsule inside the triangle AABB but outside its diagonal footprint must miss");
        expectContact(world.contacts({-.1f,-.5f,-.1f},.5f,1),{-diagonal,0,-diagonal},{0,0,0},.5f-std::sqrt(.02f));
        require(world.contacts({-.5f,-.5f,0},.5f,1).empty(),"Exact vertex tangency must not create a penetration");
        require(!world.contacts({std::nextafter(-.5f,0.f),-.5f,0},.5f,1).empty(),
                "A representable step inside vertex tangency must retain contact");
    }
    const viewer::CollisionWorld vertical(wall());
    expectContact(vertical.contacts({.25f,0,0},.5f,2),{1,0,0},{0,.5f,0},.25f);
    for (auto parameters:{glm::vec2(0,0),glm::vec2(.125f,0),glm::vec2(.125f,.0625f)}) {
        const float reach=.5f+parameters.x+parameters.y;
        require(vertical.contacts({reach,0,0},.5f,2,parameters.x,parameters.y).empty(),
                "Exact radius/skin/wall-clearance tangency must remain nonpenetrating");
        require(!vertical.contacts({std::nextafter(reach,0.f),0,0},.5f,2,parameters.x,parameters.y).empty(),
                "Wall contact just inside the complete inflated radius was lost");
        require(vertical.contacts({std::nextafter(reach,1.f),0,0},.5f,2,parameters.x,parameters.y).empty(),
                "Wall contact appeared outside the complete inflated radius");
    }
    const viewer::CollisionWorld panel(wall("fence_panel"));
    require(panel.contacts({.66f,0,0},.5f,2,.125f,.0625f).empty() &&
            !vertical.contacts({.66f,0,0},.5f,2,.125f,.0625f).empty(),
            "Extra wall clearance must inflate building walls only");
    const viewer::CollisionWorld floor(floorTriangle());
    require(floor.contacts({.5f,.03f,.5f},.5f,2,0,.125f).empty(),"Wall clearance must not inflate horizontal support");

    viewer::Model slope;triangle(slope,{0,0,0},{0,0,4},{4,2,0},"terrain");
    const viewer::CollisionWorld sloped(slope);
    expectContact(sloped.contacts({1,.4f,1},.3f,1.8f),glm::normalize(glm::vec3(-.5f,1,0)),
                  {1.08f,.54f,1},.3f-.2f/std::sqrt(1.25f));
    viewer::Model thin;triangle(thin,{0,.7f,0},{0,.7001f,.001f},{0,.701f,0});
    const viewer::CollisionWorld needle(thin);
    require(!needle.contacts({.1f,0,0},.3f,1.8f).empty(),"A thin projected-degenerate face must retain the 3D solver");
    viewer::Model nearVertical;triangle(nearVertical,{0,-2,-2},{.0001f,4,-2},{0,-2,4});
    const viewer::CollisionWorld tiltedWall(nearVertical);
    const auto tilted=tiltedWall.contacts({.1f,0,0},.3f,1.8f);
    require(!tilted.empty() && tilted.front().normal.x>.99f,"A nearly vertical face lost its contact");
    for (auto transform:{glm::rotate(glm::mat4(1),glm::radians(45.f),glm::vec3(0,1,0)),
                         glm::scale(glm::mat4(1),glm::vec3(-1,1,1))}) {
        const viewer::CollisionWorld transformed(wall("building_projection_walls",transform));
        expectContact(transformed.contacts(glm::vec3(transform*glm::vec4(.25f,0,0,1)),.5f,2),
                      glm::vec3(transform*glm::vec4(1,0,0,0)),{0,.5f,0},.25f);
    }
}

viewer::Model scene(unsigned shape,glm::mat4 transform) {
    viewer::Model model;
    switch (shape) {
    case 0: triangle(model,{0,0,0},{0,0,4},{4,0,0},"terrain",transform);break;
    case 1:
        triangle(model,{0,.25f,0},{0,.25f,4},{4,.25f,4},"roads_asphalt",transform);
        triangle(model,{0,.25f,0},{4,.25f,4},{4,.25f,0},"roads_asphalt",transform);
        triangle(model,{.5f,.5f,.5f},{3,.5f,.5f},{.5f,.5f,3},"building_capture_walls",transform);break;
    case 2:
        triangle(model,{0,-2,-2},{0,4,-2},{0,-2,4},"building_capture_walls",transform);
        triangle(model,{-2,0,0},{4,0,0},{-2,4,0},"fence_panel",transform);break;
    case 3:
        triangle(model,{0,0,0},{4,2,0},{0,0,.0001f},"terrain",transform);
        triangle(model,{0,-2,-2},{.0001f,4,-2},{0,-2,4},"platform",transform);break;
    case 4:
        triangle(model,{0,0,0},{0,0,4},{4,.8f,0},"terrain",transform);
        triangle(model,{0,.3f,0},{4,6.3f,0},{0,.3f,4},"roads_residential",transform);break;
    case 5:
        for (int i=0;i<10;++i) {
            const float x=i*.4f;
            triangle(model,{x,0,0},{x,0,4},{x+.6f,.2f,1},i%2?"terrain":"platform",transform);
        }
        break;
    case 6:
        triangle(model,{0,.7f,0},{.001f,.701f,0},{0,.7001f,.001f},"platform",transform);
        triangle(model,{0,0,0},{4,0,4},{4,4e-5f,4.0001f},"platform",transform);break;
    case 7:
        for (int i=0;i<80;++i) {
            const float height=i*.01f;
            triangle(model,{0,height,0},{0,height,4},{4,height,0},i%3?"terrain":"platform",transform);
        }
        break;
    }
    return model;
}

struct Capture {
    std::ofstream stream;
    std::uint64_t cases=0,queries=0;
    explicit Capture(const char* path):stream(path,std::ios::binary) {
        if (!stream) throw std::runtime_error("Cannot create projection contact capture");
        stream.write("GDLLPRJ1",8);
    }
    template<class T> void scalar(const T& value) {stream.write(reinterpret_cast<const char*>(&value),sizeof(value));}
    void vector(glm::vec3 value) {scalar(value.x);scalar(value.y);scalar(value.z);}
    void contact(const viewer::Contact& c) {
        vector(c.normal);vector(c.point);scalar(c.depth);scalar(std::uint8_t(c.terrain));scalar(std::uint8_t(c.terrainSupport));
    }
    void list(const std::vector<viewer::Contact>& contacts) {
        scalar(std::uint32_t(contacts.size()));for (const auto& c:contacts) contact(c);
    }
    void optional(const std::optional<viewer::Contact>& c) {scalar(std::uint8_t(bool(c)));if (c) contact(*c);}
    void query(const viewer::CollisionWorld& world,glm::vec3 feet,float radius,float height,float skin,float clearance) {
        ++cases;vector(feet);scalar(radius);scalar(height);scalar(skin);scalar(clearance);
        auto publicContacts=world.contacts(feet,radius,height,skin,clearance);++queries;
        list(publicContacts);
        auto uncached=visit(world,feet,radius,height,skin,clearance,nullptr,viewer::ContactPolicy::complete);++queries;
        list(uncached);
        auto canonical=uncached;
        std::sort(canonical.begin(),canonical.end(),[](const auto& a,const auto& b){return key(a)<key(b);});
        std::sort(publicContacts.begin(),publicContacts.end(),[](const auto& a,const auto& b){return key(a)<key(b);});
        exactOrder(canonical,publicContacts);
        viewer::ContactCandidates completeCache,walkingCache;
        for (int repeat=0;repeat<2;++repeat) {
            const auto cached=visit(world,feet,radius,height,skin,clearance,&completeCache,viewer::ContactPolicy::complete);++queries;
            exactOrder(cached,uncached);list(cached);
        }
        auto walking=visit(world,feet,radius,height,skin,clearance,&walkingCache,viewer::ContactPolicy::walking);++queries;
        const auto warmWalking=visit(world,feet,radius,height,skin,clearance,&walkingCache,viewer::ContactPolicy::walking);++queries;
        exactOrder(walking,warmWalking);list(walking);list(warmWalking);
        for (bool first:{true,false}) {
            const auto complete=select(world,feet,radius,height,skin,clearance,nullptr,viewer::ContactPolicy::complete,first);++queries;
            const auto selected=select(world,feet,radius,height,skin,clearance,&walkingCache,viewer::ContactPolicy::walking,first);++queries;
            optional(complete);optional(selected);
            if (first && !uncached.empty()) require(complete && same(*complete,uncached.front()),"Early visitor did not retain the first contact");
            if (!first && !uncached.empty()) require(complete && same(*complete,*std::max_element(uncached.begin(),uncached.end(),
                [](const auto& a,const auto& b){return key(a)<key(b);})),"Deepest visitor changed the deterministic contact tie");
        }
    }
};

void differentialCapture(const char* path) {
    Capture capture(path);
    std::mt19937 random(0x585a4341);
    std::uniform_real_distribution<float> xz(-.75f,4.75f),vertical(-.75f,1.6f);
    const std::array<float,7> radii{{1e-6f,.0001f,.05f,.1f,.3f,.5f,1}};
    for (unsigned variant=0;variant<4;++variant) for (unsigned shape=0;shape<8;++shape) {
        glm::mat4 transform(1);
        if (variant==1) transform=glm::translate(transform,{3000,0,-3000});
        if (variant==2) transform=glm::scale(glm::translate(transform,{-3000,0,3000}),glm::vec3(-.125f,1,3));
        if (variant==3) transform=glm::translate(transform,{0,1048576,0});
        const viewer::CollisionWorld world(scene(shape,transform));
        capture.scalar(variant);capture.scalar(shape);
        for (unsigned query=0;query<320;++query) {
            float radius=radii[query%radii.size()],height=std::max(1.8f,2*radius);
            const float skin=query%3==0?0:query%3==1?.002f:.125f;
            const float clearance=query%3==0?0:query%3==1?.05f:.0625f;
            glm::vec3 feet(xz(random),vertical(random),xz(random));
            if (query%11==0) height=2*radius; // A spherical capsule.
            if (query%13==0) height=64; // A remote upper endpoint relative to small faces.
            if (query%8==3 || query%8==4) {
                float offset=(radius+skin+(query%8==4?.05f:0))*std::sqrt(.5f);
                offset=std::nextafter(offset,query%2?0.f:std::numeric_limits<float>::infinity());
                feet={2+offset,-radius,2+offset};
            }
            if (query%17==0) {radius=1e-6f;height=.2f;feet={2+2e-6f,-.1f,2+2e-6f};}
            if (query%79==0) {radius=2e19f;height=4e19f;feet={.5f,0,.5f};} // Finite reach with overflowing square.
            feet=glm::vec3(transform*glm::vec4(feet,1));
            capture.query(world,feet,radius,height,skin,clearance);
        }
    }
    if (!capture.stream) throw std::runtime_error("Projection contact capture write failed");
    std::cout<<"Captured "<<capture.cases<<" inputs / "<<capture.queries<<" contact enumerations with explicit fields\n";
}
}

int main(int argc,char** argv) {
    try {
        require(argc==1 || argc==2,"Usage: test_capsule_triangle_projection [OUTPUT.bin]");
        expectedGeometry();
        if (argc==2) differentialCapture(argv[1]);
        std::cout<<"Capsule/triangle footprint, tangency, winding and conditioning regressions passed\n";
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
