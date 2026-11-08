#include "axe_combat.hpp"
#include "axe_geometry.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "dog_layer.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
struct SweepSegment {
    glm::vec3 a, b;
    float start, end; // Elapsed cut seconds, including static cross-sections.
};
struct AxeContact {
    std::size_t id;
    float elapsed;
    glm::vec3 origin, point, direction;
    std::optional<ZombieHitClaim> body;
    int joint = -1;
};
// The head is a solid slab behind the edge. Cross-sections cover initial
// overlap; swept surface points cover motion between samples (2 cm spacing).
std::vector<SweepSegment> sweep(const glm::mat4& previous, const glm::mat4& current,
        float start, float end, bool edge) {
    std::vector<SweepSegment> result;
    const auto point = [](const glm::mat4& frame, glm::vec3 p) { return glm::vec3(frame * glm::vec4(p, 1)); };
    const auto line = [&](glm::vec3 a, glm::vec3 b) {
        result.push_back({point(current, a), point(current, b), end, end});
    };
    const auto moving = [&](glm::vec3 p) {
        result.push_back({point(previous, p), point(current, p), start, end});
    };
    if (edge) {
        line({axeEdgeX, axeHeadBottom, 0}, {axeEdgeX, axeHeadTop, 0});
        for (int y = 0; y <= 14; ++y)
            moving({axeEdgeX, glm::mix(axeHeadBottom, axeHeadTop, y / 14.f), 0});
    } else {
        for (float z : {-axeHeadHalfDepth, 0.f, axeHeadHalfDepth}) {
            line({axeHeadFront, axeHeadBottom, z}, {axeHeadFront, axeHeadTop, z});
            line({axeHeadBack, axeHeadBackBottom, z}, {axeHeadBack, axeHeadBackTop, z});
            for (int y = 0; y <= 14; ++y) {
                const float height = glm::mix(axeHeadBottom, axeHeadTop, y / 14.f);
                const float taper = std::max({0.f, (axeHeadBackBottom - height) / (axeHeadBackBottom - axeHeadBottom),
                    (height - axeHeadBackTop) / (axeHeadTop - axeHeadBackTop)});
                const float back = glm::mix(axeHeadBack, axeHeadFront, taper);
                line({back, height, z}, {axeHeadFront, height, z});
                moving({back, height, z}); moving({axeHeadFront, height, z});
            }
        }
        for (float x : {-.018f, .018f}) for (float z : {-.018f, .018f}) {
            line({x, -.20f, z}, {x, axeHeadBackTop, z});
            for (int y = 0; y <= 56; ++y) moving({x, glm::mix(-.20f, axeHeadBackTop, y / 56.f), z});
        }
    }
    return result;
}
}

