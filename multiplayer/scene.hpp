#pragma once
#include "protocol.hpp"
#include "resources.hpp"
#include "box_layer.hpp"
#include "ground_items.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "forest_layer.hpp"
#include "physics_world.hpp"
#include "tree_layer.hpp"
#include "undergrowth.hpp"
#include "zombie_layer.hpp"
#include "zombie_scenario_config.hpp"
#include "weapons.hpp"
#include "inventory/player_inventory.hpp"
#include "vegetation_selection.hpp"
#include "vehicle_layer.hpp"
#include "vehicle_placement.hpp"
#include "day_night.hpp"
#include "client/display.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace multiplayer {
struct Options {
    std::filesystem::path map = "output/godollo.glb";
    std::filesystem::path zombieAssets = resourceRoot() / "assets/zombies/models";
    std::string address = "127.0.0.1:27020";
    std::uint16_t port = 27020;
    std::optional<glm::vec2> spawn;
    viewer::VegetationSelection vegetation;
    std::size_t zombieCount = 100;
    // Negotiated effective slots, including authored populations on small maps.
    // Used only by replicas; it never triggers population generation/navigation.
    std::optional<std::size_t> replicaZombieCount;
    float zombieRadius = 100;
    bool trees = true, forests = true, headless = false, hidden = false, bot = false;
    bool cars = true;
    bool audio = true;
    ClientDisplay display; // Client presentation only; excluded from scene identity.
    bool birds = false; // Server opt-in during the deterministic flock experiment.
    bool zombiePerception = true; // Authoritative server setting; not world identity.
    bool explicitLook = false;
    glm::vec2 look{-90, 0}; // Client yaw/pitch in degrees; independent of world identity.
    viewer::DayNightCycle dayNight;
    double seconds = 0;
    float fakeLagMs = 0, fakeLossPercent = 0;
    std::filesystem::path screenshot;
};
Options parseOptions(int argc, char** argv, bool server);
// Count is the effective population after checking the map extent.
std::string sceneFingerprint(const Options& options, std::size_t count, glm::vec3 spawnEye,
                             const std::vector<std::filesystem::path>& content);
using ZombiePopulationLoader = std::function<viewer::ZombieLayer(
    const std::filesystem::path&, const viewer::CollisionWorld&, const viewer::Model&)>;
enum class SceneRole { Authority, Replica };
// Clients load shared assets and hidden stable-ID slots, without generating
// authoritative placements, spatial indexes or navigation routes.
struct Scene {
    explicit Scene(const Options& options, const ZombiePopulationLoader& populationLoader = {},
                   const std::optional<viewer::ZombieScenarioConfig::Player>& player = std::nullopt,
                   SceneRole role = SceneRole::Authority);
    std::filesystem::path mapPath;
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics;
    std::unique_ptr<viewer::DoorLayer> doors;
    std::unique_ptr<viewer::BoxLayer> boxes;
    std::unique_ptr<viewer::GroundItems> groundItems;
    std::array<viewer::Model, viewer::weaponIds.size()> groundModels;
    std::unique_ptr<viewer::Grenades> grenades;

    std::unique_ptr<viewer::VehicleLayer> vehicles;
    std::optional<std::vector<viewer::VehiclePlacement>> vehiclePlacements;
    std::optional<viewer::TreeLayer> trees;
    std::optional<viewer::ForestLayer> forests;
    viewer::UndergrowthIndex undergrowth;
    std::unique_ptr<viewer::CollisionWorld> collision;
    viewer::ZombieLayer zombies;
    glm::vec3 spawnEye{0};
    glm::vec2 spawnOrientation{-90, 0};

    viewer::DayNightCycle dayNight;
    bool birdsEnabled = false;
    std::string fingerprint;
    const viewer::Weapons initialWeapons;
    const viewer::PlayerInventory initialInventory;
};
} // namespace multiplayer
