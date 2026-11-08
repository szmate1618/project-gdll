#include "house_navigation.hpp"
#include "house_floorplan.hpp"
#include "house_polygon.hpp"
#include "door_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>

namespace viewer {
namespace {
glm::vec2 xz(glm::vec3 p) { return {p.x, p.z}; }
float floorAt(const HouseFloorplan& plan, std::size_t storey) {
    return plan.floorHeight(storey);
}
std::size_t storeyAt(const HouseFloorplan& plan, float height) {
    std::size_t selected = 0; float best = std::numeric_limits<float>::max();
    for (const auto& room : plan.rooms) {
        const auto level = room.storey;
        const float distance = std::abs(floorAt(plan, level) - height);
        if (distance < best) { best = distance; selected = level; }
    }
    return selected;
}
bool common(HouseRoom::Kind kind) {
    return kind == HouseRoom::Kind::Corridor || kind == HouseRoom::Kind::Lobby || kind == HouseRoom::Kind::StairArea;
}
void append(std::vector<glm::vec3>& points, glm::vec3 point) {
    if (points.empty() || glm::distance(points.back(), point) > .0001f) points.push_back(point);
}
}

std::size_t HouseNavigationGraph::addNode(Node node) {
    nodes_.push_back(node); edges_.emplace_back(); return nodes_.size() - 1;
}
void HouseNavigationGraph::connect(std::size_t from, std::size_t to, std::vector<glm::vec3> points) {
    if (from == to || points.empty()) return;
    float length = 0;
    for (std::size_t i = 1; i < points.size(); ++i) length += glm::distance(points[i - 1], points[i]);
    for (const auto& edge : edges_[from]) if (edge.to == to && edge.length <= length) return;
    const auto install = [&](std::size_t a, std::size_t b, const auto& path) {
        auto& list = edges_[a];
        const auto found = std::find_if(list.begin(), list.end(), [&](const auto& edge) { return edge.to == b; });
        Edge edge{b, length, path};
        if (found == list.end()) list.push_back(std::move(edge)); else *found = std::move(edge);
    };
    install(from, to, points); std::reverse(points.begin(), points.end()); install(to, from, points);
}

HouseNavigationGraph::HouseNavigationGraph(const HouseFloorplan& plan, const DoorLayer& doors,
        const CollisionWorld& world, std::uint64_t generation) : generation_(generation) {
    rooms_.resize(plan.rooms.size());
    for (std::size_t room = 0; room < rooms_.size(); ++room) {
        auto& geometry = rooms_[room].geometry;
        geometry.polygon = plan.rooms[room].polygon; geometry.floor = floorAt(plan, plan.rooms[room].storey);
        for (const auto& stair : plan.stairs) {
            if (std::abs(stair.bottom.y - geometry.floor) < .08f) geometry.blocked.push_back(stair.ramp);
            if (std::abs(stair.top.y - geometry.floor) < .08f) geometry.blocked.push_back(stair.opening);
        }
    }
    const auto attach = [&](std::size_t node, glm::vec3 normal, std::optional<glm::vec3> landing) {
        for (std::size_t room = 0; room < rooms_.size(); ++room) {
            if (plan.rooms[room].storey != nodes_[node].storey) continue;
            std::optional<glm::vec3> anchor;
            if (landing) {
                if (house_polygon::contains(rooms_[room].geometry.polygon, xz(*landing))) anchor = *landing;
            } else for (const float sign : {-1.f, 1.f}) {
                const auto point = nodes_[node].point + normal * (.45f * sign);
                if (house_polygon::contains(rooms_[room].geometry.polygon, xz(point))) anchor = point;
            }
            if (!anchor) continue;
            anchor->y = rooms_[room].geometry.floor + .004f;
            rooms_[room].members.push_back({node, *anchor});
        }
    };
    std::map<std::size_t, std::size_t> doorNodes;
    const auto addDoor = [&](std::size_t id, const DoorPanel& panel, std::size_t storey) {
        if (doorNodes.count(id)) return;
        auto point = panel.center; point.y = floorAt(plan, storey) + .004f;
        const auto node = addNode({Kind::Door, point, storey, id});
        doorNodes.emplace(id, node); attach(node, panel.outward, std::nullopt);
    };
    for (const auto& door : plan.doors) addDoor(door.id, door.panel, door.storey);
    for (const auto& room : plan.rooms)
        for (const auto id : room.entrances) if (doors.present(id)) addDoor(id, doors.panel(id), room.storey);
    for (const auto& stair : plan.stairs) {
        const auto bottom = addNode({Kind::RampBottom, stair.bottom, storeyAt(plan, stair.bottom.y), std::nullopt});
        const auto top = addNode({Kind::RampTop, stair.top, storeyAt(plan, stair.top.y), std::nullopt});
        const auto axis = glm::normalize(glm::vec3(stair.top.x - stair.bottom.x, 0, stair.top.z - stair.bottom.z));
        attach(bottom, {}, stair.bottom - axis * .45f); attach(top, {}, stair.top + axis * .45f);
        connect(bottom, top, {stair.bottom, stair.top}); ramps_.push_back({bottom, top, stair.ramp});
    }
    // Apartment common-space polygons meet without physical doors. Represent
    // each walkable shared opening as a door node with no panel ID.
    if (plan.kind == HouseFloorplan::Kind::PrefabApartmentBlock) {
        for (std::size_t first = 0; first < rooms_.size(); ++first) for (std::size_t second = first + 1; second < rooms_.size(); ++second) {
            if (!common(plan.rooms[first].kind) || !common(plan.rooms[second].kind) ||
                plan.rooms[first].storey != plan.rooms[second].storey) continue;
            const auto& a = rooms_[first].geometry.polygon; const auto& b = rooms_[second].geometry.polygon;
            for (std::size_t i = 0; i < a.size(); ++i) for (std::size_t j = 0; j < b.size(); ++j) {
                const auto start = a[i], edge = a[(i + 1) % a.size()] - start;
                const float length = glm::length(edge); if (length < .75f) continue;
                const auto axis = edge / length; const glm::vec2 normal(-axis.y, axis.x);
                const auto c = b[j] - start, d = b[(j + 1) % b.size()] - start;
                if (std::abs(glm::dot(c, normal)) > .005f || std::abs(glm::dot(d, normal)) > .005f) continue;
                const float low = std::max(0.f, std::min(glm::dot(c, axis), glm::dot(d, axis)));
                const float high = std::min(length, std::max(glm::dot(c, axis), glm::dot(d, axis)));
                if (high - low < .75f) continue;
                for (const float fraction : {.5f, .4f, .6f, .3f, .7f, .2f, .8f, .1f, .9f}) {
                    const auto point = start + axis * glm::mix(low, high, fraction);
                    const auto left = point - normal * .45f, right = point + normal * .45f;
                    if (!((rooms_[first].geometry.walkable(left) && rooms_[second].geometry.walkable(right)) ||
                          (rooms_[first].geometry.walkable(right) && rooms_[second].geometry.walkable(left)))) continue;
                    const auto node = addNode({Kind::Door, {point.x, rooms_[first].geometry.floor + .004f, point.y},
                        plan.rooms[first].storey, std::nullopt});
                    attach(node, {normal.x, 0, normal.y}, std::nullopt); break;
                }
            }
        }
    }
    for (std::size_t room = 0; room < rooms_.size(); ++room) {
        auto& data = rooms_[room];
        std::vector<glm::vec2> anchors;
        for (const auto& member : data.members) anchors.push_back(xz(member.anchor));
        auto center = data.geometry.center(anchors, world);
        if (!center) {
            glm::vec2 middle(0); for (const auto p : data.geometry.polygon) middle += p / float(data.geometry.polygon.size());
            center = glm::vec3(middle.x, data.geometry.floor + .004f, middle.y);
        }
        data.center = addNode({Kind::RoomCenter, *center, plan.rooms[room].storey, std::nullopt});
        data.members.push_back({data.center, *center});
        for (std::size_t a = 0; a < data.members.size(); ++a) for (std::size_t b = a + 1; b < data.members.size(); ++b) {
            const auto& first = data.members[a]; const auto& second = data.members[b];
            const auto kindA = nodes_[first.node].kind, kindB = nodes_[second.node].kind;
            if (kindA == Kind::Door && kindB == Kind::Door) continue;
            auto path = data.geometry.path(xz(first.anchor), xz(second.anchor));
            if (path.empty()) continue;
            std::vector<glm::vec3> points{nodes_[first.node].point};
            for (const auto point : path) append(points, point);
            append(points, nodes_[second.node].point); connect(first.node, second.node, std::move(points));
        }
    }
}

std::optional<std::size_t> HouseNavigationGraph::roomAt(glm::vec3 point) const {
    std::optional<std::size_t> selected;
    float selectedFloor = -std::numeric_limits<float>::infinity(), nearest = std::numeric_limits<float>::max();
    for (std::size_t room = 0; room < rooms_.size(); ++room) {
        const auto& geometry = rooms_[room].geometry;
        if (!house_polygon::contains(geometry.polygon, xz(point))) continue;
        if (geometry.floor <= point.y + .02f) {
            if (geometry.floor > selectedFloor) { selected = room; selectedFloor = geometry.floor; }
        } else if (!std::isfinite(selectedFloor) && geometry.floor - point.y < nearest) {
            selected = room; nearest = geometry.floor - point.y;
        }
    }
    return selected;
}

HouseNavigationPath HouseNavigationGraph::route(glm::vec3 from, glm::vec3 sound) const {
    HouseNavigationPath result; result.room = roomAt(sound);
    if (!result.room) return result;
    const auto none = nodes_.size(), goal = rooms_[*result.room].center;
    std::vector<float> distance(nodes_.size(), std::numeric_limits<float>::infinity());
    std::vector<std::size_t> previous(nodes_.size(), none), previousEdge(nodes_.size());
    std::map<std::size_t, std::vector<glm::vec3>> starts;
    for (const auto& ramp : ramps_) {
        if (!house_polygon::contains(ramp.polygon, xz(from))) continue;
        const auto bottom = nodes_[ramp.bottom].point, top = nodes_[ramp.top].point;
        const auto axis = xz(top - bottom);
        const float t = glm::clamp(glm::dot(xz(from - bottom), axis) / glm::dot(axis, axis), 0.f, 1.f);
        const auto projection = glm::mix(bottom, top, t);
        if (std::abs(projection.y - from.y) > .4f) continue;
        for (const auto node : {ramp.bottom, ramp.top}) starts[node] = {projection, nodes_[node].point};
    }
    if (starts.empty()) if (const auto room = roomAt(from)) {
        const auto center = rooms_[*room].center;
        auto path = rooms_[*room].geometry.path(xz(from), xz(nodes_[center].point));
        // A body already in an aperture has no wall clearance at the room's
        // polygon boundary. First use that doorway's known inward anchor.
        if (path.empty()) for (const auto& member : rooms_[*room].members) {
            if (nodes_[member.node].kind != Kind::Door || glm::distance(xz(from), xz(nodes_[member.node].point)) > .2f) continue;
            auto inward = rooms_[*room].geometry.path(xz(member.anchor), xz(nodes_[center].point));
            if (!inward.empty()) { path = {from}; path.insert(path.end(), inward.begin(), inward.end()); break; }
        }
        if (!path.empty()) starts.emplace(center, std::move(path));
    }
    using Entry = std::pair<float, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pending;
    for (const auto& [node, path] : starts) {
        float cost = glm::distance(from, path.front());
        for (std::size_t i = 1; i < path.size(); ++i) cost += glm::distance(path[i - 1], path[i]);
        distance[node] = cost; pending.emplace(cost, node);
    }
    while (!pending.empty()) {
        const auto [cost, node] = pending.top(); pending.pop();
        if (cost != distance[node]) continue;
        if (node == goal) break;
        for (std::size_t i = 0; i < edges_[node].size(); ++i) {
            const auto& edge = edges_[node][i];
            if (cost + edge.length < distance[edge.to]) {
                distance[edge.to] = cost + edge.length; previous[edge.to] = node;
                previousEdge[edge.to] = i; pending.emplace(distance[edge.to], edge.to);
            }
        }
    }
    if (!std::isfinite(distance[goal])) return result;
    std::vector<std::size_t> sequence;
    for (auto node = goal; node != none; node = previous[node]) sequence.push_back(node);
    std::reverse(sequence.begin(), sequence.end());
    const auto add = [&](glm::vec3 point, std::optional<std::size_t> door = std::nullopt) {
        if (!result.points.empty() && glm::distance(result.points.back().point, point) < .0001f) {
            if (door) result.points.back().door = door;
        } else result.points.push_back({point, door});
    };
    for (const auto point : starts.at(sequence.front())) add(point);
    for (std::size_t i = 1; i < sequence.size(); ++i) {
        const auto& edge = edges_[sequence[i - 1]][previousEdge[sequence[i]]];
        for (std::size_t p = 0; p < edge.points.size(); ++p)
            add(edge.points[p], p + 1 == edge.points.size() ? nodes_[sequence[i]].door : std::nullopt);
    }
    return result;
}

} // namespace viewer
