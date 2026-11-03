#pragma once

#include "house_navigation_geometry.hpp"
#include <cstdint>
#include <optional>

namespace viewer {
struct HouseFloorplan;
class DoorLayer;
class CollisionWorld;

struct HouseNavigationWaypoint { glm::vec3 point; std::optional<std::size_t> door; };
struct HouseNavigationPath {
    std::optional<std::size_t> room;
    std::vector<HouseNavigationWaypoint> points;
};

// One static graph per resident floorplan. Edges within concave rooms include
// clearance waypoints; stair edges retain their actual bottom/top centerlines.
class HouseNavigationGraph {
public:
    enum class Kind { RoomCenter, Door, RampBottom, RampTop };
    struct Node { Kind kind; glm::vec3 point; std::size_t storey; std::optional<std::size_t> door; };
    struct Edge { std::size_t to; float length; std::vector<glm::vec3> points; };
    HouseNavigationGraph(const HouseFloorplan& plan, const DoorLayer& doors,
                         const CollisionWorld& world, std::uint64_t generation);
    std::uint64_t generation() const { return generation_; }
    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<std::vector<Edge>>& edges() const { return edges_; }
    std::optional<std::size_t> roomAt(glm::vec3 point) const;
    HouseNavigationPath route(glm::vec3 from, glm::vec3 sound) const;
private:
    struct Member { std::size_t node; glm::vec3 anchor; };
    struct Room { HouseNavigationRoom geometry; std::size_t center; std::vector<Member> members; };
    struct Ramp { std::size_t bottom, top; std::vector<glm::vec2> polygon; };
    std::size_t addNode(Node node);
    void connect(std::size_t from, std::size_t to, std::vector<glm::vec3> points);
    std::vector<Node> nodes_;
    std::vector<std::vector<Edge>> edges_;
    std::vector<Room> rooms_;
    std::vector<Ramp> ramps_;
    std::uint64_t generation_;
};

} // namespace viewer
