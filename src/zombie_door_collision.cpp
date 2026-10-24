#include "zombie_door_collision.hpp"
#include "zombie_layer.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {
constexpr float radius = ZombieCrowd::bodyRadius, height = ZombieCrowd::bodyHeight;
// Absorb contact slop and the transient compression from a capped player
// impulse at the app's maximum 0.1 s timestep, outside the movement body.
constexpr float contactPadding = .02f;
}
struct ZombieDoorCollision::Impl {
    struct Entry { b3BodyId body; glm::vec3 feet; };
    std::shared_ptr<PhysicsWorld> world;
    std::map<std::size_t, Entry> bodies;
    std::vector<std::size_t> candidates, selected;
    ~Impl() {
        for (const auto& item : bodies) b3DestroyBody(item.second.body);
    }
};

ZombieDoorCollision::ZombieDoorCollision(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    if (!world) throw std::invalid_argument("Zombie door contacts require a shared physics world");
    impl_->world = std::move(world);
}
ZombieDoorCollision::~ZombieDoorCollision() = default;
std::size_t ZombieDoorCollision::count() const { return impl_->bodies.size(); }

void ZombieDoorCollision::prepare(const DoorLayer& doors, ZombieLayer& zombies) {
    zombies.ensureSpatialIndex();
    auto& data = *impl_;
    data.selected.clear();
    for (const auto door : doors.activeDoors()) {
        const auto& panel = doors.panel(door);
        const float reach = 2 * panel.halfExtents.x + panel.halfExtents.z + radius + .1f;
        zombies.spatialIndex_.query(panel.hinge, reach, data.candidates);
        for (const auto id : data.candidates) {
            const auto& instance = zombies.instances[id];
            if (instance.removed || instance.ragdoll || instance.vehiclePhysics || instance.asset >= zombies.assets.size()) continue;
            const auto feet = glm::vec3(instance.restRoot[3]);
            const auto offset = glm::vec2(feet.x - panel.hinge.x, feet.z - panel.hinge.z);
            if (glm::dot(offset, offset) > reach * reach ||
                feet.y > panel.center.y + panel.halfExtents.y + contactPadding ||
                feet.y + height < panel.center.y - panel.halfExtents.y - contactPadding) continue;
            data.selected.push_back(id);
        }
    }
    // Stable body creation/removal order, including zombies shared by several
    // adjacent doorways. Never scan the full city population per physics step.
    std::sort(data.selected.begin(), data.selected.end());
    data.selected.erase(std::unique(data.selected.begin(), data.selected.end()), data.selected.end());
    for (auto it = data.bodies.begin(); it != data.bodies.end();) {
        if (std::binary_search(data.selected.begin(), data.selected.end(), it->first)) { ++it; continue; }
        b3DestroyBody(it->second.body);
        it = data.bodies.erase(it);
    }
    for (const auto id : data.selected) {
        const auto feet = glm::vec3(zombies.instances[id].restRoot[3]);
        const auto found = data.bodies.find(id);
        if (found != data.bodies.end()) {
            if (found->second.feet != feet) {
                b3Body_SetTransform(found->second.body, {feet.x, feet.y, feet.z}, b3Quat_identity);
                found->second.feet = feet;
            }
            continue;
        }
        auto definition = b3DefaultBodyDef();
        definition.type = b3_kinematicBody;
        definition.position = {feet.x, feet.y, feet.z};
        const auto body = b3CreateBody(data.world->id(), &definition);
        auto shape = b3DefaultShapeDef();
        shape.filter.categoryBits = zombieDoorBlockerCategory;
        shape.filter.maskBits = doorBodyCategory;
        shape.baseMaterial.friction = 0;
        const b3Capsule capsule{{0, radius, 0}, {0, height - radius, 0}, radius + contactPadding};
        b3CreateCapsuleShape(body, &shape, &capsule);
        try {
            data.bodies.emplace(id, Impl::Entry{body, feet});
        } catch (...) {
            b3DestroyBody(body);
            throw;
        }
    }
}
} // namespace viewer
