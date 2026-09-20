#pragma once

#include "model.hpp"
#include "zombie_skeleton.hpp"

#include <array>
#include <cstdint>

namespace viewer {

// Two RGBA32F texture-buffer texels per vertex, in scene-world coordinates.
struct AnimationVertex {
    glm::vec4 position{0.0f};
    glm::vec4 normal{0.0f};
};

struct SkinningVertex {
    std::array<std::uint32_t, 8> joints{};
    std::array<float, 8> weights{};
};

struct SkeletonBone {
    std::string name;
    int parent = -1;
    glm::mat4 referenceWorld{1.0f};
    glm::mat4 inverseBind{1.0f};
};

struct AnimatedPrimitive {
    // Frame-major; every frame contains the corresponding primitive's vertices.
    std::vector<AnimationVertex> frames;
    // Empty for a non-skinned primitive. Joint indices refer to AnimatedModel::skeleton.
    std::vector<SkinningVertex> skinning;
};

struct AnimatedModel {
    // Flattened scene: one primitive and identity-transform draw per source draw.
    // Vertex positions/normals are bind-pose data for live skinning; baked animation
    // frames contain scene-world positions for the normal rendering path.
    Model model;
    std::vector<AnimatedPrimitive> animation;
    std::vector<SkeletonBone> skeleton;
    // Frame-major asset-space bone world transforms, before inverse binds.
    // Sampled at exactly the same times as animation's baked vertex frames.
    std::vector<glm::mat4> boneFrames;
    std::array<int, ragdollPartCount> ragdollBones = [] {
        std::array<int, ragdollPartCount> result{};
        result.fill(-1);
        return result;
    }();
    std::string clipName;
    float duration = 0.0f;
    std::size_t frameCount = 0;
};

// Bakes at 30 Hz in the original scene scale. Frame i samples i*duration/frameCount;
// the duplicate endpoint is excluded so rendering can interpolate and wrap.
// An empty clipName prefers "idle", then a name ending in a separated "idle"
// token, then a unique name containing "idle" (all case-insensitive).
// Throws a path-qualified error for unsupported/invalid data or an ambiguous clip.
AnimatedModel loadIdleModel(const std::filesystem::path& path,
                            const std::string& clipName = "");

// Match the displayed baked pose, including per-instance cycle phase. Linear
// matrix interpolation preserves the shader's interpolated vertex positions;
// interpolated rotations can consequently contain a small amount of shear/scale.
// Models without boneFrames return their referenceWorld transforms instead.
std::vector<glm::mat4> sampleAnimatedPose(const AnimatedModel& model,
                                        double seconds, float phase = 0);

}  // namespace viewer
