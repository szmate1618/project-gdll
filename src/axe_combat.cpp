#include "axe_combat.hpp"
#include "axe_geometry.hpp"
#include "collision_world.hpp"
#include "zombie_layer.hpp"
#include "dog_hit_query.hpp"
#include "zombie_melee_query.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
struct AxeContact {
    std::size_t id;
    float elapsed;
    glm::vec3 origin, point, direction;
    std::optional<ZombieHitClaim> body;
    int joint = -1;
};
const std::array<glm::vec3, 8> head{{
    {axeHeadBack, axeHeadBackBottom, -axeHeadHalfDepth}, {axeHeadFront, axeHeadBottom, -axeHeadHalfDepth},
    {axeHeadFront, axeHeadTop, -axeHeadHalfDepth}, {axeHeadBack, axeHeadBackTop, -axeHeadHalfDepth},
    {axeHeadBack, axeHeadBackBottom, axeHeadHalfDepth}, {axeHeadFront, axeHeadBottom, axeHeadHalfDepth},
    {axeHeadFront, axeHeadTop, axeHeadHalfDepth}, {axeHeadBack, axeHeadBackTop, axeHeadHalfDepth}
}};
// Match the authored square handle instead of rounding away its corners.
const std::array<glm::vec3, 8> handle{{
    {-.018f, -.20f, -.018f}, {.018f, -.20f, -.018f}, {-.018f, axeHeadBackTop, -.018f}, {.018f, axeHeadBackTop, -.018f},
    {-.018f, -.20f, .018f}, {.018f, -.20f, .018f}, {-.018f, axeHeadBackTop, .018f}, {.018f, axeHeadBackTop, .018f}
}};
const std::array<glm::vec3, 2> edge{{{axeEdgeX, axeHeadBottom, 0}, {axeEdgeX, axeHeadTop, 0}}};
glm::vec3 motionDirection(const MeleeSweep& sweep, glm::vec3 fallback) {
    glm::vec3 motion(0);
    for (int i = 0; i < sweep.count; ++i) motion += sweep.to[i] - sweep.from[i];
    return glm::length(motion) > 1e-6f ? glm::normalize(motion) : fallback;
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
    DogMeleeQuery dogContacts(dogs, camera.position(), 2.5f, animationSeconds);
    if (candidates.empty() && dogContacts.empty()) { sampled_ = end; return; }
    ZombieMeleeQuery zombieContacts(zombies, candidates, animationSeconds);
    const auto eyeToWorld = glm::inverse(camera.view());
    const auto frame = [&](float elapsed) {
        auto pose = axe.pose(startedAt_); pose.elapsed = elapsed;
        return eyeToWorld * swordTransform(pose);
    };
    const auto contacts = [&](const MeleeSweep& shape, bool cutting, float start, float finish) {
        std::vector<AxeContact> found;
        const auto direction = motionDirection(shape, camera.direction());
        for (const auto& hit : zombieContacts.cast(shape, cutting)) {
            const auto point = hit.hit.point;
            const auto origin = point - direction * .02f;
            if (!world.lineOfSight(origin, point) || !world.lineOfSight(camera.position(), point)) continue;
            found.push_back({hit.id, glm::mix(start, finish, hit.hit.fraction), origin, point, direction, hit.body, hit.joint});
        }
        return found;
    };
    const auto order = [](const auto& a, const auto& b) {
        return a.elapsed < b.elapsed || (a.elapsed == b.elapsed && a.id < b.id);
    };
    auto previous = frame(sampled_);
    // Sub-centimetre angular steps keep the 5 cm leading edge independent of
    // input/frame cadence. Resolve all contacts in time order before mutating.
    while (sampled_ < end) {
        const float next = std::min(end, sampled_ + 1.f / 360);
        const auto current = frame(next);
        const auto headSweep = meleeSweep(head.data(), head.size(), previous, current);
        const auto handleSweep = meleeSweep(handle.data(), handle.size(), previous, current);
        const auto edgeSweep = meleeSweep(edge.data(), edge.size(), previous, current);
        auto main = contacts(headSweep, false, sampled_, next);
        const auto handleHits = contacts(handleSweep, false, sampled_, next);
        main.insert(main.end(), handleHits.begin(), handleHits.end());
        std::sort(main.begin(), main.end(), order);
        auto cuts = contacts(edgeSweep, true, sampled_, next);
        std::sort(cuts.begin(), cuts.end(), order);
        const float stop = main.empty() ? next : main.front().elapsed;
        if (zombies) for (const auto& hit : cuts) {
            if (hit.elapsed > stop) break;
            if (severed_.count(hit.id)) continue;
            if (zombies->severJoint(hit.id, hit.joint, hit.point, hit.direction, animationSeconds, world)) {
                severed_.insert(hit.id);
                createdCorpses_.emplace(hit.id, zombies->instances[hit.id].ragdollHandle);
                zombieContacts.refresh(hit.id);
            }
        }
        if (!main.empty()) {
            const auto& hit = main.front();
            if (hit.body && !zombies->instances[hit.id].ragdoll)
                zombies->applyReportedShot(*hit.body, hit.origin, hit.direction, animationSeconds, world, ShotCaliber::high);
            axe.stopAt(stop); stopped_ = true; sampled_ = stop;
            return;
        }
        for (const auto* shape : {&headSweep, &handleSweep}) {
            const auto direction = motionDirection(*shape, camera.direction());
            for (const auto& hit : dogContacts.cast(*shape)) {
                const auto origin = hit.hit.point - direction * .02f;
                if (!hitDogs_.count(hit.dog) && world.lineOfSight(origin, hit.hit.point) &&
                    world.lineOfSight(camera.position(), hit.hit.point)) {
                    dogs->damage(hit.dog, .2f); hitDogs_.insert(hit.dog);
                }
            }
        }
        previous = current; sampled_ = next;
    }
}
}
