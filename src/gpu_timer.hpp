#pragma once

#include <GL/glcorearb.h>
#include <array>
#include <cstddef>
#include <optional>

namespace viewer {

// Owns GL queries; construct and destroy while the rendering context is current.
// Results arrive asynchronously. Busy query slots cause a skipped measurement,
// never a wait for the GPU.
class GpuTimer {
public:
    GpuTimer();
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    bool supported() const { return supported_; }
    void begin();
    void end();
    void collect();
    std::optional<double> takeAverageMilliseconds();

private:
    static constexpr std::size_t queryCount = 8;
    std::array<GLuint, queryCount> queries_{};
    std::array<bool, queryCount> pending_{};
    std::size_t active_ = queryCount;
    std::size_t next_ = 0;
    double totalNanoseconds_ = 0.0;
    std::size_t samples_ = 0;
    bool supported_ = false;
};

} // namespace viewer
