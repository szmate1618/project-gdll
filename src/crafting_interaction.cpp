#include "player_interaction.hpp"
#include "ground_items.hpp"
#include "nail_placement.hpp"
#include "binding_placement.hpp"
#include "grip_placement.hpp"

namespace viewer {
void PlayerInteraction::setCraftingMode(bool enabled, GroundItems* ground) {
    objects_.setCraftingMode(enabled, enabled && ground ? &ground->nails() : nullptr,
                             enabled && ground ? &ground->bindings() : nullptr,
                             enabled && ground ? &ground->grips() : nullptr);
}
}
