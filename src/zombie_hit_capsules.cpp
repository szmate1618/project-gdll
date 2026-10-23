#include "zombie_hit_capsules.hpp"
#include "animated_model.hpp"
#include "file_fingerprint.hpp"

#include <json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace viewer {
ZombieCapsuleAsset loadZombieHitCapsules(const std::filesystem::path& basePath, const AnimatedModel& asset) {
    auto path = basePath; path.replace_extension(".capsules.json");
    if (!std::filesystem::exists(path)) return {};
    try {
        std::ifstream input(path); nlohmann::json document; input >> document;
        if (document.at("schema") != "godollo.zombie_bone_capsules" || document.at("version") != 1 ||
            document.at("units") != "meters" || document.at("space") != "bone_reference_meters" ||
            document.at("source_sha256") != fileSha256(basePath))
            throw std::runtime_error("Unsupported or stale bone capsule manifest");
        const auto& bones = document.at("bones");
        if (!bones.is_array() || bones.size() != asset.skeleton.size())
            throw std::runtime_error("Capsule manifest must describe every skeleton bone");
        ZombieCapsuleAsset result;
        std::set<int> seen;
        const int head = asset.ragdollBones[std::size_t(RagdollPart::head)];
        for (const auto& entry : bones) {
            ZombieBoneCapsule capsule;
            const auto name = entry.at("bone").get<std::string>();
            for (std::size_t i = 0; i < asset.skeleton.size(); ++i)
                if (asset.skeleton[i].name == name) capsule.bone = static_cast<int>(i);
            if (capsule.bone < 0 || !seen.insert(capsule.bone).second)
                throw std::runtime_error("Unknown or duplicate capsule bone");
            if (!entry.at("enabled").get<bool>()) continue;
            if (entry.at("shape") != "capsule") throw std::runtime_error("Unsupported bone collider shape");
            const auto point = [&](const char* key) {
                const auto values = entry.at(key).get<std::array<float, 3>>();
                for (const auto v : values)
                    if (!std::isfinite(v) || std::abs(v) > 3) throw std::runtime_error("Invalid capsule endpoint");
                return glm::vec3(values[0], values[1], values[2]);
            };
            capsule.a = point("a"); capsule.b = point("b");
            capsule.radius = entry.at("radius").get<float>();
            if (!std::isfinite(capsule.radius) || capsule.radius <= 0 || capsule.radius > 1)
                throw std::runtime_error("Invalid capsule radius");
            const auto& reference = asset.skeleton[std::size_t(capsule.bone)].referenceWorld;
            const float determinant = glm::determinant(reference);
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-20f)
                throw std::runtime_error("Invalid capsule reference transform");
            auto meters = reference;
            for (int c = 0; c < 3; ++c) {
                const float length = glm::length(glm::vec3(reference[c]));
                if (!std::isfinite(length) || length <= 1e-8f) throw std::runtime_error("Invalid capsule bone scale");
                meters[c] /= length;
            }
            capsule.boneFromMeters = glm::inverse(reference) * meters;
            for (int bone = capsule.bone, depth = 0; bone >= 0 && depth < int(asset.skeleton.size()); ++depth) {
                if (std::size_t(bone) >= asset.skeleton.size()) throw std::runtime_error("Invalid capsule bone ancestry");
                if (bone == head) { capsule.headshot = true; break; }
                bone = asset.skeleton[std::size_t(bone)].parent;
            }
            result.capsules.push_back(capsule);
        }
        if (result.capsules.empty()) throw std::runtime_error("Capsule manifest has no enabled surface colliders");
        result.resources.push_back(path);
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

bool zombieCapsuleRay(glm::vec3 origin, glm::vec3 direction, const glm::mat4& instance,
                      const ZombieBoneCapsule& capsule, const std::vector<glm::mat4>& pose, float& distance) {
    const auto inverse = glm::inverse(glm::dmat4(instance) * glm::dmat4(pose.at(std::size_t(capsule.bone))) *
                                      glm::dmat4(capsule.boneFromMeters));
    const auto o = glm::dvec3(inverse * glm::dvec4(origin, 1));
    const auto d = glm::dvec3(inverse * glm::dvec4(direction, 0));
    const double rayLength = glm::dot(d, d);
    if (!std::isfinite(rayLength) || rayLength <= 0) return false;
    const auto a = glm::dvec3(capsule.a), b = glm::dvec3(capsule.b);
    const auto segment = b - a;
    const double lengthSquared = glm::dot(segment, segment);
    const auto closest = a + segment * (lengthSquared > 0 ? std::clamp(glm::dot(o-a, segment) / lengthSquared, 0.0, 1.0) : 0);
    const double radiusSquared = double(capsule.radius) * capsule.radius;
    if (glm::dot(o-closest, o-closest) <= radiusSquared) { distance = 0; return true; }
    double nearest = std::numeric_limits<double>::infinity();
    const auto sphere = [&](glm::dvec3 center) {
        const auto offset = center - o;
        const double projection = glm::dot(offset, d) / rayLength;
        const auto perpendicular = offset - projection * d;
        const double square = radiusSquared - glm::dot(perpendicular, perpendicular);
        if (square < 0) return;
        const double root = std::sqrt(square / rayLength);
        for (const double t : {projection-root, projection+root}) if (t >= 0) nearest = std::min(nearest, t);
    };
    sphere(a);
    if (lengthSquared > 1e-16) {
        sphere(b);
        const double length = std::sqrt(lengthSquared);
        const auto axis = segment / length;
        const double axialOrigin = glm::dot(o-a, axis), axialDirection = glm::dot(d, axis);
        const auto perpendicularOrigin = o-a-axis*axialOrigin;
        const auto perpendicularDirection = d-axis*axialDirection;
        const double squareDirection = glm::dot(perpendicularDirection, perpendicularDirection);
        if (squareDirection > 1e-20) {
            const double projection = -glm::dot(perpendicularOrigin, perpendicularDirection) / squareDirection;
            const auto perpendicular = perpendicularOrigin + projection * perpendicularDirection;
            const double square = radiusSquared - glm::dot(perpendicular, perpendicular);
            if (square >= 0) {
                const double root = std::sqrt(square / squareDirection);
                for (const double t : {projection-root, projection+root}) {
                    const double axial = axialOrigin + t*axialDirection;
                    if (t >= 0 && axial >= 0 && axial <= length) nearest = std::min(nearest, t);
                }
            }
        }
    }
    distance = static_cast<float>(nearest);
    return std::isfinite(distance);
}
} // namespace viewer
