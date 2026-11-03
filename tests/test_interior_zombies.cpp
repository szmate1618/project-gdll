#include "house_fixture.hpp"
#include "apartment_fixture.hpp"
#include "interior_zombies.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"
#include "zombie_layer.hpp"
#include "simulation_clock.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
float wallDistance(const viewer::HouseRoom& room, glm::vec2 p) {
    float distance = 1000;
    for (std::size_t i = 0; i < room.polygon.size(); ++i) {
        const auto a = room.polygon[i], edge = room.polygon[(i + 1) % room.polygon.size()] - a;
        const float t = glm::clamp(glm::dot(p - a, edge) / glm::dot(edge, edge), 0.f, 1.f);
        distance = std::min(distance, glm::distance(p, a + t * edge));
    }
    return distance;
}
void placement(const glm::mat4& transform, bool apartment = false) {
    auto model = apartment ? apartment_fixture::block(20,24,5,transform) : house_fixture::house(transform);
    // This deterministic layout has partitioned upper rooms and their doors,
    // so concealment/placement are exercised on both storeys.
    const std::string prefix = apartment ? "building_prefab" : "building_house";
    for (auto& draw : model.draws) if (draw.name.find(prefix + "_") == 0)
        draw.name.replace(0, prefix.size(), "building_render_0");
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, collision);
    doors.setAngle(1, .7f);
    floors.update({doors.panel(1).center});
    require(floors.residentCount() == 1, "Placement fixture must generate its building interior");
    auto plan = floors.resident().begin()->second;
    int selected = 0, occupied = 0, groups = 0, hidden = 0, corners = 0, upstairs = 0;
    bool testedConcealment = false;
    for (int seed = 0; seed < 1000; ++seed) {
        plan.name = "encounter_" + std::to_string(seed);
        const auto placements = viewer::placeInteriorZombies(plan, doors, collision);
        const auto repeated = viewer::placeInteriorZombies(plan, doors, collision);
        require(placements.size() == repeated.size(), "Repeated generation must retain the same encounter size");
        const bool eligible = viewer::hasInteriorZombieEncounter(plan.name); selected += eligible;
        if (!eligible) require(placements.empty(), "Failed building roll must not place zombies");
        if (placements.empty()) continue;
        ++occupied;
        require(placements.size() <= viewer::maxInteriorZombies, "Interior encounter must respect its population bound");
        const bool group = placements.front().position == viewer::InteriorZombiePosition::CenterGroup;
        if (group) { ++groups; require(placements.size() >= 3, "A middle-room encounter must contain an entire group"); }
        for (std::size_t i = 0; i < placements.size(); ++i) {
            const auto& p = placements[i]; const auto feet = glm::vec3(p.transform[3]);
            require(p.transform == repeated[i].transform && p.position == repeated[i].position && p.room == repeated[i].room,
                    "Interior placement and facing must be deterministic");
            const auto& room = plan.rooms.at(p.room); upstairs += room.storey > 0;
            if (apartment) require(room.kind == viewer::HouseRoom::Kind::Apartment,
                                   "Block ambushers must use actual apartment walls, not open common-space boundaries");
            const float floor = room.storey == 0 ? plan.floor + .01f : plan.upperFloors.at(room.storey - 1);
            require(std::abs(feet.y - floor) < .02f, "Interior zombies must stand on their storey's generated floor");
            require(wallDistance(room, {feet.x, feet.z}) > .35f, "Zombie bodies must leave physical clearance to walls");
            if (!group) require(wallDistance(room, {feet.x, feet.z}) < .43f, "Ordinary ambushers must hug a room wall");
            else require(p.room == placements.front().room && p.position == viewer::InteriorZombiePosition::CenterGroup,
                         "The whole center group must stay together in one room");
            for (std::size_t j = 0; j < i; ++j)
                require(glm::distance(feet, glm::vec3(placements[j].transform[3])) >= .74f, "Group members and ambushers must not overlap");
            corners += p.position == viewer::InteriorZombiePosition::Corner;
            hidden += p.position == viewer::InteriorZombiePosition::BehindDoor;
            if (testedConcealment || p.position != viewer::InteriorZombiePosition::BehindDoor) continue;
            for (const auto& door : plan.doors) if (door.storey == room.storey) {
                const auto& panel = door.panel;
                const auto normal = panel.inward ? -panel.outward : panel.outward;
                const auto right = glm::cross(glm::vec3(0, 1, 0), panel.outward);
                const auto delta = feet - panel.hinge;
                if (std::abs(glm::dot(delta, right) + .7f) > .002f || std::abs(glm::dot(delta, normal) - .42f) > .002f) continue;
                const auto origin = panel.center + normal * .15f;
                auto target = feet; target.y = origin.y;
                const auto oldAngle = doors.angle(door.id);
                doors.setAngle(door.id, panel.inward ? -1.5707963f : 1.5707963f);
                const auto ray = target - origin;
                const auto hit = doors.raycast(origin, ray, glm::length(ray));
                doors.setAngle(door.id, oldAngle);
                require(hit && hit->door == door.id, "The opened door must actually conceal the ambusher from the room entrance");
                auto facing = panel.center - feet; facing.y = 0;
                require(glm::dot(glm::normalize(glm::vec3(p.transform[2])), glm::normalize(facing)) > .99f,
                        "A hidden ambusher must face the doorway");
                testedConcealment = true; break;
            }
        }
    }
    std::cout << "Interior rolls=" << selected << "/1000, occupied=" << occupied << ", center groups=" << groups << ", hidden=" << hidden << ", corners=" << corners << ", upstairs=" << upstairs << '\n';
    require(selected > 160 && selected < 240 && occupied > 150, "Building occupancy must be approximately 20 percent");
    require(groups > 3 && groups < occupied / 4 && hidden > 30 && corners > 10 && upstairs > 10,
            "Rare center groups, corner ambushers and concealed doorway ambushers must all occur across storeys");
    require(testedConcealment, "Concealment regression must exercise a valid opened-door ambush");
}
struct EncounterFixture {
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    std::unique_ptr<viewer::DoorLayer> doors;
    std::unique_ptr<viewer::CollisionWorld> collision;
    std::unique_ptr<viewer::FloorplanLayer> floors;
    explicit EncounterFixture(const std::string& name) : model(house_fixture::house()) {
        for (auto& draw : model.draws) if (draw.name.find("building_house") == 0) draw.name.replace(0, 14, name);
        doors = std::make_unique<viewer::DoorLayer>(viewer::prepareDoorPanels(model), physics);
        collision = std::make_unique<viewer::CollisionWorld>(model, std::vector<viewer::TrunkCollider>{}, doors.get());
        floors = std::make_unique<viewer::FloorplanLayer>(model, *doors, *collision);
        load();
    }
    void load() { doors->setAngle(1, .7f); floors->update({{0, 0, 8}}); }
    std::vector<viewer::InteriorZombiePlacement> positions() const {
        return viewer::placeInteriorZombies(floors->resident().begin()->second, *doors, *collision);
    }
};
std::unique_ptr<EncounterFixture> groupFixture() {
    for (int n = 0; n < 1000; ++n) {
        const auto name = "building_encounter_" + std::to_string(n);
        if (!viewer::hasInteriorZombieEncounter(name)) continue;
        auto fixture = std::make_unique<EncounterFixture>(name);
        const auto positions = fixture->positions();
        if (positions.size() >= 3 && positions.front().position == viewer::InteriorZombiePosition::CenterGroup) return fixture;
    }
    throw std::runtime_error("Could not find a deterministic group fixture");
}
viewer::ZombieLayer dormant(std::size_t count) {
    viewer::ZombieLayer layer; layer.assets.emplace_back(); layer.instances.resize(count);
    for (auto& actor : layer.instances) actor.removed = true;
    layer.rebuildSpatialIndex();
    return layer;
}
void spawnBehaviors() {
    auto& clock = viewer::SimulationClock::instance();
    bool hiding = false, ordinary = false, midnight = false;
    for (int n = 0; n < 1000 && !(hiding && ordinary && midnight); ++n) {
        const auto name = "building_encounter_" + std::to_string(n);
        if (!viewer::hasInteriorZombieEncounter(name)) continue;
        EncounterFixture fixture(name);
        const auto positions = fixture.positions();
        if (positions.empty()) continue;
        clock.reset();
        auto layer = dormant(positions.size());
        viewer::InteriorZombies population(0, positions.size());
        population.update(*fixture.floors, layer, *fixture.collision, {{0, 0, 8}}, {23, 1200});
        const auto& plan = fixture.floors->resident().begin()->second;
        std::vector<bool> upper;
        for (const auto& position : positions) upper.push_back(plan.rooms.at(position.room).storey > 0);
        for (std::size_t id = 0; id < positions.size(); ++id) {
            const auto* behavior = layer.behaviors.find(id);
            require(behavior && !layer.instances[id].removed,
                    "Real encounter activation must immediately create a behavior entry for every spawn");
            if (upper[id]) {
                require(behavior->state == viewer::ZombieState::midnight,
                        "Every upstairs spawn, including corner and doorway placements, must wait for midnight");
                midnight = true;
            } else if (positions[id].position == viewer::InteriorZombiePosition::CenterGroup) {
                require(behavior->state == viewer::ZombieState::idle,
                        "Ground-floor middle-room groups must start as ordinary idle zombies");
                ordinary = true;
            } else {
                require(behavior->state == viewer::ZombieState::hiding,
                        "Ground-floor wall and doorway ambushers must start hiding");
                hiding = true;
            }
        }
        fixture.floors->update({});
        population.update(*fixture.floors, layer, *fixture.collision, {}, {23, 1200});
        clock.advance(51);
        fixture.load();
        population.update(*fixture.floors, layer, *fixture.collision, {{0, 0, 8}}, {23, 1200});
        for (std::size_t id = 0; id < positions.size(); ++id) {
            const auto* behavior = layer.behaviors.find(id);
            require(behavior, "Streaming reentry must restore each living interior behavior entry");
            if (upper[id])
                require(behavior->state == viewer::ZombieState::idle,
                        "An upstairs spawn sleeping through midnight must wake without rescheduling for tomorrow");
            else if (positions[id].position != viewer::InteriorZombiePosition::CenterGroup)
                require(behavior->state == viewer::ZombieState::hiding,
                        "An undiscovered ground-floor ambusher must retain hiding across streaming");
        }
    }
    require(hiding && ordinary && midnight, "Encounter fixtures must exercise hiding, ordinary and upstairs initial behaviors");
    clock.reset();
}
void lifecycle() {
    require(viewer::InteriorZombies(0).reserveCount() == 10000, "Production allocation must be one shared 10,000-slot pool");
    auto fixture = groupFixture(); auto& f = *fixture;
    const auto positions = f.positions();
    auto layer = dormant(17);
    const auto outdoor = glm::translate(glm::mat4(1), glm::vec3(-20, 0, 20));
    layer.activateInteriorZombie(0, outdoor);
    viewer::InteriorZombies population(1, 16);
    population.update(*f.floors, layer, *f.collision, {{0, 0, 8}});
    for (std::size_t i = 0; i < positions.size(); ++i)
        require(!layer.instances[i + 1].removed && layer.instances[i + 1].restRoot == positions[i].transform,
                "Encounter activation must fill only its assigned dormant tail slots");
    require(layer.instances.size() == 17 && layer.instances[0].restRoot == outdoor,
            "Interior activation must preserve population size and outdoor placement");
    auto injuredFeet = glm::vec3(layer.instances[1].restRoot[3]);
    const auto shoot = [&](std::uint32_t id) {
        viewer::ZombieHitClaim claim; claim.zombie = id; claim.point = {0, .9f, 0};
        return layer.applyReportedShot(claim, glm::vec3(layer.instances[id].restRoot[3]) + glm::vec3(0, .9f, 2),
                                       {0, 0, -1}, 0, *f.collision, viewer::ShotCaliber::low);
    };
    require(shoot(1) && layer.damage().injury(1),
            "Lifecycle fixture must inflict a real nonlethal injury");
    for (int hit = 0; hit < 10; ++hit) require(shoot(2), "Lifecycle fixture must apply each lethal-sequence hit");
    require(layer.instances[2].ragdoll, "Lifecycle fixture must kill one encounter actor");
    layer.instances[3].restRoot = layer.instances[3].restTransform = layer.instances[3].transform =
        glm::translate(glm::mat4(1), {20, 0, 20}); // Explicit escape/teleport fixture.
    layer.rebuildSpatialIndex();
    f.floors->update({}); population.update(*f.floors, layer, *f.collision, {});
    require(layer.instances[1].removed && !layer.instances[3].removed && !layer.instances[0].removed,
            "Eviction must sleep indoor survivors and retain escaped/outdoor actors");
    std::vector<std::size_t> nearby; layer.queryLivingNear(injuredFeet, .1f, nearby);
    require(nearby.empty(), "Sleeping interior actors must leave living spatial queries");
    for (int visit = 0; visit < 3; ++visit) {
        f.load(); population.update(*f.floors, layer, *f.collision, {{0, 0, 8}});
        require(!layer.instances[1].removed && glm::vec3(layer.instances[1].restRoot[3]) == injuredFeet &&
                layer.damage().injury(1) && layer.damage().injury(1)->health == 9,
                "Regeneration must wake the same survivor with its position and health");
        require(layer.instances[2].ragdoll && !layer.instances[3].removed && glm::vec3(layer.instances[3].restRoot[3]).x == 20,
                "Regeneration must never resurrect kills or teleport escaped actors home");
        for (std::size_t id = positions.size() + 1; id < layer.instances.size(); ++id)
            require(layer.instances[id].removed, "Reentry must not consume fresh reserve IDs");
        f.floors->update({}); population.update(*f.floors, layer, *f.collision, {});
    }
    f.load();
    auto insufficient = dormant(positions.size() - 1);
    viewer::InteriorZombies smallPool(0, insufficient.instances.size());
    smallPool.update(*f.floors, insufficient, *f.collision, {{0, 0, 8}});
    require(std::all_of(insufficient.instances.begin(), insufficient.instances.end(), [](const auto& z) { return z.removed; }),
            "An exhausted pool must not leave a partial middle-room group");
    auto blocked = dormant(10);
    viewer::InteriorZombies blockedPool(0, 10);
    blockedPool.update(*f.floors, blocked, *f.collision, {glm::vec3(positions[0].transform[3])});
    require(std::all_of(blocked.instances.begin(), blocked.instances.end(), [](const auto& z) { return z.removed; }),
            "A player blocking one group member must suppress the entire group");
    f.floors->update({}); blockedPool.update(*f.floors, blocked, *f.collision, {});
    f.load(); blockedPool.update(*f.floors, blocked, *f.collision, {{0, 0, 8}});
    require(std::all_of(blocked.instances.begin(), blocked.instances.end(), [](const auto& z) { return z.removed; }),
            "A blocked first encounter must remain empty on later visits");
}
} // namespace
int main() {
    try {
        placement(glm::mat4(1));
        placement(glm::translate(glm::mat4(1), {-1108, 5, -250}) * glm::rotate(glm::mat4(1), glm::radians(31.f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1}));
        placement(glm::mat4(1), true);
        spawnBehaviors();
        lifecycle();
        std::cout << "Interior zombie placement tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
