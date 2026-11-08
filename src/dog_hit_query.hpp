#pragma once
#include "dog_layer.hpp"

namespace viewer {
// One authoritative sword update. Borrows the layer; dogs cannot move during
// the query. Damage/death is allowed and eligibility is checked per segment.
// Posed world-space triangles are built lazily and reused only in this update.
class DogMeleeQuery {
public:
    DogMeleeQuery(const DogLayer* dogs, glm::vec3 center, float radius, double seconds);
    std::optional<DogShotTrace> trace(glm::vec3 start, glm::vec3 end, const CollisionWorld& world);
private:
    struct Triangle { glm::vec3 a, b, c; };
    struct Target {
        std::size_t id;
        VisibilityBounds bounds;
        bool prepared = false;
        std::vector<Triangle> triangles;
    };
    const DogLayer* dogs_;
    double seconds_;
    std::vector<Target> targets_;
};
} // namespace viewer
