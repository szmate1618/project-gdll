#include "melee_sweep.hpp"
#include <box3d/collision.h>
#include <box3d/constants.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
namespace {
b3Vec3 b3(glm::vec3 p) { return {p.x, p.y, p.z}; }
glm::vec3 vec(b3Vec3 p) { return {p.x, p.y, p.z}; }
}
MeleeSweep meleeSweep(const glm::vec3* points, int count, const glm::mat4& from, const glm::mat4& to) {
    MeleeSweep result; result.count = count;
    result.bounds = {glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    for (int i = 0; i < count; ++i) {
        result.from[i] = glm::vec3(from * glm::vec4(points[i], 1));
        result.to[i] = glm::vec3(to * glm::vec4(points[i], 1));
        result.bounds.minimum = glm::min(result.bounds.minimum, glm::min(result.from[i], result.to[i]));
        result.bounds.maximum = glm::max(result.bounds.maximum, glm::max(result.from[i], result.to[i]));
    }
    return result;
}
MeleeCollider meleeCollider(const glm::vec3* points, int count, float radius, const glm::dmat4& frame) {
    MeleeCollider result; result.count = count; result.radius = radius;
    result.frame = frame; result.inverse = glm::inverse(frame);
    glm::dvec3 lo(std::numeric_limits<double>::max()), hi(std::numeric_limits<double>::lowest());
    // Exact axis extents of an affine-transformed sphere, including shear.
    glm::dvec3 extent;
    for (int axis = 0; axis < 3; ++axis)
        extent[axis] = radius * glm::length(glm::dvec3(frame[0][axis], frame[1][axis], frame[2][axis]));
    for (int i = 0; i < count; ++i) {
        result.points[i] = points[i];
        const auto p = glm::dvec3(frame * glm::dvec4(points[i], 1));
        lo = glm::min(lo, p - extent); hi = glm::max(hi, p + extent);
    }
    result.bounds = {glm::vec3(lo), glm::vec3(hi)};
    return result;
}
std::optional<MeleeSweepHit> castMeleeShape(const MeleeSweep& sweep, const MeleeCollider& target) {
    constexpr float tolerance = .0001f;
    if (glm::any(glm::greaterThan(sweep.bounds.minimum, target.bounds.maximum + glm::vec3(tolerance))) ||
        glm::any(glm::lessThan(sweep.bounds.maximum, target.bounds.minimum - glm::vec3(tolerance)))) return {};
    std::array<b3Vec3, 3> fixed;
    std::array<b3Vec3, 8> moving;
    std::array<glm::vec3, 8> start, delta;
    for (int i = 0; i < target.count; ++i) fixed[i] = b3(target.points[i]);
    for (int i = 0; i < sweep.count; ++i) {
        start[i] = glm::vec3(target.inverse * glm::dvec4(sweep.from[i], 1));
        const auto end = glm::vec3(target.inverse * glm::dvec4(sweep.to[i], 1));
        delta[i] = end - start[i];
    }
    b3DistanceInput input{};
    input.proxyA = {fixed.data(), target.count, target.radius};
    input.proxyB = {moving.data(), sweep.count, 0};
    input.transform = b3Transform_identity;
    input.useRadii = true;
    b3SimplexCache cache{};
    float fraction = 0;
    glm::vec3 translation(0);
    for (int i = 0; i < sweep.count; ++i) {
        moving[i] = b3(start[i]);
        translation += delta[i] / float(sweep.count);
    }
    // A translated hull inflated by the maximum angular vertex displacement
    // encloses the moving shape. Box3D's built-in cast gives a conservative
    // starting time; the distance iterations below resolve the actual volume.
    float angularPadding = 2 * B3_LINEAR_SLOP;
    for (int i = 0; i < sweep.count; ++i)
        angularPadding = std::max(angularPadding, glm::length(delta[i] - translation) + 2 * B3_LINEAR_SLOP);
    const auto initial = b3ShapeDistance(&input, &cache, nullptr, 0);
    if (initial.distance > angularPadding + B3_LINEAR_SLOP) {
        b3ShapeCastPairInput cast{};
        cast.proxyA = input.proxyA; cast.proxyB = input.proxyB;
        cast.proxyB.radius = angularPadding;
        cast.transform = input.transform; cast.translationB = b3(translation); cast.maxFraction = 1;
        const auto coarse = b3ShapeCast(&cast);
        if (!coarse.hit) return {};
        fraction = coarse.fraction;
    }
    // Conservative advancement of the rotating/deforming point cloud. A
    // separating plane stays clear until its fastest approaching vertex arrives.
    // Unlike a translation-only cast, this includes every vertex's angular motion.
    for (int iteration = 0; iteration < 32; ++iteration) {
        for (int i = 0; i < sweep.count; ++i) moving[i] = b3(start[i] + fraction * delta[i]);
        const auto distance = b3ShapeDistance(&input, &cache, nullptr, 0);
        if (distance.distance <= tolerance)
            return MeleeSweepHit{fraction, glm::vec3(target.frame * glm::dvec4(vec(distance.pointA), 1))};
        float approach = 0;
        for (int i = 0; i < sweep.count; ++i)
            approach = std::max(approach, -glm::dot(vec(distance.normal), delta[i]));
        if (approach <= 1e-9f) return {};
        const float advance = (distance.distance - tolerance * .5f) / approach;
        if (fraction + advance > 1) return {};
        const float next = fraction + advance;
        if (next <= fraction) return {};
        fraction = next;
    }
    return {};
}
} // namespace viewer
