#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace viewer::boid {
// Millimetres, millimetres/second, and millimetres/second². Only boid motion
// uses these units. Division truncates toward zero; sqrt rounds downward.
// Integration carries signed remainders between exact 1/60-second steps.
constexpr std::int32_t unitsPerMeter = 1000;
constexpr std::int32_t ticksPerSecond = 60;
constexpr std::int32_t coordinateLimit = 100000000; // +/-100 km.

inline std::int32_t narrow(std::int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) throw std::overflow_error("Boid integer overflow");
    return static_cast<std::int32_t>(value);
}
struct Vec3 {
    std::int32_t x = 0, y = 0, z = 0;
};
inline bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator!=(Vec3 a, Vec3 b) { return !(a == b); }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {narrow(std::int64_t(a.x)+b.x), narrow(std::int64_t(a.y)+b.y), narrow(std::int64_t(a.z)+b.z)}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {narrow(std::int64_t(a.x)-b.x), narrow(std::int64_t(a.y)-b.y), narrow(std::int64_t(a.z)-b.z)}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a+b; return a; }
inline Vec3 ratio(Vec3 v, std::int64_t numerator, std::int64_t denominator) {
    // Callers use bounded ratios: products must fit int64, denominator > 0.
    if (denominator <= 0 || numerator < -2000000 || numerator > 2000000)
        throw std::invalid_argument("Invalid boid integer ratio");
    return {narrow(std::int64_t(v.x)*numerator/denominator), narrow(std::int64_t(v.y)*numerator/denominator), narrow(std::int64_t(v.z)*numerator/denominator)};
}
inline std::uint64_t lengthSquared(Vec3 v) {
    return std::uint64_t(std::int64_t(v.x)*v.x) + std::uint64_t(std::int64_t(v.y)*v.y) + std::uint64_t(std::int64_t(v.z)*v.z);
}
inline std::int64_t dot(Vec3 a, Vec3 b) {
    // Simulation coordinates and directions are validated below 100 km and
    // 1 m respectively before this attack-plane calculation.
    return std::int64_t(a.x)*b.x + std::int64_t(a.y)*b.y + std::int64_t(a.z)*b.z;
}
inline std::uint64_t integerSqrt(std::uint64_t n) {
    if (!n) return 0;
    auto v = n; unsigned bits = 0;
    if (v >> 32) { v >>= 32; bits += 32; }
    if (v >> 16) { v >>= 16; bits += 16; }
    if (v >> 8) { v >>= 8; bits += 8; }
    if (v >> 4) { v >>= 4; bits += 4; }
    if (v >> 2) { v >>= 2; bits += 2; }
    if (v >> 1) ++bits;
    auto root = std::uint64_t(1) << ((bits+2)/2);
    for (;;) {
        const auto next = (root+n/root)/2;
        if (next >= root) return root;
        root = next;
    }
}
inline Vec3 direction(Vec3 v, std::int32_t magnitude, Vec3 fallback = {0,0,1000}) {
    const auto length = integerSqrt(lengthSquared(v));
    return length ? ratio(v, magnitude, static_cast<std::int64_t>(length)) : ratio(fallback, magnitude, unitsPerMeter);
}
inline Vec3 limited(Vec3 v, std::int32_t maximum) {
    return lengthSquared(v) > std::uint64_t(maximum)*maximum ? direction(v,maximum) : v;
}
inline Vec3 integrate(Vec3 value, Vec3& remainder) {
    const auto total = value+remainder;
    const auto step = ratio(total,1,ticksPerSecond);
    remainder = total-ratio(step,ticksPerSecond,1);
    return step;
}
inline bool bounded(Vec3 v, std::int32_t limit) {
    return std::int64_t(v.x) >= -limit && std::int64_t(v.x) <= limit &&
           std::int64_t(v.y) >= -limit && std::int64_t(v.y) <= limit &&
           std::int64_t(v.z) >= -limit && std::int64_t(v.z) <= limit;
}
inline std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16);
}
inline Vec3 birdOffset(std::uint32_t seed, std::uint32_t index) {
    const auto s = hash(seed ^ index);
    Vec3 v{static_cast<std::int32_t>(hash(s+1)%9601)-4800,
           static_cast<std::int32_t>(hash(s+2)%4001)-2000,
           static_cast<std::int32_t>(hash(s+3)%9601)-4800};
    const auto horizontal = limited({v.x,0,v.z},4800);
    return {horizontal.x,v.y,horizontal.z};
}
} // namespace viewer::boid
