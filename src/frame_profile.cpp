#include "frame_profile.hpp"
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace viewer {
FrameProfile::FrameProfile(const std::filesystem::path& path) : output_(path) {
    if (!output_) throw std::runtime_error("Cannot write frame profile: " + path.string());
    output_ << "frame,gpu_ms,cpu_render_ms,cpu_simulation_ms,cpu_preparation_ms,cpu_other_ms,cpu_work_ms,presentation_ms\n";
    output_ << std::setprecision(9);
    GLint bits = 0;
    glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
    supported_ = bits > 0;
    if (supported_)
        for (auto& slot : slots_) glGenQueries(2, slot.queries.data());
}
FrameProfile::~FrameProfile() {
    collect();
    for (auto& slot : slots_) {
        if (slot.pending) write(slot.frame, std::numeric_limits<double>::quiet_NaN());
        if (supported_) glDeleteQueries(2, slot.queries.data());
    }
}
void FrameProfile::beginGpu() {
    collect();
    active_ = slots_.size();
    if (!supported_) return;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].pending) continue;
        active_ = i;
        glQueryCounter(slots_[i].queries[0], GL_TIMESTAMP);
        break;
    }
}
void FrameProfile::endGpu() {
    if (active_ < slots_.size()) glQueryCounter(slots_[active_].queries[1], GL_TIMESTAMP);
}
void FrameProfile::record(const CpuFrameProfile& frame) {
    if (active_ == slots_.size()) write(frame, std::numeric_limits<double>::quiet_NaN());
    else {
        auto& slot = slots_[active_];
        slot.frame = frame;
        slot.pending = true;
        active_ = slots_.size();
    }
}
void FrameProfile::collect() {
    for (auto& slot : slots_) {
        if (!slot.pending) continue;
        GLint ready = GL_FALSE;
        glGetQueryObjectiv(slot.queries[1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) continue;
        GLuint64 start = 0, end = 0;
        glGetQueryObjectui64v(slot.queries[0], GL_QUERY_RESULT, &start);
        glGetQueryObjectui64v(slot.queries[1], GL_QUERY_RESULT, &end);
        write(slot.frame, static_cast<double>(end - start) / 1e6);
        slot.pending = false;
    }
}
std::size_t FrameProfile::pending() const {
    std::size_t count = 0;
    for (const auto& slot : slots_) if (slot.pending) ++count;
    return count;
}
void FrameProfile::write(const CpuFrameProfile& frame, double gpu) {
    const auto cpuWork = frame.render + frame.simulation + frame.preparation + frame.other;
    output_ << frame.frame << ',' << gpu << ',' << frame.render << ',' << frame.simulation << ','
            << frame.preparation << ',' << frame.other << ',' << cpuWork << ',' << frame.presentation << '\n';
}
} // namespace viewer
