#pragma once

namespace viewer {
class Camera;
class PhysicsProp;

// Borrowed by the hand; the world retains completed ties after the tool moves on.
class BindingInteraction {
public:
    virtual ~BindingInteraction() = default;
    virtual bool isBinding(const PhysicsProp&) const = 0;
    virtual void update(const PhysicsProp&, const Camera&) = 0;
    virtual bool finish(const PhysicsProp&) = 0;
    virtual void cancel(const PhysicsProp&) = 0;
};
}
