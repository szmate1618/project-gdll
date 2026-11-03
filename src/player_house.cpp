#include "player_house.hpp"
#include <cmath>

namespace viewer {
void PlayerHouseState::update(glm::vec3 feet, double seconds, const BuildingFootprints& buildings) {
    if (!std::isfinite(seconds) || seconds < 0) return;
    if (checkedSeconds_ && seconds >= *checkedSeconds_ && seconds - *checkedSeconds_ < 3.) return;
    house_ = buildings.find(feet);
    checkedSeconds_ = seconds;
}
} // namespace viewer
