#include "gpu_timer.hpp"

namespace viewer {

GpuTimer::GpuTimer() {
    GLint counterBits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &counterBits);
    supported_ = counterBits > 0;
    if (supported_) glGenQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
}

GpuTimer::~GpuTimer() {
    // Also handles scope exit if drawing throws between begin() and end().
    if (active_ != queryCount) glEndQuery(GL_TIME_ELAPSED);
    if (supported_) glDeleteQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
}

void GpuTimer::begin() {
    if (!supported_ || active_ != queryCount) return;
    collect();
    for (std::size_t offset = 0; offset < queryCount; ++offset) {
        const std::size_t index = (next_ + offset) % queryCount;
        if (pending_[index]) continue;
        glBeginQuery(GL_TIME_ELAPSED, queries_[index]);
        active_ = index;
        next_ = (index + 1) % queryCount;
        return;
    }
}

void GpuTimer::end() {
    if (active_ == queryCount) return;
    glEndQuery(GL_TIME_ELAPSED);
    pending_[active_] = true;
    active_ = queryCount;
}

void GpuTimer::collect() {
    if (!supported_) return;
    for (std::size_t index = 0; index < queryCount; ++index) {
        if (!pending_[index]) continue;
        GLint available = GL_FALSE;
        glGetQueryObjectiv(queries_[index], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_FALSE) continue;
        GLuint64 nanoseconds = 0;
        // GL_QUERY_RESULT can block unless availability has already been checked.
        glGetQueryObjectui64v(queries_[index], GL_QUERY_RESULT, &nanoseconds);
        totalNanoseconds_ += static_cast<double>(nanoseconds);
        ++samples_;
        pending_[index] = false;
    }
}

std::optional<double> GpuTimer::takeAverageMilliseconds() {
    collect();
    if (samples_ == 0) return std::nullopt;
    const double milliseconds = totalNanoseconds_ / static_cast<double>(samples_) / 1.0e6;
    totalNanoseconds_ = 0.0;
    samples_ = 0;
    return milliseconds;
}

} // namespace viewer
