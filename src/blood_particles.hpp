#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace viewer {
struct BloodFloor {
    glm::vec3 point{0}, normal{0,1,0}; // Cached static surface, world metres.
    bool valid = false;
};
struct BloodAttachment {
    std::size_t zombie = 0;
    int bone = -1;
    glm::vec3 point{0}, direction{0,1,0}; // Bone-local coordinates.
};
struct BloodParticle {
    glm::vec3 start{0}, velocity{0};
    double born = 0;
    float lifetime = 0, landing = 0, size = 0, angle = 0;
    BloodFloor floor;
};
struct BloodStain {
    glm::vec3 point{0}, normal{0,1,0};
    double born = 0;
    float size = 0, angle = 0;
};
struct BloodEmitter {
    BloodAttachment attachment;
    glm::vec3 point{0}, direction{0,1,0};
    BloodFloor floor;
    double next = 0, end = 0;
};

// Presentation only: fixed storage, ballistic droplets and predicted landings.
// The owner resolves wound attachments; this class never queries scene geometry.
class BloodParticles {
public:
    static constexpr std::size_t particleCapacity = 1024, emitterCapacity = 16, stainCapacity = 256;
    static constexpr float gravity = 9.81f, stainDuration = 20;
    void hit(glm::vec3 point, glm::vec3 direction, double seconds, const BloodFloor& floor,
             const BloodAttachment& attachment = {});
    void moveEmitter(std::size_t slot, glm::vec3 point, glm::vec3 direction);
    void stopEmitter(std::size_t slot) { emitters_.at(slot).end = 0; }
    void update(double seconds);
    bool hasEffects(double seconds) const;
    const auto& particles() const { return particles_; }
    const auto& emitters() const { return emitters_; }
    const auto& stains() const { return stains_; }
private:
    void spawn(glm::vec3 point, glm::vec3 direction, double seconds, const BloodFloor& floor);
    float random();
    std::array<BloodParticle,particleCapacity> particles_{};
    std::array<BloodEmitter,emitterCapacity> emitters_{};
    std::array<BloodStain,stainCapacity> stains_{};
    std::size_t particleSlot_ = 0, stainSlot_ = 0;
    std::uint32_t seed_ = 1;
};
} // namespace viewer
