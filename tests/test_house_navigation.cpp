#include "house_fixture.hpp"
#include "apartment_fixture.hpp"
#include "floorplan_layer.hpp"
#include "house_navigation.hpp"
#include "building_footprints.hpp"
#include "player_house.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void membership(const glm::mat4& transform) {
    auto model = house_fixture::house(transform);
    viewer::BuildingFootprints buildings(model);
    const auto point = [&](glm::vec3 p) { return glm::vec3(transform * glm::vec4(p, 1)); };
    require(buildings.find(point({0, 0, 0})) == std::optional<std::string>("building_house"),
            "Building membership must work without a generated interior or an open door");
    require(buildings.contains("building_house", point({0, -10.079f, 0})) &&
            buildings.contains("building_house", point({0, 16.499f, 0})),
            "House height membership must extend ten meters below the lowest point and above the highest point");
    require(!buildings.find(point({0, -10.09f, 0})) && !buildings.find(point({0, 16.51f, 0})) &&
            !buildings.find(point({6, 0, 0})), "Footprint and extended height boundaries must reject outside positions");
    viewer::PlayerHouseState player;
    player.update(point({0, 0, 0}), 0, buildings);
    require(!player.outside() && player.house() == std::optional<std::string>("building_house"), "First house sample must be immediate and queryable");
    player.update(point({20, 0, 0}), 2.999, buildings);
    require(!player.outside(), "Membership must remain cached between three-second checks");
    player.update(point({20, 0, 0}), 3, buildings);
    require(player.outside() && !player.house(), "An outside sample must explicitly clear the stored building ID");
    player.update(point({0, 0, 0}), 6, buildings);
    require(!player.outside(), "Later samples must detect reentry");
}
void concavityAndCourtyard() {
    const auto model = house_fixture::outlined({{-5,-6},{-5,6},{2,6},{2,3},{5,3},{5,-6}});
    viewer::BuildingFootprints buildings(model);
    require(buildings.find({-3, 0, 4}) && !buildings.find({4, 0, 4}),
            "Membership must follow a concave footprint instead of its bounding rectangle");
    auto courtyard = house_fixture::house();
    const auto inner = house_fixture::house(glm::scale(glm::mat4(1), { .4f, 1.f, .4f }));
    for (const auto& draw : inner.draws) if (draw.name == "building_house_walls") {
        auto copy = draw; copy.primitive = courtyard.primitives.size();
        courtyard.primitives.push_back(inner.primitives[draw.primitive]); courtyard.draws.push_back(copy);
    }
    viewer::BuildingFootprints holes(courtyard);
    require(!holes.find({0, 0, 0}) && holes.find({4, 0, 0}), "Courtyard wall rings must remain outside the building footprint");
    auto detailed = house_fixture::house();
    viewer::Primitive detail;
    for (const auto p : {glm::vec3(0, 11, 0), glm::vec3(.1f, 11, 0), glm::vec3(0, -2, .1f)}) {
        viewer::Vertex vertex; vertex.position = p; detail.vertices.push_back(vertex);
    }
    detail.indices = {0, 1, 2};
    detailed.draws.push_back({detailed.primitives.size(), glm::mat4(1), "building_house_details"});
    detailed.primitives.push_back(std::move(detail));
    viewer::BuildingFootprints fullHeight(detailed);
    require(fullHeight.find({0, -11.99f, 0}) && fullHeight.find({0, 20.99f, 0}) && !fullHeight.find({0, 21.01f, 0}),
            "Membership height must include building details, not only the plain roof and foundation");
}

void graph(viewer::Model model, glm::vec3 observer) {
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, world);
    for (std::size_t id = 0; id < doors.count(); ++id) if (doors.present(id)) doors.setAngle(id, .7f);
    floors.update({observer});
    require(floors.residentCount() == 1, "Navigation fixture must build its interior");
    const auto& plan = floors.resident().begin()->second;
    const auto* navigation = floors.navigationGraph(plan.name);
    require(navigation, "Every built interior must publish a navigation graph");
    std::vector<glm::vec3> centers;
    std::size_t bottoms = 0, tops = 0;
    for (const auto& node : navigation->nodes()) {
        if (node.kind == viewer::HouseNavigationGraph::Kind::RoomCenter) {
            centers.push_back(node.point);
            const auto support = world.walkingGroundAt(node.point.x, node.point.z, node.point.y - .06f, node.point.y + .06f);
            require(support && std::abs(support->height - node.point.y) < .02f,
                    "Room centers must stand on actual floors rather than stair holes");
        }
        bottoms += node.kind == viewer::HouseNavigationGraph::Kind::RampBottom;
        tops += node.kind == viewer::HouseNavigationGraph::Kind::RampTop;
    }
    require(centers.size() == plan.rooms.size() && bottoms == plan.stairs.size() && tops == plan.stairs.size(),
            "Graphs must contain every room center and both endpoints of every ramp");
    for (const auto& node : navigation->nodes()) if (node.kind == viewer::HouseNavigationGraph::Kind::RoomCenter && node.storey == 1) {
        const auto below = navigation->roomAt(node.point - glm::vec3(0, .1f, 0));
        require(below && plan.rooms[*below].storey == 0,
                "A sound below the next floor must remain in the lower room rather than jumping upstairs");
    }
    for (const auto& node : navigation->nodes()) if (node.kind == viewer::HouseNavigationGraph::Kind::Door && node.door)
        require(!navigation->route(node.point, centers.front()).points.empty(),
                "An investigation starting inside a doorway must be able to join its room graph");
    for (std::size_t target = 0; target < centers.size(); ++target) {
        const auto path = navigation->route(centers.front(), centers[target] + glm::vec3(0, 1.65f, 0));
        if (path.points.empty()) std::cerr << "Unreachable room " << target << "/" << centers.size() << " at "
            << centers[target].x << ", " << centers[target].y << ", " << centers[target].z << '\n';
        require(!path.points.empty(), "All generated rooms must be connected through doors and stairs");
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            const auto a = path.points[i - 1].point, b = path.points[i].point;
            const int samples = std::max(1, int(std::ceil(glm::distance(a, b) / .2f)));
            for (int sample = 0; sample <= samples; ++sample) {
                const auto p = glm::mix(a, b, float(sample) / samples);
                const auto support = world.walkingGroundAt(p.x, p.z, p.y - .08f, p.y + .08f);
                require(support && std::abs(support->height - p.y) < .08f,
                        "Graph edges must retain floor or ramp support, including across stair landings");
            }
        }
    }
    const auto id = plan.name; const auto generation = navigation->generation();
    floors.update({});
    require(!floors.navigationGraph(id) && floors.footprints().find(centers.front()),
            "Eviction must remove the graph while retaining map footprint queries");
    floors.update({observer});
    require(floors.navigationGraph(id) && floors.navigationGraph(id)->generation() != generation,
            "A regenerated interior must publish a new graph generation");
}
}
int main() {
    try {
        membership(glm::mat4(1));
        const auto transform = glm::translate(glm::mat4(1), {-1108, 5, -250}) *
            glm::rotate(glm::mat4(1), .54f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1});
        membership(transform); concavityAndCourtyard();
        graph(house_fixture::house(), {0, 1.7f, 8});
        graph(house_fixture::house(transform), glm::vec3(transform * glm::vec4(0, 1.7f, 8, 1)));
        graph(house_fixture::outlined({{-5,-6},{-5,6},{2,6},{2,3},{5,3},{5,-6}}), {0, 1.7f, 8});
        graph(apartment_fixture::block(24,20,11), {0, 1.7f, 12});
        std::cout << "House membership and navigation graph tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
