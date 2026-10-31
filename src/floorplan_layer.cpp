#include "floorplan_layer.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "model_merge.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <limits>

namespace viewer {
namespace {
constexpr float cellSize = 32;
std::string key(const std::string& name, const std::string& suffix) {
    if (name.rfind("building_", 0) != 0 || name.size() <= suffix.size() ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return {};
    return name.substr(0, name.size() - suffix.size());
}
float distanceSquared(const HouseFloorplan& plan, glm::vec3 position) {
    const glm::vec2 p(position.x, position.z);
    float nearest = std::numeric_limits<float>::max();
    bool positive = false, negative = false;
    for (std::size_t i = 0; i < plan.footprint.size(); ++i) {
        const auto a = plan.footprint[i], b = plan.footprint[(i + 1) % plan.footprint.size()];
        const auto edge = b - a, delta = p - a;
        const float cross = edge.x * delta.y - edge.y * delta.x;
        positive |= cross > .001f; negative |= cross < -.001f;
        const auto d = delta - edge * glm::clamp(glm::dot(delta, edge) / glm::dot(edge, edge), 0.f, 1.f);
        nearest = std::min(nearest, glm::dot(d, d));
    }
    const float dy = std::max({plan.floor - position.y, position.y - plan.ceiling, 0.f});
    return (positive && negative ? nearest : 0.f) + dy * dy;
}
int cell(float p) { return static_cast<int>(std::floor(p / cellSize)); }
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && glm::all(glm::lessThan(glm::abs(p), glm::vec3(1e8f))); }
} // namespace
struct FloorplanLayer::Impl {
    const Model* source;
    DoorLayer* doors;
    CollisionWorld* collision;
    std::vector<DoorPanel> entrances;
    std::vector<std::string> doorBuildings;
    std::map<std::string, HouseSource> sources;
    std::map<std::pair<int, int>, std::vector<std::size_t>> cells;
    std::set<std::string> rejected;
    std::map<std::string, HouseFloorplan> resident;
    Model renderModel;
    std::map<std::size_t, std::size_t> doorDraws;
    std::uint64_t revision = 1;

    void publish() {
        Model collisionModel;
        renderModel = {}; doorDraws.clear();
        for (const auto& entry : resident) {
            const auto& plan = entry.second;
            appendStaticModel(collisionModel, Model(plan.structure));
            const auto materialOffset = static_cast<int>(renderModel.materials.size());
            appendStaticModel(renderModel, Model(plan.structure));
            for (const auto& door : plan.doors) {
                const auto draw = renderModel.draws.size();
                auto mesh = door.geometry; mesh.material += materialOffset;
                renderModel.draws.push_back({renderModel.primitives.size(), glm::mat4(1), "house_interior_door"});
                renderModel.primitives.push_back(std::move(mesh));
                doorDraws.emplace(draw, door.id);
            }
        }
        collision->setRuntimeGeometry(collisionModel);
        ++revision;
    }
};
FloorplanLayer::FloorplanLayer(const Model& model, DoorLayer& doors, CollisionWorld& collision)
    : impl_(std::make_unique<Impl>()) {
    auto& data = *impl_;
    data.source = &model; data.doors = &doors; data.collision = &collision;
    const auto exteriorCount = doors.count();
    data.entrances.reserve(exteriorCount);
    data.doorBuildings.resize(exteriorCount);
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const auto& draw = model.draws[i];
        auto name = key(draw.name, "_windows");
        if (!name.empty() && !draw.lowDetail) data.sources[name].windows.push_back(i);
        if (draw.detailDistance > 0 && !draw.lowDetail) continue;
        name = key(draw.name, "_walls");
        if (!name.empty()) data.sources[name].walls.push_back(i);
        name = key(draw.name, "_roof");
        if (!name.empty()) data.sources[name].roofs.push_back(i);
    }
    for (std::size_t i = 0; i < exteriorCount; ++i) {
        const auto& panel = doors.panel(i);
        data.entrances.push_back(panel);
        const auto name = key(model.draws.at(panel.draw).name, "_doors");
        const auto found = data.sources.find(name);
        if (found == data.sources.end()) continue;
        found->second.name = name; found->second.doors.push_back(i);
        data.doorBuildings[i] = name;
        data.cells[{cell(panel.center.x), cell(panel.center.z)}].push_back(i);
    }
    // Each entrance can add one entry door and two side-room doors. At most
    // two further partitions connect the three living rooms. Reserve by sorted
    // building identity, so skipped entries and activation order cannot shift IDs.
    for (auto& entry : data.sources) {
        auto& source = entry.second;
        if (source.doors.empty()) continue;
        source.firstDoor = doors.reserveRuntimeDoors(source.doors.size() * 3 + 2);
    }
}
FloorplanLayer::~FloorplanLayer() {
    for (const auto& entry : impl_->resident) for (const auto& door : entry.second.doors)
        impl_->doors->evictRuntimeDoor(door.id);
    if (!impl_->resident.empty()) impl_->collision->setRuntimeGeometry({});
}
void FloorplanLayer::update(const std::vector<glm::vec3>& observers) {
    auto& data = *impl_;
    bool changed = false;
    for (auto it = data.resident.begin(); it != data.resident.end();) {
        const auto& plan = it->second;
        const bool keep = std::any_of(observers.begin(), observers.end(), [&](glm::vec3 p) {
            if (!finite(p)) return false;
            return distanceSquared(plan, p) <= 30 * 30 + .001f;
        });
        if (keep) { ++it; continue; }
        for (const auto& door : plan.doors) data.doors->evictRuntimeDoor(door.id);
        it = data.resident.erase(it); changed = true;
    }
    std::set<std::string> candidates;
    for (const auto p : observers) {
        if (!finite(p)) continue;
        for (int x = cell(p.x - 20); x <= cell(p.x + 20); ++x) for (int z = cell(p.z - 20); z <= cell(p.z + 20); ++z) {
            const auto found = data.cells.find({x, z});
            if (found == data.cells.end()) continue;
            for (const auto id : found->second) {
                const auto d = data.entrances[id].center - p;
                if (glm::dot(d, d) <= 20 * 20 && std::abs(data.doors->angle(id)) > .1f)
                    candidates.insert(data.doorBuildings[id]);
            }
        }
    }
    for (const auto& name : candidates) {
        if (data.resident.count(name) || data.rejected.count(name)) continue;
        auto plan = generateHouseFloorplan(*data.source, data.sources.at(name), data.entrances);
        if (!plan) { data.rejected.insert(name); continue; }
        for (const auto& door : plan->doors) data.doors->installRuntimeDoor(door.id, door.panel);
        data.resident.emplace(name, std::move(*plan)); changed = true;
    }
    if (changed) data.publish();
}
std::uint64_t FloorplanLayer::revision() const { return impl_->revision; }
std::size_t FloorplanLayer::residentCount() const { return impl_->resident.size(); }
const std::map<std::string, HouseFloorplan>& FloorplanLayer::resident() const { return impl_->resident; }
const Model& FloorplanLayer::renderModel() const { return impl_->renderModel; }
const std::map<std::size_t, std::size_t>& FloorplanLayer::doorDraws() const { return impl_->doorDraws; }
const DoorLayer& FloorplanLayer::doors() const { return *impl_->doors; }
} // namespace viewer
