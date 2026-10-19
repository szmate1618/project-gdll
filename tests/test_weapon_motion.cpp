#include "weapon_motion.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool atRest(const viewer::WeaponMotionPose& p) {
    return p.offset == glm::vec3(0) && p.angles == glm::vec3(0) && p.pumpTravel == 0;
}
void recoilAndPump() {
    viewer::WeaponMotion motion;
    for (std::size_t slot = 0; slot < 5; ++slot) require(atRest(motion.pose(slot, 0)), "Idle weapon moved");
    for (std::size_t slot = 0; slot < 4; ++slot) {
        motion.fire(slot, 10);
        const auto kick = motion.pose(slot, 10), returning = motion.pose(slot, 10.08);
        require(kick.offset.z > .01f && kick.angles.x > 1, "Shot must move rearward and lift the barrel");
        require(returning.offset.z < kick.offset.z && returning.angles.x < kick.angles.x,
                "Recoil did not recover toward the ready pose");
        require(atRest(motion.pose(slot, 11)), "A stalled frame must skip directly to rest");
        const auto transform = viewer::animatedWeaponPose(glm::mat4(1), kick);
        require(std::abs(glm::determinant(glm::mat3(transform))-1) < 1e-5f,
                "Recoil changed weapon size or handedness");
        require((transform*glm::vec4(0,0,-1,0)).y > 0, "Positive pitch must lift the muzzle");
    }
    require(motion.pose(2,10).offset.z > motion.pose(0,10).offset.z &&
            motion.pose(0,10).offset.z > motion.pose(1,10).offset.z, "Weapon kick strengths must differ");
    require(atRest(motion.pose(1,10.2)) && !atRest(motion.pose(3,10.2)), "SMG and sniper recovery must differ");
    require(motion.pose(2,10.1).pumpTravel == 0, "Pump must wait for the initial kick");
    const float rear = motion.pose(2,10.36).pumpTravel;
    require(rear > .09f && rear < .10f && motion.pose(2,10.24).pumpTravel > 0 &&
            motion.pose(2,10.5).pumpTravel < rear, "Shotgun must pull back, then return its pump");
    require(motion.pose(2,10.8).pumpTravel == 0, "Pump must finish before the default next shot");
    require(motion.pose(0,10.36).pumpTravel == 0, "Other weapons must not pump");
    viewer::WeaponMotion automatic;
    for (int i=0;i<200;++i) automatic.fire(1, i*.08);
    const auto held = automatic.pose(1,199*.08);
    require(held.angles.x > motion.pose(1,10).angles.x && held.angles.x < 4,
            "Held SMG fire must accumulate a small, bounded kick");
    require(atRest(automatic.pose(0,199*.08)), "Recoil leaked into another weapon slot");
    require(atRest(automatic.pose(1,100)), "Automatic recoil did not settle after firing");
    motion.fire(4,20);
    require(atRest(motion.pose(4,20)), "Grenade throw must not trigger firearm recoil");
    motion.fire(0,std::numeric_limits<double>::quiet_NaN());
    motion.fire(0,9);
    require(atRest(motion.pose(0,9)) && atRest(motion.pose(0,11)), "Invalid or stale shot rewound motion");
    require(atRest(motion.pose(999,10)), "Unavailable weapon produced motion");
}

void scopeAim() {
    viewer::WeaponMotion motion;
    viewer::Camera input; input.setPosition({5,7,9}); input.setOrientation(-45,12);
    require(motion.aimCamera(input,3,1,true).view() == input.view(), "Hip aim must remain unchanged");
    input.cycleViewMode();
    const auto wide = motion.aimCamera(input,3,1,true);
    require(wide.view() != input.view(), "Scope has no sway");
    require(wide.position() == input.position() && wide.viewMode() == input.viewMode(),
            "Scope motion changed the eye or zoom mode");
    const float widePitch = std::abs(wide.pitchDegrees()-input.pitchDegrees());
    const float wideYaw = std::abs(wide.yawDegrees()-input.yawDegrees());
    require(widePitch > .12f && widePitch < .18f && wideYaw > .20f && wideYaw < .28f,
            "Scope sway must be pronounced while remaining bounded");
    require(motion.aimCamera(input,3,1,false).view() == input.view() &&
            motion.aimCamera(input,0,1,true).view() == input.view(), "Sway escaped scoped sniper combat");
    const auto center = wide.projection(1.5f)*wide.view()*glm::vec4(wide.position()+wide.direction()*100.0f,1);
    require(std::abs(center.x/center.w) < 1e-4f && std::abs(center.y/center.w) < 1e-4f,
            "Shot direction must project onto the scoped reticle");
    require(input.yawDegrees() == -45 && input.pitchDegrees() == 12, "Sway accumulated into mouse orientation");
    motion.fire(3,1);
    const auto kicked = motion.aimCamera(input,3,1,true);
    require(kicked.pitchDegrees() > wide.pitchDegrees()+.2f, "Scoped shot must show upward kick");
    require(motion.aimCamera(input,3,2,true).view() == viewer::WeaponMotion{}.aimCamera(input,3,2,true).view(),
            "Scoped recoil did not recover to the breathing motion");
    input.cycleViewMode();
    const auto narrow = viewer::WeaponMotion{}.aimCamera(input,3,1,true);
    const float narrowYaw = std::abs(narrow.yawDegrees()-input.yawDegrees());
    require(narrowYaw > .020f && narrowYaw < .028f,
            "High zoom must retain pronounced sway with comparable screen motion");
}
} // namespace
int main() {
    try { recoilAndPump(); scopeAim(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Firearm recoil, bounded automatic fire, pump timing and scoped aim passed\n";
}
