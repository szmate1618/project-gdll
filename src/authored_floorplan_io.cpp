#include "authored_floorplan.hpp"
#include <tiny_gltf.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
using Value = tinygltf::Value;
[[noreturn]] void invalid() { throw std::runtime_error("Invalid authored_floorplan mesh metadata"); }
const Value& field(const Value& object, const char* key) {
    if (!object.IsObject() || !object.Has(key))
        throw std::runtime_error(std::string("Missing authored_floorplan field: ") + key);
    return object.Get(key);
}
float number(const Value& value) {
    if (!value.IsNumber()) invalid();
    const auto n = static_cast<float>(value.GetNumberAsDouble());
    if (!std::isfinite(n) || std::abs(n) > 1e7f) invalid();
    return n;
}
std::size_t index(const Value& value, std::size_t count) {
    const auto n = number(value);
    if (n < 0 || n != std::floor(n) || n >= static_cast<float>(count)) invalid();
    return static_cast<std::size_t>(n);
}
const Value::Array& array(const Value& value, std::size_t limit = 10000) {
    if (!value.IsArray() || value.ArrayLen() > limit) invalid();
    return value.Get<Value::Array>();
}
std::string string(const Value& value) {
    if (!value.IsString()) invalid();
    return value.Get<std::string>();
}
glm::vec3 vector(const Value& value) {
    if (array(value).size() != 3) invalid();
    return {number(value.Get(0)), number(value.Get(1)), number(value.Get(2))};
}
std::vector<glm::vec2> polygon(const Value& value) {
    std::vector<glm::vec2> points;
    for (const auto& p : array(value, 512)) {
        if (array(p).size() != 2) invalid();
        points.emplace_back(number(p.Get(0)), number(p.Get(1)));
    }
    // GEOS can retain duplicate/collinear vertices at Boolean seams that
    // collapse on float conversion. Remove submillimetre spurs before using
    // these polygons for triangulation and navigation.
    bool changed = true;
    while (changed && points.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto a = points[(i + points.size() - 1) % points.size()];
            const auto b = points[i], c = points[(i + 1) % points.size()];
            const auto ab = b - a, bc = c - b;
            const float cross = ab.x * bc.y - ab.y * bc.x;
            if (glm::distance(a, b) < .0001f ||
                std::abs(cross) < .0001f * std::max(glm::length(ab), glm::length(bc))) {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; break;
            }
        }
    }
    if (points.size() < 3) invalid();
    return points;
}
house_layout::Rectangle bounds(const std::vector<glm::vec2>& points) {
    house_layout::Rectangle r{glm::vec2(std::numeric_limits<float>::max()), glm::vec2(std::numeric_limits<float>::lowest())};
    for (const auto p : points) { r.low = glm::min(r.low, p); r.high = glm::max(r.high, p); }
    return r;
}
house_layout::Rectangle rectangle(const Value& value) {
    if (array(value).size() != 4) invalid();
    house_layout::Rectangle r{{number(value.Get(0)), number(value.Get(1))}, {number(value.Get(2)), number(value.Get(3))}};
    if (r.low.x >= r.high.x || r.low.y >= r.high.y) invalid();
    return r;
}
HouseRoom::Kind kind(const Value& value) {
    const auto s = string(value);
    if (s == "living") return HouseRoom::Kind::Living;
    if (s == "small_rooms") return HouseRoom::Kind::SmallRoom;
    if (s == "corridor") return HouseRoom::Kind::Corridor;
    if (s == "lobby") return HouseRoom::Kind::Lobby;
    if (s == "stair_area") return HouseRoom::Kind::StairArea;
    invalid();
}
}
std::shared_ptr<const AuthoredFloorplan> readAuthoredFloorplan(const Value& value, const glm::mat4& transform) {
    const auto version = number(field(value, "version"));
    if (version != 1 && version != 2) invalid();
    auto result = std::make_shared<AuthoredFloorplan>();
    result->version = static_cast<int>(version);
    const auto local = vector(field(value, "origin"));
    const auto origin = glm::vec3(transform * glm::vec4(local, 1));
    const auto u = glm::vec3(transform * glm::vec4(vector(field(value, "u")), 0));
    const auto v = glm::vec3(transform * glm::vec4(vector(field(value, "v")), 0));
    // Metre-preserving upright frames include rotations and reflections. Reject
    // tilted/scaled authoring transforms rather than deforming physical doors.
    if (std::abs(u.y) > .001f || std::abs(v.y) > .001f || std::abs(glm::length(u) - 1) > .001f ||
        std::abs(glm::length(v) - 1) > .001f || std::abs(glm::dot(u, v)) > .001f) invalid();
    const auto vertical = glm::vec3(transform * glm::vec4(0, 0, 1, 0));
    if (glm::distance(vertical, glm::vec3(0, 1, 0)) > .001f) invalid();
    result->origin = {origin.x, origin.z}; result->u = {u.x, u.z}; result->v = {v.x, v.z};
    result->floor = origin.y; result->ceiling = origin.y + number(field(value, "ceiling"));
    float previous = result->floor;
    for (const auto& height : array(field(value, "upper_floors"), 16)) {
        const auto y = result->floor + number(height);
        if (y - previous < 2.4f || y > result->ceiling - 2.4f) invalid();
        result->upperFloors.push_back(y); previous = y;
    }
    if (result->ceiling - previous < 2.4f) invalid();
    result->footprint = polygon(field(value, "footprint"));
    float doorHeight = 2.7f;
    if (version == 2) {
        doorHeight = number(field(value, "door_height"));
        if (doorHeight < 1.9f || doorHeight > 3.5f || string(field(value, "finish")) != "institutional") invalid();
        result->institutional = true;
        for (const auto& court : array(field(value, "courtyards"), 64)) result->courtyards.push_back(polygon(court));
        for (const auto& region : array(field(value, "floor_regions"), 2048))
            result->layout.floorRegions.push_back(polygon(region));
        if (result->layout.floorRegions.empty()) invalid();
    }
    for (const auto& room : array(field(value, "rooms"), 2048)) {
        auto p = polygon(field(room, "polygon"));
        result->layout.rooms.push_back({kind(field(room, "kind")), bounds(p), {},
            index(field(room, "storey"), result->upperFloors.size() + 1), std::move(p)});
        result->labels.push_back(string(field(room, "label")));
    }
    for (const auto& wall : array(field(value, "partitions"), 8192)) {
        const auto axis = static_cast<int>(index(field(wall, "axis"), 2));
        house_layout::Partition p{axis, number(field(wall, "position")), number(field(wall, "low")),
            number(field(wall, "high")), {}, index(field(wall, "storey"), result->upperFloors.size() + 1)};
        if (p.low >= p.high) invalid();
        float end = p.low;
        // tinygltf omits empty arrays in extras. A solid wall therefore has
        // no openings field after parsing, even though the GLB records [].
        const Value::Array noOpenings;
        const auto& openings = wall.Has("openings") ? array(field(wall, "openings"), 64) : noOpenings;
        for (const auto& opening : openings) {
            if (array(opening).size() != 2) invalid();
            const float low = number(opening.Get(0)), high = number(opening.Get(1));
            if (low < end || high <= low || high > p.high) invalid();
            glm::vec2 outward(0); outward[axis] = 1;
            p.openings.push_back({low, high, outward, std::nullopt, true, doorHeight}); end = high;
        }
        result->layout.partitions.push_back(std::move(p));
    }
    for (const auto& stair : array(field(value, "stairs"), 64)) {
        const auto& ascending = field(stair, "ascending");
        if (!ascending.IsBool()) invalid();
        house_layout::Stair s{index(field(stair, "room"), result->layout.rooms.size()),
            rectangle(field(stair, "ramp")), rectangle(field(stair, "opening")),
            static_cast<int>(index(field(stair, "axis"), 2)), ascending.Get<bool>(),
            result->floor + number(field(stair, "bottom")), result->floor + number(field(stair, "top"))};
        if (s.top <= s.bottom || s.top >= result->ceiling) invalid();
        result->layout.stairs.push_back(s);
    }
    result->layout.wholeGroundFloor = true;
    return result;
}
} // namespace viewer
