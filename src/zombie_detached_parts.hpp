#pragma once

#include "physics_attachment.hpp"
#include "zombie_skeleton.hpp"
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace viewer {
class PhysicsWorld;
struct DetachedZombiePart {
    b3BodyId body{}; // Generation-bearing ID of the severed assembly's root.
    RagdollPart part = RagdollPart::head;
    glm::vec3 position{0}; // Current body center in world meters.
};

// Metadata only: Box3D owns the spatial index. Ragdolls register severed limb
// assemblies and forget them before destroying bodies or their revision owner.
class ZombieDetachedParts {
public:
    explicit ZombieDetachedParts(std::shared_ptr<PhysicsWorld> world);
    void record(RagdollPart part, const std::array<b3BodyId,2>& bodies,
                std::size_t count, std::uint64_t& revision);
    void forget(b3BodyId root);
    // Nearest unclaimed arm/leg roots; at most limit results, exact 3D radius.
    void queryNear(glm::vec3 center, float radius, std::size_t limit,
                   std::vector<DetachedZombiePart>& result) const;
    std::optional<glm::vec3> availablePosition(b3BodyId root) const;
    // Snap the complete detached assembly, preserving its internal joints.
    // The returned point joint owns the claim and releases it on destruction.
    std::unique_ptr<PhysicsAttachment> carry(b3BodyId root, glm::vec3 mouth);
private:
    struct Entry {
        RagdollPart part;
        std::array<b3BodyId,2> bodies;
        std::size_t count;
        std::uint64_t* revision;
    };
    std::shared_ptr<PhysicsWorld> world_;
    std::unordered_map<std::uint64_t,Entry> entries_;
};
} // namespace viewer
