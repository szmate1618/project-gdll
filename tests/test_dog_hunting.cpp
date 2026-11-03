#include "dog_layer.hpp"
#include "navmesh_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using viewer::DogState;
using Phase = viewer::DogHunting::Phase;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::shared_ptr<const viewer::navmesh_path::Mesh> mesh(const std::set<std::pair<unsigned,unsigned>>& holes = {}) {
    auto source = test_navmesh::convexSquares(6,4,holes);
    auto vertices = source.vertices(); for (auto& p : vertices) { p.x *= 20; p.z *= 20; }
    return std::make_shared<const viewer::navmesh_path::Mesh>(std::move(vertices),source.polygons(),source.edges());
}
void quad(viewer::Model& model, const char* name, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    viewer::Primitive primitive;
    for (auto p : {a,b,c,d}) { viewer::Vertex v; v.position = p; primitive.vertices.push_back(v); }
    primitive.indices = {0,1,2,0,2,3};
    model.draws.push_back({model.primitives.size(),glm::mat4(1),name}); model.primitives.push_back(std::move(primitive));
}
viewer::Model ground() {
    viewer::Model model; quad(model,"terrain",{-100,0,-100},{-100,0,200},{200,0,200},{200,0,-100}); return model;
}
viewer::DogLayer dog(std::shared_ptr<const viewer::navmesh_path::Mesh> navigation, glm::vec3 spawn = {10,0,30}) {
    viewer::DogLayer dogs; dogs.assets.emplace_back(); dogs.instances.resize(1);
    dogs.instances[0].root = dogs.instances[0].transform = glm::translate(glm::mat4(1),spawn);
    dogs.rebuildSpatialIndex(); dogs.enableHunting(std::move(navigation));
    return dogs;
}
void scent(viewer::DogLayer& dogs, glm::vec3 source = {90,0,30}) {
    dogs.updateScent(0,{}); dogs.updateScent(90,{{source,true,true}});
}
void fallbacks() {
    const viewer::CollisionWorld world(ground());
    for (unsigned count = 0; count <= 3; ++count) {
        std::set<std::pair<unsigned,unsigned>> holes;
        for (unsigned i = 0; i < count; ++i) holes.emplace(4-i,1);
        auto dogs = dog(mesh(holes)); scent(dogs);
        auto& before = dogs.behaviors[0]; before.active = true; before.state = DogState::wandering;
        before.target = {10,0,60}; before.wander.hasTarget = true; before.wander.remainingLegs = 4;
        before.wander.waypoints = {{10,0,30}}; before.wander.nextPartCheck = 1000;
        dogs.updateBehavior(.1f,{},world,90);
        const auto& behavior = dogs.behaviors[0];
        if (count < 3) {
            require(behavior.state == DogState::hunting && behavior.hunt &&
                    viewer::ScentGrid::cellAt(behavior.target) == std::optional<viewer::ScentGrid::Cell>({4-count,1}),
                    "Hunting must try the peak, then each of the two preceding ascent cells");
        } else require(behavior.state == DogState::wandering && !behavior.hunt && before.target == glm::vec3(10,0,60) &&
                       behavior.wander.remainingLegs == 4 && behavior.wander.hasTarget,
                       "Three unreachable candidates must preserve the prior wandering trip");
    }
    auto peak = dog(mesh()); scent(peak,{10,0,30});
    peak.updateBehavior(.1f,{},world,90);
    require(peak.behaviors[0].state == DogState::hunting && peak.behaviors[0].hunt->phase == Phase::search,
            "An ascent containing just the starting cell must safely begin its local search");
    auto shortRoute = dog(mesh({{1,1}})); scent(shortRoute,{30,0,30});
    shortRoute.updateBehavior(.1f,{},world,90);
    require(shortRoute.behaviors[0].state == DogState::hunting,
            "A two-cell ascent must use its sole predecessor without accessing a nonexistent third cell");
}
void interruptions() {
    const viewer::CollisionWorld world(ground());
    auto noise = dog(mesh()); scent(noise); noise.updateBehavior(.1f,{},world,90);
    noise.makeNoise({10,0,60},100);
    require(noise.behaviors[0].state == DogState::looking && !noise.behaviors[0].hunt &&
            noise.huntingNavigation()->pathFor(0) == 0,"Hearing must cancel hunting immediately and release its route");
    auto sight = dog(mesh()); scent(sight); sight.updateBehavior(.1f,{},world,90);
    const auto feet = glm::vec3(sight.instances[0].root[3]);
    const auto ahead = feet+glm::vec3(sight.instances[0].root[2])*5.f;
    sight.updateBehavior(.1f,{{ahead,1.7f,1.8f,1}},world,90.1);
    require(sight.behaviors[0].state == DogState::chasing && !sight.behaviors[0].hunt &&
            sight.huntingNavigation()->pathFor(0) == 0,"Sight must take priority over hunting and release the route");
    auto pursuing = dog(mesh()); scent(pursuing);
    pursuing.behaviors[0].active = true; pursuing.behaviors[0].state = DogState::investigating;
    pursuing.behaviors[0].target = {10,0,60};
    pursuing.updateBehavior(.1f,{{{300,0,300},1.7f,1.8f,99}},world,90);
    require(pursuing.behaviors[0].state == DogState::investigating && !pursuing.behaviors[0].hunt,
            "Scent must not replace an active investigation");
    auto dead = dog(mesh()); scent(dead); dead.updateBehavior(.1f,{},world,90); dead.damage(0,100);
    require(!dead.behaviors[0].hunt && dead.huntingNavigation()->pathFor(0) == 0,"Death must free a hunting route");
}
void routeAndTerritorialHome() {
    const auto navigation = mesh({{2,0},{2,1}});
    // The real bake expands obstacles for body clearance. Keep that same
    // contract: the mesh boundary at z=40 must clear the physical wall tip.
    auto model = ground(); quad(model,"wall",{50,0,-10},{50,0,39.5f},{50,3,39.5f},{50,3,-10});
    const viewer::CollisionWorld world(model);
    auto dogs = dog(navigation,{10,0,10});
    dogs.instances[0].root[3] = glm::vec4(10,0,30,1); dogs.instances[0].transform = dogs.instances[0].root;
    dogs.rebuildSpatialIndex();
    require(dogs.instances[0].home == std::optional<glm::vec3>({10,0,10}),"Moving/reindexing must never replace the spawn home");
    scent(dogs);
    bool entered = false, searching = false, returning = false, finished = false;
    float maximumZ = 30;
    int legs = 0;
    for (int step = 0; step < 6500; ++step) {
        const auto before = glm::vec3(dogs.instances[0].root[3]);
        const auto oldTarget = dogs.behaviors[0].target;
        dogs.updateBehavior(.1f,{},world,90+step*.1);
        const auto feet = glm::vec3(dogs.instances[0].root[3]);
        require(glm::distance(feet,before) <= .1203f,"Hunting and return must use investigation walking speed");
        require(navigation->segmentClear({before.x,before.z},{feet.x,feet.z}),"Dog path movement must remain on its navmesh");
        for (const auto& contact : world.contacts(feet,viewer::dogRadius,viewer::dogHeight))
            require(contact.terrainSupport || contact.depth < .001f || contact.normal.y > .7f,"Hunting must not cross physical walls");
        const auto& behavior = dogs.behaviors[0];
        if (behavior.hunt) {
            entered = true;
            maximumZ = std::max(maximumZ,feet.z);
            if (behavior.hunt->phase == Phase::search) {
                searching = true;
                if (behavior.target != oldTarget) {
                    ++legs;
                    require(std::abs(glm::length(glm::vec2(behavior.target.x-before.x,behavior.target.z-before.z))-5) < .002f,
                            "Local hunting searches must select five-meter legs");
                }
            }
            returning = returning || behavior.hunt->phase == Phase::returning;
        } else if (entered && behavior.state == DogState::idle) { finished = true; break; }
    }
    const auto end = glm::vec3(dogs.instances[0].root[3]);
    std::cout << "Hunt walk: entered=" << entered << " search=" << searching << " returning=" << returning
              << " finished=" << finished << " maxZ=" << maximumZ << " legs=" << legs
              << " end=" << end.x << ',' << end.y << ',' << end.z << '\n';
    require(entered && searching && returning && finished && maximumZ >= 40 && legs >= 1 && legs <= 7 &&
            glm::distance(glm::vec3(dogs.instances[0].root[3]),glm::vec3(10,0,10)) < .16f,
            "Hunting must route around the wall, search locally, and return to its original spawn rather than the hunt start");
    // Route storage/progress is independent even when sharing the mesh object.
    viewer::ZombieNavigation humanoids(navigation,1);
    require(humanoids.sharedMesh() == dogs.huntingNavigation()->sharedMesh() && humanoids.pathFor(0) == 0,
            "Shared immutable geometry must not share dog/humanoid paths");
}
void failedLocalSearchAndReturn() {
    const viewer::CollisionWorld world(viewer::Model{});
    const auto tiny = std::make_shared<const viewer::navmesh_path::Mesh>(test_navmesh::convexSquares(1,1));
    for (const bool reachableHome : {true,false}) {
        const glm::vec3 home(reachableHome ? .2f : -.2f,0,.2f);
        auto dogs = dog(tiny,home);
        dogs.instances[0].root[3] = glm::vec4(.6f,0,.6f,1); dogs.instances[0].transform = dogs.instances[0].root;
        dogs.rebuildSpatialIndex(); scent(dogs,{.6f,0,.6f});
        bool entered = false, returning = false, finished = false;
        for (int step = 0; step < 100; ++step) {
            dogs.updateBehavior(.1f,{},world,90+step*.1);
            const auto feet = glm::vec3(dogs.instances[0].root[3]);
            require(tiny->locate({feet.x,feet.z}) != viewer::navmesh_path::noPolygon,
                    "A failed local search or home route must never move outside the walkable mesh");
            const auto& behavior = dogs.behaviors[0];
            if (behavior.hunt) {
                entered = true;
                returning = returning || behavior.hunt->phase == Phase::returning;
            } else if (entered && behavior.state == DogState::idle) { finished = true; break; }
        }
        require(entered && finished && returning == reachableHome && dogs.huntingNavigation()->pathFor(0) == 0,
                "When no five-meter leg fits, return home if reachable; otherwise stop and release the route");
        if (reachableHome) require(glm::distance(glm::vec3(dogs.instances[0].root[3]),home) < .16f,
                                   "A failed local search must return to the original spawn");
    }
}
}
int main() {
    try { fallbacks(); interruptions(); routeAndTerritorialHome(); failedLocalSearchAndReturn(); std::cout << "Dog scent hunting, fallback and territorial returns passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
