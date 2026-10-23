#pragma once
#include "zombie_wounds.hpp"
#include <memory>

namespace viewer {
class PhysicsWorld;
class PhysicsScene;
// Owns hollow kinematic cavities and their loose demonstration props.
// Geometry is rebuilt on damage; normal movement changes only the rigid frame.
class ZombieCavityPhysics {
public:
    static constexpr std::size_t torsoObjectCount = 6, headObjectCount = 1;
    static constexpr std::size_t maxObjectCount = torsoObjectCount+headObjectCount;
    explicit ZombieCavityPhysics(std::shared_ptr<PhysicsWorld> world);
    ~ZombieCavityPhysics();
    void wound(std::size_t id, const AnimatedModel& asset, const ZombieWoundGeometry& geometry,
               const ZombieWound& wound, const glm::mat4& frame, glm::vec3 velocity);
    void move(std::size_t id, ZombieWoundRegion region, const glm::mat4& frame, float seconds);
    bool has(std::size_t id, ZombieWoundRegion region) const;
    void prepare(PhysicsScene* scene);
    void remove(std::size_t id);
    std::vector<std::pair<std::size_t,ZombieWoundRegion>> ids() const;
    std::vector<glm::mat4> contents() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
