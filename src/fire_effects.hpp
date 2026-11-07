#pragma once
#include "molotov.hpp"
#include "physics_prop.hpp"
#include <array>
#include <vector>
#include <functional>

namespace viewer {
class CollisionWorld;
class ZombieLayer;
class DogLayer;
class DoorLayer;
struct FireScene {
    const CollisionWorld& collision;
    std::shared_ptr<PhysicsWorld> physics;
    ZombieLayer* zombies = nullptr;
    const DogLayer* dogs = nullptr;
    const DoorLayer* doors = nullptr;
    double animationSeconds = 0;
};
enum class FireAttachment { world, body, door, zombie, dog };
struct FireContact {
    FireContact() = default;
    FireContact(glm::vec3 position, glm::vec3 surfaceNormal, FireAttachment kind = FireAttachment::world)
        : point(position), normal(surfaceNormal), attachment(kind) {}
    glm::vec3 point{0}, normal{0,1,0}; // World metres; normal faces the incoming ray.
    FireAttachment attachment = FireAttachment::world;
    std::optional<PhysicsProp> body;
    std::size_t actor = 0;
    int bone = -1;
    glm::vec3 localPoint{0};
};
struct FirePatch {
    FireContact contact;
    double born = 0;
    std::uint32_t seed = 0;
    bool active = false;
};
std::array<glm::vec3,molotovRayCount> molotovRayDirections(glm::vec3 direction, std::uint32_t seed);
using FireRaycast = std::function<std::optional<FireContact>(glm::vec3,glm::vec3,float)>;
// One bounded visual pool. No gameplay damage, propagation or physics bodies.
class FireEffects {
public:
    static constexpr std::size_t capacity = 256, historyCapacity = 128;
    void ignite(const MolotovImpact& impact, double seconds, const FireRaycast& raycast);
    // Repeated complete impact history, with server age but a local effect clock.
    void receive(const std::vector<MolotovImpact>& impacts, double snapshotSeconds,
                 double presentationSeconds, double now, const FireScene& scene);
    void update(double seconds, const FireScene& scene);
    bool hasEffects(double seconds) const;
    const auto& patches() const { return patches_; }
private:
    std::array<FirePatch,capacity> patches_{};
    std::array<std::uint32_t,historyCapacity> seen_{};
    std::size_t slot_ = 0;
};
} // namespace viewer
