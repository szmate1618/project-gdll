#include "interior_objects.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "physics_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <random>

namespace viewer {
namespace {
using Face = std::array<glm::vec3, 3>;
bool suffix(const std::string& name, const std::string& ending) {
    return name.size() >= ending.size() && name.compare(name.size() - ending.size(), ending.size(), ending) == 0;
}
std::string buildingName(const std::string& name, const std::string& ending) {
    return name.rfind("building_", 0) == 0 && suffix(name, ending) ? name.substr(0, name.size() - ending.size()) : "";
}
std::optional<float> roofHeight(const std::vector<Face>& roof, glm::vec2 p) {
    std::optional<float> height;
    for (const auto& face : roof) {
        const glm::dvec2 a(face[0].x, face[0].z), b(face[1].x, face[1].z), c(face[2].x, face[2].z);
        const auto v = b - a, w = c - a, d = glm::dvec2(p) - a;
        const auto cross = [](glm::dvec2 x, glm::dvec2 y) { return x.x * y.y - x.y * y.x; };
        const double determinant = cross(v, w);
        if (std::abs(determinant) < 1e-10) continue;
        const double u = cross(d, w) / determinant, t = cross(v, d) / determinant;
        if (u < -1e-7 || t < -1e-7 || u + t > 1 + 1e-7) continue;
        const float y = static_cast<float>(face[0].y + u * (face[1].y - face[0].y) + t * (face[2].y - face[0].y));
        if (!height || y < *height) height = y;
    }
    return height;
}
bool segmentIntersects(glm::vec2 a, glm::vec2 b, glm::vec2 minimum, glm::vec2 maximum) {
    float first = 0, last = 1;
    const auto direction = b - a;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(direction[axis]) < 1e-7f) {
            if (a[axis] < minimum[axis] || a[axis] > maximum[axis]) return false;
        } else {
            float low = (minimum[axis] - a[axis]) / direction[axis];
            float high = (maximum[axis] - a[axis]) / direction[axis];
            if (low > high) std::swap(low, high);
            first = std::max(first, low); last = std::min(last, high);
            if (first > last) return false;
        }
    }
    return true;
}
} // namespace

