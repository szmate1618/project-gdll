#include "player_interaction.hpp"

namespace viewer {
void PlayerInteraction::toggleMode() { objects_.cancel(); doors_.toggleMode(); }
void PlayerInteraction::cancel() { objects_.cancel(); doors_.release(); }
void PlayerInteraction::release(Hand hand, double seconds) {
    objects_.release(hand, seconds);
    if (hand == Hand::Left) doors_.release();
}
bool PlayerInteraction::press(Hand hand, const Camera& camera, DoorLayer& doors, BoxLayer& boxes,
                              const CollisionWorld& world, double seconds) {
    if (mode() != ControlMode::Interaction) return false;
    if (hand == Hand::Left) doors_.release();
    const auto box = boxes.raycast(camera.position(), camera.direction(), 2);
    const auto door = hand == Hand::Left ? doors.raycast(camera.position(), camera.direction(), 1.5f) : std::nullopt;
    if (door && (!box || door->distance <= box->distance)) {
        // Clear only the left prop grip, leaving a right-hand object attached.
        // Replacing a grip must not add a throw boost.
        objects_.cancel(hand);
        return doors_.press(camera, doors, world);
    }
    return objects_.press(hand, camera, boxes, world, seconds);
}
void PlayerInteraction::look(Camera& camera, float dx, float dy, double seconds) {
    objects_.moveMouse(dx, dy, seconds);
    if (doors_.grabbing()) doors_.look(camera, dx, dy);
    else camera.look(dx, dy);
}
void PlayerInteraction::update(const Camera& camera, float dt) {
    doors_.update(camera, dt);
    objects_.update(camera, dt);
}
} // namespace viewer
