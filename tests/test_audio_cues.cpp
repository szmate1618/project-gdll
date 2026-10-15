#include "zombie_audio.hpp"
#include "door_audio.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool hearing(viewer::AudioEffect effect) {
    return effect == viewer::AudioEffect::huh || effect == viewer::AudioEffect::intrigued;
}
void reactionsAndCalls() {
    viewer::ZombieAudioScheduler scheduler;
    viewer::ZombieAudioSample zombie;
    zombie.id = 7; zombie.feet = {3, 0, 4};
    require(scheduler.update(0, {zombie}).empty(), "Idle calls start staggered rather than all at initialization");
    zombie.heardRevision = 1; zombie.heardSeconds = .1;
    const auto heard = scheduler.update(.1, {zombie});
    require(heard.size() == 1 && hearing(heard[0].effect) && heard[0].zombie == 7 &&
            heard[0].position == glm::vec3(3, 1.5f, 4) && heard[0].priority == 2,
            "Accepted hearing produces exactly one positional huh/intrigued reaction");
    scheduler.update(.15, {});
    require(scheduler.update(.2, {zombie}).empty(), "Brief voice preemption cannot replay a still-fresh hearing revision");
    zombie.chaseRevision = 1; zombie.chaseSeconds = .3;
    const auto chase = scheduler.update(.3, {zombie});
    require(chase.size() == 1 && chase[0].priority == 3 &&
            (chase[0].effect == viewer::AudioEffect::chase || chase[0].effect == viewer::AudioEffect::chase2),
            "Chase acquisition produces one grunt with higher priority");
    require(scheduler.update(.4, {zombie}).empty(), "Remaining in chase does not repeat the start grunt");
    require(scheduler.update(100, {zombie}).size() == 1, "A stall produces one ordinary call rather than an overdue burst");
    require(scheduler.update(100, {zombie}).empty(), "Ordinary calls have a quiet interval");
    scheduler.update(101, {});
    require(scheduler.update(102, {zombie}).empty(), "Returning to audible range cannot replay old hearing/chase events");
    zombie.heardRevision = 2; zombie.heardSeconds = 102;
    require(scheduler.update(102, {zombie}).size() == 1, "A new hearing event remains audible after reentry");

    std::vector<viewer::ZombieAudioSample> crowd;
    for (std::size_t id = 0; id < 32; ++id) {
        viewer::ZombieAudioSample sample;
        sample.id = id; sample.heardRevision = 1; sample.heardSeconds = 1;
        crowd.push_back(sample);
    }
    viewer::ZombieAudioScheduler a, b;
    const auto first = a.update(1, crowd), second = b.update(1, crowd);
    std::set<viewer::AudioEffect> variants;
    require(first.size() == crowd.size() && first.size() == second.size(), "Fresh first-observation hearing is not missed");
    for (std::size_t i = 0; i < first.size(); ++i) {
        require(first[i].effect == second[i].effect && first[i].pitch == second[i].pitch, "Variation is reproducible without gameplay RNG");
        variants.insert(first[i].effect);
    }
    require(variants == std::set<viewer::AudioEffect>{viewer::AudioEffect::huh, viewer::AudioEffect::intrigued},
            "Both requested hearing recordings are used across the crowd");
}

void footsteps() {
    for (const int frames : {4, 40}) {
        viewer::ZombieAudioScheduler scheduler;
        viewer::ZombieAudioSample zombie;
        zombie.id = 1; zombie.walking = true;
        scheduler.update(0, {zombie});
        std::size_t steps = 0;
        for (int i = 1; i <= frames; ++i) {
            const double seconds = double(i) / frames;
            zombie.walkSeconds = seconds;
            zombie.feet.x = static_cast<float>(seconds * 1.2);
            for (const auto& cue : scheduler.update(seconds, {zombie})) if (!cue.zombie) {
                require(cue.position == zombie.feet, "Zombie footsteps originate at the actual feet");
                ++steps;
            }
        }
        require(steps == 1, "Actual one-second walking travel produces one step across frame rates");
        zombie.walking = false;
        zombie.walkSeconds += .9;
        require(scheduler.update(1.1, {zombie}).empty(), "Stopped/blocked zombies do not make footsteps");
        zombie.walking = true; zombie.feet.x += 100; zombie.walkSeconds += .9;
        require(scheduler.update(1.2, {zombie}).empty(), "Teleporting cannot emit catch-up footsteps");
        scheduler.update(1.3, {});
        require(scheduler.update(1.4, {zombie}).empty(), "Dead/removed/reentering actors do not retain a stale stride");
    }
}

void doorSlams() {
    viewer::DoorAudioTracker tracker;
    const glm::vec3 position(12, 2, -9);
    const auto sample = [&](float angle, float seconds = .1f) { return tracker.update(seconds, {{4, angle, position}}); };
    require(sample(0).empty() && sample(0).empty(), "Initially closed doors are silent");
    require(sample(.3f).empty(), "Opening does not slam");
    for (int i = 29; i >= 0; --i) require(sample(i * .01f).empty(), "Slow closing does not slam");
    sample(.5f); sample(.15f);
    const auto slam = sample(0);
    require(slam == std::vector<glm::vec3>{position}, "Fast closure plays once at the door position");
    require(sample(.01f).empty() && sample(0).empty(), "Closed-stop jitter cannot repeat a slam");
    sample(.2f);
    require(sample(0).empty(), "Rapid hinge bounce is debounced");
    sample(.5f, .5f); sample(.15f);
    require(sample(0).size() == 1, "A separate later slam is audible");
    tracker.update(.1f, {});
    require(sample(0).empty(), "Doors entering the audible neighborhood do not create phantom slams");
}
}
int main() {
    try {
        reactionsAndCalls(); footsteps(); doorSlams();
        std::cout << "Audio cue scheduling tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
