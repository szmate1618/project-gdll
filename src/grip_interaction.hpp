#pragma once
#include <glm/gtc/quaternion.hpp>
#include <cstdint>

namespace viewer {
class Camera;
// World-owned placement service borrowed by the hand. Tokens keep simultaneous
// multiplayer editors independent without making the ghost an inventory item.
class GripInteraction {
public:
    virtual ~GripInteraction()=default;
    virtual std::uint64_t begin(const Camera&)=0;
    virtual bool active(std::uint64_t) const=0;
    virtual void move(std::uint64_t,const Camera&)=0;
    virtual void release(std::uint64_t)=0;
    virtual glm::quat rotation(std::uint64_t) const=0;
    virtual void rotate(std::uint64_t,glm::quat)=0;
    virtual void finish(std::uint64_t)=0;
    virtual void cancel(std::uint64_t)=0;
};
}
