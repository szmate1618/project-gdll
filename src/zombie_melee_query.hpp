#pragma once
#include "melee_sweep.hpp"
#include "zombie_hit.hpp"

namespace viewer {
struct ZombieLayer;
struct ZombieMeleeContact {
    std::size_t id;
    MeleeSweepHit hit;
    std::optional<ZombieHitClaim> body;
    int joint = -1;
};
// Borrows one authoritative population state. Refresh an actor immediately
// after a sever changes its living/physical pose; never retain across updates.
class ZombieMeleeQuery {
public:
    ZombieMeleeQuery(ZombieLayer* layer, const std::vector<std::size_t>& ids, double seconds);
    void refresh(std::size_t id);
    std::vector<ZombieMeleeContact> cast(const MeleeSweep& sweep, bool jointsOnly) const;
private:
    struct Part {
        MeleeCollider collider;
        std::optional<ZombieHitClaim> body;
        glm::dmat4 claimInverse{1};
        int joint = -1;
    };
    struct Target { std::size_t id; std::vector<Part> parts; };
    ZombieLayer* layer_;
    double seconds_;
    std::vector<Target> targets_;
};
} // namespace viewer
