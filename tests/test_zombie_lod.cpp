#include "zombie_lod.hpp"
#include "zombie_animation.hpp"
#include "zombie_layer.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        constexpr float normalProjection = 1.7320508f;
        require(viewer::zombieCameraDetail(5, normalProjection).level == 0, "Close zombie must retain original mesh");
        require(viewer::zombieCameraDetail(20, normalProjection).level == 1, "Medium zombie must use LOD1");
        require(viewer::zombieCameraDetail(60, normalProjection).level == 2, "Distant zombie must use LOD2");
        require(viewer::zombieCameraDetail(120, normalProjection).animationHz == 5, "Far animation must use 5 Hz");
        require(!viewer::zombieCameraDetail(200, normalProjection).visible, "Far camera zombie must be culled");
        require(viewer::zombieCameraDetail(1000, normalProjection, false).visible, "Comparison must retain full crowd");
        require(viewer::zombieCameraDetail(1000, normalProjection, true, 0).visible, "Zero cutoff disables distance removal");
        require(viewer::zombieCameraDetail(300, 114.59f).level == 0, "One-degree scope must retain distant detail");
        viewer::AnimatedModel asset;
        asset.frameCount = 2; asset.duration = 1; asset.skeleton.resize(1);
        asset.boneFrames = {glm::mat4(1), glm::mat4(1)};
        asset.boneFrames[1][3].y = 3;
        viewer::ZombieInstance instance;
        instance.displayedAnimationFrame = 1;
        auto pose = viewer::sampleZombiePose(asset, instance, 0.25);
        require(pose[0][3].y == 3, "Hit handoff must use published stepped pose, not current continuous time");
        instance.displayedAnimationFrame = 0.5;
        pose = viewer::sampleZombiePose(asset, instance, 0);
        require(pose[0][3].y == 1.5f, "Published close pose must retain interpolation");
        asset.walkClip = viewer::BakedAnimationClip{"walk", 1, 2, 2, asset.boneFrames};
        asset.walkClip->boneFrames[1][3].y = 7;
        instance.displayedAnimationFrame = 1; instance.displayedAnimationClip = 1;
        require(viewer::sampleZombiePose(asset, instance, 0)[0][3].y == 7, "Handoff must retain last displayed clip");
        std::cout << "Zombie distance and displayed pose tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
