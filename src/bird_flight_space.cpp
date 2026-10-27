#include "bird_flight_space.hpp"
#include "collision_world.hpp"
#include "tree_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
namespace {
bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool intersectsSegment(glm::vec3 from, glm::vec3 to, VisibilityBounds box, float radius) {
    box.minimum -= glm::vec3(radius);
    box.maximum += glm::vec3(radius);
    const auto delta = to - from;
    float enter = 0, leave = 1;
    for (int axis = 0; axis != 3; ++axis) {
        if (std::abs(delta[axis]) < 1e-7f) {
            if (from[axis] < box.minimum[axis] || from[axis] > box.maximum[axis]) return false;
        } else {
            float low = (box.minimum[axis] - from[axis]) / delta[axis];
            float high = (box.maximum[axis] - from[axis]) / delta[axis];
            if (low > high) std::swap(low, high);
            enter = std::max(enter, low);
            leave = std::min(leave, high);
            if (enter > leave) return false;
        }
    }
    return true;
}
}

BirdFlightSpace::BirdFlightSpace(const CollisionWorld& world, const TreeLayer* trees) : world_(world) {
    if (trees) {
        canopies_.reserve(trees->instances.size());
        for (const auto& tree : trees->instances) {
            if (finite(tree.boundsMin) && finite(tree.boundsMax))
                canopies_.push_back({tree.boundsMin, tree.boundsMax});
        }
    }
    canopyIndex_ = VisibilityIndex(canopies_);
    // Every query reuses storage even when an unusually large canopy region is
    // selected. No dependence on camera culling or rendering's visible list.
    candidates_.reserve(canopies_.size());
}

bool BirdFlightSpace::lineOfSight(glm::vec3 from, glm::vec3 to) const {
    if (!finite(from) || !finite(to)) return false;
    canopyIndex_.query({glm::min(from, to), glm::max(from, to)}, candidates_);
    for (const auto candidate : candidates_)
        if (intersectsSegment(from, to, canopies_[candidate], 0)) return false;
    return world_.lineOfSight(from, to);
}

bool BirdFlightSpace::clearPoint(glm::vec3 point, float radius) const {
    if (!finite(point) || !std::isfinite(radius) || radius <= 0) return false;
    canopyIndex_.query({point - glm::vec3(radius), point + glm::vec3(radius)}, candidates_);
    if (!candidates_.empty()) return false;
    const auto terrain = world_.groundAt(point.x, point.z, std::numeric_limits<float>::lowest(),
                                         std::numeric_limits<float>::max(), true);
    if (terrain && point.y - radius <= terrain->height) return false;
    const auto feet = point - glm::vec3(0, radius, 0);
    if (world_.insideBuilding(feet, radius, 2 * radius)) return false;
    bool clear = true;
    world_.visitContacts(feet, radius, 2 * radius, 0, 0,
        {&clear, [](void* context, const Contact&) {
            *static_cast<bool*>(context) = false;
            return false;
        }});
    return clear;
}

bool BirdFlightSpace::clearSegment(glm::vec3 from, glm::vec3 to, float radius) const {
    if (!finite(from) || !clearPoint(to, radius)) return false;
    canopyIndex_.query({glm::min(from, to) - glm::vec3(radius),
                       glm::max(from, to) + glm::vec3(radius)}, candidates_);
    for (const auto candidate : candidates_)
        if (intersectsSegment(from, to, canopies_[candidate], radius)) return false;
    // Six offset rays approximate a swept flock envelope cheaply. Flight is a
    // prototype: thin corners between rays may still clip an outer wingtip.
    if (!world_.lineOfSight(from, to)) return false;
    for (int axis = 0; axis != 3; ++axis) {
        glm::vec3 offset(0);
        offset[axis] = radius;
        if (!world_.lineOfSight(from + offset, to + offset) ||
            !world_.lineOfSight(from - offset, to - offset)) return false;
    }
    return true;
}
} // namespace viewer
