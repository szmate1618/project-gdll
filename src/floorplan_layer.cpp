#include "floorplan_layer.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "model_merge.hpp"
#include "house_layout.hpp"
#include "house_polygon.hpp"
#include "prefab_floorplan.hpp"
#include "building_footprints.hpp"
#include "house_navigation.hpp"
#include "authored_floorplan.hpp"
#include "basic_floorplan.hpp"
#include "small_building_floorplan.hpp"
#include "low_rise_floorplan.hpp"
#include "complex_floorplan.hpp"

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
    for (std::size_t i = 0; i < plan.footprint.size(); ++i) {
        const auto a = plan.footprint[i], b = plan.footprint[(i + 1) % plan.footprint.size()];
        const auto edge = b - a, delta = p - a;
        const auto d = delta - edge * glm::clamp(glm::dot(delta, edge) / glm::dot(edge, edge), 0.f, 1.f);
        nearest = std::min(nearest, glm::dot(d, d));
    }
    const float dy = std::max({plan.basementFloor.value_or(plan.floor) - position.y, position.y - plan.ceiling, 0.f});
    for (const auto& court : plan.courtyards) for (std::size_t i = 0; i < court.size(); ++i) {
        const auto a = court[i], b = court[(i + 1) % court.size()];
        const auto edge = b - a;
        const float t = glm::clamp(glm::dot(p - a, edge) / glm::dot(edge, edge), 0.f, 1.f);
        const auto d = p - (a + edge * t);
        nearest = std::min(nearest, glm::dot(d, d));
    }
    return (plan.contains(p) ? 0.f : nearest) + dy * dy;
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
    std::map<std::string, PrefabBlock> blocks; // Shell metadata only; no generated rooms/meshes.
    std::map<std::string, LowRiseBuilding> lowRise;
    std::map<std::string, BuildingSections> complex;
    std::map<std::pair<int, int>, std::vector<std::size_t>> cells;
    std::set<std::string> rejected;
    std::map<std::string, HouseFloorplan> resident;
    std::unique_ptr<BuildingFootprints> footprints;
    std::map<std::string, HouseNavigationGraph> graphs;
    std::uint64_t graphGeneration = 0;
    Model renderModel;
    std::map<std::size_t, std::size_t> doorDraws;
    std::set<std::string> rendered;
    std::uint64_t revision = 1;

    void publish(bool collisionChanged) {
        Model collisionModel;
        renderModel = {}; doorDraws.clear();
        for (const auto& entry : resident) {
            const auto& plan = entry.second;
            if (collisionChanged) appendStaticModel(collisionModel, Model(plan.structure));
            if (!rendered.count(entry.first)) continue;
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
        if (collisionChanged) collision->setRuntimeGeometry(collisionModel);
        ++revision;
    }
};
FloorplanLayer::FloorplanLayer(const Model& model, DoorLayer& doors, CollisionWorld& collision)
    : impl_(std::make_unique<Impl>()) {
    auto& data = *impl_;
    data.source = &model; data.doors = &doors; data.collision = &collision;
    data.footprints = std::make_unique<BuildingFootprints>(model);
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
    // Ground: entry/side doors and at most two living partitions. Upper floors:
    // at most four rooms per side, each adding one connection. The 10 m house
    // height / 2.4 m minimum storey permits at most three upper floors.
    // Reserve by sorted identity and loaded window rows, never activation order.
    for (auto& entry : data.sources) {
        auto& source = entry.second;
        source.name = entry.first;
        if (source.doors.empty()) continue;
        const auto authored = model.authoredFloorplans.find(source.name);
        if (authored != model.authoredFloorplans.end()) {
            std::size_t count = 0;
            for (const auto& wall : authored->second->layout.partitions) count += wall.openings.size();
            source.firstDoor = doors.reserveRuntimeDoors(count);
            continue;
        }
        const auto upperStoreys = houseUpperStoreyCount(model, source);
        auto sections=prepareBuildingSections(model,source,*data.footprints);
        if (auto block = classifyPrefabBlock(model, source)) {
            auto capacity=prefabDoorCapacity(*block, source.doors.size());
            if (sections) capacity=std::max(capacity,complexDoorCapacity(*sections,upperStoreys+1,source.doors.size()));
            source.firstDoor = doors.reserveRuntimeDoors(capacity);
            data.blocks.emplace(source.name, std::move(*block));
            if (sections) data.complex.emplace(source.name,std::move(*sections));
            continue;
        }
        auto capacity = source.doors.size() * 3 + 2 + 2 * house_layout::maxUpperRoomsPerSide * std::min<std::size_t>(3, upperStoreys);
        if (auto building = classifyLowRiseBuilding(model, source)) {
            capacity = std::max(capacity, lowRiseDoorCapacity(*building, upperStoreys + 1, source.doors.size()));
            data.lowRise.emplace(source.name, std::move(*building));
        }
        if (sections) {
            capacity=std::max(capacity,complexDoorCapacity(*sections,upperStoreys+1,source.doors.size()));
            data.complex.emplace(source.name,std::move(*sections));
        }
        source.firstDoor = doors.reserveRuntimeDoors(capacity);
    }
}
FloorplanLayer::~FloorplanLayer() {
    for (const auto& entry : impl_->resident) for (const auto& door : entry.second.doors)
        impl_->doors->evictRuntimeDoor(door.id);
    if (!impl_->resident.empty()) impl_->collision->setRuntimeGeometry({});
}
void FloorplanLayer::update(const std::vector<glm::vec3>& observers,
                           const std::vector<glm::vec3>& physicsSupport) {
    auto& data = *impl_;
    bool changed = false;
    std::set<std::string> supportedBuildings;
    for (const auto p : physicsSupport) {
        if (!finite(p)) continue;
        // Part centers can sit just outside a supporting slab at a doorway.
        // Query the footprint index rather than scan all corpses per building.
        for (const auto& name : data.footprints->near(p, 1.f)) {
            const auto plan = data.resident.find(name);
            if (plan != data.resident.end() && distanceSquared(plan->second, p) <= 1.f)
                supportedBuildings.insert(name);
        }
    }
    for (auto it = data.resident.begin(); it != data.resident.end();) {
        const auto& plan = it->second;
        const bool keep = std::any_of(observers.begin(), observers.end(), [&](glm::vec3 p) {
            if (!finite(p)) return false;
            return distanceSquared(plan, p) <= 30 * 30 + .001f;
        });
        if (keep || supportedBuildings.count(it->first)) { ++it; continue; }
        for (const auto& door : plan.doors) data.doors->evictRuntimeDoor(door.id);
        data.graphs.erase(it->first);
        it = data.resident.erase(it); changed = true;
    }
    std::set<std::string> candidates;
    for (const auto p : observers) {
        if (!finite(p)) continue;
        for (const auto& name : data.footprints->near(p, 20)) {
            const auto found = data.sources.find(name);
            if (found != data.sources.end() && found->second.doors.empty()) candidates.insert(name);
        }
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
        const auto block = data.blocks.find(name);
        const auto authored = data.source->authoredFloorplans.find(name);
        auto plan = authored != data.source->authoredFloorplans.end() ?
            generateAuthoredFloorplan(*authored->second, data.sources.at(name), data.entrances) :
            block == data.blocks.end() ? generateHouseFloorplan(*data.source, data.sources.at(name), data.entrances) :
            generatePrefabFloorplan(*data.source, data.sources.at(name), data.entrances, block->second);
        if (!plan) plan = generateSmallBuildingFloorplan(*data.source, data.sources.at(name), data.entrances);
        if (!plan) {
            const auto building = data.lowRise.find(name);
            if (building != data.lowRise.end())
                plan = generateLowRiseFloorplan(*data.source, data.sources.at(name), data.entrances, building->second);
        }
        if (!plan) {
            const auto building=data.complex.find(name);
            if (building!=data.complex.end()) for (const auto mode:{ComplexLayout::Rooms,ComplexLayout::OpenSections}) {
                auto candidate=generateComplexFloorplan(*data.source,data.sources.at(name),data.entrances,building->second,mode);
                if (!candidate) continue;
                CollisionWorld support(candidate->structure);
                HouseNavigationGraph navigation(*candidate,*data.doors,support,0);
                if (!navigation.reachesAllRooms(data.sources.at(name).doors)) continue;
                plan=std::move(candidate); break;
            }
        }
        if (!plan) plan = generateBasicFloorplan(*data.source, data.sources.at(name), data.entrances, *data.footprints);
        if (!plan) { data.rejected.insert(name); continue; }
        for (const auto& door : plan->doors) data.doors->installRuntimeDoor(door.id, door.panel);
        data.resident.emplace(name, std::move(*plan)); changed = true;
    }
    std::set<std::string> rendered;
    for (const auto& [name, plan] : data.resident)
        if (std::any_of(observers.begin(), observers.end(), [&](glm::vec3 p) {
                return finite(p) && distanceSquared(plan, p) <= 30 * 30 + .001f;
            })) rendered.insert(name);
    const bool renderChanged = data.rendered != rendered;
    data.rendered = std::move(rendered);
    if (changed || renderChanged) data.publish(changed);
    if (changed) {
        for (const auto& [name, plan] : data.resident)
            if (!data.graphs.count(name)) data.graphs.try_emplace(name, plan, *data.doors, *data.collision, ++data.graphGeneration);
    }
}
std::uint64_t FloorplanLayer::revision() const { return impl_->revision; }
std::size_t FloorplanLayer::residentCount() const { return impl_->resident.size(); }
const std::map<std::string, HouseFloorplan>& FloorplanLayer::resident() const { return impl_->resident; }
const Model& FloorplanLayer::renderModel() const { return impl_->renderModel; }
const std::map<std::size_t, std::size_t>& FloorplanLayer::doorDraws() const { return impl_->doorDraws; }
const DoorLayer& FloorplanLayer::doors() const { return *impl_->doors; }
const BuildingFootprints& FloorplanLayer::footprints() const { return *impl_->footprints; }
const HouseNavigationGraph* FloorplanLayer::navigationGraph(const std::string& building) const {
    const auto found = impl_->graphs.find(building);
    return found == impl_->graphs.end() ? nullptr : &found->second;
}
} // namespace viewer
