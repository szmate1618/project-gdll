#pragma once
#include <filesystem>
#include <GL/glcorearb.h>

namespace viewer {
// Camera-local grey cubes, without scene assets, collision, or shadow casters.
class WeaponOverlay {
public:
    explicit WeaponOverlay(const std::filesystem::path& shaders);
    ~WeaponOverlay();
    WeaponOverlay(const WeaponOverlay&) = delete;
    WeaponOverlay& operator=(const WeaponOverlay&) = delete;
    void draw(std::size_t slot, int width, int height);
private:
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    GLint transform_ = -1;
};
} // namespace viewer
