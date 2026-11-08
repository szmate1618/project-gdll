#include "interior_zombies.hpp"
#include "house_layout_rules.hpp"
#include "house_polygon.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "zombie_crowd.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace viewer {
namespace {
using house_layout::rules::Random;
constexpr float inset = .42f, spacing = .75f;
struct Candidate { glm::vec2 feet, facing; std::size_t room; InteriorZombiePosition kind; };
bool inside(const HouseRoom& room, glm::vec2 p) {
    return house_polygon::contains(room.polygon, p);
}
std::optional<glm::vec3> safeFeet(const HouseFloorplan& plan, const Candidate& candidate, const CollisionWorld& world) {
    const auto& room = plan.rooms[candidate.room];
    if (!inside(room, candidate.feet)) return std::nullopt;
    // A point in a doorway can pass capsule collision yet no longer belong
    // against this room's wall. Keep the whole body inside its chosen room.
    for (std::size_t i = 0; i < room.polygon.size(); ++i) {
        const auto a = room.polygon[i], edge = room.polygon[(i + 1) % room.polygon.size()] - a;
        const float t = glm::clamp(glm::dot(candidate.feet - a, edge) / glm::dot(edge, edge), 0.f, 1.f);
        if (glm::distance(candidate.feet, a + t * edge) < .4f) return std::nullopt;
    }
    const float floor = plan.floorHeight(room.storey);
    const auto hit = world.walkingGroundAt(candidate.feet.x, candidate.feet.y, floor - .04f, floor + .04f);
    if (!hit || hit->normal.y < .99f || std::abs(hit->height - floor) > .04f) return std::nullopt;
    const glm::vec3 feet(candidate.feet.x, hit->height + .004f, candidate.feet.y);
    for (const auto& contact : world.contacts(feet, ZombieCrowd::bodyRadius, ZombieCrowd::bodyHeight, .002f, .05f)) {
        if (contact.normal.y > .7f && contact.point.y <= feet.y + .01f) continue;
        if (contact.depth > .005f) return std::nullopt;
    }
    return feet;
}
InteriorZombiePlacement placement(const Candidate& c, glm::vec3 feet) {
    const auto direction = c.facing - c.feet;
    const float yaw = std::atan2(direction.x, direction.y);
    return {glm::rotate(glm::translate(glm::mat4(1), feet), yaw, {0, 1, 0}), c.room, c.kind};
}
template<class T> void shuffle(std::vector<T>& values, Random& random) {
    for (std::size_t i = values.size(); i > 1; --i) std::swap(values[i - 1], values[random.pick(static_cast<std::uint32_t>(i))]);
}
std::vector<DoorPanel> roomDoors(const HouseFloorplan& plan, const HouseRoom& room, const DoorLayer& doors) {
    std::vector<DoorPanel> result;
    const auto add = [&](const DoorPanel& panel) {
        const glm::vec2 center(panel.center.x, panel.center.z), normal(panel.outward.x, panel.outward.z);
        if (inside(room, center + normal * .1f) || inside(room, center - normal * .1f)) result.push_back(panel);
    };
    for (const auto& door : plan.doors) if (door.storey == room.storey) add(door.panel);
    for (const auto id : room.entrances) if (doors.present(id)) add(doors.panel(id));
    return result;
}
} // namespace
bool hasInteriorZombieEncounter(const std::string& building) {
    Random random(building, 0x696e746572696f72ull);
    return random.pick(100) < 20;
}
std::vector<InteriorZombiePlacement> placeInteriorZombies(const HouseFloorplan& plan,
    const DoorLayer& doors, const CollisionWorld& collision) {
    if (!hasInteriorZombieEncounter(plan.name)) return {};
    Random random(plan.name, 0x7a6f6d6269657370ull);
    std::vector<std::size_t> rooms;
    for (std::size_t i = 0; i < plan.rooms.size(); ++i) {
        // Adjacent common polygons have open boundaries; their corners are not
        // necessarily physical walls where an ordinary ambusher can hide.
        if (plan.kind == HouseFloorplan::Kind::PrefabApartmentBlock && plan.rooms[i].kind != HouseRoom::Kind::Apartment) continue;
        rooms.push_back(i);
    }
    shuffle(rooms, random);
    // A rare middle-room encounter is atomic: either the whole 3-6 group fits
    // in one living room, or fall back to ordinary wall-hugging ambushers.
    if (random.pick(100) < 10) {
        const auto count = 3 + random.pick(4);
        for (const auto id : rooms) {
            const auto& room = plan.rooms[id];
            if (room.kind != HouseRoom::Kind::Living && room.kind != HouseRoom::Kind::Apartment) continue;
            const auto u = glm::normalize(plan.envelope[3] - plan.envelope[0]), v = glm::normalize(plan.envelope[1] - plan.envelope[0]);
            glm::vec2 center(0); for (const auto p : room.polygon) center += p / float(room.polygon.size());
            const auto access = roomDoors(plan, room, doors);
            const glm::vec2 target = access.empty() ? center + v : glm::vec2(access.front().center.x, access.front().center.z);
            std::vector<InteriorZombiePlacement> group;
            for (std::uint32_t member = 0; member < count; ++member) {
                const auto point = center + u * ((float(member % 3) - 1) * spacing) + v * ((float(member / 3) - .5f) * spacing);
                Candidate c{point, target, id, InteriorZombiePosition::CenterGroup};
                const auto feet = safeFeet(plan, c, collision);
                if (!feet) break;
                group.push_back(placement(c, *feet));
            }
            if (group.size() == count) return group;
        }
    }
    std::vector<Candidate> hidden, beside, corners;
    for (const auto id : rooms) {
        const auto& room = plan.rooms[id];
        glm::vec2 center(0); for (const auto p : room.polygon) center += p / float(room.polygon.size());
        const auto access = roomDoors(plan, room, doors);
        for (const auto& door : access) {
            const glm::vec2 p(door.center.x, door.center.z), normal(door.outward.x, door.outward.z);
            const auto right3 = glm::cross(glm::vec3(0, 1, 0), door.outward);
            const glm::vec2 right(right3.x, right3.z), hinge(door.hinge.x, door.hinge.z);
            for (const float sign : {-1.f, 1.f}) {
                const auto inward = normal * sign;
                if (!inside(room, p + inward * inset)) continue;
                if ((door.inward ? -sign : sign) > 0)
                    hidden.push_back({hinge + inward * inset - right * .7f, p, id, InteriorZombiePosition::BehindDoor});
                for (const float flank : {-1.f, 1.f})
                    beside.push_back({p + inward * inset + right * (flank * (door.halfExtents.x + .5f)), p, id, InteriorZombiePosition::BesideDoor});
            }
        }
        const glm::vec2 target = access.empty() ? center : glm::vec2(access.front().center.x, access.front().center.z);
        if (room.polygon.size() == 4 && plan.footprint.size() == 4 &&
            std::equal(plan.footprint.begin(), plan.footprint.end(), plan.envelope.begin())) {
            const auto u = glm::normalize(room.polygon[3] - room.polygon[0]), v = glm::normalize(room.polygon[1] - room.polygon[0]);
            const auto width = glm::distance(room.polygon[3], room.polygon[0]), depth = glm::distance(room.polygon[1], room.polygon[0]);
            for (const bool highU : {false, true}) for (const bool highV : {false, true})
                corners.push_back({room.polygon[0] + u * (highU ? width - inset : inset) + v * (highV ? depth - inset : inset), target, id, InteriorZombiePosition::Corner});
        } else for (std::size_t i = 0; i < room.polygon.size(); ++i) {
            const auto p = room.polygon[i];
            const auto a = glm::normalize(room.polygon[(i + room.polygon.size() - 1) % room.polygon.size()] - p);
            const auto b = glm::normalize(room.polygon[(i + 1) % room.polygon.size()] - p);
            const float sine = std::abs(a.x * b.y - a.y * b.x);
            if (sine > .1f) corners.push_back({p + (a + b) * (inset / sine), target, id, InteriorZombiePosition::Corner});
        }
    }
    shuffle(hidden, random); shuffle(beside, random); shuffle(corners, random);
    std::vector<InteriorZombiePlacement> result;
    std::set<std::size_t> occupiedRooms;
    const auto take = [&](const std::vector<Candidate>& candidates, bool freshRoom) {
        for (const auto& c : candidates) {
            if (freshRoom && occupiedRooms.count(c.room)) continue;
            const auto feet = safeFeet(plan, c, collision);
            if (!feet) continue;
            if (std::any_of(result.begin(), result.end(), [&](const auto& other) { return glm::distance(glm::vec3(other.transform[3]), *feet) < spacing; })) continue;
            result.push_back(placement(c, *feet)); occupiedRooms.insert(c.room); return true;
        }
        return false;
    };
    const auto count = 1 + random.pick(3);
    for (std::uint32_t member = 0; member < count; ++member) {
        const bool door = random.pick(100) < 75;
        bool placed = false;
        for (const bool fresh : {true, false}) {
            placed = door ? take(hidden, fresh) || take(beside, fresh) || take(corners, fresh) :
                            take(corners, fresh) || take(hidden, fresh) || take(beside, fresh);
            if (placed) break;
        }
        if (!placed) break;
    }
    return result;
}
} // namespace viewer
