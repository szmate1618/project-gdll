#pragma once

#include <box3d/id.h>
#include <memory>

namespace viewer {
class PhysicsWorld;

// Additional sleep policy for physical corpses, in runtime Y-up metres.
// Qualifies complete live contact/joint groups using actual solved motion;
// native collision, impulse and grab wake-up remain authoritative.
class CorpseSettling {
public:
    explicit CorpseSettling(PhysicsWorld& world);
    ~CorpseSettling();
    CorpseSettling(const CorpseSettling&) = delete;
    CorpseSettling& operator=(const CorpseSettling&) = delete;

    // Register after constructing the body's shapes; unregister before destruction.
    void track(b3BodyId body);
    void untrack(b3BodyId body);
    // Both transitions begin a fresh quiet window.
    void setEnabled(b3BodyId body, bool enabled);
    void invalidate(b3BodyId body);

    // PhysicsWorld calls these immediately around each fixed solve, before
    // consuming movement events so forced sleep publishes its final pose too.
    void prepare();
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
