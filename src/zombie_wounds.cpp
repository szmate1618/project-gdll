#include "zombie_wounds.hpp"
#include "zombie_layer.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
glm::vec3 posedVertex(const AnimatedModel& asset, std::size_t p, std::size_t v,
                      const std::vector<glm::mat4>& pose) {
    const auto point = glm::vec4(asset.model.primitives[p].vertices[v].position, 1);
    if (p >= asset.animation.size() || asset.animation[p].skinning.empty()) return glm::vec3(point);
    glm::vec4 result(0);
    const auto& skin = asset.animation[p].skinning[v];
    for (std::size_t i = 0; i < skin.weights.size(); ++i)
        if (skin.weights[i] > 0)
            result += pose[skin.joints[i]] * asset.skeleton[skin.joints[i]].inverseBind * point * skin.weights[i];
    return glm::vec3(result);
}
std::vector<glm::mat4> referencePose(const AnimatedModel& asset) {
    std::vector<glm::mat4> pose;
    for (const auto& bone : asset.skeleton) pose.push_back(bone.referenceWorld);
    return pose;
}
bool rayTriangle(glm::dvec3 origin, glm::dvec3 direction, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, float& distance) {
    const auto ab = b-a, ac = c-a, h = glm::cross(direction, ac);
    const double determinant = glm::dot(ab,h);
    if (std::abs(determinant) < 1e-12) return false;
    const auto s = origin-a;
    const double u = glm::dot(s,h)/determinant;
    if (u < 0 || u > 1) return false;
    const auto q = glm::cross(s,ab);
    const double v = glm::dot(direction,q)/determinant;
    if (v < 0 || u+v > 1) return false;
    const double d = glm::dot(ac,q)/determinant;
    if (d < 0) return false;
    distance = static_cast<float>(d);
    return true;
}
}
bool zombieTorsoBone(const AnimatedModel& asset, int bone) {
    if (bone < 0 || std::size_t(bone) >= asset.skeleton.size()) return false;
    auto name = asset.skeleton[std::size_t(bone)].name;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    return name.find("spine") != std::string::npos || name.find("chest") != std::string::npos ||
        name.find("torso") != std::string::npos || name.find("pelvis") != std::string::npos;
}
ZombieTorsoGeometry prepareZombieTorso(const AnimatedModel& asset) {
    ZombieTorsoGeometry result;
    result.bone = asset.ragdollBones[std::size_t(RagdollPart::torso)];
    if (result.bone < 0) return result;
    const auto pose = referencePose(asset);
    const auto& frame = pose.at(std::size_t(result.bone));
    result.referenceFrame = frame;
    for (int i=0; i<3; ++i) result.referenceFrame[i] = glm::vec4(glm::normalize(glm::vec3(frame[i])), 0);
    auto low = glm::vec3(std::numeric_limits<float>::max()), high = -low;
    for (std::size_t p=0; p<asset.model.primitives.size(); ++p) {
        const auto& primitive = asset.model.primitives[p];
        result.triangleOffsets.push_back(result.triangleCount);
        result.referenceVertices.emplace_back();
        result.torsoVertices.emplace_back();
        for (std::size_t v=0; v<primitive.vertices.size(); ++v) {
            const auto point = posedVertex(asset,p,v,pose);
            float weight = 0;
            if (p < asset.animation.size() && !asset.animation[p].skinning.empty()) {
                const auto& skin = asset.animation[p].skinning[v];
                for (std::size_t i=0; i<skin.weights.size(); ++i)
                    if (zombieTorsoBone(asset, int(skin.joints[i]))) weight += skin.weights[i];
            }
            const bool torso = weight >= .5f;
            result.referenceVertices.back().push_back(point);
            result.torsoVertices.back().push_back(torso);
            if (torso) { low = glm::min(low,point); high = glm::max(high,point); }
        }
        for (std::size_t t=0; t<primitive.indices.size(); t+=3) {
            const auto& selected = result.torsoVertices.back();
            result.torsoTriangles.push_back(selected[primitive.indices[t]] && selected[primitive.indices[t+1]] && selected[primitive.indices[t+2]]);
            ++result.triangleCount;
        }
    }
    result.center = low.x <= high.x ? (low+high)*.5f : glm::vec3(frame[3]);
    if (std::none_of(result.torsoTriangles.begin(), result.torsoTriangles.end(), [](bool value) { return value; })) result.bone = -1;
    return result;
}
const ZombieTorsoWound* ZombieWounds::find(std::size_t id) const {
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
}
bool zombieTorsoRay(const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
                    const ZombieTorsoWound* wound, const ZombieInstance& instance,
                    const std::vector<glm::mat4>& pose, glm::vec3 origin, glm::vec3 direction, float& distance) {
    if (geometry.bone < 0) return false;
    // Work close to the actor to avoid precision loss at large map coordinates.
    const auto inverse = glm::inverse(glm::dmat4(instance.transform));
    const auto o = glm::dvec3(inverse * glm::dvec4(origin,1));
    const auto d = glm::dvec3(inverse * glm::dvec4(direction,0));
    bool found = false;
    distance = std::numeric_limits<float>::max();
    for (std::size_t p=0; p<asset.model.primitives.size(); ++p) {
        const auto& primitive = asset.model.primitives[p];
        std::vector<glm::vec3> points(primitive.vertices.size());
        for (std::size_t v=0; v<points.size(); ++v) points[v] = posedVertex(asset,p,v,pose);
        for (std::size_t t=0; t<primitive.indices.size(); t+=3) {
            const auto id = geometry.triangleOffsets[p]+t/3;
            if (!geometry.torsoTriangles[id] || (wound && wound->removedTriangles[id])) continue;
            float hit = 0;
            if (rayTriangle(o,d,points[primitive.indices[t]],points[primitive.indices[t+1]],points[primitive.indices[t+2]],hit) && hit < distance) {
                distance = hit; found = true;
            }
        }
    }
    return found;
}
bool ZombieWounds::hit(std::size_t id, const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
                       const ZombieInstance& instance, const std::vector<glm::mat4>& pose, glm::vec3 point) {
    if (geometry.bone < 0) return false;
    auto& wound = entries_[id];
    if (wound.removedTriangles.empty()) wound.removedTriangles.resize(geometry.triangleCount);
    bool changed = false;
    const auto inverse = glm::inverse(glm::dmat4(instance.transform));
    const auto localHit = glm::dvec3(inverse * glm::dvec4(point,1));
    const auto metric = glm::dmat3(instance.transform);
    std::size_t vertices = 0;
    for (const auto& primitive : asset.model.primitives) vertices += primitive.vertices.size();
    if (wound.vertexMask.empty()) wound.vertexMask.resize((vertices+7)/8);
    std::size_t vertexOffset = 0;
    for (std::size_t p=0; p<asset.model.primitives.size(); ++p) {
        const auto& primitive = asset.model.primitives[p];
        std::vector<bool> close(primitive.vertices.size());
        for (std::size_t v=0; v<close.size(); ++v) {
            if (!geometry.torsoVertices[p][v]) continue;
            const auto delta = metric * (glm::dvec3(posedVertex(asset,p,v,pose))-localHit);
            close[v] = glm::dot(delta,delta) <= .1*.1 + 1e-9;
            const auto bit = vertexOffset + v;
            if (close[v] && !(wound.vertexMask[bit/8] & (1u << (bit%8)))) {
                wound.vertexMask[bit/8] |= std::uint8_t(1u << (bit%8));
                wound.removedVertices.push_back(geometry.referenceVertices[p][v]);
            }
        }
        for (std::size_t t=0; t<primitive.indices.size(); t+=3) {
            const auto triangle = geometry.triangleOffsets[p]+t/3;
            if (!wound.removedTriangles[triangle] &&
                (close[primitive.indices[t]] || close[primitive.indices[t+1]] || close[primitive.indices[t+2]])) {
                wound.removedTriangles[triangle] = 1; changed = true;
            }
        }
        vertexOffset += primitive.vertices.size();
    }
    if (changed) ++wound.revision;
    if (!wound.revision) entries_.erase(id);
    return changed;
}
bool ZombieWounds::applyMask(std::size_t id, const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
                            const std::vector<std::uint8_t>& mask) {
    if (mask.empty()) {
        return entries_.erase(id) != 0;
    }
    std::size_t vertices = 0;
    for (const auto& primitive : asset.model.primitives) vertices += primitive.vertices.size();
    if (mask.size() != (vertices+7)/8 || (vertices%8 && (mask.back() >> (vertices%8))))
        throw std::runtime_error("Replicated zombie wound does not match its asset");
    const auto* previous = find(id);
    if (previous && previous->vertexMask == mask) return false;
    ZombieTorsoWound wound;
    wound.revision = previous ? previous->revision+1 : 1;
    wound.vertexMask = mask;
    std::size_t offset = 0;
    for (std::size_t p=0; p<asset.model.primitives.size(); ++p) {
        const auto removed = [&](std::size_t v) { return (mask[(offset+v)/8] & (1u << ((offset+v)%8))) != 0; };
        const auto& primitive = asset.model.primitives[p];
        for (std::size_t v=0; v<primitive.vertices.size(); ++v)
            if (removed(v)) wound.removedVertices.push_back(geometry.referenceVertices[p][v]);
        for (std::size_t t=0; t<primitive.indices.size(); t+=3)
            wound.removedTriangles.push_back(removed(primitive.indices[t]) || removed(primitive.indices[t+1]) || removed(primitive.indices[t+2]));
        offset += primitive.vertices.size();
    }
    entries_[id] = std::move(wound);
    return true;
}
std::vector<std::uint8_t> ZombieWounds::mask(const AnimatedModel& asset, const ZombieTorsoWound& wound) {
    const auto pose = referencePose(asset);
    std::vector<std::uint8_t> mask;
    for (std::size_t p=0; p<asset.model.primitives.size(); ++p) {
        const auto& primitive = asset.model.primitives[p];
        std::vector<bool> removed(primitive.vertices.size());
        for (std::size_t v=0; v<removed.size(); ++v) {
            const auto point = posedVertex(asset,p,v,pose);
            for (const auto hit : wound.removedVertices)
                if (glm::dot(point-hit,point-hit) < 1e-8f) { removed[v] = true; break; }
        }
        for (std::size_t t=0; t<primitive.indices.size(); t+=3)
            mask.push_back(removed[primitive.indices[t]] || removed[primitive.indices[t+1]] || removed[primitive.indices[t+2]]);
    }
    return mask;
}
} // namespace viewer
