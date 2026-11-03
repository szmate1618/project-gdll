#include "dog_layer.hpp"
#include "building_footprints.hpp"
#include "file_fingerprint.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
float gap(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x-b.x,a.z-b.z)); }
constexpr float arrivalDistance = .15f;
}
void configureDogHunting(DogLayer& dogs, const std::filesystem::path& map,
                         const BuildingFootprints& footprints,
                         std::shared_ptr<const navmesh_path::Mesh> sharedMesh) {
    if (dogs.instances.empty()) return;
    auto path = map; path.replace_extension(".navmesh");
    if (!sharedMesh && !std::filesystem::exists(path)) return;
    if (!sharedMesh) sharedMesh = std::make_shared<const navmesh_path::Mesh>(navmesh_path::Mesh::load(path.string()));
    auto metadata = path; metadata += ".json";
    if (std::filesystem::exists(metadata)) {
        std::ifstream stream(metadata);
        const auto document = nlohmann::json::parse(stream);
        if (document.at("map_sha256").get<std::string>() != fileSha256(map) ||
            document.at("source_sha256").get<std::string>() != sharedMesh->sourceSha256() ||
            document.at("output_sha256").get<std::string>() != fileSha256(path))
            throw std::runtime_error("Dog hunting navigation does not match the published map: " + path.string());
    }
    dogs.enableHunting(std::move(sharedMesh),&footprints);
}
void DogLayer::enableHunting(std::shared_ptr<const navmesh_path::Mesh> mesh, const BuildingFootprints* footprints) {
    auto navigation = std::make_unique<ZombieNavigation>(std::move(mesh),instances.size());
    for (std::size_t id = 0; id < behaviors.size(); ++id) if (behaviors[id].hunt) finishHunt(id,0);
    huntingNavigation_ = std::move(navigation);
    huntingFootprints_ = footprints;
}
bool DogLayer::validHuntingTarget(glm::vec3 target, const CollisionWorld& world) const {
    if (!huntingNavigation_ || !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z) ||
        huntingNavigation_->mesh().locate({target.x,target.z}) == navmesh_path::noPolygon) return false;
    if (huntingFootprints_ ? bool(huntingFootprints_->find(target)) : world.insideBuilding(target,dogRadius,dogHeight)) return false;
    for (const auto& contact : world.contacts(target,dogRadius,dogHeight,0,.05f)) {
        if (contact.terrainSupport || (contact.normal.y > .7f && contact.point.y <= target.y+.01f)) continue;
        if (contact.depth > .001f) return false;
    }
    return true;
}
std::optional<glm::vec3> DogLayer::huntingCellTarget(ScentGrid::Cell cell, glm::vec3 from, const CollisionWorld& world) const {
    const auto& mesh = huntingNavigation_->mesh();
    const auto start = mesh.locate({from.x,from.z});
    if (start == navmesh_path::noPolygon) return std::nullopt;
    const auto center = ScentGrid::center(cell,from.y);
    std::vector<glm::vec3> candidates{center};
    // Bounded probes stay inside the selected cell. A projection outside the
    // cell would silently change which step of the scent ascent is targeted.
    for (int x = -5; x <= 5; ++x) for (int z = -5; z <= 5; ++z)
        if (x || z) candidates.push_back(center+glm::vec3(x*1.8f,0,z*1.8f));
    std::stable_sort(candidates.begin(),candidates.end(),[&](auto a, auto b) { return gap(a,center) < gap(b,center); });
    for (auto target : candidates) {
        const auto polygon = mesh.locate({target.x,target.z});
        if (polygon == navmesh_path::noPolygon || mesh.component(polygon) != mesh.component(start)) continue;
        if (!world.empty()) {
            auto ground = world.groundAt(target.x,target.z,std::numeric_limits<float>::lowest(),std::numeric_limits<float>::max(),true);
            if (!ground) ground = world.staticGroundAt(target.x,target.z,std::numeric_limits<float>::lowest(),from.y+.3f);
            if (!ground || ground->normal.y < .70710678f) continue;
            target.y = ground->height + .002f;
        }
        if (validHuntingTarget(target,world)) return target;
    }
    return std::nullopt;
}
void DogLayer::cancelHunt(std::size_t id) {
    if (behaviors[id].hunt && huntingNavigation_) huntingNavigation_->reset(id);
    behaviors[id].hunt.reset();
}
void DogLayer::finishHunt(std::size_t id, double now) {
    cancelHunt(id);
    auto& behavior = behaviors[id];
    behavior.state = DogState::idle; behavior.idleSince = now;
    instances[id].nextHuntSeconds = now+10;
}
bool DogLayer::tryStartHunt(std::size_t id, const CollisionWorld& world, double now) {
    auto& behavior = behaviors[id]; auto& dog = instances[id];
    if (!huntingNavigation_ || !huntAttemptsRemaining_ || now < dog.nextHuntSeconds ||
        (behavior.state != DogState::idle && behavior.state != DogState::wandering && behavior.state != DogState::fetching)) return false;
    if (scent_.at(glm::vec3(dog.root[3])) < ScentGrid::minimumScent) {
        dog.nextHuntSeconds = now+1; return false;
    }
    --huntAttemptsRemaining_;
    dog.nextHuntSeconds = now+5;
    const auto feet = glm::vec3(dog.root[3]);
    const auto ascent = scent_.ascent(feet);
    for (std::size_t back = 0; back < std::min<std::size_t>(3,ascent.size()); ++back) {
        const auto target = huntingCellTarget(ascent[ascent.size()-1-back],feet,world);
        if (!target || !huntingNavigation_->plan(id,feet,*target)) continue;
        // Publish only after success: failed searches preserve all prior trip
        // state and any carried limb/joint.
        behavior.wander = {}; behavior.fetch.reset();
        behavior.hunt.emplace();
        behavior.state = DogState::hunting; behavior.active = true;
        behavior.target = *target; behavior.sight = 0;
        doorways_.erase(id);
        return true;
    }
    return false;
}
bool DogLayer::beginHuntReturn(std::size_t id, const CollisionWorld& world, double now) {
    auto& behavior = behaviors[id];
    const auto feet = glm::vec3(instances[id].root[3]);
    const auto home = instances[id].home;
    if (!home || !validHuntingTarget(*home,world) || !huntingNavigation_->plan(id,feet,*home)) {
        finishHunt(id,now); return false;
    }
    behavior.wander = {}; behavior.fetch.reset();
    behavior.hunt.emplace(); behavior.hunt->phase = DogHunting::Phase::returning;
    behavior.target = *home; behavior.state = DogState::hunting; behavior.sight = 0;
    return true;
}
bool DogLayer::prepareHuntStep(std::size_t id, const CollisionWorld& world, double now) {
    auto& behavior = behaviors[id];
    if (!behavior.hunt || !huntingNavigation_) { finishHunt(id,now); return false; }
    auto& hunt = *behavior.hunt;
    const auto feet = glm::vec3(instances[id].root[3]);
    const bool arrived = gap(feet,behavior.target) <= arrivalDistance;
    if (!arrived && hunt.stalledSeconds < 3) return true;
    if (hunt.phase == DogHunting::Phase::returning) { finishHunt(id,now); return false; }
    if (!arrived) return beginHuntReturn(id,world,now);
    if (hunt.phase == DogHunting::Phase::travel) {
        hunt.phase = DogHunting::Phase::search;
        hunt.searchLegs = std::uniform_int_distribution<int>(3,7)(wanderRandom_);
    } else --hunt.searchLegs;
    if (hunt.searchLegs <= 0) return beginHuntReturn(id,world,now);
    // Short search legs are exactly five horizontal meters. They must be
    // directly covered by the outdoor mesh, so a detour cannot turn a local
    // sniffing search into another long journey.
    const float angle = std::uniform_real_distribution<float>(0,6.28318530718f)(wanderRandom_);
    const glm::vec2 direction(std::sin(angle),std::cos(angle));
    const auto target = dogWanderTarget(feet,direction,5,world);
    if (!target || !validHuntingTarget(*target,world) || !huntingNavigation_->segmentClear(feet,*target) ||
        !huntingNavigation_->plan(id,feet,*target)) return beginHuntReturn(id,world,now);
    behavior.target = *target; hunt.stalledSeconds = 0;
    return true;
}
} // namespace viewer
