#pragma once
#include "opengl.hpp"
#include "player_body_pose.hpp"
#include <array>
#include <filesystem>
#include <glm/glm.hpp>

namespace viewer {
class PlayerBody;
// World-space physical body with depth-tested arm trails. Owns GPU resources;
// borrow the PlayerBody only while sampling/drawing. Does not clear scene depth.
class PlayerBodyRenderer {
public:
    explicit PlayerBodyRenderer(const std::filesystem::path& shaders);
    ~PlayerBodyRenderer();
    PlayerBodyRenderer(const PlayerBodyRenderer&) = delete;
    PlayerBodyRenderer& operator=(const PlayerBodyRenderer&) = delete;
    void captureBody(const PlayerBody& body, float seconds);
    void drawBody(const PlayerBody& body, const glm::mat4& projectionView, int width, int height);
    void capturePose(const PlayerBodyPose& pose, float seconds);
    // Delay trail samples to match an interpolated pose's simulation timestamp.
    void drawPose(const PlayerBodyPose& pose, const glm::mat4& projectionView, int width, int height,
                  float secondsBehindNewest = 0);
private:
    void drawBodyTrail(const PlayerBodyPose& pose, const glm::mat4& projectionView, float secondsBehindNewest);
    struct BodySample {
        std::array<glm::mat4, 6> arms{}; // Left upper/lower/fist, then right.
        glm::vec3 hip{0};
        double time = 0;
    };
    std::array<BodySample, 4> bodyHistory_{}; // 50 ms covers the 32 ms shutter.
    std::size_t bodyHistoryCount_ = 0;
    double bodyTime_ = 0;
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    GLsizei vertexCount_ = 0;
    GLint transform_ = -1, opacity_ = -1;
};
} // namespace viewer
