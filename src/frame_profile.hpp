#pragma once
#include "opengl.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace viewer {
struct CpuFrameProfile {
    std::uint64_t frame = 0;
    double render = 0, simulation = 0, preparation = 0, other = 0, presentation = 0;
};
// Optional per-frame CSV. Timestamp results are read only when available; busy
// slots skip GPU measurements. CPU rows are emitted even if GPU timing is absent.
class FrameProfile {
public:
    explicit FrameProfile(const std::filesystem::path& path);
    ~FrameProfile();
    FrameProfile(const FrameProfile&) = delete;
    FrameProfile& operator=(const FrameProfile&) = delete;
    void beginGpu();
    void endGpu();
    void record(const CpuFrameProfile& frame);
    void collect();
    std::size_t pending() const;
private:
    struct Slot {
        std::array<GLuint, 2> queries{};
        CpuFrameProfile frame;
        bool pending = false;
    };
    void write(const CpuFrameProfile& frame, double gpu);
    std::array<Slot, 16> slots_{};
    std::ofstream output_;
    std::size_t active_ = slots_.size();
    bool supported_ = false;
};
} // namespace viewer
