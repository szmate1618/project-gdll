#include "zombie_render_updates.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}

int main() {
    try {
        viewer::ZombieRenderUpdates changes;
        auto update = changes.since(0);
        require(update.full, "A new consumer must initialize all instances");
        auto cursor = update.revision;
        changes.acknowledge(cursor);
        require(!changes.since(cursor).ids, "An unchanged layer must have no instance work");
        changes.mark(99999, false);
        changes.mark(99999, true);
        changes.mark(7, false);
        update = changes.since(cursor);
        require(!update.full && *update.ids == std::vector<std::size_t>{99999, 7}, "Multiple changes must deduplicate by stable ID");
        require(changes.boundsChanged(99999) && !changes.boundsChanged(7), "Bounds changes must merge without promoting state-only updates");
        const auto stale = update.revision;
        changes.mark(42, true);
        changes.acknowledge(stale);
        require(changes.since(cursor).ids->size() == 3, "An old acknowledgement must not discard newer changes");
        update = changes.since(cursor);
        cursor = update.revision;
        changes.acknowledge(cursor);
        require(changes.since(stale).full, "A second consumer that missed a retired batch must fully refresh");
        changes.mark(7, false);
        update = changes.since(cursor);
        require(update.ids->size() == 1 && !changes.boundsChanged(7), "Retired flags must not contaminate later updates");
        changes.invalidate();
        require(changes.since(cursor).full, "Bulk edits must invalidate cached instances");
        update = changes.since(cursor);
        cursor = update.revision;
        changes.acknowledge(cursor);
        viewer::ZombieRenderUpdates replacement;
        changes = std::move(replacement);
        require(changes.since(cursor).full, "Replacing a publisher must invalidate its existing consumers");
        std::cout << "Zombie render update tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
