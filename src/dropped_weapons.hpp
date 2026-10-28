#pragma once

#include "box_layer.hpp"
#include "weapons.hpp"

namespace viewer {
class Camera;
class CollisionWorld;
class DoorLayer;
class PhysicsScene;
struct DroppedWeaponPose {
    std::size_t slot;
    glm::mat4 transform;
    glm::vec3 minimum, maximum;
};

// Equippable weapon ownership and metadata around the shared box-physics primitive.
// These bodies are separate from ordinary crates and are picked up with F.
class DroppedWeapons {
public:
    static constexpr float throwSpeed = 12.5f; // m/s, plus actual player travel.
    static constexpr float spinSpeed = 14; // rad/s around world Y.
    static constexpr float pickupReach = 2; // metres along the camera ray.
    DroppedWeapons(std::shared_ptr<PhysicsWorld> world, const std::array<Model,weaponIds.size()>& models);
    bool discard(Weapons& inventory, const Camera& camera, const CollisionWorld& world, glm::vec3 playerVelocity);
    bool pickUp(Weapons& inventory, const Camera& camera, const CollisionWorld& world);
    void prepare(PhysicsScene& scene, DoorLayer& doors);
    void removeBelow(float cutoffY);
    std::vector<DroppedWeaponPose> poses() const;
    std::size_t count() const { return items_.size(); }
private:
    void remove(std::size_t index);
    BoxLayer bodies_;
    std::array<glm::vec3,weaponIds.size()> centers_, sizes_;
    std::vector<WeaponItem> items_;
};
} // namespace viewer
