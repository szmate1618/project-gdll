#pragma once
#include "dog_state.hpp"
#include <vector>
namespace multiplayer {
class DogReplication {
public:
    std::optional<DogFrame> nextFrame(const viewer::DogLayer& dogs, std::uint64_t tick) const;
    void frameSent(const DogFrame& frame);
    void append(const viewer::DogLayer& dogs, glm::vec3 observer, std::uint64_t tick, std::vector<DogState>& samples);
private:
    struct Sent { std::uint64_t tick = 0; std::uint8_t mode = 0; float health = 1; bool initialized = false, interested = false, setupSent = false; };
    std::vector<Sent> sent_;
    struct Interest { std::size_t id; float distance; };
    struct Pending { std::size_t id; std::uint64_t deadline; std::size_t rank; std::uint8_t mode; };
    std::vector<Interest> interests_;
    std::vector<Pending> pending_;
};
}
