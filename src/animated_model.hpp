#pragma once

#include "model.hpp"

namespace viewer {

// Two RGBA32F texture-buffer texels per vertex, in scene-world coordinates.
struct AnimationVertex {
    glm::vec4 position{0.0f};
    glm::vec4 normal{0.0f};
};

struct AnimatedPrimitive {
    // Frame-major; every frame contains the corresponding primitive's vertices.
    std::vector<AnimationVertex> frames;
};

struct AnimatedModel {
    // Flattened scene: one primitive and identity-transform draw per source draw.
    // Vertex positions/normals contain frame zero; bounds cover all baked frames.
    Model model;
    std::vector<AnimatedPrimitive> animation;
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

}  // namespace viewer
