#include "gpu_timer.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Query {
    bool pending = false;
    bool ready = false;
    GLuint64 nanoseconds = 0;
};

struct FakeGL {
    GLint counterBits = 64;
    std::unordered_map<GLuint, Query> queries;
    std::vector<GLuint> started;
    GLuint active = 0;
    GLuint nextId = 1;
    int ended = 0;
    int read = 0;
    int deleted = 0;
} fake;

void finish(GLuint id, GLuint64 nanoseconds) {
    auto& query = fake.queries.at(id);
    require(query.pending, "Only a submitted query can finish");
    query.ready = true;
    query.nanoseconds = nanoseconds;
}

void requireAverage(std::optional<double> value, double expected) {
    require(value.has_value() && std::abs(*value - expected) < 1.0e-9,
            "Incorrect sample average or nanosecond-to-millisecond conversion");
}

void testUnavailable() {
    fake = {};
    fake.counterBits = 0;
    {
        viewer::GpuTimer timer;
        require(!timer.supported(), "Zero timer bits must report unsupported timing");
        timer.begin();
        timer.end();
        timer.collect();
        require(!timer.takeAverageMilliseconds(), "Unsupported timing produced a sample");
    }
    require(fake.queries.empty() && fake.started.empty() && fake.deleted == 0,
            "Unsupported timing allocated or used queries");
}

void testDelayedResultsAndAverages() {
    fake = {};
    {
        viewer::GpuTimer timer;
        require(timer.supported(), "Supported timing reported unavailable");
        require(!timer.takeAverageMilliseconds(), "Empty timer returned a measurement");
        for (int index = 0; index < 3; ++index) {
            timer.begin();
            timer.end();
        }
        timer.collect();
        require(fake.read == 0 && !timer.takeAverageMilliseconds(),
                "Unavailable GPU results must not be read");

        // A ready result must be collectable even if an earlier one is pending.
        finish(fake.started[2], 3000000);
        requireAverage(timer.takeAverageMilliseconds(), 3.0);
        require(!timer.takeAverageMilliseconds(), "A consumed result was counted twice");
        finish(fake.started[0], 1000000);
        finish(fake.started[1], 2000000);
        requireAverage(timer.takeAverageMilliseconds(), 1.5);

        timer.begin();
        timer.end();
        finish(fake.started.back(), 0);
        requireAverage(timer.takeAverageMilliseconds(), 0.0);

        timer.begin();
        timer.end();
        finish(fake.started.back(), 6000000000ULL);
        timer.begin();
        timer.end();
        finish(fake.started.back(), 7000000000ULL);
        requireAverage(timer.takeAverageMilliseconds(), 6500.0);
    }
    require(fake.queries.empty() && fake.active == 0,
            "Timer destruction left query objects or an active measurement");
}

void testSaturationAndReuse() {
    fake = {};
    {
        viewer::GpuTimer timer;
        const auto capacity = fake.queries.size();
        require(capacity > 1, "Timer needs multiple query slots");
        for (std::size_t index = 0; index < capacity; ++index) {
            timer.begin();
            timer.end();
        }
        for (int index = 0; index < 20; ++index) {
            timer.begin();
            timer.end();
        }
        require(fake.started.size() == capacity && fake.read == 0,
                "Saturated timer waited for a result or reused a pending query");

        const GLuint released = fake.started.front();
        finish(released, 4000000);
        timer.begin();
        timer.end();
        require(fake.started.size() == capacity + 1 && fake.started.back() == released,
                "A completed query slot was not reused");
        requireAverage(timer.takeAverageMilliseconds(), 4.0);
        require(!timer.takeAverageMilliseconds(), "Reused pending slot duplicated its old result");
    }
    require(fake.queries.empty(), "Pending queries were not deleted during cleanup");
}

void testActiveCleanup() {
    fake = {};
    {
        viewer::GpuTimer timer;
        timer.end();
        require(fake.ended == 0, "Ending a skipped measurement touched OpenGL");
        timer.begin();
        timer.begin();
        require(fake.started.size() == 1, "Nested begin started another elapsed query");
    }
    require(fake.ended == 1 && fake.active == 0 && fake.queries.empty(),
            "Cleanup failed to end and delete an active query");
}

} // namespace

// Link-time fake OpenGL entry points make synchronization mistakes fail without
// a GPU: reading an unavailable result or reusing a pending slot is forbidden.
extern "C" {
void APIENTRY glGetQueryiv(GLenum target, GLenum name, GLint* value) {
    require(target == GL_TIME_ELAPSED && name == GL_QUERY_COUNTER_BITS,
            "Unexpected timer capability query");
    *value = fake.counterBits;
}

void APIENTRY glGenQueries(GLsizei count, GLuint* ids) {
    for (GLsizei index = 0; index < count; ++index) {
        ids[index] = fake.nextId++;
        fake.queries.emplace(ids[index], Query{});
    }
}

void APIENTRY glDeleteQueries(GLsizei count, const GLuint* ids) {
    for (GLsizei index = 0; index < count; ++index) {
        require(fake.active != ids[index], "Deleting an active elapsed query");
        require(fake.queries.erase(ids[index]) == 1, "Deleting a query twice");
        ++fake.deleted;
    }
}

void APIENTRY glBeginQuery(GLenum target, GLuint id) {
    require(target == GL_TIME_ELAPSED && fake.active == 0, "Invalid elapsed query begin");
    auto& query = fake.queries.at(id);
    require(!query.pending, "Reusing a query whose result has not been collected");
    query = {};
    fake.active = id;
    fake.started.push_back(id);
}

void APIENTRY glEndQuery(GLenum target) {
    require(target == GL_TIME_ELAPSED && fake.active != 0, "Invalid elapsed query end");
    fake.queries.at(fake.active).pending = true;
    fake.active = 0;
    ++fake.ended;
}

void APIENTRY glGetQueryObjectiv(GLuint id, GLenum name, GLint* value) {
    require(name == GL_QUERY_RESULT_AVAILABLE, "Only nonblocking availability checks are allowed");
    const auto& query = fake.queries.at(id);
    require(query.pending && fake.active != id, "Checking an unsubmitted or active query");
    *value = query.ready ? GL_TRUE : GL_FALSE;
}

void APIENTRY glGetQueryObjectui64v(GLuint id, GLenum name, GLuint64* value) {
    auto& query = fake.queries.at(id);
    require(name == GL_QUERY_RESULT && query.pending && query.ready,
            "Reading an unavailable result would block the rendering thread");
    *value = query.nanoseconds;
    query.pending = false;
    ++fake.read;
}
}

int main() {
    try {
        testUnavailable();
        testDelayedResultsAndAverages();
        testSaturationAndReuse();
        testActiveCleanup();
        std::cout << "GPU timer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GPU timer test failed: " << error.what() << '\n';
        return 1;
    }
}
