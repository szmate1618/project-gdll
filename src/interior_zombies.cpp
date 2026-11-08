#include "interior_zombies.hpp"
#include "floorplan_layer.hpp"
#include "zombie_layer.hpp"
#include "house_polygon.hpp"
#include "simulation_clock.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace viewer {
struct InteriorZombies::Impl {
    struct Actor { std::size_t id; bool sleeping = false; };
    struct Encounter {
        std::vector<glm::vec2> footprint;
        std::vector<std::vector<glm::vec2>> courtyards;
        float floor, ceiling;
        std::vector<Actor> actors;
        bool contains(glm::vec3 p) const {
            if (p.y < floor - .1f || p.y > ceiling) return false;
            if (!house_polygon::contains(footprint, {p.x, p.z})) return false;
            for (const auto& court : courtyards) if (house_polygon::contains(court, {p.x, p.z})) return false;
            return true;
        }
    };
    std::size_t first, reserved, used = 0;
    std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
    std::map<std::string, Encounter> encounters; // Small session state; no retained room/mesh geometry.
    std::set<std::string> resident;
    Impl(std::size_t first, std::size_t reserved) : first(first), reserved(reserved) {}
};
InteriorZombies::InteriorZombies(std::size_t first, std::size_t reserved) :
    impl_(std::make_unique<Impl>(first, reserved)) {}
InteriorZombies::~InteriorZombies() = default;
std::size_t InteriorZombies::reserveCount() const { return impl_->reserved; }
void InteriorZombies::update(const FloorplanLayer& floors, ZombieLayer& zombies,
    const CollisionWorld& collision, const std::vector<glm::vec3>& observers, const DayNightCycle& dayNight) {
    auto& state = *impl_;
    if (floors.revision() == state.revision) return;
    if (state.first > zombies.instances.size() || state.reserved > zombies.instances.size() - state.first)
        throw std::logic_error("Interior zombie pool must be loaded before streaming");
    state.revision = floors.revision();
    for (const auto& name : state.resident) {
        if (floors.resident().count(name)) continue;
        auto& encounter = state.encounters.at(name);
        auto& actors = encounter.actors;
        actors.erase(std::remove_if(actors.begin(), actors.end(), [&](auto& actor) {
            const auto& instance = zombies.instances.at(actor.id);
            // Corpses retain their ordinary cleanup lifecycle. Escaped survivors
            // become ordinary outdoor actors rather than vanishing with the house.
            if (instance.ragdoll || instance.removed || !encounter.contains(glm::vec3(instance.restRoot[3]))) return true;
            zombies.setInteriorSleeping(actor.id, true);
            actor.sleeping = true;
            return false;
        }), actors.end());
    }
    std::set<std::string> resident;
    std::vector<std::size_t> nearby;
    for (const auto& [name, plan] : floors.resident()) {
        if (!hasInteriorZombieEncounter(name)) continue;
        resident.insert(name);
        if (state.resident.count(name)) continue;
        const auto found = state.encounters.find(name);
        if (found != state.encounters.end()) {
            for (auto& actor : found->second.actors) if (actor.sleeping) {
                zombies.setInteriorSleeping(actor.id, false);
                actor.sleeping = false;
            }
            continue;
        }
        // Record even an empty/blocked/exhausted encounter: no fresh roll on reentry.
        auto& encounter = state.encounters.emplace(name,
            Impl::Encounter{plan.footprint, plan.courtyards, plan.basementFloor.value_or(plan.floor), plan.ceiling, {}}).first->second;
        if (state.used == state.reserved) continue;
        auto placements = placeInteriorZombies(plan, floors.doors(), collision);
        if (placements.empty()) continue;
        const bool group = placements.front().position == InteriorZombiePosition::CenterGroup;
        const auto available = [&](const InteriorZombiePlacement& placement) {
            const auto feet = glm::vec3(placement.transform[3]);
            for (const auto observer : observers)
                if (glm::distance(observer, feet) < 1.2f) return false;
            zombies.queryLivingNear(feet, .75f, nearby);
            return nearby.empty();
        };
        // A center encounter is a complete group even when capacity or other
        // living actors/player positions prevent some of its planned placements.
        if (group && (placements.size() > state.reserved - state.used ||
            !std::all_of(placements.begin(), placements.end(), available))) continue;
        for (const auto& placement : placements) {
            if (state.used == state.reserved) break;
            if (!group && !available(placement)) continue;
            const auto id = state.first + state.used++;
            // Upstairs takes precedence over the doorway/corner placement kind.
            const auto initial = plan.rooms.at(placement.room).storey > 0 &&
                plan.rooms.at(placement.room).storey != HouseFloorplan::basementStorey ? ZombieState::midnight :
                placement.position == InteriorZombiePosition::CenterGroup ? ZombieState::idle : ZombieState::hiding;
            const auto wake = initial == ZombieState::midnight ?
                dayNight.nextMidnightAtOrAfter(SimulationClock::instance().seconds()) : 0.;
            zombies.activateInteriorZombie(id, placement.transform, initial, wake);
            encounter.actors.push_back({id, false});
        }
    }
    state.resident = std::move(resident);
}
} // namespace viewer