void AxeCombat::update(SwordMelee& axe, const Camera& camera, double seconds,
        double animationSeconds, const CollisionWorld& world, ZombieLayer* zombies, DogLayer* dogs) {
    if (!std::isfinite(seconds) || !std::isfinite(animationSeconds)) return;
    if (!std::isfinite(axe.startedAt())) { startedAt_ = axe.startedAt(); return; }
    if (startedAt_ != axe.startedAt()) {
        startedAt_ = axe.startedAt(); sampled_ = swordWindupSeconds; stopped_ = false;
        severed_.clear(); hitDogs_.clear(); createdCorpses_.clear();
    }
    if (stopped_) return;
    const float end = std::clamp(static_cast<float>(seconds - startedAt_),
        swordWindupSeconds, swordWindupSeconds + swordCutSeconds);
    if (end <= sampled_) return;
    std::vector<std::size_t> candidates;
    if (zombies) {
        zombies->queryLivingNear(camera.position(), 2.5f, candidates);
        for (const auto& [id, handle] : createdCorpses_)
            if (id < zombies->instances.size() && zombies->instances[id].ragdoll && zombies->instances[id].ragdollHandle == handle)
                candidates.push_back(id);
    }
    if (dogs && dogs->instances.empty()) dogs = nullptr;
    if (candidates.empty() && !dogs) { sampled_ = end; return; }
    const auto eyeToWorld = glm::inverse(camera.view());
    const auto frame = [&](float elapsed) {
        auto pose = axe.pose(startedAt_); pose.elapsed = elapsed;
        return eyeToWorld * swordTransform(pose);
    };
    const auto contacts = [&](const std::vector<SweepSegment>& segments, bool edge) {
        std::vector<AxeContact> found;
        for (const auto& s : segments) {
            const float length = glm::distance(s.a, s.b);
            if (length < 1e-6f) continue;
            const auto direction = (s.b - s.a) / length;
            for (const auto id : candidates) {
                const auto& instance = zombies->instances[id];
                if (instance.removed) continue;
                const auto corpse = createdCorpses_.find(id);
                if (instance.ragdoll && (corpse == createdCorpses_.end() || corpse->second != instance.ragdollHandle)) continue;
                std::optional<ZombieShotTrace> body;
                if (!edge) body = zombies->traceMeleeBody(id, s.a, s.b, animationSeconds);
                const auto joint = zombies->traceMeleeJoint(id, s.a, s.b, animationSeconds);
                if (!body && !joint) continue;
                float distance;
                std::optional<ZombieHitClaim> claim;
                int jointId = -1;
                if (body) { distance = body->distance; claim = body->claim; }
                if (joint && (!body || joint->distance < body->distance)) {
                    distance = joint->distance; claim.reset(); jointId = joint->joint;
                }
                const auto point = s.a + direction * distance;
                if (!world.lineOfSight(s.a, point) || !world.lineOfSight(camera.position(), point)) continue;
                found.push_back({id, glm::mix(s.start, s.end, distance / length), s.a, point, direction,
                    claim, jointId});
            }
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
            return a.elapsed < b.elapsed || (a.elapsed == b.elapsed && a.id < b.id);
        });
        return found;
    };
    auto previous = frame(sampled_);
    // Sub-centimetre angular steps keep the 5 cm leading edge independent of
    // input/frame cadence. Resolve all contacts in time order before mutating.
    while (sampled_ < end) {
        const float next = std::min(end, sampled_ + 1.f / 360);
        const auto current = frame(next);
        const auto mainSegments = sweep(previous, current, sampled_, next, false);
        const auto edgeSegments = sweep(previous, current, sampled_, next, true);
        const auto main = zombies ? contacts(mainSegments, false) : std::vector<AxeContact>{};
        const float stop = main.empty() ? next : main.front().elapsed;
        if (zombies) for (const auto& hit : contacts(edgeSegments, true)) {
            if (hit.elapsed > stop) break;
            if (severed_.count(hit.id)) continue;
            if (zombies->severJoint(hit.id, hit.joint, hit.point, hit.direction, animationSeconds, world)) {
                severed_.insert(hit.id);
                createdCorpses_.emplace(hit.id, zombies->instances[hit.id].ragdollHandle);
            }
        }
        if (!main.empty()) {
            const auto& hit = main.front();
            if (hit.body && !zombies->instances[hit.id].ragdoll)
                zombies->applyReportedShot(*hit.body, hit.origin, hit.direction, animationSeconds, world, ShotCaliber::high);
            axe.stopAt(stop); stopped_ = true; sampled_ = stop;
            return;
        }
        if (dogs) for (const auto& s : mainSegments) {
            const float length = glm::distance(s.a, s.b);
            if (length < 1e-6f) continue;
            const auto direction = (s.b - s.a) / length;
            const auto hit = dogs->traceShot(s.a, direction, world, animationSeconds);
            if (hit && hit->distance <= length && !hitDogs_.count(hit->dog) &&
                world.lineOfSight(camera.position(), s.a + direction * hit->distance)) {
                dogs->damage(hit->dog, .2f); hitDogs_.insert(hit->dog);
            }
        }
        previous = current; sampled_ = next;
    }
}
}