struct InteriorObjects::Impl {
    struct Building { std::vector<std::size_t> draws, doors; bool populated = false; };
    const Model* model;
    const DoorLayer* doors;
    std::map<std::string, Building> buildings;
    std::vector<std::string> doorBuildings;
    std::mt19937 random;
    float sample(float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); }
    glm::vec3 size() {
        const float kind = sample(0, 1);
        if (kind < .75f) return {sample(.25f, .65f), sample(.25f, .65f), sample(.25f, .65f)};
        if (kind < .93f) return {sample(.65f, 1.1f), sample(.65f, 1.2f), sample(.45f, .9f)};
        return {sample(.7f, 1.3f), sample(1.65f, 2.15f), sample(.45f, .75f)};
    }
};
InteriorObjects::InteriorObjects(const Model& model, const DoorLayer& doors, std::uint32_t seed)
    : impl_(std::make_unique<Impl>()) {
    impl_->model = &model; impl_->doors = &doors; impl_->random.seed(seed);
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const auto& draw = model.draws[i];
        if (draw.detailDistance > 0 && !draw.lowDetail) continue;
        auto key = buildingName(draw.name, "_roof");
        if (key.empty()) key = buildingName(draw.name, "_walls");
        if (!key.empty()) impl_->buildings[key].draws.push_back(i);
    }
    for (std::size_t i = 0; i < doors.count(); ++i) {
        const auto& draw = model.draws.at(doors.panel(i).draw);
        const auto key = buildingName(draw.name, "_doors");
        impl_->doorBuildings.push_back(key);
        const auto found = impl_->buildings.find(key);
        if (found != impl_->buildings.end()) found->second.doors.push_back(i);
    }
}
InteriorObjects::~InteriorObjects() = default;
std::size_t InteriorObjects::populate(std::size_t door, const CollisionWorld& collision, PhysicsScene& scene, BoxLayer& boxes) {
    const auto found = impl_->buildings.find(impl_->doorBuildings.at(door));
    if (found == impl_->buildings.end() || found->second.populated) return 0;
    auto& building = found->second;
    std::vector<Face> roof, walls;
    glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(-minimum);
    for (const auto drawIndex : building.draws) {
        const auto& draw = impl_->model->draws[drawIndex];
        const auto& primitive = impl_->model->primitives.at(draw.primitive);
        for (std::size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
            Face face;
            for (int j = 0; j < 3; ++j) {
                face[j] = glm::vec3(draw.transform * glm::vec4(primitive.vertices.at(primitive.indices[i + j]).position, 1));
                minimum = glm::min(minimum, face[j]); maximum = glm::max(maximum, face[j]);
            }
            if (suffix(draw.name, "_roof")) roof.push_back(face);
            else {
                const auto normal = glm::cross(face[1] - face[0], face[2] - face[0]);
                if (std::abs(normal.y) < .5f * glm::length(normal)) walls.push_back(face);
            }
        }
    }
    // Missing roof/terrain cannot safely establish an interior or support.
    if (roof.empty()) { building.populated = true; return 0; }
    const auto initialCount = boxes.count();
    const int target = std::uniform_int_distribution<int>(12, 36)(impl_->random);
    std::vector<std::pair<glm::vec2, glm::vec2>> occupied;
    for (int object = 0; object < target; ++object) {
        const auto size = impl_->size();
        const glm::vec2 half(size.x * .5f, size.z * .5f);
        for (int attempt = 0; attempt < 96; ++attempt) {
            const glm::vec2 position(impl_->sample(minimum.x, maximum.x), impl_->sample(minimum.z, maximum.z));
            const auto low = position - half - glm::vec2(.1f), high = position + half + glm::vec2(.1f);
            bool clear = true;
            for (const auto& occupiedBox : occupied)
                if (glm::all(glm::lessThan(low, occupiedBox.second)) && glm::all(glm::greaterThan(high, occupiedBox.first))) clear = false;
            for (const auto index : building.doors) {
                const auto& panel = impl_->doors->panel(index);
                const glm::vec2 hinge(panel.hinge.x, panel.hinge.z);
                if (glm::distance(hinge, glm::clamp(hinge, low, high)) < panel.halfExtents.x * 2 + .65f) clear = false;
            }
            // Corner-only inclusion misses a courtyard/concavity enclosed by
            // the rectangle. Reject any crossing of the original wall edges.
            for (const auto& face : walls) for (int edge = 0; edge < 3; ++edge) {
                const auto a = face[edge], b = face[(edge + 1) % 3];
                if (segmentIntersects({a.x, a.z}, {b.x, b.z}, low, high)) clear = false;
            }
            if (!clear) continue;
            float floor = -std::numeric_limits<float>::max(), lowestFloor = -floor, ceiling = lowestFloor;
            for (const auto p : std::array<glm::vec2, 5>{{position, low, {low.x, high.y}, high, {high.x, low.y}}}) {
                const auto top = roofHeight(roof, p);
                const auto support = collision.groundAt(p.x, p.y, minimum.y - 2, maximum.y, true);
                if (!top || !support || support->normal.y < .97f) { clear = false; break; }
                floor = std::max(floor, support->height);
                lowestFloor = std::min(lowestFloor, support->height);
                ceiling = std::min(ceiling, *top);
            }
            if (!clear || floor - lowestFloor > .12f || floor + size.y + .12f > ceiling) continue;
            const glm::vec3 center(position.x, floor + size.y * .5f + .02f, position.y);
            if (!boxes.query(center - size * .5f - glm::vec3(.05f), center + size * .5f + glm::vec3(.05f)).empty()) continue;
            scene.ensureNear(center - size * .5f - glm::vec3(2), center + size * .5f + glm::vec3(2));
            boxes.add(center, size);
            occupied.emplace_back(low, high);
            break;
        }
    }
    building.populated = true;
    return boxes.count() - initialCount;
}
} // namespace viewer
