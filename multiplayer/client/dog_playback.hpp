#pragma once
#include "dog_state.hpp"
#include <optional>
#include <vector>
namespace multiplayer {
class DogPlayback {
public:
    void push(std::uint64_t tick, const std::vector<DogState>& states, double receivedSeconds);
    void apply(viewer::DogLayer& dogs, double serverTick, double now) const;
private:
    struct Sample { std::uint64_t tick; DogState state; };
    struct Replica { std::optional<Sample> previous, latest; double received = 0; };
    std::vector<Replica> replicas_;
};
}
