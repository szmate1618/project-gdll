#pragma once

#include <GL/glcorearb.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace viewer {

struct ZombiePaletteStats {
    std::size_t needed = 0, uploaded = 0, uploadCalls = 0, uploadBytes = 0;
    std::size_t resident = 0, storageBytes = 0;
};

// One fixed-size palette slot per resident corpse in an asset batch. Only the
// camera/shadow union requests slots. Hidden slots retain their last GPU pose.
// All methods run on the render thread with its GL context current.
class ZombiePaletteCache {
public:
    ZombiePaletteCache(std::size_t instances, std::size_t bones, std::size_t maximumTexels);
    ~ZombiePaletteCache();
    ZombiePaletteCache(const ZombiePaletteCache&) = delete;
    ZombiePaletteCache& operator=(const ZombiePaletteCache&) = delete;
    void release(std::size_t instance);
    void beginFrame();
    // Borrows palette until flush() in the same frame; returns a matrix offset.
    float request(std::size_t instance, std::uint64_t revision, const std::vector<glm::mat4>& palette);
    void flush();
    GLuint texture() const { return texture_; }
    const ZombiePaletteStats& stats() const { return stats_; }

private:
    static constexpr std::size_t noSlot = static_cast<std::size_t>(-1);
    struct Slot {
        bool uploaded = false;
        std::uint64_t revision = 0;
        const std::vector<glm::mat4>* pending = nullptr;
        std::uint64_t pendingRevision = 0;
    };
    void grow(std::size_t required);
    std::size_t bones_, maximumSlots_, capacity_ = 0;
    GLuint buffer_ = 0, texture_ = 0;
    std::vector<std::size_t> instanceSlots_, freeSlots_, dirtySlots_;
    std::vector<Slot> slots_;
    std::vector<glm::mat4> scratch_;
    ZombiePaletteStats stats_;
};

} // namespace viewer
