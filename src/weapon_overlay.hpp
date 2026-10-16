#pragma once
#include <array>
#include <filesystem>
#include "opengl.hpp"

namespace viewer {
// Camera-local meshes, without collision or shadow casters. Draw after scene and
// debug geometry: this final overlay pass clears depth for weapon self-occlusion.
class WeaponOverlay {
public:
    WeaponOverlay(const std::filesystem::path& shaders, const std::filesystem::path& models);
    ~WeaponOverlay();
    WeaponOverlay(const WeaponOverlay&) = delete;
    WeaponOverlay& operator=(const WeaponOverlay&) = delete;
    void draw(std::size_t slot, int width, int height);
private:
    struct Range { GLint first = 0; GLsizei count = 0; };
    std::array<Range, 4> ranges_{};
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    GLint transform_ = -1;
};
} // namespace viewer
