#pragma once

#include "doorway.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace viewer {
class CollisionWorld;

// Simulation-thread local passage coordination, owned by one actor population.
// It neither changes behavior targets nor opens doors. Only direct movers
// request passage; stale requests expire, including actors entering attacks.
class DoorwayGuidance {
public:
    DoorwayGuidance(float radius, float height) : bodyRadius_(radius), bodyHeight_(height) {}
    void beginStep(float seconds);
    void erase(std::size_t id);
    void reset();
    std::optional<glm::vec3> destination(std::size_t id, glm::vec3 feet,
                                         glm::vec3 target, const CollisionWorld& world);

private:
    float bodyRadius_, bodyHeight_;
    struct Key {
        std::uint64_t source;
        std::size_t door;
        bool operator==(const Key& other) const { return source == other.source && door == other.door; }
    };
    struct Hash { std::size_t operator()(const Key& key) const; };
    struct Request {
        Key key;
        glm::vec3 feet;
        float entrySide, yieldSide;
        std::uint64_t step;
        double yieldUntil = 0;
        std::size_t slot;
    };
    struct Passage {
        DoorwayAperture opening;
        std::vector<std::size_t> members;
        std::optional<std::size_t> owner;
        bool committed = false;
        float bestProgress = 0;
        double progressSeconds = 0;
    };
    void reserve(Passage& passage, std::size_t owner);
    std::unordered_map<std::size_t, Request> requests_;
    std::unordered_map<Key, Passage, Hash> passages_;
    std::uint64_t step_ = 0;
    double seconds_ = 0;
};

} // namespace viewer
