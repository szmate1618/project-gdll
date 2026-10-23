#include "zombie_dismemberment.hpp"
#include "file_fingerprint.hpp"

#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <random>
#include <stdexcept>

namespace viewer {
namespace {
std::mt19937& severRandom() {
    thread_local std::mt19937 random(std::random_device{}());
    return random;
}
}
glm::vec3 zombieSeverImpulse(ShotCaliber caliber, glm::vec3 direction) {
    std::uniform_real_distribution<float> magnitude(caliber == ShotCaliber::high ? 5.0f : 2.0f,
                                                 caliber == ShotCaliber::high ? 8.0f : 4.0f);
    return direction * magnitude(severRandom());
}
glm::vec3 zombieSeverSpin() {
    std::uniform_real_distribution<float> unit(0, 1), speed(1, 4);
    const float y = 2 * unit(severRandom()) - 1;
    const float angle = 6.28318530718f * unit(severRandom());
    const float radius = std::sqrt(std::max(0.0f, 1 - y * y));
    return glm::vec3(radius * std::cos(angle), y, radius * std::sin(angle)) * speed(severRandom());
}
namespace {
void validateRig(const AnimatedModel& base, const AnimatedModel& mesh) {
    if (base.skeleton.size() != mesh.skeleton.size() || base.ragdollBones != mesh.ragdollBones)
        throw std::runtime_error("Dismemberment rig does not match original");
    for (std::size_t b = 0; b < base.skeleton.size(); ++b) {
        const auto& a = base.skeleton[b]; const auto& other = mesh.skeleton[b];
        if (a.name != other.name || a.parent != other.parent)
            throw std::runtime_error("Dismemberment bone order does not match original");
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (std::abs(a.inverseBind[c][r] - other.inverseBind[c][r]) > 1e-5f ||
                std::abs(a.referenceWorld[c][r] - other.referenceWorld[c][r]) > 1e-5f)
                throw std::runtime_error("Dismemberment bone frames do not match original");
    }
}
}

ZombieDismembermentAsset loadZombieDismemberment(const std::filesystem::path& basePath, const AnimatedModel& base) {
    auto manifest = basePath; manifest.replace_extension(".hitboxes.json");
    if (!std::filesystem::exists(manifest)) return {};
    try {
        std::ifstream input(manifest); nlohmann::json document; input >> document;
        if (document.at("schema") != "godollo.zombie_joint_hits" || document.at("version") != 1 ||
            document.at("units") != "meters" || document.at("source_sha256") != fileSha256(basePath))
            throw std::runtime_error("Unsupported or stale joint-hit manifest");
        ZombieDismembermentAsset result;
        result.resources.push_back(manifest);
        const auto& joints = document.at("joints");
        if (!joints.is_array() || joints.empty() || joints.size() >= ragdollPartCount)
            throw std::runtime_error("Invalid joint-hit count");
        std::set<int> parts;
        const auto root = std::filesystem::weakly_canonical(basePath.parent_path());
        for (const auto& entry : joints) {
            ZombieJointCollider joint;
            joint.name = entry.at("name").get<std::string>();
            const int part = entry.at("ragdoll_part").get<int>();
            if (joint.name.empty() || part <= 0 || part >= int(ragdollPartCount) || !parts.insert(part).second)
                throw std::runtime_error("Invalid or duplicate severed ragdoll part");
            joint.part = static_cast<RagdollPart>(part);
            const auto name = entry.at("bone").get<std::string>();
            for (std::size_t b = 0; b < base.skeleton.size(); ++b)
                if (base.skeleton[b].name == name) joint.bone = static_cast<int>(b);
            if (joint.bone < 0 || base.ragdollBones[std::size_t(part)] != joint.bone)
                throw std::runtime_error("Joint collider must attach to its severable ragdoll bone");
            if (entry.at("shape") != "sphere") throw std::runtime_error("Unsupported joint collider shape");
            const auto center = entry.at("center").get<std::array<float, 3>>();
            joint.center = {center[0], center[1], center[2]};
            joint.radius = entry.at("radius").get<float>();
            if (!std::isfinite(joint.radius) || joint.radius <= 0 || joint.radius > .5f)
                throw std::runtime_error("Invalid joint collider radius");
            for (const float v : center) if (!std::isfinite(v)) throw std::runtime_error("Invalid joint collider center");
            auto reference = base.skeleton[std::size_t(joint.bone)].referenceWorld;
            auto meters = reference;
            for (int c = 0; c < 3; ++c) {
                const float length = glm::length(glm::vec3(reference[c]));
                if (!std::isfinite(length) || length <= 1e-8f) throw std::runtime_error("Invalid joint bone scale");
                meters[c] /= length;
            }
            joint.boneFromMeters = glm::inverse(reference) * meters;
            const auto relative = std::filesystem::path(entry.at("mesh").get<std::string>());
            const auto path = std::filesystem::weakly_canonical(root / relative);
            const auto tail = path.lexically_relative(root);
            if (relative.is_absolute() || tail.empty() || *tail.begin() == ".." || path.extension() != ".glb")
                throw std::runtime_error("Severed mesh path leaves the asset directory");
            if (fileSha256(path) != entry.at("mesh_sha256")) throw std::runtime_error("Severed mesh fingerprint mismatch");
            auto mesh = loadIdleModel(path, "Severed");
            validateRig(base, mesh);
            result.resources.push_back(path);
            result.joints.push_back(std::move(joint));
            result.meshes.push_back(std::move(mesh));
        }
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(manifest.string() + ": " + error.what());
    }
}

glm::mat4 zombieJointTransform(const ZombieJointCollider& joint, const std::vector<glm::mat4>& pose) {
    return pose.at(std::size_t(joint.bone)) * joint.boneFromMeters;
}

bool zombieJointRay(glm::vec3 origin, glm::vec3 direction, const glm::mat4& instance,
                    const ZombieJointCollider& joint, const std::vector<glm::mat4>& pose, float& distance) {
    const auto inverse = glm::inverse(instance * zombieJointTransform(joint, pose));
    const auto offset = glm::dvec3(inverse * glm::vec4(origin, 1)) - glm::dvec3(joint.center);
    // Do not normalize: the ray parameter must remain world meters, even with affine scale.
    const auto ray = glm::dvec3(inverse * glm::vec4(direction, 0));
    const double a = glm::dot(ray, ray);
    if (!std::isfinite(a) || a <= 0) return false;
    const double projection = -glm::dot(offset, ray) / a;
    const auto perpendicular = offset + projection * ray;
    const double square = double(joint.radius) * joint.radius - glm::dot(perpendicular, perpendicular);
    if (square < 0) return false;
    const double root = std::sqrt(square / a);
    const double near = projection - root, far = projection + root;
    distance = static_cast<float>(near >= 0 ? near : far);
    return std::isfinite(distance) && distance >= 0;
}

} // namespace viewer
