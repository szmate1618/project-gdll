#include "fire_effects.hpp"
#include "zombie_update_profile.hpp"
#include "renderer.hpp"
#include "settings_file.hpp"
#include "boid/bird_flock.hpp"
#include "boid/bird_renderer.hpp"
#include "frame_profile.hpp"
#include "frame_stage_profile.hpp"
#include "door_layer.hpp"
#include "player_interaction.hpp"
#include "hand_inventory.hpp"
#include "physics_world.hpp"
#include "box_layer.hpp"
#include "physics_scene.hpp"
#include "doodad_layer.hpp"
#include "doodad_state.hpp"
#include "file_fingerprint.hpp"
#include "physics_impacts.hpp"
#include "vehicle_player.hpp"
#include "vehicle_zombie_collision.hpp"
#include "vehicle_placement.hpp"
#include <random>
#include "interior_objects.hpp"
#include "floorplan_layer.hpp"
#include "player_house.hpp"
#include "interior_zombies.hpp"
#include "dog_layer.hpp"
#include "reticle_overlay.hpp"
#include "gui/hud.hpp"
#include "inventory/inventory_config.hpp"
#include "minimap.hpp"
#include "weapons.hpp"
#include "sword_combat.hpp"
#include "axe_combat.hpp"
#include "weapon_accuracy.hpp"
#include "action_selection.hpp"
#include "weapon_switch.hpp"
#include "weapon_reload.hpp"
#include "weapon_overlay.hpp"
#include "player_body.hpp"
#include "player_body_renderer.hpp"
#include "player_presentation.hpp"
#include "player_vitals.hpp"
#include "player_medical.hpp"
#include "melee_combat.hpp"
#include "camera_rig.hpp"
#include "mouse_input.hpp"
#include "collision_debug.hpp"
#include "gpu_timer.hpp"
#include "tree_layer.hpp"
#include "pole_layer.hpp"
#include "guardrail_layer.hpp"
#include "model_merge.hpp"
#include "forest_layer.hpp"
#include "vegetation_selection.hpp"
#include "zombie_layer.hpp"
#include "zombie_navigation.hpp"
#include "zombie_scenario.hpp"
#include "dog_scenario.hpp"
#include "zombie_door_collision.hpp"
#include "zombie_park.hpp"
#include "simulation_clock.hpp"
#include "sleep_threat.hpp"
#include "scene_audio.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Options {
    std::filesystem::path model = VIEWER_DEFAULT_ASSET;
    std::filesystem::path shaders = VIEWER_SHADER_DIR;
    std::filesystem::path screenshot;
    std::filesystem::path doodadSave;
    bool noDoodads = false;
    viewer::HudScreen hudScreen = viewer::HudScreen::Gameplay;
    std::filesystem::path trees;
    std::filesystem::path poles;
    std::filesystem::path guardrails;
    std::filesystem::path zombies;
    std::filesystem::path dogs = VIEWER_DOG_ASSETS;
    int dogCount = 0;
    std::filesystem::path scenario;
    bool fullScenarioMovement = false, fixedStep = false;
    int frames = 0;
    int width = 1280, height = 720;
    bool fullscreen = false;
    viewer::GraphicsSettings graphics;
    std::filesystem::path settingsFile;
    bool hidden = false, allowSoftware = false, selfTest = false, help = false;
    bool fps = false, collisionDebug = false, selfTestFPS = false;
    bool shadows = true;
    int sunShadowResolution = viewer::SunLighting::shadowResolution;
    bool treeShadows = true;
    bool treeCanopyShading = true;
    bool treeDepthOrdering = true;
    bool treeCardSplitting = true;
    bool treeNearestFiltering = true;
    float treeBillboardDistance = 100;
    float oldForestDistance = viewer::OldForestVisibility::distance;
    bool night = false;
    viewer::DayNightCycle dayNight;
    bool explicitDayNight = false;
    bool rain = false;
    bool noBirds = false, explicitBirds = false;
    viewer::BirdFlockSettings birds;
    viewer::AudioOptions audio{VIEWER_AUDIO_DIR};
    bool noForests = false;
    viewer::VegetationSelection vegetation;
    bool noFences = false;
    bool noPoles = false;
    bool noGuardrails = false;
    bool roofBatching = true;
    bool wallBatching = true;
    bool distantSceneCache = false;
    bool haze = true;
    bool groundFog = true;
    bool noTrees = false, treeCulling = true, treeCollisions = true, sceneCulling = true;
    bool noZombies = false, zombieView = false, explicitZombies = false, zombieCulling = true;
    bool zombiePerceptionStaggering = true;
    bool zombiePerception = true;

    bool noCars = false, vehicleDistanceDetail = true, vehicleInstancing = true;
    bool zombieDistanceDetail = true, zombieShadows = true, profile = false;
    std::filesystem::path frameProfile;
    bool flashlightShadows = false;
    std::optional<float> zombieDrawDistance;
    int zombieCount = 1000;
    bool explicitZombieCount = false, explicitZombieRadius = false;
    float zombieRadius = 100.0f;
    std::optional<double> animationTime;
    std::optional<glm::vec2> spawn;
};

Options parseOptions(int argc, char** argv, const std::optional<viewer::Settings>& saved = {}) {
    Options options;
    if (saved) {
        options.graphics = saved->graphics;
        options.audio.gain = saved->audio.volume;
        options.fullscreen = saved->graphics.fullscreen;
        const auto size = viewer::renderResolutions[saved->graphics.resolution];
        options.width = size.x; options.height = size.y;
        options.distantSceneCache = saved->graphics.distantScenery != 3;
        options.shadows = saved->graphics.shadowCasters != viewer::ShadowCasters::None;
        options.treeShadows = saved->graphics.shadowCasters >= viewer::ShadowCasters::Trees;
        options.zombieShadows = saved->graphics.shadowCasters >= viewer::ShadowCasters::Npcs;
        options.flashlightShadows = options.shadows;
    }
    bool hasModel = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value after " + argument);
            return argv[i];
        };
        if (argument == "--help" || argument == "-h") options.help = true;
        else if (argument == "--profile") options.profile = true;
        else if (argument == "--birds") options.explicitBirds = true;
        else if (argument == "--no-birds") options.noBirds = true;
        else if (argument == "--bird-flocks" || argument == "--birds-per-flock") {
            const auto text = value();
            std::size_t used = 0;
            int count;
            const int maximum = argument == "--bird-flocks" ? 64 : 4096;
            const auto message = argument + " requires an integer in [1, " + std::to_string(maximum) + "]";
            try { count = std::stoi(text, &used); }
            catch (const std::exception&) { throw std::runtime_error(message); }
            if (used != text.size() || count < 1 || count > maximum) throw std::runtime_error(message);
            if (argument == "--bird-flocks") options.birds.flockCount = static_cast<std::size_t>(count);
            else options.birds.birdsPerFlock = static_cast<std::size_t>(count);
            options.explicitBirds = true;
        }
        else if (argument == "--hud-screen") {
            const auto screen = value();
            if (screen == "inventory") options.hudScreen = viewer::HudScreen::Inventory;
            else if (screen == "skills") options.hudScreen = viewer::HudScreen::Skills;
            else if (screen == "health") options.hudScreen = viewer::HudScreen::Health;
            else if (screen == "map") options.hudScreen = viewer::HudScreen::Map;
            else if (screen == "settings" || screen == "graphics") options.hudScreen = viewer::HudScreen::Settings;
            else throw std::runtime_error("--hud-screen requires inventory, skills, health, map or settings");
        }
        else if (argument == "--frame-profile") options.frameProfile = value();
        else if (argument == "--sun-shadow-resolution") {
            const auto text=value();std::size_t used=0;
            try { options.sunShadowResolution=std::stoi(text,&used); }
            catch(const std::exception&) { throw std::runtime_error(argument+" requires integer pixels (256..8192)"); }
            if(used!=text.size() || options.sunShadowResolution<256 || options.sunShadowResolution>8192)
                throw std::runtime_error(argument+" requires integer pixels (256..8192)");
        }
        else if (argument == "--tree-billboard-distance" || argument == "--old-forest-distance") {
            auto& meters = argument == "--tree-billboard-distance" ? options.treeBillboardDistance : options.oldForestDistance;
            const auto text = value(); std::size_t used = 0;
            try { meters = std::stof(text, &used); }
            catch (const std::exception&) { throw std::runtime_error(argument + " requires nonnegative meters (0 disables)"); }
            if (used != text.size() || !std::isfinite(meters) || meters < 0)
                throw std::runtime_error(argument + " requires nonnegative meters (0 disables)");
        }

        else if (argument == "--no-vehicle-instancing") options.vehicleInstancing = false;
        else if (argument == "--no-cars") options.noCars = true;
        else if (argument == "--no-vehicle-lod") options.vehicleDistanceDetail = false;
        else if (argument == "--no-zombie-lod") options.zombieDistanceDetail = false;
        else if (argument == "--flashlight-shadows") options.flashlightShadows = true;
        else if (argument == "--no-flashlight-shadows") options.flashlightShadows = false;
        else if (argument == "--no-zombie-shadows") options.zombieShadows = false;
        else if (argument == "--zombie-draw-distance") {
            const auto text = value();
            std::size_t used = 0;
            float meters;
            try { meters = std::stof(text, &used); }
            catch (const std::exception&) { throw std::runtime_error("--zombie-draw-distance requires a nonnegative distance in meters"); }
            if (used != text.size() || !std::isfinite(meters) || meters < 0)
                throw std::runtime_error("--zombie-draw-distance requires a nonnegative distance in meters");
            options.zombieDrawDistance = meters;
        }
        else if (argument == "--hidden") options.hidden = true;
        else if (argument == "--fullscreen") options.fullscreen = true;
        else if (argument == "--resolution") {
            const auto size = value();
            if (size == "960x540") { options.width = 960; options.height = 540; }
            else if (size == "1280x720") { options.width = 1280; options.height = 720; }
            else if (size == "1920x1080") { options.width = 1920; options.height = 1080; }
            else throw std::runtime_error("--resolution requires 960x540, 1280x720 or 1920x1080");
        }
        else if (argument == "--allow-software") options.allowSoftware = true;
        else if (argument == "--self-test-input") options.selfTest = true;
        else if (argument == "--fps") options.fps = true;
        else if (argument == "--night") options.night = true;
        else if (argument == "--time-of-day" || argument == "--day-length") {
            const auto text = value();
            std::size_t used = 0;
            double number;
            const auto message = argument == "--time-of-day"
                ? "--time-of-day requires an hour in [0, 24)"
                : "--day-length requires zero (frozen) or at least one second";
            try { number = std::stod(text, &used); }
            catch (const std::exception&) { throw std::runtime_error(message); }
            if (used != text.size() || !std::isfinite(number) || number < 0.0 ||
                (argument == "--time-of-day" ? number >= 24.0 : number > 0.0 && number < 1.0))
                throw std::runtime_error(message);
            if (argument == "--time-of-day") options.dayNight.startHour = number;
            else options.dayNight.dayLengthSeconds = number;
            options.explicitDayNight = true;
        }
        else if (argument == "--rain") options.rain = true;
        else if (argument == "--no-audio") options.audio.enabled = false;
        else if (argument == "--no-hrtf") options.audio.hrtf = false;
        else if (argument == "--audio-dir") options.audio.directory = value();
        else if (argument == "--no-shadows") options.shadows = false;
        else if (argument == "--no-tree-shadows") options.treeShadows = false;
        else if (argument == "--no-tree-canopy-shading") options.treeCanopyShading = false;
        else if (argument == "--no-haze") options.haze = false;
        else if (argument == "--no-ground-fog") options.groundFog = false;
        else if (argument == "--collision-debug") options.collisionDebug = true;
        else if (argument == "--self-test-fps") options.selfTestFPS = true;
        else if (argument == "--trees") options.trees = value();
        else if (argument == "--poles") options.poles = value();
        else if (argument == "--guardrails") options.guardrails = value();
        else if (argument == "--no-guardrails") options.noGuardrails = true;
        else if (argument == "--no-poles") options.noPoles = true;
        else if (argument == "--forests" || argument == "--forest")
            throw std::runtime_error("--forests was removed; choose --vegetation osm-tuned2, orthophoto, osm, sentinel-astra or none");
        else if (argument == "--vegetation") options.vegetation.select(value());
        else if (argument == "--osm-vegetation")
            throw std::runtime_error("--osm-vegetation was replaced by --vegetation osm");
        else if (argument == "--no-forests") options.noForests = true;
        else if (argument == "--no-doodads") options.noDoodads = true;
        else if (argument == "--doodad-save") options.doodadSave = value();
        else if (argument == "--no-fences") options.noFences = true;
        else if (argument == "--no-roof-batching") options.roofBatching = false;
        else if (argument == "--distant-scene-cache") { options.distantSceneCache = true; options.graphics.distantScenery = 2; }
        else if (argument == "--no-wall-batching") options.wallBatching = false;
        else if (argument == "--no-trees") options.noTrees = true;
        else if (argument == "--no-tree-culling") options.treeCulling = false;
        else if (argument == "--no-tree-depth-ordering") options.treeDepthOrdering = false;
        else if (argument == "--no-tree-card-splitting") options.treeCardSplitting = false;
        else if (argument == "--no-tree-nearest-filtering") options.treeNearestFiltering = false;
        else if (argument == "--no-zombie-culling") options.zombieCulling = false;
        else if (argument == "--no-zombie-staggering") options.zombiePerceptionStaggering = false;
        else if (argument == "--no-zombie-perception") options.zombiePerception = false;
        else if (argument == "--no-town-culling") options.sceneCulling = false;
        else if (argument == "--no-tree-collisions") options.treeCollisions = false;
        else if (argument == "--zombies") { options.zombies = value(); options.explicitZombies = true; }
        else if (argument == "--dogs") options.dogs = value();
        else if (argument == "--dog-count") {
            const auto text = value(); std::size_t used = 0;
            options.dogCount = std::stoi(text, &used);
            if (used != text.size() || options.dogCount < 0 || options.dogCount > static_cast<int>(viewer::maxDogs))
                throw std::runtime_error("--dog-count requires an integer from 0 to " + std::to_string(viewer::maxDogs));
        }
        else if (argument == "--no-dogs") options.dogCount = 0;
        else if (argument == "--fixed-step") options.fixedStep = true;
        else if (argument == "--full-scenario-movement") options.fullScenarioMovement = true;
        else if (argument == "--scenario") {
            if (!options.scenario.empty()) throw std::runtime_error("Repeated --scenario option");
            options.scenario = value();
            if (options.scenario.empty() || options.scenario.string().rfind("--", 0) == 0)
                throw std::runtime_error("--scenario requires a scenario filename");
        }
        else if (argument == "--no-zombies") options.noZombies = true;
        else if (argument == "--zombie-view") { options.zombieView = true; options.explicitZombies = true; }
        else if (argument == "--zombie-count") {
            const auto text = value();
            std::size_t used = 0;
            try { options.zombieCount = std::stoi(text, &used); }
            catch (const std::exception&) { throw std::runtime_error("--zombie-count requires an integer from 1 to 100000"); }
            if (used != text.size() || options.zombieCount < 1 || options.zombieCount > 100000)
                throw std::runtime_error("--zombie-count requires an integer from 1 to 100000");
            options.explicitZombies = true;
            options.explicitZombieCount = true;
        }
        else if (argument == "--zombie-radius" || argument == "--animation-time") {
            const auto text = value();
            std::size_t used = 0;
            double number;
            try { number = std::stod(text, &used); }
            catch (const std::exception&) { throw std::runtime_error(argument + " requires a finite number"); }
            if (used != text.size() || !std::isfinite(number) || number < 0 ||
                (argument == "--zombie-radius" && (number <= 0 || number > 1000)))
                throw std::runtime_error(argument + " requires nonnegative seconds or a radius in (0, 1000] meters");
            if (argument == "--animation-time") options.animationTime = number;
            else {
                options.zombieRadius = static_cast<float>(number);
                options.explicitZombies = true;
                options.explicitZombieRadius = true;
            }
        }
        else if (argument == "--spawn") {
            auto coordinate = [&]() {
                const auto text = value();
                std::size_t used = 0;
                float number;
                try { number = std::stof(text, &used); }
                catch (const std::exception&) { throw std::runtime_error("--spawn requires finite X and Z coordinates in meters"); }
                if (used != text.size() || !std::isfinite(number))
                    throw std::runtime_error("--spawn requires finite X and Z coordinates in meters");
                return number;
            };
            const float x = coordinate(), z = coordinate();
            options.spawn = glm::vec2(x, z);
            options.fps = true;
        }
        else if (argument == "--screenshot") options.screenshot = value();
        else if (argument == "--shader-dir") options.shaders = value();
        else if (argument == "--frames") {
            const auto text = value();
            std::size_t used = 0;
            try { options.frames = std::stoi(text, &used); }
            catch (const std::exception&) { throw std::runtime_error("--frames requires a positive integer"); }
            if (used != text.size() || options.frames < 1)
                throw std::runtime_error("--frames requires a positive integer");
        } else if (!argument.empty() && argument[0] == '-') throw std::runtime_error("Unknown option: " + argument);
        else if (hasModel) throw std::runtime_error("Only one model filepath may be supplied");
        else { options.model = argument; hasModel = true; }
    }
    if ((options.hidden || options.selfTest || options.selfTestFPS || !options.screenshot.empty()) && options.frames == 0) options.frames = 3;
    if (options.noTrees && !options.trees.empty())
        throw std::runtime_error("Use either --trees FILE or --no-trees");
    (void)viewer::vegetationPaths(options.vegetation, options.model, VIEWER_FOREST_CONFIG,
                                  options.trees);
    if (options.noPoles && !options.poles.empty())
        throw std::runtime_error("Use either --poles FILE or --no-poles");
    if (options.noGuardrails && !options.guardrails.empty())
        throw std::runtime_error("Use either --guardrails FILE or --no-guardrails");
    if (options.noZombies && options.explicitZombies)
        throw std::runtime_error("Use zombie options or --no-zombies, not both");
    if (options.noBirds && options.explicitBirds)
        throw std::runtime_error("Use bird options or --no-birds, not both");
    if (options.birds.flockCount * options.birds.birdsPerFlock > 65536)
        throw std::runtime_error("Bird population must not exceed 65536");
    if (options.zombieView && options.fps)
        throw std::runtime_error("--zombie-view uses a free-fly overview; omit --fps/--spawn");
    if (options.night && options.explicitDayNight)
        throw std::runtime_error("Use --night for fixed night or --time-of-day/--day-length for the cycle");
    return options;
}

void printHelp() {
    std::cout << "Usage: godollo_viewer [model.glb|model.gltf] [options]\n"
              << "Default model: " << VIEWER_DEFAULT_ASSET << "\n\n"
              << "W/S forward/back, A/D strafe, Q/E down/up, mouse look, Shift faster\n"
              << "Slot 0 or held Alt: Object handling; hold left to drag, release to drop; hold right to charge throw/push\n"
              << "Object handling: mouse moves the held object; WASD moves the grip; right click also pushes objects within reach\n"
              << "Right tap drops; hold right and release throws. Heavy boxes drag; light boxes lift. Door mouse up/down still pushes/pulls\n"
              << "Hotbar: 0 Object handling, 1 Punch, 2 pistol, 3 SMG, 4 shotgun, 5 sniper, 6 HE grenade, 7 Molotov, 8 Sword, 9 Axe; R reload; hold left mouse for automatic fire\n"
              << "Combat: scoped weapons use right mouse to cycle normal, 10-degree, and 1-degree views\n"
              << "Alt handling / vehicle camera, Tab Sleep/Inventory cursor, Home cursor, wheel preview action, F throw weapon, G pick up weapon, L toggle flashlight, End frame model, Escape mission menu\n\n"
              << "F1 Inventory, F2 Skills, F3 Health, F4 Map, F12 Settings, F8 free-fly, F9 FPS walking, F10 collision overlay; Space jump, C prone in FPS\n\n"
              << "  --fps               Start in walking mode\n"
              << "  --fullscreen        Fill the primary monitor at the selected render resolution\n"
              << "  --resolution WxH    960x540, 1280x720 (default) or 1920x1080\n"
              << "  --time-of-day H     Starting hour in [0, 24), default 12 (noon)\n"
              << "  --day-length S      Seconds per full day, default 1200; 0 freezes the hour\n"
              << "  --night             Fixed night with a camera flashlight\n"
              << "  --rain              Enable nearby animated rain streaks\n"
              << "  --birds             Enable bird flocks even on small test scenes\n"
              << "  --bird-flocks N     Flock count (default 6, max 64)\n"
              << "  --birds-per-flock N Birds per flock (default 192, max 4096; total max 65536)\n"
              << "  --no-birds          Disable automatic bird flocks on town-sized terrain\n"
              << "  --no-audio          Disable playback; AI hearing stays active\n"
              << "  --no-hrtf           Use ordinary positional audio\n"
              << "  --audio-dir DIR     Load PCM16 WAV effects and ambience from DIR\n"
              << "  --no-shadows        Disable sun and flashlight shadows for comparison\n"
              << "  --sun-shadow-resolution N Sunlight shadow-map width/height (1024; 256..8192)\n"
              << "  --no-tree-shadows   Disable tree casting for both lights\n"
              << "  --distant-scene-cache Cache static scenery beyond 1 km at 15 Hz; F7 toggles at runtime\n"
              << "  --no-tree-canopy-shading Disable approximate daytime canopy shade\n"
              << "  --no-tree-depth-ordering Keep tree visibility-query order for comparison\n"
              << "  --tree-billboard-distance M Single-card tree LoD beyond M meters (100; 0 disables)\n"
              << "  --old-forest-distance M Visibility radius inside old forests (100; 0 disables)\n"
              << "  --no-haze           Disable atmospheric distance haze for comparison\n"
              << "  --no-ground-fog     Disable the analytical ground mist layer\n"
              << "  --spawn X Z         Start walking near world X/Z in meters\n"
              << "  --collision-debug   Show colliders and ground normal (F10)\n"
              << "  --trees FILE        Load a tree instance JSON explicitly\n"
              << "  --no-trees          Disable individual trees and runtime forests\n"
              << "  --vegetation MODE  Select osm-tuned2 (default), orthophoto, osm, sentinel-astra or none\n"
              << "  --no-forests        Disable runtime forests, keep individual trees\n"
              << "  --no-doodads        Disable streamed forest branches, stones and bottles\n"
              << "  --doodad-save FILE  Moved doodad poses (default beside map: .doodads.json)\n"
              << "  --no-fences         Disable fence panels, shadows and collisions\n"
              << "  --poles FILE        Load a mapped pole column JSON explicitly\n"
              << "  --no-poles          Disable pole columns, shadows and collisions\n"
              << "  --guardrails FILE   Load a mapped guardrail GLB layer explicitly\n"
              << "  --no-guardrails     Disable guardrail strips, shadows and collisions\n"
              << "  --no-roof-batching  Draw static roofs individually for comparison\n"
              << "  --no-wall-batching  Draw simple building walls individually for comparison\n"
              << "  --no-tree-culling   Render all tree instances for comparison\n"
              << "  --no-tree-card-splitting Keep full-width healthy tree cards for comparison\n"
              << "  --no-tree-nearest-filtering Restore authored vegetation filtering (usually trilinear)\n"

              << "  --no-vehicle-instancing Draw vehicles individually for comparison\n"
              << "  --no-cars           Disable parked cars and the fallback car\n"
              << "  --no-vehicle-lod    Keep near vehicle meshes and disable their distance cutoff\n"
              << "  --no-zombie-lod     Disable zombie mesh LOD, distance cutoff and stepped animation\n"
              << "  --zombie-draw-distance M Camera cutoff at normal FOV (180 m; overview unlimited; 0 disables)\n"
              << "  --flashlight-shadows Enable nearby flashlight shadows (default off)\n"
              << "  --no-flashlight-shadows Disable nearby flashlight shadows\n"
              << "  --no-zombie-shadows Disable zombie casting for GPU cost comparisons\n"
              << "  --profile           Print frame/GPU/CPU simulation and render timings\n"
              << "  --frame-profile FILE Save per-frame CPU/GPU timing CSV\n"
              << "  --no-zombie-culling Disable zombie camera-frustum culling for comparison\n"
              << "  --no-zombie-staggering Check zombie sight every simulation step\n"
              << "  --no-zombie-perception Skip activation scans, behaviors, sight and hearing\n"
              << "  --no-town-culling   Render all town draw instances for comparison\n"
              << "  --no-tree-collisions Disable the estimated trunk colliders\n"
              << "  --dogs PATH         Approved dog asset directory\n"
              << "  --dog-count N       Separate dog population, 0.." << viewer::maxDogs << " (default 0)\n"
              << "  --no-dogs           Disable dogs\n"
              << "  --zombies PATH      Load individual animated GLB/glTF files from PATH\n"
              << "  --fixed-step        Advance simulation by 1/60 s per frame (benchmark)\n"
              << "  --full-scenario-movement  Disable distant traveler approximations\n"
              << "  --scenario FILE     Load zombie groups and density-placed dog packs from TOML\n"
              << "  --zombie-count N    Number of zombies (default 300 in palace gardens, otherwise 1000)\n"
              << "  --zombie-radius M   Use radial placement near spawn/map center (default 100 m)\n"
              << "  --zombie-view       Start with the camera framing the crowd\n"
              << "  --no-zombies        Disable automatic zombies on town-sized terrain\n"
              << "  --animation-time S  Freeze animation at seconds S for repeatable captures\n"
              << "  --self-test-fps     Exercise mode-switch and jump callbacks\n"
              << "  --frames N          Render N frames, then exit\n"
              << "  --screenshot FILE   Save a PPM image; defaults to 3 frames\n"
              << "  --hud-screen NAME   Start with inventory, skills, health, map or settings visible\n"
              << "  --hidden            Hide the window for capture (display still required)\n"
              << "  --allow-software    Permit a CPU OpenGL driver for diagnostics\n"
              << "  --self-test-input   Exercise registered input/resize callbacks\n"
              << "  --shader-dir DIR    Load the viewer shaders from this directory\n";
}

void glfwError(int code, const char* description) {
    std::cerr << "GLFW error " << code << ": " << (description ? description : "unknown") << '\n';
}

struct GLFWLifetime {
    GLFWLifetime() {
        glfwSetErrorCallback(glfwError);
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW. Run from a desktop session with an accessible DISPLAY/XWayland (or configure the native Wayland backend).");
    }
    ~GLFWLifetime() { glfwTerminate(); }
};

struct AppState {
    AppState(const viewer::CollisionWorld& collision, const viewer::Model& model)
        : rig(collision, model.boundsMin, model.boundsMax, 1280.0f / 720.0f), world(collision) { weapons.syncAmmo(inventory); }
    viewer::CameraRig rig;
    viewer::PlayerInteraction interaction;
    viewer::Weapons weapons{VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY, true};
    viewer::PlayerInventory inventory = viewer::loadPlayerInventory(std::filesystem::path(VIEWER_INVENTORY).parent_path());
    viewer::HandInventory handInventory{inventory};
    viewer::ActionSelection actionSelection;
    viewer::WeaponSwitch weaponSwitch;
    viewer::WeaponReload weaponReload;
    viewer::WeaponMotion weaponMotion;
    viewer::SwordCombat swordCombat;
    viewer::AxeCombat axeCombat;
    viewer::ShotInaccuracy shotInaccuracy;
    viewer::PlayerVitals vitals;
    double lastDamageSeconds = -1;
    viewer::DoorLayer* doors = nullptr;
    viewer::BoxLayer* boxes = nullptr;
    viewer::GroundItems* groundItems = nullptr;
    viewer::PhysicsScene* physicsScene = nullptr;
    viewer::VehicleLayer* cars = nullptr;
    viewer::VehiclePlayer driver;
    viewer::InteriorObjects* interiors = nullptr;
    const viewer::FloorplanLayer* floorplans = nullptr;
    viewer::PlayerHouseState house;
    const viewer::CollisionWorld& world;
    viewer::ZombieLayer* zombies = nullptr;
    viewer::DogLayer* dogs = nullptr;
    viewer::SceneAudio* audio = nullptr;
    viewer::Grenades* grenades = nullptr;
    viewer::PlayerBody* body = nullptr;
    viewer::MeleeCombat* melee = nullptr;
    double displayedAnimationSeconds = 0.0;
    glm::vec3 boundsMin, boundsMax;
    std::array<bool, GLFW_KEY_LAST + 1> keys{};
    bool proneToggleQueued = false;
    int width = 1280, height = 720;
    bool capture = true, firstMouse = true, hidden = false;
    bool equipmentCursor = false;
    bool collisionDebug = false;
    viewer::HudScreen hudScreen = viewer::HudScreen::Gameplay;
    viewer::Hud* hud = nullptr; // Borrows the graphics-thread HUD.
    viewer::SleepPlan* sleepPlan = nullptr;
    bool flashlight = true;
    bool distantSceneCache = false;
    glm::vec3 playerVelocity{0};
    double lastX = 0, lastY = 0;
    bool temporaryHandling() const {
        return (capture || equipmentCursor) && hudScreen == viewer::HudScreen::Gameplay && !driver.occupied() &&
            weapons.slot() != viewer::handlingSlot && (keys[GLFW_KEY_LEFT_ALT] || keys[GLFW_KEY_RIGHT_ALT]);
    }
    bool handling() const { return weapons.slot() == viewer::handlingSlot || temporaryHandling(); }
};

void emitNoise(AppState& app, const viewer::NoiseEvent& noise) {
    if (app.audio) if (const auto sound = viewer::noiseImpactSound(noise)) app.audio->impact(*sound);
    if (app.zombies) app.zombies->makeNoise(noise);
    if (app.dogs) app.dogs->makeNoise(noise);
    if (app.sleepPlan && app.sleepPlan->state().inSleepSession(1))
        app.sleepPlan->hearNoise(noise, app.rig.camera().position(), viewer::SimulationClock::instance().seconds());
}

AppState& state(GLFWwindow* window) { return *static_cast<AppState*>(glfwGetWindowUserPointer(window)); }

void captureMouse(GLFWwindow* window, bool captured) {
    auto& app = state(window);
    app.equipmentCursor = false;
    app.capture = captured;
    if (!captured) {
        if (app.groundItems) app.handInventory.release(app.interaction, *app.groundItems, app.weapons);
        else app.interaction.cancel();
        app.weapons.cancelTrigger();
        app.keys[GLFW_KEY_LEFT_ALT] = app.keys[GLFW_KEY_RIGHT_ALT] = false;
    }
    if (!captured) app.actionSelection.close();
    app.firstMouse = true;
    if (!app.hidden) glfwSetInputMode(window, GLFW_CURSOR,
        captured && app.hudScreen == viewer::HudScreen::Gameplay ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void framebufferCallback(GLFWwindow* window, int width, int height) {
    auto& app = state(window);
    app.width = width;
    app.height = height;
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto& app = state(window);
    if (app.sleepPlan && app.sleepPlan->state().asleep(1) && app.hudScreen != viewer::HudScreen::MissionMenu &&
        key != GLFW_KEY_TAB && key != GLFW_KEY_HOME && key != GLFW_KEY_ESCAPE) {
        if (key >= 0 && key <= GLFW_KEY_LAST) app.keys[static_cast<std::size_t>(key)] = false;
        return;
    }
    const bool wasTemporaryHandling = app.temporaryHandling();
    if (key >= 0 && key <= GLFW_KEY_LAST)
        app.keys[static_cast<std::size_t>(key)] = action != GLFW_RELEASE && app.hudScreen == viewer::HudScreen::Gameplay;
    if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT) {
        if (wasTemporaryHandling != app.temporaryHandling()) {
            app.interaction.cancel(); app.weapons.cancelTrigger(); app.actionSelection.close();
            if (app.temporaryHandling()) while (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original)
                app.rig.camera().cycleViewMode();
        }
        if (action == GLFW_PRESS && app.driver.occupied() && app.hudScreen == viewer::HudScreen::Gameplay && app.cars)
            app.driver.toggleThirdPerson(*app.cars, app.rig.camera(), app.world);
        return;
    }
    if (action != GLFW_PRESS) return;
    if (app.hudScreen == viewer::HudScreen::MissionMenu) {
        if (key == GLFW_KEY_ESCAPE && app.hud && app.hud->cancelMissionConfirmation()) return;
        if (key != GLFW_KEY_ESCAPE) {
            if (app.hud) {
                if (key == GLFW_KEY_UP) app.hud->missionMenuKey(viewer::MissionMenuKey::Previous);
                if (key == GLFW_KEY_DOWN) app.hud->missionMenuKey(viewer::MissionMenuKey::Next);
                if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) app.hud->missionMenuKey(viewer::MissionMenuKey::Activate);
            }
            return;
        }
    }
    if ((key >= GLFW_KEY_F1 && key <= GLFW_KEY_F4) || key == GLFW_KEY_F12 ||
        (key == GLFW_KEY_ESCAPE && app.hudScreen != viewer::HudScreen::Gameplay)) {
        constexpr std::array<viewer::HudScreen, 4> screens{viewer::HudScreen::Inventory, viewer::HudScreen::Skills,
            viewer::HudScreen::Health, viewer::HudScreen::Map};
        const auto requested = key == GLFW_KEY_ESCAPE ? viewer::HudScreen::Gameplay :
            key == GLFW_KEY_F12 ? viewer::HudScreen::Settings : screens[key - GLFW_KEY_F1];
        app.hudScreen = app.hudScreen == requested ? viewer::HudScreen::Gameplay : requested;
        app.keys.fill(false); app.proneToggleQueued = false; app.firstMouse = true;
        app.actionSelection.close();
        app.weapons.cancelTrigger(); app.interaction.cancel();
        if (app.hud) app.hud->cancelPointerInteraction();
        if (!app.hidden) glfwSetInputMode(window, GLFW_CURSOR,
            app.capture && app.hudScreen == viewer::HudScreen::Gameplay ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        return;
    }
    if (app.hudScreen != viewer::HudScreen::Gameplay) return;
    if (key == GLFW_KEY_TAB) {
        app.equipmentCursor = app.capture; app.capture = !app.capture;
        app.firstMouse = true; app.actionSelection.close(); app.weapons.cancelTrigger();
        if (!app.hidden) glfwSetInputMode(window, GLFW_CURSOR, app.capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        return;
    }
    if (key == GLFW_KEY_F7) {
        app.distantSceneCache = !app.distantSceneCache;
        std::cout << "Distant scenery cache: " << (app.distantSceneCache ? "on" : "off") << '\n';
        return;
    }
    if (key == GLFW_KEY_ENTER && app.cars) {
        if (app.driver.toggle(*app.cars, app.rig, app.world)) {
            app.weapons.cancelTrigger(); app.interaction.cancel(); app.proneToggleQueued = false;
            while (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original)
                app.rig.camera().cycleViewMode();
            std::cout << (app.driver.occupied() ? (app.driver.driving() ? "Driving: WASD, Space brake, Enter exit when stopped\n" : "Passenger: mouse look, Alt camera, Enter exit when stopped\n") : "Left vehicle\n");
        } else std::cout << "Enter needs a nearby stopped car, or a clear exit beside a stopped car\n";
        return;
    }
    if (app.driver.occupied() && key != GLFW_KEY_ESCAPE && key != GLFW_KEY_HOME &&
        key != GLFW_KEY_LEFT_ALT && key != GLFW_KEY_RIGHT_ALT) return;
    if (key == GLFW_KEY_ESCAPE && app.actionSelection.open()) { app.actionSelection.close(); return; }
    if (key == GLFW_KEY_ESCAPE) {
        app.hudScreen = viewer::HudScreen::MissionMenu;
        app.keys.fill(false); app.proneToggleQueued = false;
        app.weapons.cancelTrigger(); app.interaction.cancel(); app.firstMouse = true;
        if (app.hud) app.hud->cancelPointerInteraction();
        if (!app.hidden) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    else if (key == GLFW_KEY_0 || key == GLFW_KEY_GRAVE_ACCENT || (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)) {
        app.interaction.cancel();
        app.actionSelection.highlight((key == GLFW_KEY_0 || key == GLFW_KEY_GRAVE_ACCENT) ? 0 : key - GLFW_KEY_1 + 1);
        app.weapons.cancelPrimaryTrigger();
    } else if (key == GLFW_KEY_R) {
        if (app.temporaryHandling() || app.weaponSwitch.pending() || app.actionSelection.open()) return;
        if (app.weaponReload.begin(app.weapons, app.inventory, viewer::SimulationClock::instance().seconds()) && app.audio)
            app.audio->reload(app.weapons.slot());
        std::cout << app.weapons.status() << '\n';
    } else if (key == GLFW_KEY_HOME) captureMouse(window, !app.capture);
    else if (key == GLFW_KEY_F && app.capture && app.groundItems) {
        if (app.weaponSwitch.pending() || app.weaponReload.pending()) return;
        if (app.groundItems->discard(app.weapons,app.inventory,app.rig.camera(),app.world,app.playerVelocity)) {
            while (app.rig.camera().viewMode()!=viewer::Camera::ViewMode::original) app.rig.camera().cycleViewMode();
            std::cout << "Discarded weapon | " << app.weapons.status() << '\n';
        }
    } else if (key == GLFW_KEY_G && app.capture && app.groundItems) {
        if (app.weaponSwitch.pending() || app.weaponReload.pending()) return;
        if (app.groundItems->pickUp(app.weapons,app.inventory,app.rig.camera(),app.world)) {
            while (app.rig.camera().viewMode()!=viewer::Camera::ViewMode::original) app.rig.camera().cycleViewMode();
            std::cout << "Picked up weapon | " << app.weapons.status() << '\n';
        }
    } else if (key == GLFW_KEY_L) {
        app.flashlight = !app.flashlight;
        std::cout << "Flashlight: " << (app.flashlight ? "on" : "off") << '\n';
    } else if (key == GLFW_KEY_END) {
        app.weapons.cancelTrigger();
        app.interaction.cancel();
        app.rig.frame(static_cast<float>(std::max(app.width, 1)) / static_cast<float>(std::max(app.height, 1)));
    } else if (key == GLFW_KEY_F8) {
        app.proneToggleQueued = false;
        app.weapons.cancelTrigger();
        app.interaction.cancel();
        app.rig.enterFreeFly();
    } else if (key == GLFW_KEY_F9) {
        app.weapons.cancelTrigger();
        app.interaction.cancel();
        try { app.rig.enterWalking(); }
        catch (const std::exception& error) { std::cerr << "FPS placement failed: " << error.what() << '\n'; }
    } else if (key == GLFW_KEY_F10) {
        app.collisionDebug = !app.collisionDebug;
        std::cout << "Collision overlay: " << (app.collisionDebug ? "on" : "off") << '\n';
    } else if (key == GLFW_KEY_C && app.rig.walking()) {
        app.proneToggleQueued = !app.proneToggleQueued;
    }
}

void hudPointer(GLFWwindow* window, viewer::Hud* hud, double x, double y) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    if (hud && width > 0 && height > 0) hud->pointerMove(float(x / width), float(y / height));
}
void cursorCallback(GLFWwindow* window, double x, double y) {
    auto& app = state(window);
    if (app.sleepPlan && app.sleepPlan->state().asleep(1) && app.capture && app.hudScreen == viewer::HudScreen::Gameplay) { app.firstMouse = true; return; }
    if (app.hudScreen != viewer::HudScreen::Gameplay || !app.capture) {
        hudPointer(window, app.hud, x, y);
        if (app.equipmentCursor && !app.firstMouse) app.interaction.moveGrip(float(x - app.lastX), float(app.lastY - y));
        app.lastX = x; app.lastY = y; app.firstMouse = false; return;
    }
    if (!app.capture || app.hudScreen != viewer::HudScreen::Gameplay) return;
    if (!app.firstMouse && std::isfinite(x) && std::isfinite(y)) {
        const glm::vec2 drag(static_cast<float>(x - app.lastX), static_cast<float>(app.lastY - y));
        if (viewer::directionalMeleeSlot(app.weapons.slot()) && !app.handling() && !app.driver.occupied() &&
            !app.actionSelection.open() && !app.weaponSwitch.pending() &&
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            app.weapons.directionalMelee().drag(drag);
            app.weapons.directionalMelee().update(viewer::SimulationClock::instance().seconds());
        } else app.interaction.look(app.rig.camera(), drag.x, drag.y);
    }
    app.lastX = x;
    app.lastY = y;
    app.firstMouse = false;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    auto& app = state(window);
    if (app.sleepPlan && app.sleepPlan->state().asleep(1) && app.capture && app.hudScreen == viewer::HudScreen::Gameplay) return;
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
        app.weapons.secondaryTrigger(false, viewer::SimulationClock::instance().seconds());
    if (app.hudScreen != viewer::HudScreen::Gameplay || !app.capture) {
        double x, y; glfwGetCursorPos(window, &x, &y);
        hudPointer(window, app.hud, x, y);
        if (app.hud) app.hud->pointerButton(button, action == GLFW_PRESS);
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE && app.handInventory.active() && app.groundItems) {
        app.handInventory.release(app.interaction, *app.groundItems, app.weapons); return;
    }
    if (app.handInventory.active() && button != GLFW_MOUSE_BUTTON_RIGHT) return;
    if ((button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) && action == GLFW_RELEASE) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) app.weapons.trigger(false);
        if (app.handling() && !app.weaponSwitch.pending() && app.capture &&
            app.doors && app.boxes)
            app.interaction.release(button == GLFW_MOUSE_BUTTON_LEFT ? viewer::Hand::Left : viewer::Hand::Right,
                app.rig.camera(), *app.doors, *app.boxes, app.world, viewer::SimulationClock::instance().seconds());
        return;
    }
    if (action != GLFW_PRESS || !app.capture || app.driver.occupied() || app.hudScreen != viewer::HudScreen::Gameplay) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT && app.actionSelection.open()) {
        const auto target = app.actionSelection.confirm(viewer::actionShortcuts(app.inventory), app.weapons.slot());
        if (target && app.weaponSwitch.begin(app.weapons, app.inventory, *target, viewer::SimulationClock::instance().seconds())) {
            app.interaction.cancel();
            while (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original)
                app.rig.camera().cycleViewMode();
        }
        return; // Confirmation never fires, punches or grabs an object.
    }
    if (app.weaponSwitch.pending() || app.weaponReload.pending()) return;
    if (button == GLFW_MOUSE_BUTTON_RIGHT && !app.handling()) {
        if (app.weapons.equipped().config.grenade)
            app.weapons.secondaryTrigger(true, viewer::SimulationClock::instance().seconds());
        else if (app.weapons.equipped().owned && app.weapons.equipped().config.hasScope) app.rig.camera().cycleViewMode();
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) return;
    if (app.handling()) {
        const auto hand = button == GLFW_MOUSE_BUTTON_LEFT ? viewer::Hand::Left : viewer::Hand::Right;
        if (app.doors && app.boxes && app.interaction.press(hand, app.rig.camera(), *app.doors, *app.boxes, app.world,
                viewer::SimulationClock::instance().seconds()) &&
            app.interaction.grabbedDoor() && hand == viewer::Hand::Left) {
            const auto id = *app.interaction.grabbedDoor();
            if (app.audio) app.audio->grabDoor(*app.doors, id);
            if (app.interiors) {
                const auto added = app.interiors->populate(id, app.world, *app.physicsScene, *app.boxes);
                if (added) std::cout << "Interior: spawned " << added << " physical boxes\n";
            }
        }
        return;
    }
    app.weapons.trigger(true);
}

void scrollCallback(GLFWwindow* window, double, double y) {
    if ((state(window).hudScreen != viewer::HudScreen::Gameplay || !state(window).capture) && state(window).hud) {
        double x, cursorY; glfwGetCursorPos(window, &x, &cursorY);
        hudPointer(window, state(window).hud, x, cursorY);
        state(window).hud->mouseWheel(static_cast<float>(y));
    }
    if (state(window).hudScreen != viewer::HudScreen::Gameplay || !state(window).capture) return;
    auto& app = state(window);
    if (app.capture && !app.driver.occupied()) {
        if (std::isfinite(y) && y != 0) {
            app.interaction.cancel();
            app.actionSelection.cycle(y > 0 ? 1 : -1, viewer::actionShortcuts(app.inventory), app.weapons.slot());
            app.weapons.cancelPrimaryTrigger();
        }
        return;
    }
    auto& rig = state(window).rig;
    if (!rig.walking()) rig.camera().adjustSpeed(std::pow(1.2f, static_cast<float>(std::clamp(y, -20.0, 20.0))));
}

void focusCallback(GLFWwindow* window, int focused) {
    if (!focused) {
        if (state(window).hud) state(window).hud->cancelPointerInteraction();
        state(window).keys.fill(false);
        state(window).proneToggleQueued = false;
        state(window).firstMouse = true;
        if (state(window).groundItems) state(window).handInventory.release(state(window).interaction, *state(window).groundItems, state(window).weapons);
        else state(window).interaction.cancel();
        state(window).weapons.cancelTrigger();
        state(window).actionSelection.close();
    }
}

// True only for a firearm shot that should pulse the held muzzle light.
bool updateWeapons(AppState& app, double seconds) {
    if (app.driver.occupied() || !app.capture || app.weaponSwitch.pending() ||
        app.handling() || !app.weapons.equipped().owned) {
        app.weapons.cancelTrigger();
        return false;
    }
    const auto& config = app.weapons.equipped().config;
    if (config.grenade && (!app.grenades || !app.grenades->canThrow(app.rig.camera().position(), app.world, config.grenade->kind))) {
        app.weapons.cancelTrigger();
        return false;
    }
    if (app.weapons.slot() == viewer::axeSlot) {
        app.weapons.axe().update(seconds);
        app.axeCombat.update(app.weapons.axe(), app.rig.camera(), seconds, app.displayedAnimationSeconds,
            app.world, app.zombies, app.dogs);
        return false;
    }
    if (app.weapons.slot() == viewer::swordSlot) {
        app.weapons.sword().update(seconds);
        app.swordCombat.update(app.weapons.sword(), app.rig.camera(), seconds, app.displayedAnimationSeconds,
            app.world, app.zombies, app.dogs);
        return false;
    }
    if (app.weapons.slot() == viewer::punchSlot) {
        app.weapons.fireAttempt(seconds, [&] {
            return app.body && app.melee && app.melee->punch(*app.body,
                app.rig.camera().position(), app.rig.camera().direction(), app.world);
        });
        return false; // Fists do not create muzzle lights or firearm recoil.
    }
    const auto result = app.weapons.fireAttempt(seconds);
    if (result == viewer::WeaponFireResult::empty && app.audio) app.audio->emptyClick();
    if (result != viewer::WeaponFireResult::fired) return false;
    const float sway = viewer::sniperSwayScale(glm::length(glm::vec2(app.playerVelocity.x, app.playerVelocity.z)),
        app.rig.player().movementState().posture, app.rig.player().config());
    const auto aim = app.weaponMotion.aimCamera(app.rig.camera(), app.weapons.slot(), seconds, true, sway);
    if (config.grenade) {
        if (!app.grenades->throwFrom(app.rig.camera().position(), app.rig.camera().direction(),
                app.playerVelocity, seconds, *config.grenade, app.world, app.weapons.grenadeThrowPower()))
            throw std::logic_error("Accepted grenade throw failed");
        app.weaponReload.begin(app.weapons, app.inventory, seconds);
    } else if (app.audio) app.audio->shot(app.weapons.slot());
    if (!config.grenade) {
        const auto origin = app.rig.camera().position();
        const float sigma = viewer::hipFireInaccuracy(config,
            glm::length(glm::vec2(app.playerVelocity.x, app.playerVelocity.z)),
            app.rig.player().movementState().posture, app.rig.player().config(),
            aim.viewMode() != viewer::Camera::ViewMode::original);
        const auto shotDirection = viewer::inaccurateShotDirection(aim.direction(), app.shotInaccuracy.sample(sigma));
        emitNoise(app, {origin,110});
        for (const auto direction : viewer::weaponShotDirections(config,shotDirection)) {
            const auto dog = app.dogs ? app.dogs->traceShot(origin,direction,app.world,app.displayedAnimationSeconds) : std::nullopt;
            // Keep the nearest species on the ray; a pellet cannot hit both.
            bool nearerZombie = false;
            if (dog && app.zombies) {
                std::vector<std::size_t> ids;
                app.zombies->queryLivingNear(origin,dog->distance+2,ids);
                for (const auto id : ids) {
                    const auto trace = app.zombies->traceShot(id,app.zombies->instances[id],origin,direction,app.displayedAnimationSeconds);
                    if (trace && trace->distance < dog->distance) { nearerZombie = true; break; }
                }
            }
            if (dog && !nearerZombie) app.dogs->damage(dog->dog,config.caliber == viewer::ShotCaliber::high ? .2f : .1f);
            else if (app.zombies) app.zombies->shootWithOcclusion(origin,direction,app.displayedAnimationSeconds,app.world,config.caliber);
        }
    }
    if (!config.grenade) app.weaponMotion.fire(app.weapons.slot(), seconds);
    std::cout << app.weapons.status() << '\n';
    return !config.grenade;
}

void moveCamera(AppState& app, float dt) {
    if (app.sleepPlan && app.sleepPlan->state().asleep(1)) {
        app.rig.restSleeping(dt);
        app.playerVelocity = {};
        return;
    }
    app.rig.setMovementHealth(app.vitals.healthState());
    if (app.driver.occupied()) {
        app.rig.restSeated(dt);
        app.driver.updateCamera(*app.cars, app.rig.camera(), app.world);
        return;
    }
    const auto previousFeet = app.rig.player().feet();
    const auto held = [&](int key) { return app.keys[static_cast<std::size_t>(key)] ? 1.0f : 0.0f; };
    app.rig.update(dt, held(GLFW_KEY_W) - held(GLFW_KEY_S), held(GLFW_KEY_D) - held(GLFW_KEY_A),
                   held(GLFW_KEY_E) - held(GLFW_KEY_Q),
                   app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT], app.keys[GLFW_KEY_SPACE],
                   app.keys[GLFW_KEY_LEFT_CONTROL] || app.keys[GLFW_KEY_RIGHT_CONTROL], app.proneToggleQueued);
    app.playerVelocity = app.rig.walking() && dt > 0 ? (app.rig.player().feet() - previousFeet) / dt : glm::vec3(0);
    if (app.rig.walking()) {
        // Fixed player steps can span several short render frames. Bound the
        // measured horizontal speed and use the actual airborne velocity.
        const float horizontal = glm::length(glm::vec2(app.playerVelocity.x, app.playerVelocity.z));
        const float speed = glm::length(app.rig.player().movementState().horizontalVelocity);
        if (horizontal > speed) {
            app.playerVelocity.x *= speed / horizontal;
            app.playerVelocity.z *= speed / horizontal;
        }
        app.playerVelocity.y = app.rig.player().verticalVelocity();
    }
    if (dt > 0) app.proneToggleQueued = false;
    if (app.doors && app.boxes)
        app.interaction.update(app.rig.camera(), dt, *app.doors, *app.boxes, app.world,
                               viewer::SimulationClock::instance().seconds());
    if (app.groundItems && app.boxes) {
        if (app.handInventory.active() && (!app.handling() || app.hudScreen != viewer::HudScreen::Gameplay || app.actionSelection.open()))
            app.handInventory.release(app.interaction, *app.groundItems, app.weapons);
        else app.handInventory.update(dt, app.rig.camera(), app.interaction, *app.boxes, *app.groundItems, app.weapons, app.world);
    }
}

void updateZombieBehavior(AppState& app, float seconds, double animationSeconds,
                          viewer::zombie_profile::Profile* profile) {
    const auto& player = app.rig.player();
    const auto& config = player.config();
    const float eyeHeight = app.rig.walking() ? player.eyeHeight() : config.eyeHeight;
    const float height = app.rig.walking() ? player.bodyHeight() : config.height;
    const auto eye = app.driver.occupied() ? app.driver.eyePosition(*app.cars) : app.rig.camera().position();
    const auto feet = app.rig.walking() ? player.feet() : eye - glm::vec3(0, eyeHeight, 0);
    if (app.floorplans) app.house.update(feet, viewer::SimulationClock::instance().seconds(), app.floorplans->footprints());
    if (!app.zombies && !app.dogs) return;
    viewer::ZombiePlayer target{feet, eyeHeight, height};
    target.velocity = app.driver.car() ? app.cars->velocity(*app.driver.car()) : app.playerVelocity;
    target.radius = app.rig.walking() ? player.bodyRadius() : config.radius;
    if (app.rig.walking()) target.collisionBox = player.collisionBox();
    target.house = app.house.house();
    if (app.dogs) {
        {
            viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::dogScent);
            app.dogs->updateScent(viewer::SimulationClock::instance().seconds(),{{feet,app.vitals.bleeding(),app.house.outside()}});
        }
        {
            viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::dogBehavior);
            app.dogs->updateBehavior(seconds, {target}, app.world, viewer::SimulationClock::instance().seconds(),
                                     app.zombies ? app.zombies->detachedParts() : nullptr);
        }
        for (const auto& hit : app.dogs->attackHits())
            if (app.vitals.applyDamage(hit.damage) > 0) {
                app.lastDamageSeconds = viewer::SimulationClock::instance().seconds();
            }
    }
    viewer::zombie_profile::Capture capture(profile);
    if (app.zombies) app.zombies->updateBehavior(seconds, target, app.world, animationSeconds);
    if (app.zombies) for (const auto& hit : app.zombies->attackHits())
        if (app.vitals.applyDamage(hit.damage) > 0) {
            app.lastDamageSeconds = viewer::SimulationClock::instance().seconds();
        }
}

void inputSelfTest(GLFWwindow* window) {
    int originalWidth, originalHeight;
    glfwGetWindowSize(window,&originalWidth,&originalHeight);
    auto& app = state(window);
    // Retrieve the installed callbacks, so this tests the same input path as
    // GLFW events, rather than separate synthetic camera movement code.
    const auto key = glfwSetKeyCallback(window, keyCallback);
    const auto cursor = glfwSetCursorPosCallback(window, cursorCallback);
    const auto mouse = glfwSetMouseButtonCallback(window, mouseButtonCallback);
    const auto resize = glfwSetFramebufferSizeCallback(window, framebufferCallback);
    if (!key || !cursor || !mouse || !resize) throw std::runtime_error("Input callbacks were not registered");
    const auto original = app.rig.camera();
    const auto originalWeapons = app.weapons;
    const auto originalInventory = app.inventory;
    const auto choose = [&](int hotbarSlot) {
        const auto previous = app.weapons.slot();
        const int code = hotbarSlot == 0 ? GLFW_KEY_GRAVE_ACCENT : GLFW_KEY_1 + hotbarSlot - 1;
        key(window, code, 0, GLFW_PRESS, 0);
        key(window, code, 0, GLFW_RELEASE, 0);
        if (!app.actionSelection.open() || app.weapons.slot() != previous)
            throw std::runtime_error("Number key must preview without equipping");
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        if (const auto* timing = app.weaponSwitch.timing()) {
            if (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original)
                throw std::runtime_error("Switch confirmation must immediately drop the scope");
            const double ready = viewer::SimulationClock::instance().seconds() + timing->total();
            if (app.weaponSwitch.update(app.weapons, ready))
                while (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original) app.rig.camera().cycleViewMode();
        }
    };
    key(window, GLFW_KEY_1, 0, GLFW_PRESS, 0);
    scrollCallback(window, 0, 1);
    if (app.actionSelection.selected() != 2)
        throw std::runtime_error("Scrolling up must preview the next hotbar action");
    scrollCallback(window, 0, -1);
    if (app.actionSelection.selected() != 1 || app.weapons.slot() != originalWeapons.slot())
        throw std::runtime_error("Scrolling down must preview the previous action without equipping");
    app.actionSelection.close();
    const bool originalFlashlight = app.flashlight;
    const auto viewBeforeFlashlight = app.rig.camera().view();
    key(window, GLFW_KEY_L, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_L, 0, GLFW_REPEAT, 0);
    key(window, GLFW_KEY_L, 0, GLFW_RELEASE, 0);
    if (app.flashlight == originalFlashlight || app.rig.camera().view() != viewBeforeFlashlight)
        throw std::runtime_error("L must toggle the flashlight once without reframing the camera");
    key(window, GLFW_KEY_L, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_L, 0, GLFW_RELEASE, 0);
    if (app.flashlight != originalFlashlight)
        throw std::runtime_error("A second L press must restore the flashlight");
    if (app.groundItems) {
        const auto before = app.groundItems->count();
        const auto middle = (app.boundsMin+app.boundsMax)*.5f;
        app.rig.camera().setPosition({middle.x,app.boundsMax.y+5,middle.z});
        app.rig.camera().setOrientation(-90,0);
        key(window,GLFW_KEY_F,0,GLFW_PRESS,0);
        key(window,GLFW_KEY_F,0,GLFW_REPEAT,0);
        key(window,GLFW_KEY_F,0,GLFW_RELEASE,0);
        if (app.groundItems->count()!=before+1 || app.weapons.equip(originalWeapons.slot()))
            throw std::runtime_error("F must discard one weapon and remove its inventory ownership");
        key(window,GLFW_KEY_G,0,GLFW_PRESS,0);
        key(window,GLFW_KEY_G,0,GLFW_RELEASE,0);
        if (app.groundItems->count()!=before || app.weapons.slot()!=originalWeapons.slot() ||
            app.weapons.equipped().loaded!=originalWeapons.equipped().loaded || app.flashlight!=originalFlashlight)
            throw std::runtime_error("G must recover the weapon and ammunition without toggling the flashlight");
        app.rig.camera() = original;
    }
    const bool wasWalking = app.rig.walking();
    app.rig.enterFreeFly();
    for (int code : {GLFW_KEY_W, GLFW_KEY_S, GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_Q, GLFW_KEY_E}) {
        const auto before = app.rig.camera().position();
        key(window, code, 0, GLFW_PRESS, 0);
        moveCamera(app, 0.1f);
        key(window, code, 0, GLFW_RELEASE, 0);
        if (glm::length(app.rig.camera().position() - before) < 1e-5f) throw std::runtime_error("Movement input self-test failed");
    }
    const auto before = app.rig.camera().view();
    app.firstMouse = true;
    cursor(window, 100, 100);
    cursor(window, 145, 80);
    float change = 0;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) change += std::abs(app.rig.camera().view()[c][r] - before[c][r]);
    if (change < 1e-5f) throw std::runtime_error("Mouse input self-test failed");
    const auto originalProjection = app.rig.camera().projection(1.0f);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Unscoped pistol changed the view mode");
    choose(5);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    const auto tenDegreeProjection = app.rig.camera().projection(1.0f);
    const float tenDegrees = glm::degrees(2.0f * std::atan(1.0f / tenDegreeProjection[1][1]));
    if (std::abs(tenDegrees - 10.0f) > 1e-5f || app.rig.camera().viewMode() != viewer::Camera::ViewMode::zoom10)
        throw std::runtime_error("Right-click 10-degree view self-test failed");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != tenDegreeProjection)
        throw std::runtime_error("Mouse release changed the view mode");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    const auto oneDegreeProjection = app.rig.camera().projection(1.0f);
    const float oneDegree = glm::degrees(2.0f * std::atan(1.0f / oneDegreeProjection[1][1]));
    if (std::abs(oneDegree - 1.0f) > 1e-5f || app.rig.camera().viewMode() != viewer::Camera::ViewMode::zoom1)
        throw std::runtime_error("Right-click 1-degree view self-test failed");
    key(window, GLFW_KEY_2, 0, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original || !app.actionSelection.open())
        throw std::runtime_error("Scope clicks must work without confirming or closing the hotbar selector");
    key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Right-click did not restore the original field of view");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    choose(5);
    if (app.rig.camera().viewMode() != viewer::Camera::ViewMode::zoom10)
        throw std::runtime_error("Confirming the equipped action must preserve the scope");
    choose(2);
    for (const bool wheel : {false, true}) {
        const auto loaded = app.weapons.equipped().loaded;
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        if (wheel) scrollCallback(window, 0, 1);
        else key(window, GLFW_KEY_3, 0, GLFW_PRESS, 0);
        updateWeapons(app, viewer::SimulationClock::instance().seconds());
        if (app.actionSelection.selected() != 3 || app.weaponSwitch.pending() || app.weapons.slot() != 0 ||
            app.weapons.equipped().loaded != loaded)
            throw std::runtime_error("Opening the selector with fire held must neither fire nor switch weapons");
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        if (app.weaponSwitch.pending() || !app.actionSelection.open())
            throw std::runtime_error("Releasing held fire must not confirm the selector");
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        if (!app.weaponSwitch.pending() || app.actionSelection.open())
            throw std::runtime_error("A fresh click must confirm the highlighted weapon");
        app.weaponSwitch.update(app.weapons,
            viewer::SimulationClock::instance().seconds() + app.weaponSwitch.timing()->total());
        if (app.weapons.slot() != 1) throw std::runtime_error("Fresh-click confirmation must equip the SMG");
        choose(2);
    }
    key(window, GLFW_KEY_0, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_0, 0, GLFW_RELEASE, 0);
    if (app.actionSelection.selected() != 0 || app.weapons.slot() != 0)
        throw std::runtime_error("Number-row 0 must preview Object handling without equipping");
    choose(0);
    if (app.weapons.slot() != viewer::handlingSlot)
        throw std::runtime_error("Default slot 0 failed to equip Object handling");
    choose(0);
    if (app.weaponSwitch.pending())
        throw std::runtime_error("Confirming the current action must have no switch delay");
    choose(1);
    if (app.weapons.slot() != viewer::punchSlot || app.weapons.equipped().config.hasScope)
        throw std::runtime_error("Configured Punch shortcut selection failed");
    if (app.body && app.melee) {
        app.body->follow(app.rig.camera().position(), app.rig.camera().direction(), true);
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        if (updateWeapons(app, 0) || !app.body->punching(viewer::PlayerBodyPart::rightFist))
            throw std::runtime_error("Fists input must strike without a muzzle flash");
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        if (!app.body->punch(app.rig.camera().direction()))
            throw std::runtime_error("Fists rejection fixture must occupy the other hand");
        const auto readyAt = app.weapons.equipped().readyAt;
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        updateWeapons(app, 1);
        if (app.weapons.equipped().readyAt != readyAt)
            throw std::runtime_error("A rejected physical punch consumed the weapon cooldown");
        app.body->follow(app.rig.camera().position(), app.rig.camera().direction(), false);
        app.body->follow(app.rig.camera().position(), app.rig.camera().direction(), true);
        updateWeapons(app, 1);
        if (app.body->punching(viewer::PlayerBodyPart::rightFist))
            throw std::runtime_error("Holding a rejected punch retried without a fresh click");
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        if (updateWeapons(app, 1) || !app.body->punching(viewer::PlayerBodyPart::rightFist) ||
            app.weapons.equipped().readyAt != 1.5)
            throw std::runtime_error("A fresh click after rejection must punch without another cooldown delay");
        mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        app.body->follow(app.rig.camera().position(), app.rig.camera().direction(), false);
    }
    for (int slot = 0; slot < static_cast<int>(viewer::weaponIds.size()); ++slot) {
        choose(slot + 2);
        if (app.weapons.slot() != static_cast<std::size_t>(slot))
            throw std::runtime_error("Weapon selection callback failed");
    }
    choose(3);
    const auto loaded = app.weapons.equipped().loaded;
    mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    updateWeapons(app, 0);
    mouse(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    if (app.weapons.equipped().loaded != loaded - app.weapons.equipped().config.ammoPerShot)
        throw std::runtime_error("Shooting callback did not consume ammo");
    key(window, GLFW_KEY_R, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_R, 0, GLFW_RELEASE, 0);
    if (!app.weaponReload.pending() || app.weapons.equipped().loaded != loaded - 1)
        throw std::runtime_error("Reload callback must start a timer before transferring ammo");
    app.weaponReload.update(app.weapons, app.inventory,
        viewer::SimulationClock::instance().seconds() + app.weaponReload.duration());
    if (app.weapons.equipped().loaded != loaded)
        throw std::runtime_error("Reload callback did not refill magazine");
    for (const bool blocked : {true, false}) {
        viewer::Model fixture;
        fixture.boundsMin = {-5, 0, 0}; fixture.boundsMax = {5, 3, 12};
        if (blocked) {
            viewer::Primitive wall;
            for (const auto p : {glm::vec3(-5, 0, 5), {5, 0, 5}, {5, 3, 5}, {-5, 3, 5}}) {
                viewer::Vertex vertex; vertex.position = p; wall.vertices.push_back(vertex);
            }
            wall.indices = {0, 1, 2, 0, 2, 3};
            fixture.primitives.push_back(std::move(wall)); fixture.draws.push_back({});
        }
        viewer::Primitive ground;
        for (const auto p : {glm::vec3(-5, 0, -1), {-5, 0, 12}, {5, 0, 12}, {5, 0, -1}}) {
            viewer::Vertex vertex; vertex.position = p; ground.vertices.push_back(vertex);
        }
        ground.indices = {0, 1, 2, 0, 2, 3};
        fixture.draws.push_back({fixture.primitives.size(), glm::mat4(1), "terrain"});
        fixture.primitives.push_back(std::move(ground));
        const viewer::CollisionWorld world(fixture);
        viewer::ZombieLayer zombies;
        zombies.assets.emplace_back(); zombies.instances.emplace_back();
        auto& zombie = zombies.instances.front();
        zombie.transform[3] = {0, 0, 10, 1};
        zombie.restTransform = zombie.restRoot = zombie.transform;
        zombies.behaviors.create(0, viewer::SimulationClock::instance().seconds(), glm::vec3(zombie.restRoot[3]));
        AppState shot(world, fixture); shot.zombies = &zombies;
        // This fixture isolates occlusion and damage using exact crouched aim.
        if (!shot.rig.enterWalking(glm::vec3(0, 1.7f, 0)))
            throw std::runtime_error("Shooting self-test could not place a crouched player");
        shot.rig.update(.5f, 0, 0, 0, false, false, true);
        shot.rig.camera().setPosition({0, .9f, 0}); shot.rig.camera().setOrientation(90, 0);
        shot.weapons.equip(0); shot.weapons.trigger(true);
        const auto rounds = shot.weapons.equipped().loaded;
        updateWeapons(shot, 0);
        const auto* injury = zombies.damage().injury(0);
        if (zombie.ragdoll || (blocked ? injury != nullptr : !injury || injury->health != .9f) ||
            shot.weapons.equipped().loaded != rounds - 1)
            throw std::runtime_error("Single-player shooting occlusion/ammo self-test failed");
        if (zombies.behaviors.find(0)->state != viewer::ZombieState::looking || zombie.heardNoiseRevision != 1)
            throw std::runtime_error("A standalone shot must emit AI noise without audio, including behind cover");
        zombies.behaviors.find(0)->state = viewer::ZombieState::idle;
        const auto hearingCandidates = zombies.behaviorStats().hearingCandidates;
        shot.weapons.trigger(false); shot.weapons.trigger(true);
        if (updateWeapons(shot, 0) || zombie.heardNoiseRevision != 1 ||
            zombies.behaviorStats().hearingCandidates != hearingCandidates)
            throw std::runtime_error("A rejected firearm shot must not emit AI noise");
        shot.weapons.equip(2);
        shot.rig.camera().setOrientation(-90, 0); // Miss the listener and the wall.
        shot.weapons.trigger(true);
        if (!updateWeapons(shot, 0) || zombie.heardNoiseRevision != 2 ||
            zombies.behaviorStats().hearingCandidates != hearingCandidates + 1)
            throw std::runtime_error("A missed shotgun volley must emit exactly one AI noise event");
        auto grenadePhysics = std::make_shared<viewer::PhysicsWorld>();
        viewer::Grenades thrown(grenadePhysics);
        shot.grenades = &thrown;
        shot.weapons.equip(4);
        const auto grenadeCount = shot.weapons.equipped().loaded;
        shot.weapons.trigger(true);
        if (updateWeapons(shot, 0) || thrown.poses().size() != 1 || shot.weapons.equipped().loaded != grenadeCount - 1 ||
            zombies.behaviorStats().hearingCandidates != hearingCandidates + 1)
            throw std::runtime_error("Grenade click must spawn a body and consume one grenade without a muzzle flash");
        updateWeapons(shot, 10);
        if (thrown.poses().size() != 1) throw std::runtime_error("Holding left click repeated a grenade throw");
        shot.weapons = viewer::Weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY, true);
        shot.weaponReload = {};
        shot.weapons.equip(4);
        glfwSetWindowUserPointer(window, &shot);
        key(window, GLFW_KEY_2, 0, GLFW_PRESS, 0);
        mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
        updateWeapons(shot, viewer::SimulationClock::instance().seconds());
        mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
        glfwSetWindowUserPointer(window, &app);
        if (updateWeapons(shot, viewer::SimulationClock::instance().seconds()) || thrown.poses().size() != 2 ||
            shot.weapons.grenadeThrowPower() != 0 || !shot.actionSelection.open() || shot.weapons.slot() != 4)
            throw std::runtime_error("Right-click must drop a grenade while leaving the hotbar selector open");
    }
    app.weapons.equip(viewer::handlingSlot);
    key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
    if (app.capture || !app.equipmentCursor || app.weapons.slot() != viewer::handlingSlot)
        throw std::runtime_error("Tab must unlock the cursor and preserve object handling");
    key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
    if (!app.capture || app.equipmentCursor) throw std::runtime_error("Tab must recapture the cursor");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Object handling right click changed zoom");
    app.weapons.equip(0);
    const auto cameraBeforeAlt = app.rig.camera();
    for (int code : {GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT}) {
        const auto equippedBeforeAlt = app.weapons.slot();
        const auto loadedBeforeAlt = app.weapons.equipped().loaded;
        key(window, code, 0, GLFW_PRESS, 0);
        if (!app.temporaryHandling() || app.weapons.slot() != equippedBeforeAlt)
            throw std::runtime_error("Held Alt must enable handling without changing the equipped weapon");
        key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
        if (app.capture || !app.temporaryHandling()) throw std::runtime_error("Tab must preserve held-Alt handling with a free cursor");
        key(window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
        app.weapons.trigger(true); updateWeapons(app, 20);
        if (app.weapons.equipped().loaded != loadedBeforeAlt)
            throw std::runtime_error("Temporary handling fired the held weapon");
        key(window, code, 0, GLFW_REPEAT, 0);
        key(window, code, 0, GLFW_RELEASE, 0);
        if (app.temporaryHandling()) throw std::runtime_error("Alt release must restore weapon controls");
        if (!app.capture || app.driver.thirdPerson() || app.rig.camera().position() != cameraBeforeAlt.position() ||
            app.rig.camera().view() != cameraBeforeAlt.view())
            throw std::runtime_error("Alt must leave mouse capture and the on-foot camera unchanged");
    }
    key(window, GLFW_KEY_HOME, 0, GLFW_PRESS, 0);
    if (app.capture) throw std::runtime_error("Mouse release self-test failed");
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    mouse(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    if (app.rig.camera().projection(1.0f) != originalProjection)
        throw std::runtime_error("Released cursor changed zoom");
    key(window, GLFW_KEY_HOME, 0, GLFW_RELEASE, 0);
    key(window, GLFW_KEY_HOME, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_HOME, 0, GLFW_RELEASE, 0);
    key(window, GLFW_KEY_F12, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_F12, 0, GLFW_REPEAT, 0);
    if (app.hudScreen != viewer::HudScreen::Settings || !app.capture)
        throw std::runtime_error("F12 must open Settings without changing the capture preference");
    key(window, GLFW_KEY_F12, 0, GLFW_PRESS, 0);
    if (app.hudScreen != viewer::HudScreen::Gameplay || !app.capture)
        throw std::runtime_error("F12 must close Settings and retain mouse capture");
    resize(window, 800, 600);
    if (app.width != 800 || app.height != 600) throw std::runtime_error("Resize callback self-test failed");
    glfwSetWindowSize(window, originalWidth, originalHeight);
    glfwPollEvents();
    glfwGetFramebufferSize(window, &app.width, &app.height);
    key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    if (glfwWindowShouldClose(window) || app.hudScreen != viewer::HudScreen::MissionMenu)
        throw std::runtime_error("Escape must open the mission menu without leaving");
    key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    if (app.hudScreen != viewer::HudScreen::Gameplay) throw std::runtime_error("Escape must resume from the mission menu");
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    app.keys.fill(false);
    app.rig.camera() = original;
    app.weapons = originalWeapons;
    app.inventory = originalInventory;
    if (wasWalking && !app.rig.enterWalking()) throw std::runtime_error("Failed to restore walking mode after input test");
    app.firstMouse = true;
    std::cout << "Input self-test passed: movement, mouse capture, object-handling skill, flashlight toggle, weapon selection/fire/reload/discard/pickup, shot occlusion, scope, resize, Escape\n";
}

void fpsInputSelfTest(GLFWwindow* window) {
    auto& app = state(window);
    const auto key = glfwSetKeyCallback(window, keyCallback);
    const bool wasWalking = app.rig.walking();
    key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
    if (app.hudScreen != viewer::HudScreen::Inventory || app.rig.walking() != wasWalking)
        throw std::runtime_error("F1 failed to open inventory without changing camera mode");
    key(window, GLFW_KEY_F1, 0, GLFW_REPEAT, 0);
    if (app.hudScreen != viewer::HudScreen::Inventory) throw std::runtime_error("Repeated F1 closed inventory");
    key(window, GLFW_KEY_2, 0, GLFW_PRESS, 0);
    if (app.keys[GLFW_KEY_2]) throw std::runtime_error("Inventory passed gameplay input through");
    key(window, GLFW_KEY_F2, 0, GLFW_PRESS, 0);
    if (app.hudScreen != viewer::HudScreen::Skills) throw std::runtime_error("F2 failed to select skills");
    const bool collisionBeforeScreens = app.collisionDebug;
    key(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
    if (app.hudScreen != viewer::HudScreen::Health || app.collisionDebug != collisionBeforeScreens)
        throw std::runtime_error("F3 failed to open health without toggling collision debugging");
    key(window, GLFW_KEY_F4, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_F4, 0, GLFW_REPEAT, 0);
    if (app.hudScreen != viewer::HudScreen::Map) throw std::runtime_error("F4 failed to select map without repeat toggling");
    key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    if (app.hudScreen != viewer::HudScreen::Gameplay || glfwWindowShouldClose(window))
        throw std::runtime_error("Escape did not close the map screen before quitting");
    key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
    if (app.hudScreen != viewer::HudScreen::Gameplay) throw std::runtime_error("Active screen key did not close inventory");
    key(window, GLFW_KEY_F8, 0, GLFW_PRESS, 0);
    if (app.rig.walking()) throw std::runtime_error("F8 failed to select free-fly mode");
    key(window, GLFW_KEY_F9, 0, GLFW_PRESS, 0);
    if (!app.rig.walking()) throw std::runtime_error("F9 failed to select FPS mode");
    for (int frame = 0; frame < 120; ++frame) moveCamera(app, 1.0f / 120.0f);
    if (!app.rig.player().grounded()) throw std::runtime_error("FPS player did not settle on the ground");
    const float feet = app.rig.player().feet().y;
    const float eyeHeight = app.rig.camera().position().y - feet;
    if (std::abs(eyeHeight - 1.7f) > 0.003f) throw std::runtime_error("FPS camera is not at human eye height");
    key(window, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    moveCamera(app, 1.0f / 60.0f);
    key(window, GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
    if (app.rig.player().verticalVelocity() <= 0) throw std::runtime_error("Space failed to start a grounded jump");
    for (int frame = 0; frame < 240; ++frame) moveCamera(app, 1.0f / 120.0f);
    if (!app.rig.player().grounded() || std::abs(app.rig.player().feet().y - feet) > 0.03f)
        throw std::runtime_error("FPS jump failed to return to the supporting ground");
    key(window, GLFW_KEY_LEFT_CONTROL, 0, GLFW_PRESS, 0);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().stance() != viewer::PlayerStance::Crouched)
        throw std::runtime_error("Held Ctrl failed to crouch");
    if (app.rig.player().eyeHeight() <= 1.f || app.rig.player().eyeHeight() >= 1.7f)
        throw std::runtime_error("Crouching must begin gradually");
    for (int frame = 0; frame < 40; ++frame) moveCamera(app, 1.f / 120.f);
    if (std::abs(app.rig.player().eyeHeight() - 1.f) > .001f)
        throw std::runtime_error("Crouching failed to reach its final height");
    key(window, GLFW_KEY_LEFT_CONTROL, 0, GLFW_RELEASE, 0);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().eyeHeight() <= 1.f || app.rig.player().eyeHeight() >= 1.7f)
        throw std::runtime_error("Standing from crouch must begin gradually");
    for (int frame = 0; frame < 40; ++frame) moveCamera(app, 1.f / 120.f);
    if (app.rig.player().stance() != viewer::PlayerStance::Standing)
        throw std::runtime_error("Releasing Ctrl failed to stand");
    key(window, GLFW_KEY_C, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_C, 0, GLFW_REPEAT, 0);
    key(window, GLFW_KEY_C, 0, GLFW_RELEASE, 0);
    moveCamera(app, 1.0f / 120.0f);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().stance() != viewer::PlayerStance::Prone)
        throw std::runtime_error("A short prone press must toggle once, ignore repeats and persist after release");
    if (app.rig.player().eyeHeight() <= .35f || app.rig.player().eyeHeight() >= 1.7f)
        throw std::runtime_error("Going prone must begin gradually");
    for (int frame = 0; frame < 100; ++frame) moveCamera(app, 1.f / 120.f);
    if (std::abs(app.rig.player().eyeHeight() - .35f) > .001f || !app.rig.player().collisionBox())
        throw std::runtime_error("Going prone failed to reach its final box and camera height");
    key(window, GLFW_KEY_C, 0, GLFW_PRESS, 0);
    key(window, GLFW_KEY_C, 0, GLFW_RELEASE, 0);
    moveCamera(app, 1.0f / 120.0f);
    if (app.rig.player().eyeHeight() <= .35f || app.rig.player().eyeHeight() >= 1.7f)
        throw std::runtime_error("Getting up from prone must begin gradually");
    for (int frame = 0; frame < 100; ++frame) moveCamera(app, 1.f / 120.f);
    if (app.rig.player().stance() != viewer::PlayerStance::Standing)
        throw std::runtime_error("A second prone press failed to stand");
    const bool overlay = app.collisionDebug;
    key(window, GLFW_KEY_F10, 0, GLFW_PRESS, 0);
    if (app.collisionDebug == overlay) throw std::runtime_error("F10 failed to toggle collision debugging");
    key(window, GLFW_KEY_F10, 0, GLFW_PRESS, 0);
    const auto walkingEye = app.rig.camera().position();
    key(window, GLFW_KEY_F8, 0, GLFW_PRESS, 0);
    if (glm::length(app.rig.camera().position() - walkingEye) > 1e-5f)
        throw std::runtime_error("Leaving FPS mode moved the free-fly camera unexpectedly");
    key(window, GLFW_KEY_F9, 0, GLFW_PRESS, 0);
    app.keys.fill(false);
    std::cout << "FPS input self-test passed: F8/F9 modes, 1.70 m eye height, Space jump/landing, Ctrl crouch, prone toggle, F10 overlay\n";
}

std::vector<viewer::DebugVertex> collisionOverlay(const AppState& app) {
    std::vector<viewer::DebugVertex> vertices;
    const auto near = app.rig.hasPlayer() ? app.rig.player().feet() : app.rig.camera().position();
    for (const auto& point : app.world.debugLines(near, 12.0f))
        vertices.push_back({point, {0.95f, 0.65f, 0.10f}});
    if (!app.rig.hasPlayer()) return vertices;
    const auto& player = app.rig.player();
    const auto& config = player.config();
    const auto color = player.grounded() ? glm::vec3(0.15f, 1.0f, 0.3f) : glm::vec3(1.0f, 0.2f, 0.25f);
    const auto line = [&](glm::vec3 a, glm::vec3 b, glm::vec3 tint) {
        vertices.push_back({a, tint}); vertices.push_back({b, tint});
    };
    const auto feet = player.feet();
    constexpr int slices = 24;
    constexpr float pi = 3.14159265359f;
    if (const auto box = player.collisionBox()) {
        const auto corners = viewer::collisionBoxCorners(*box);
        for (unsigned i = 0; i < corners.size(); ++i) for (unsigned bit : {1u, 2u, 4u})
            if (!(i & bit)) line(corners[i], corners[i | bit], color);
    } else for (int i = 0; i < slices; ++i) {
        const float a = 2.0f * pi * static_cast<float>(i) / slices;
        const float b = 2.0f * pi * static_cast<float>(i + 1) / slices;
        for (float y : {config.radius, player.bodyHeight() - config.radius}) {
            line(feet + glm::vec3(config.radius * std::cos(a), y, config.radius * std::sin(a)),
                 feet + glm::vec3(config.radius * std::cos(b), y, config.radius * std::sin(b)), color);
        }
        // Two perpendicular great circles show the rounded capsule caps.
        for (int axis = 0; axis < 2; ++axis) {
            const auto capPoint = [&](float angle) {
                const float y = std::sin(angle);
                glm::vec3 p(0, y * config.radius + (y > 0 ? player.bodyHeight() - config.radius : config.radius), 0);
                p[axis == 0 ? 0 : 2] = std::cos(angle) * config.radius;
                return feet + p;
            };
            line(capPoint(a), capPoint(b), color);
        }
    }
    line(feet, feet + player.groundNormal(), {0.2f, 0.9f, 1.0f});
    for (glm::vec3 axis : {glm::vec3(0.12f, 0, 0), glm::vec3(0, 0, 0.12f)})
        line(feet - axis, feet + axis, {1.0f, 1.0f, 1.0f});
    return vertices;
}

void APIENTRY debugMessage(GLenum, GLenum type, GLuint id, GLenum severity,
                          GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::cerr << "OpenGL debug [" << id << ", type 0x" << std::hex << type
              << ", severity 0x" << severity << std::dec << "]: " << message << '\n';
}

void initializeGraphics(bool allowSoftware) {
    const auto text = [](GLenum name) {
        const auto* value = glGetString(name);
        return value ? std::string(reinterpret_cast<const char*>(value)) : std::string("unavailable");
    };
    const auto renderer = text(GL_RENDERER);
    std::cout << "OpenGL: " << text(GL_VERSION) << "\nRenderer: " << renderer
              << "\nVendor: " << text(GL_VENDOR) << "\nGLSL: " << text(GL_SHADING_LANGUAGE_VERSION) << '\n';
    GLint major = 0, minor = 0, profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if ((major < 3 || (major == 3 && minor < 3)) || !(profile & GL_CONTEXT_CORE_PROFILE_BIT))
        throw std::runtime_error("OpenGL 3.3 or newer Core Profile is required");
    std::string lower = renderer;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool software = lower.find("llvmpipe") != std::string::npos || lower.find("softpipe") != std::string::npos ||
                          lower.find("software") != std::string::npos || lower.find("swrast") != std::string::npos;
    if (software && !allowSoftware)
        throw std::runtime_error("A software OpenGL driver was selected. Enable GPU access/install its driver; --allow-software permits diagnostic rendering only.");
    if (software) std::cerr << "Diagnostic software rendering: GPU acceleration is NOT active.\n";
#ifndef NDEBUG
    if (major > 4 || (major == 4 && minor >= 3) || glfwExtensionSupported("GL_KHR_debug")) {
        const auto callback = reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(glfwGetProcAddress("glDebugMessageCallback"));
        if (callback) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            callback(debugMessage, nullptr);
            std::cout << "OpenGL debug callback enabled\n";
        }
    } else std::cout << "KHR_debug unavailable; using glGetError checks\n";
#endif
    viewer::Renderer::checkErrors("context initialization");
}

int run(const Options& options) {
    std::cout << "Loading: " << options.model << '\n';
    auto model = viewer::loadModel(options.model);
    if (!options.noPoles) {
        const bool explicitPoles = !options.poles.empty();
        const auto path = explicitPoles ? options.poles : options.model.parent_path() / "poles.instances.json";
        if (explicitPoles || std::filesystem::exists(path)) {
            try {
                auto poles = viewer::loadPoleLayer(path, options.model);
                std::cout << "Poles: " << poles.columns.size() << " columns in "
                          << poles.batches.draws.size() << " spatial batches\n";
                viewer::appendPoleLayer(model, std::move(poles));
            } catch (const std::exception& error) {
                if (explicitPoles) throw;
                std::cerr << "Adjacent pole layer skipped: " << error.what()
                          << "\nRun tools/prepare_pole_instances.py for this map.\n";
            }
        }
    }
    if (!options.noGuardrails) {
        const bool explicitGuardrails = !options.guardrails.empty();
        const auto path = explicitGuardrails ? options.guardrails : options.model.parent_path() / "guardrails.glb";
        if (explicitGuardrails || std::filesystem::exists(path)) {
            try {
                auto layer = viewer::loadGuardrailLayer(path, options.model);
                std::cout << "Guardrails: " << layer.ways << " mapped ways, " << layer.lengthMeters
                          << " m in " << layer.model.draws.size() << " spatial batches\n";
                viewer::appendStaticModel(model, std::move(layer.model));
            } catch (const std::exception& error) {
                if (explicitGuardrails) throw;
                std::cerr << "Adjacent guardrail layer skipped: " << error.what()
                          << "\nRun tools/prepare_guardrails.py for this map.\n";
            }
        }
    }
    if (options.noFences)
        std::cout << "Fences disabled: " << viewer::removeFenceDraws(model) << " draw instances removed\n";
    auto physics = std::make_shared<viewer::PhysicsWorld>(std::filesystem::path(VIEWER_WEAPON_CONFIGS).parent_path());
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics, viewer::loadDoorConfig(VIEWER_DOOR_CONFIG));
    viewer::ZombieDoorCollision zombieDoorCollision(physics);
    std::cout << "Doors: " << doors.count() << " hinged panels; nearby physics activation\n";
    std::size_t triangles = 0;
    for (const auto& draw : model.draws) triangles += model.primitives.at(draw.primitive).indices.size() / 3;
    std::cout << "Loaded " << model.primitives.size() << " primitives, " << model.draws.size()
              << " draw instances, " << triangles << " triangles, " << model.images.size() << " images\n";
    for (const auto& warning : model.warnings) std::cerr << "Model warning: " << warning << '\n';
    std::optional<viewer::TreeLayer> trees;
    const auto vegetation = viewer::vegetationPaths(options.vegetation, options.model,
        VIEWER_FOREST_CONFIG, options.trees);
    if (!options.noTrees && !vegetation.trees.empty()) {
        const bool explicitTrees = vegetation.requireTrees;
        const auto& path = vegetation.trees;
        if (explicitTrees || std::filesystem::exists(path)) {
            try {
                std::cout << "Loading trees: " << path << '\n';
                trees = viewer::loadTreeLayer(path, options.model);
                std::cout << "Trees: " << trees->instances.size() << " instances, " << trees->assets.size()
                          << " shared assets; frustum culling " << (options.treeCulling ? "on" : "off") << '\n';
                for (const auto& asset : trees->assets)
                    for (const auto& warning : asset.model.warnings)
                        std::cerr << "Tree asset " << asset.id << ": " << warning << '\n';
            } catch (const std::exception& error) {
                if (explicitTrees) throw;
                std::cerr << "Adjacent tree layer skipped: " << error.what()
                          << "\nRegenerate placements for this map, or use --trees FILE to select a layer.\n";
            }
        }
    }
    std::optional<viewer::ForestLayer> forests;
    if (!options.noTrees && !options.noForests && !vegetation.forests.empty()) {
        const bool explicitForests = vegetation.requireForests;
        const auto& path = vegetation.forests;
        if (explicitForests || (model.geographicFrame && std::filesystem::exists(path))) {
            try {
                if (!trees) trees.emplace();
                const auto before = trees->instances.size();
                const viewer::CollisionWorld placementWorld(model, {}, &doors);
                std::cout << "Loading runtime forests: " << path << '\n';
                forests = viewer::loadForestLayer(path, options.model, model, placementWorld, *trees);
                std::cout << "Forests: " << forests->patches.size() << " patches, "
                          << trees->instances.size()-before << " deterministic trees/shrubs; individual vegetation within "
                          << forests->detailDistance << " m or through either scope\n";
            } catch (const std::exception& error) {
                if (explicitForests) throw;
                std::cerr << "Runtime forest layer skipped: " << error.what() << '\n';
            }
        }
    }
    std::vector<viewer::TrunkCollider> trunks;
    if (trees && options.treeCollisions) {
        trunks.reserve(trees->instances.size());
        for (const auto& tree : trees->instances) {
            if (!tree.collidable) continue;
            const auto height = tree.dimensions.y;
            const auto radius = std::min(height * .5f,
                glm::clamp(.035f * std::min(tree.dimensions.x, tree.dimensions.z), .10f, .50f));
            trunks.push_back({glm::vec3(tree.transform[3]), radius, height});
        }
    }
    // Town geometry, trunks and live props participate in walking collision.
    // Shrub cards provide vision cover separately from solid collision.
    viewer::BoxLayer boxes(physics);
    viewer::Grenades grenades(physics);
    viewer::FireEffects fireEffects;
    viewer::CollisionWorld collision(model, trunks, &doors);
    if (trees) collision.setShrubCover(*trees);
    const auto undergrowth = trees ? viewer::UndergrowthIndex(*trees) : viewer::UndergrowthIndex{};
    viewer::UndergrowthEmitter undergrowthEmitter;
    collision.setBoxes(&boxes);
    viewer::PhysicsScene physicsScene(physics, collision, model.boundsMin, model.boundsMax);
    viewer::InteriorObjects interiors(model, doors, std::random_device{}());
    viewer::FloorplanLayer floorplans(model, doors, collision);
    const auto& collisionStats = collision.stats();
    std::cout << "Collision world: " << collisionStats.triangles << " triangles, "
              << collisionStats.buildings << " buildings, " << collisionStats.bvhNodes << " BVH nodes, "
              << collisionStats.trunks << " tree trunks\n";
    // Source identities suppress procedural spawns; saved destinations may be
    // outside forests. Keep the state and generator alive through the layer.
    std::optional<viewer::DoodadState> doodadState;
    std::optional<viewer::DoodadPlacementGenerator> doodadGenerator;
    std::unique_ptr<viewer::DoodadLayer> doodads;
    std::array<viewer::Model,viewer::doodadKindCount> doodadModels;
    auto doodadSave = options.doodadSave;
    std::uint64_t savedDoodadRevision = 0;
    if (forests && !options.noDoodads) {
        if (doodadSave.empty()) {
            doodadSave = options.model;
            doodadSave.replace_extension(".doodads.json");
        }
        doodadState.emplace(viewer::fileSha256(options.model)+":"+viewer::fileSha256(vegetation.forests));
        try {
            doodadState->load(doodadSave);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(error.what()) +
                "\nDoodad identity is map SHA-256:forest configuration SHA-256. Regenerating either file can invalidate a save."
                "\nThe existing save has been left unchanged. Use --doodad-save FILE with a separate save path,"
                " or --no-doodads to launch without forest props.");
        }
        savedDoodadRevision = doodadState->revision();
        doodadGenerator.emplace(*forests,collision);
        constexpr std::array<const char*,viewer::doodadKindCount> names{"branch","stone","beer_bottle"};
        for (std::size_t i=0; i<names.size(); ++i)
            doodadModels[i] = viewer::loadModel(std::filesystem::path(VIEWER_DOODAD_ASSETS)/(std::string(names[i])+".glb"));
        doodads = std::make_unique<viewer::DoodadLayer>(physics,*doodadGenerator,*doodadState,doodadModels);
        std::cout << "Doodads: forest-only cells; " << doodadState->changes().size()
                  << " saved moves; " << doodadSave << '\n';
    }
    std::optional<viewer::ZombieScenarioConfig> scenario;
    if (!options.scenario.empty()) scenario = viewer::loadZombieScenarioConfig(options.scenario);
    if (scenario && options.noZombies && scenario->count())
        throw std::runtime_error("This scenario contains zombies; omit --no-zombies");
    viewer::DogLayer dogs;
    if (scenario && !scenario->dogPacks.empty()) dogs = viewer::loadDogScenario(options.dogs,collision,model,*scenario);
    else if (options.dogCount) {
        const auto center = options.spawn.value_or(glm::vec2((model.boundsMin.x+model.boundsMax.x)*.5f,
            (model.boundsMin.z+model.boundsMax.z)*.5f));
        dogs = viewer::loadDogLayer(options.dogs, collision, model, center, options.dogCount, 100);
    }
    std::optional<viewer::ZombieLayer> zombies;
    std::unique_ptr<viewer::InteriorZombies> interiorZombies;
    const auto interiorReserve = doors.count() ? viewer::interiorZombieReserve : 0;
    std::optional<viewer::ZombieScenarioConfig::Player> scenarioPlayer;
    auto navmesh = options.model;
    navmesh.replace_extension(".navmesh");
    const auto extent = model.boundsMax - model.boundsMin;
    const bool townSizedTerrain = collisionStats.terrainTriangles != 0 && extent.x >= 100 && extent.z >= 100;
    if (scenario) {
        scenarioPlayer = scenario->player;
        if (scenario->count()) {
            const auto path = options.zombies.empty() ? std::filesystem::path(VIEWER_ZOMBIE_ASSETS) : options.zombies;
            zombies = viewer::loadZombieScenario(path, collision, model, navmesh, *scenario, interiorReserve);
            if (interiorReserve) interiorZombies = std::make_unique<viewer::InteriorZombies>(scenario->count(), interiorReserve);
            if (options.fullScenarioMovement) zombies->setDistantScenarioTravelEnabled(false);
        }
        std::cout << "Scenario: " << options.scenario << "; " << (zombies ? zombies->instances.size() : 0)
                  << " zombies; " << dogs.instances.size() << " dogs; "
                  << (scenario->needsNavigation() ? "travel routes preplanned" : "initially idle") << '\n';
    } else if (!options.noZombies && (options.explicitZombies || townSizedTerrain)) {
        const auto path = options.zombies.empty() ? std::filesystem::path(VIEWER_ZOMBIE_ASSETS) : options.zombies;
        if (options.explicitZombies || viewer::hasZombieModels(path)) {
            const auto middle = (model.boundsMin + model.boundsMax) * .5f;
            const auto center = options.spawn.value_or(glm::vec2(middle.x, middle.z));
            const auto boundary = options.explicitZombieRadius ? std::vector<glm::vec2>{} :
                viewer::zombieParkBoundary(model, VIEWER_ZOMBIE_PARK);
            const auto count = !boundary.empty() && !options.explicitZombieCount ? 300 : options.zombieCount;
            zombies = viewer::loadZombieLayer(path, collision, model, center,
                                              static_cast<std::size_t>(count), options.zombieRadius, boundary, interiorReserve);
            if (interiorReserve) interiorZombies = std::make_unique<viewer::InteriorZombies>(count, interiorReserve);
            if (!boundary.empty()) std::cout << "Zombie placement: evenly spread across Grassalkovich Royal Palace gardens\n";
            std::cout << "Zombies: " << zombies->instances.size() << " instances, " << zombies->assets.size()
                      << " shared animated assets; looping idle with individual phase offsets\n";
        } else std::cerr << "Zombie assets are missing at " << path << "; see assets/zombies/README.md for download instructions.\n";
    }
    if (zombies) {
        if (interiorZombies) std::cout << "Interior zombies: " << interiorZombies->reserveCount() << " shared dormant slots\n";
        zombies->attachPhysics(physics);
        zombies->setRagdollCleanupHeight(model.boundsMin.y - 10.f);
        zombies->setPerceptionStaggering(options.zombiePerceptionStaggering);
        zombies->setPerceptionEnabled(options.zombiePerception);
        zombies->setHouseNavigation(&floorplans);
        if (const auto* navigation = zombies->navigation()) {
            std::cout << "Scenario navigation: " << navmesh
                      << "; g+8h, 50000 nodes, " << navigation->pathArenaBytes() << " path bytes"
                      << "; cache SHA256 " << navigation->meshSha256()
                      << "; embedded replay SHA256 " << navigation->sourceSha256() << '\n';
        }
    }
    if (!dogs.instances.empty())
        viewer::configureDogHunting(dogs,options.model,floorplans.footprints(),
            zombies && zombies->navigation() ? zombies->navigation()->sharedMesh() : nullptr);
    GLFWLifetime lifetime;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    if (options.hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    // Keep the desktop video mode, with a separate fixed-size render target.
    // Hidden captures use a regular hidden window and never occupy the monitor.
    GLFWmonitor* monitor = nullptr;
    int windowWidth = options.width, windowHeight = options.height;
    if (options.fullscreen) {
        auto* primary = glfwGetPrimaryMonitor();
        const auto* mode = primary ? glfwGetVideoMode(primary) : nullptr;
        if (!mode) throw std::runtime_error("Cannot find a monitor for fullscreen mode");
        windowWidth = mode->width; windowHeight = mode->height;
        if (!options.hidden) monitor = primary;
        glfwWindowHint(GLFW_RED_BITS,mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS,mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS,mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE,mode->refreshRate);
    }
    const auto title = "Gödöllő Viewer — " + options.model.filename().string();
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(windowWidth, windowHeight, title.c_str(), monitor, nullptr), glfwDestroyWindow);
    if (!window) throw std::runtime_error("Cannot create an OpenGL 3.3 Core window. Check display access and the graphics driver.");
    glfwMakeContextCurrent(window.get());
    viewer::initializeOpenGL();
    glfwSwapInterval(options.frames > 0 ? 0 : 1);
    initializeGraphics(options.allowSoftware);
    AppState app(collision, model);
    app.hudScreen = options.hudScreen;
    app.boundsMin = model.boundsMin;
    app.boundsMax = model.boundsMax;
    app.hidden = options.hidden;
    app.collisionDebug = options.collisionDebug;
    glfwGetFramebufferSize(window.get(), &app.width, &app.height);
    const auto renderAspect = options.fullscreen ? float(options.width)/options.height :
        static_cast<float>(std::max(app.width, 1))/static_cast<float>(std::max(app.height, 1));
    app.rig.frame(renderAspect);
    std::cout << (options.fullscreen ? "Fullscreen" : "Windowed") << "; render resolution "
              << (options.fullscreen ? options.width : app.width) << 'x'
              << (options.fullscreen ? options.height : app.height)
              << "; GUI resolution " << app.width << 'x' << app.height << '\n';
    if (options.fps || scenarioPlayer) {
        auto preferred = (model.boundsMin + model.boundsMax) * 0.5f;
        preferred.y = model.boundsMax.y + 5.0f;
        if (scenarioPlayer) { preferred.x = scenarioPlayer->spawn[0]; preferred.z = scenarioPlayer->spawn[1]; }
        if (options.spawn) { preferred.x = options.spawn->x; preferred.z = options.spawn->y; }
        if (!app.rig.enterWalking(preferred)) throw std::runtime_error("Unable to find a valid FPS starting position");
        if (scenarioPlayer)
            app.rig.camera().setOrientation(scenarioPlayer->orientation[0], scenarioPlayer->orientation[1]);
    }
    if (options.zombieView && zombies)
        app.rig.camera().frame(zombies->boundsMin, zombies->boundsMax, renderAspect);
    viewer::VehicleLayer cars(physics, viewer::loadVehicleConfigs(VIEWER_VEHICLES));
    const auto placements = options.noCars ? std::optional<std::vector<viewer::VehiclePlacement>>{} :
        viewer::loadVehiclePlacements(options.model);
    if (placements) {
        viewer::placeVehicles(cars, *placements);
        std::cout << "Loaded " << cars.count() << " parked cars with repeatable vehicle types (seed "
                  << viewer::vehiclePlacementSeed << ")\n";
    } else if (!options.noCars) {
        const auto carOrigin = app.rig.walking() ? app.rig.camera().position() :
            glm::vec3((model.boundsMin.x + model.boundsMax.x) * .5f, model.boundsMax.y + 2, (model.boundsMin.z + model.boundsMax.z) * .5f);
        std::mt19937 random(viewer::vehiclePlacementSeed);
        const auto type = std::uniform_int_distribution<std::size_t>(0,cars.typeCount()-1)(random);
        if (viewer::placeVehicleNear(cars, collision, carOrigin, model.boundsMin, model.boundsMax,type)) {
            const auto p = cars.pose(0).chassis.center;
            std::cout << "Car at (" << p.x << ", " << p.y << ", " << p.z << "); Enter beside driver side to drive\n";
        } else std::cout << "No clear terrain footprint near spawn for the prototype car\n";
    }
    collision.setVehicles(&cars);
    if (placements && app.rig.walking() && !app.rig.enterWalking(app.rig.camera().position()))
        throw std::runtime_error("Unable to find a valid FPS starting position beside parked vehicles");
    app.cars = &cars;
    std::unique_ptr<viewer::BirdFlocks> birds;
    std::unique_ptr<viewer::BirdRenderer> birdRenderer;
    if (!options.noBirds && (options.explicitBirds || townSizedTerrain)) {
        birds = std::make_unique<viewer::BirdFlocks>(collision, trees ? &*trees : nullptr,
            model.boundsMin, model.boundsMax, app.rig.camera().position(), options.birds);
        if (!birds->birds().empty()) birdRenderer = std::make_unique<viewer::BirdRenderer>(options.shaders, VIEWER_BIRD_ASSET);
        std::cout << "Birds: " << birds->birds().size() << " in " << birds->flocks().size()
                  << " flocks; move or duck when they gather for a dive; cover blocks attacks\n";
    }
    app.zombies = zombies ? &*zombies : nullptr;
    app.dogs = &dogs;
    app.doors = &doors;
    app.boxes = &boxes;
    app.physicsScene = &physicsScene;
    physicsScene.prewarmGround(app.rig.camera().position());
    app.interiors = &interiors;
    app.floorplans = &floorplans;
    auto audioOptions = options.audio;
    audioOptions.rain = options.rain;
    viewer::SceneAudio audio(audioOptions);
    app.audio = &audio;
    app.grenades = &grenades;
    glfwSetWindowUserPointer(window.get(), &app);
    glfwSetFramebufferSizeCallback(window.get(), framebufferCallback);
    glfwSetKeyCallback(window.get(), keyCallback);
    glfwSetCursorPosCallback(window.get(), cursorCallback);
    glfwSetMouseButtonCallback(window.get(), mouseButtonCallback);
    glfwSetScrollCallback(window.get(), scrollCallback);
    glfwSetWindowFocusCallback(window.get(), focusCallback);
    captureMouse(window.get(), true);
    if (!options.hidden) viewer::enableRawMouseMotion(window.get());
    const char* proneKeyName = glfwGetKeyName(GLFW_KEY_C, 0);
    std::cout << "Camera mode: " << app.rig.modeName() << "; speed: " << app.rig.speed()
              << " m/s; F1 Inventory, F2 Skills, F3 Health, F4 Map, F12 Settings, F8 free-fly, F9 walk, F10 collisions, WASD move, Space jump, Ctrl hold crouch/airborne tuck, "
              << (proneKeyName ? proneKeyName : "C")
              << " toggle prone, mouse look, F throw weapon, G pick up weapon, L flashlight, End frame scene, Alt handling / vehicle camera, Home cursor, Escape mission menu\n";
    std::array<viewer::Model,viewer::weaponIds.size()> droppedModels;
    for (std::size_t i=0; i<droppedModels.size(); ++i)
        droppedModels[i] = viewer::loadModel(std::filesystem::path(VIEWER_WEAPON_ASSETS)/(std::string(viewer::weaponIds[i])+".glb"));
    viewer::GroundItems groundItems(physics);
    groundItems.setModels(droppedModels);
    app.groundItems = &groundItems;
    viewer::Renderer renderer(model, options.shaders, trees ? &*trees : nullptr,
                              zombies ? &*zombies : nullptr, forests ? &*forests : nullptr, &dogs);
    if (zombies) zombies->releaseAnimationVertices();
    dogs.releaseAnimationVertices();
    if (doodads) {
        doodads->update(app.rig.camera().position(),physicsScene,doors);
        renderer.setDoodads(*doodads,doodadModels);
    }
    renderer.setGroundItems(groundItems,droppedModels);
    renderer.setVehicleAssets(std::filesystem::path(VIEWER_VEHICLES).parent_path() / "assets/vehicles/models");
    renderer.setVehicleDistanceDetail(options.vehicleDistanceDetail);
    renderer.setVehicleInstancing(options.vehicleInstancing);
    viewer::ReticleOverlay reticle(options.shaders);
    viewer::Hud hud(VIEWER_HUD_DIR, app.inventory);
    viewer::SleepPlan sleepPlan;
    sleepPlan.join(1);
    app.sleepPlan = &sleepPlan;
    app.rig.setDayLengthSeconds(options.dayNight.dayLengthSeconds);
    std::uint64_t sleepDamageEvents = app.vitals.damageEvents();
    std::vector<std::size_t> sleepThreatCandidates;
    hud.setGroundItems(groundItems, app.weapons, app.world);
    app.hud = &hud;
    if (const auto minimap = viewer::findMinimapSource(model)) {
        hud.setMinimap(*minimap, renderer.sceneTexture(minimap->texture));
        hud.setMinimapRoads(model);
    }
    const auto& playerConfig = app.rig.player().config();
    viewer::WeaponOverlay weaponOverlay(options.shaders, VIEWER_WEAPON_ASSETS,
                                       {playerConfig.height, playerConfig.eyeHeight});
    viewer::PlayerBody playerBody(physics, playerConfig.eyeHeight);
    viewer::MeleeCombat melee(physics);
    viewer::VehicleZombieCollision vehicleZombies;
    viewer::PlayerBodyRenderer bodyRenderer(options.shaders);
    app.body = &playerBody; app.melee = &melee;
    viewer::MuzzleFlash muzzleFlash;
    std::size_t muzzleSlot = 0;
    glm::vec3 muzzleModelPosition{0};
    renderer.loadGrenadeModel(std::filesystem::path(VIEWER_WEAPON_ASSETS) / "he_grenade.glb");
    renderer.loadGrenadeModel(std::filesystem::path(VIEWER_WEAPON_ASSETS) / "molotov.glb", viewer::GrenadeKind::molotov);
    std::uint32_t lastExplosion = 0;
    std::cout << "Actions: 0 or key left of 1 previews Object handling in slot 0; 1-9 preview hotbar slots, wheel cycles, left click confirms; R reload\n" << app.weapons.status() << '\n';
    renderer.setTreeCulling(options.treeCulling);
    renderer.setZombieCulling(options.zombieCulling);
    renderer.setZombieDistanceDetail(options.zombieDistanceDetail);
    renderer.setZombieDrawDistance(options.zombieDrawDistance.value_or(options.zombieView ? 0.0f : 180.0f));
    renderer.setZombieShadows(options.zombieShadows);
    renderer.setSceneCulling(options.sceneCulling);
    renderer.setRoofBatching(options.roofBatching);
    app.distantSceneCache = options.distantSceneCache;
    renderer.setDistantSceneCache(app.distantSceneCache);
    renderer.setDistantSceneDistance(viewer::distantSceneryDistances[options.graphics.distantScenery]);
    renderer.setSkipHighestDetail(options.graphics.skipHighestDetail);
    renderer.setObjectShadows(options.graphics.shadowCasters >= viewer::ShadowCasters::Objects);
    renderer.setWallBatching(options.wallBatching);
    renderer.setShadows(options.shadows);
    renderer.setFlashlightShadows(options.flashlightShadows);
    renderer.setTreeShadows(options.treeShadows);
    renderer.setTreeCanopyShading(options.treeCanopyShading);
    renderer.setTreeDepthOrdering(options.treeDepthOrdering);
    renderer.setTreeCardSplitting(options.treeCardSplitting);
    renderer.setTreeNearestFiltering(options.treeNearestFiltering);
    renderer.setSunShadowResolution(options.sunShadowResolution);
    renderer.setTreeBillboardDistance(options.treeBillboardDistance);
    renderer.setOldForestDistance(options.oldForestDistance);
    renderer.setNight(options.night);
    renderer.setRain(options.rain);
    renderer.setHaze(options.haze);
    renderer.setGroundFog(options.groundFog);
    viewer::Atmosphere atmosphere;
    bool fogAnchored = false;
    const auto anchorGroundFog = [&] {
        if (!fogAnchored && app.rig.walking()) {
            atmosphere.groundFog.baseHeight = app.rig.player().feet().y;
            renderer.setAtmosphere(atmosphere);
            fogAnchored = true;
        }
    };
    anchorGroundFog();
    double displayedHour = options.dayNight.startHour;
    if (!options.night) {
        renderer.setDayNightLighting(viewer::dayNightLighting(displayedHour, {}, {}, atmosphere));
        std::cout << "Day/night: starting hour " << displayedHour << ", full day "
                  << options.dayNight.dayLengthSeconds << " simulation seconds (0 freezes time)\n";
    }
    std::unique_ptr<viewer::CollisionDebug> debug;
    if (options.selfTest) inputSelfTest(window.get());
    if (options.selfTestFPS) fpsInputSelfTest(window.get());
    if (options.selfTest) {
        renderer.resize(800, 600);
        renderer.render(app.rig.camera());
        for (const auto mode : {viewer::Camera::ViewMode::original, viewer::Camera::ViewMode::zoom10,
                                viewer::Camera::ViewMode::zoom1})
            reticle.draw(mode, 800, 600);
        renderer.renderBlack();
        std::array<unsigned char,4> sleepPixel{};
        glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sleepPixel.data());
        if (sleepPixel[0] || sleepPixel[1] || sleepPixel[2])
            throw std::runtime_error("Sleep frame did not clear the scene target to black");
        viewer::Renderer::checkErrors("sleep blackout self-test");
        viewer::Renderer::checkErrors("resized render target and reticle modes");
    }
    viewer::RuntimeGraphicsSettings graphics(window.get(), renderer, options.fullscreen,
        glm::ivec2(options.width, options.height));
    viewer::Settings preferences{graphics.settings(), {options.audio.gain}};
    renderer.resize(graphics.renderSize().x, graphics.renderSize().y);
    viewer::GpuTimer gpuTimer;
    std::unique_ptr<viewer::FrameProfile> frameProfile;
    if (!options.frameProfile.empty()) frameProfile=std::make_unique<viewer::FrameProfile>(options.frameProfile);
    if (options.fixedStep) std::cout << "Fixed simulation step: 1/60 s per frame\n";
    double previous = glfwGetTime(), titleTime = previous;
    auto& simulationClock = viewer::SimulationClock::instance();
    simulationClock.reset();
    double intervalRenderSeconds = 0.0;
    double intervalSimulationSeconds = 0.0;
    double intervalBehaviorSeconds = 0.0;
    viewer::zombie_profile::Profile zombieProfile;
    viewer::frame_stage_profile::Profile frameStages;
    double intervalPreparationSeconds = 0.0;
    int frameCount = 0, intervalFrames = 0;
    viewer::PlayerPresentation playerPresentation;
    playerPresentation.reset(app.rig.camera().position(), playerBody.pose());
    double nextDoodadSave = glfwGetTime()+5;
    while (!glfwWindowShouldClose(window.get())) {
        // Commit last frame's UI edits before newer keyboard shortcuts.
        bool settingsChanged = false;
        if (const auto settings = hud.takeSettingsRequest()) {
            graphics.apply(settings->graphics);
            audio.setGain(settings->audio.volume);
            preferences = *settings;
            settingsChanged = true;
            app.distantSceneCache = settings->graphics.distantScenery != 3;
            app.firstMouse = true;
            glfwGetFramebufferSize(window.get(), &app.width, &app.height);
        }
        glfwPollEvents();
        const auto previousDistance = graphics.settings().distantScenery;
        graphics.syncDistantScenery(app.distantSceneCache);
        preferences.graphics = graphics.settings();
        if (settingsChanged || previousDistance != preferences.graphics.distantScenery)
            viewer::writeSettings(options.settingsFile, preferences);
        hud.setSettings(preferences);
        renderer.setFlashlight(app.flashlight);
        const double now = glfwGetTime();
        const float dt = options.fixedStep?1.f/60:static_cast<float>(std::clamp(now - previous, 0.0, 0.1));
        previous = now;
        if (app.width <= 0 || app.height <= 0) {
            audio.service();
            glfwWaitEventsTimeout(0.05);
            titleTime = glfwGetTime();
            intervalFrames = 0;
            intervalRenderSeconds = 0.0;
            intervalSimulationSeconds = intervalBehaviorSeconds = intervalPreparationSeconds = 0.0;
            gpuTimer.takeAverageMilliseconds();
            zombieProfile = {};
            frameStages = {};
            continue;
        }
        const double simulationStart = glfwGetTime();
        viewer::frame_stage_profile::Capture frameStageCapture(options.profile ? &frameStages : nullptr);
        floorplans.update({app.rig.camera().position()},
                          zombies ? zombies->physicsSupportPositions() : std::vector<glm::vec3>{});
        physicsScene.prewarmGround(app.rig.camera().position());
        if (interiorZombies)
            interiorZombies->update(floorplans, *zombies, collision, {app.rig.player().feet()}, options.dayNight);
        if (doodads) doodads->update(app.rig.camera().position(),physicsScene,doors);
        const auto previousCameraPosition = playerPresentation.eye(physics->interpolationAlpha());
        bool simulated = false;
        const auto followPlayerBody = [&] {
            playerBody.follow(app.rig.camera().position(), app.rig.camera().direction(),
                !sleepPlan.state().asleep(1) && !app.driver.occupied() && !app.weaponSwitch.pending() && !app.handling() && app.weapons.slot() == viewer::punchSlot);
        };
        const auto fireWeapons = [&] {
            viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::weapons);
            if (updateWeapons(app, simulationClock.seconds())) {
                muzzleSlot = app.weapons.slot();
                muzzleModelPosition = app.weapons.equipped().config.muzzlePosition;
                muzzleFlash.fire(simulationClock.seconds(), {});
            }
        };
        const auto beforePhysics = [&](float step) {
            simulated = true;
            app.vitals.advance(step, options.dayNight, sleepPlan.state().asleep(1));
            simulationClock.advance(step);
            sleepPlan.update(simulationClock.seconds());
            if (zombies) zombies->setSleepTravel(sleepPlan.state().rate() > 1, collision);
            if (sleepPlan.state().asleep(1)) {
                app.weapons.cancelTrigger(); app.interaction.cancel();
            }
            app.displayedAnimationSeconds = options.animationTime.value_or(simulationClock.seconds());
            app.weaponReload.update(app.weapons, app.inventory, simulationClock.seconds());
            app.weapons.syncAmmo(app.inventory);
            if (app.weaponSwitch.update(app.weapons, simulationClock.seconds())) {
                while (app.rig.camera().viewMode() != viewer::Camera::ViewMode::original)
                    app.rig.camera().cycleViewMode();
            }
            doors.activateNear(app.rig.camera().position());
            for (std::size_t i = 0; i < boxes.count(); ++i)
                if (boxes.active(i)) doors.activateNear(boxes.pose(i).center, 8);
            if (zombies)
                for (const auto id : zombies->ragdollIds())
                    doors.activateNear(glm::vec3(zombies->instances[id].transform[3]));
            physicsScene.prepare(boxes);
            groundItems.prepare(physicsScene, &doors);
            grenades.prepare(physicsScene, doors);

            const double behaviorStart = glfwGetTime();
            updateZombieBehavior(app, step, app.displayedAnimationSeconds, options.profile ? &zombieProfile : nullptr);
            if (zombies) for (const auto& noise : zombies->attackNoises()) emitNoise(app, noise);
            if (zombies && zombies->takeSleepTravelThreat()) sleepPlan.wake(viewer::SleepWakeReason::Threat, simulationClock.seconds());
            if (app.dogs) {
                viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::dogFetchPhysics);
                app.dogs->prepareFetchPhysics(physicsScene,&doors);
            }
            intervalBehaviorSeconds += glfwGetTime() - behaviorStart;
            // Aim from the solved start-of-step eye/fist pair, before movement
            // sets the anchor's endpoint for this same physics solve.
            followPlayerBody();
            melee.prepare(playerBody, zombies ? &*zombies : nullptr, app.displayedAnimationSeconds);
            if (app.weapons.slot() == viewer::punchSlot) fireWeapons();

            const auto& movingPlayer = app.rig.player();
            const viewer::UndergrowthMotion shrubBefore{movingPlayer.feet(), movingPlayer.bodyHeight(),
                movingPlayer.config().radius, movingPlayer.grounded(), false};
            moveCamera(app, step);
            // Throws need this step's measured velocity; firearm origins also
            // retain their existing post-movement camera position.
            if (app.weapons.slot() != viewer::punchSlot) fireWeapons();
            if (app.rig.walking()) {
                const bool running = movingPlayer.stance() == viewer::PlayerStance::Standing &&
                    (app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT]);
                if (const auto event = undergrowthEmitter.update(step, shrubBefore,
                        {movingPlayer.feet(), movingPlayer.bodyHeight(), movingPlayer.config().radius,
                         movingPlayer.grounded(), running}, undergrowth)) {
                    emitNoise(app, event->noise);
                    audio.rustle(*event);
                }
                for (const auto& noise : movingPlayer.noises()) emitNoise(app, noise);
            } else undergrowthEmitter.reset();
            anchorGroundFog();
            followPlayerBody();
            if (playerBody.enabled()) {
                const auto eye = app.rig.camera().position();
                physicsScene.ensureNear(eye - glm::vec3(3), eye + glm::vec3(3));
            }
            if (app.driver.driving())
                cars.drive(*app.driver.car(), float(app.keys[GLFW_KEY_W]) - float(app.keys[GLFW_KEY_S]),
                           float(app.keys[GLFW_KEY_D]) - float(app.keys[GLFW_KEY_A]), app.keys[GLFW_KEY_SPACE]);
            if (zombies) {
                viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::wounds);
                zombies->prepareAttackPhysics(physicsScene);
                zombies->prepareScenePhysics(physicsScene);
                zombies->prepareWoundPhysics(step, app.displayedAnimationSeconds, &physicsScene);
            }
            playerBody.beforeStep(step);
            cars.beforeStep(step);
            viewer::prepareVehicleScene(cars, physicsScene);
            for (const auto i : cars.simulatedCars()) {
                doors.activateNear(cars.pose(i).chassis.center, 10);
            }
            if (zombies) {
                zombieDoorCollision.prepare(doors, *zombies);
            }
            vehicleZombies.capture(cars);
        };
        const auto afterPhysics = [&](float step) {
            if (zombies) vehicleZombies.resolve(cars, *zombies, collision, step,
                    app.displayedAnimationSeconds);
            melee.finish(playerBody, zombies ? &*zombies : nullptr,
                         app.displayedAnimationSeconds);
            boxes.retireBelow(model.boundsMin.y - 10.f);
            groundItems.removeBelow(model.boundsMin.y - 10.f);
            playerBody.afterStep();
            bodyRenderer.captureBody(playerBody, step);
            if (zombies) {
                viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::synchronization);
                zombies->synchronizePhysics();
            }
            if (app.driver.occupied()) app.driver.updateCamera(cars, app.rig.camera(), app.world);
            for (const auto& noise : grenades.update(simulationClock.seconds(), collision, zombies ? &*zombies : nullptr,
                    app.displayedAnimationSeconds, &cars))
                emitNoise(app, noise);
            for (const auto& noise : doors.takeNoises()) emitNoise(app, noise);
            for (const auto& explosion : grenades.explosions(simulationClock.seconds())) {
                if (explosion.id <= lastExplosion) continue;
                audio.explosion(explosion.position);
                lastExplosion = explosion.id;
            }
            if (zombies) {
                viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::blood);
                zombies->updateBloodEffects(app.displayedAnimationSeconds);
            }
            const auto impactEvents = physics->impacts().take();
            for (const auto& noise : physics->impacts().takeNoises()) emitNoise(app, noise);
            {
                viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::audio);
                audio.update(app.rig, zombies ? &*zombies : nullptr, step, &doors);
                for (const auto& impact : impactEvents) audio.impact(impact.sound);
            }
            playerPresentation.advance(app.rig.camera().position(), playerBody.pose());
            if (birds) {
                const auto& player = app.rig.player();
                const viewer::BirdFlockPlayer target{player.feet(), player.bodyHeight(), player.bodyRadius(),
                    app.rig.walking() && !app.driver.occupied() && app.vitals.health() > 0, player.collisionBox()};
                const auto hits = birds->update(step, target);
                for (unsigned hit = 0; hit < hits; ++hit)
                    if (app.vitals.applyDamage(.01f) > 0) app.lastDamageSeconds = simulationClock.seconds();
            }
            if (sleepPlan.state().asleep(1)) {
                if (app.vitals.damageEvents() != sleepDamageEvents || app.vitals.health() <= 0)
                    sleepPlan.wake(viewer::SleepWakeReason::Damage, simulationClock.seconds());
                else if (viewer::sleepThreatNearby(zombies ? &*zombies : nullptr, app.rig.player().feet(), sleepThreatCandidates))
                    sleepPlan.wake(viewer::SleepWakeReason::Threat, simulationClock.seconds());
            }
        };
        const int sleepSteps = sleepPlan.state().rate();
        for (int sleepStep = 0; sleepStep < sleepSteps; ++sleepStep) {
            if (sleepStep && !sleepPlan.state().sleeping) break;
            physics->step(dt, beforePhysics, afterPhysics);
        }
        if (zombies) zombies->setSleepTravel(sleepPlan.state().rate() > 1, collision);
        if (doodads) {
            doodads->captureChanges();
            if (now >= nextDoodadSave) {
                if (savedDoodadRevision != doodadState->revision()) {
                    doodadState->save(doodadSave);
                    savedDoodadRevision = doodadState->revision();
                }
                nextDoodadSave = now+5;
            }
        }
        if (!simulated) audio.service();
        app.displayedAnimationSeconds = options.animationTime.value_or(simulationClock.seconds());
        glm::vec3 dayNightTint;
        if (!options.night) {
            displayedHour = options.dayNight.hourAt(simulationClock.seconds());
            const auto lighting = viewer::dayNightLighting(displayedHour, {}, {}, atmosphere);
            renderer.setDayNightLighting(lighting);
            dayNightTint = viewer::dayNightDisplayTint(lighting);
        } else {
            dayNightTint = viewer::dayNightDisplayTint(viewer::dayNightLighting(0));
        }
        // Mouse orientation remains immediate; interpolate only solved position
        // and body snapshots so render rate never changes movement or punch work.
        const float presentationAlpha = physics->interpolationAlpha();
        if (glm::distance(app.rig.camera().position(), playerPresentation.currentEye()) > 2.f)
            playerPresentation.reset(app.rig.camera().position(), playerBody.pose());
        if (app.driver.occupied()) app.driver.updateCamera(cars, app.rig.camera(), app.world);
        auto presentedCamera = app.rig.camera();
        if (!app.driver.occupied()) presentedCamera.setPosition(playerPresentation.eye(presentationAlpha));
        const auto presentedBody = playerPresentation.pose(presentationAlpha);
        const float sway = viewer::sniperSwayScale(glm::length(glm::vec2(app.playerVelocity.x, app.playerVelocity.z)),
            app.rig.player().movementState().posture, app.rig.player().config());
        const auto viewCamera = app.weaponMotion.aimCamera(presentedCamera, app.weapons.slot(),
            simulationClock.seconds(), !app.driver.occupied() && !app.handling() && app.weapons.equipped().owned, sway);
        auto flash = muzzleFlash.light(simulationClock.seconds());
        flash.position = weaponOverlay.muzzlePosition(muzzleSlot, muzzleModelPosition,
            app.weaponMotion.pose(muzzleSlot, simulationClock.seconds(), app.temporaryHandling()));
        renderer.setMuzzleFlash(flash.transformed(glm::inverse(viewCamera.view())));
        const double preparationStart = glfwGetTime();
        intervalSimulationSeconds += preparationStart - simulationStart;
        renderer.updateDoors(doors);
        renderer.updateBoxes(boxes);
        if (doodads) renderer.updateDoodads(*doodads);
        renderer.updateVehicles(cars, app.driver.car());
        fireEffects.receive(grenades.molotovImpacts(simulationClock.seconds()),simulationClock.seconds(),
            simulationClock.seconds(),simulationClock.seconds(),
            {collision,physics,zombies ? &*zombies : nullptr,&dogs,&doors,app.displayedAnimationSeconds});
        renderer.setFireEffects(fireEffects,simulationClock.seconds());
        renderer.updateGrenades(grenades.poses());
        renderer.updateVegetationBlasts(grenades.explosions(simulationClock.seconds()));
        renderer.setExplosionLights(grenades.lights(simulationClock.seconds()));
        const auto renderSize = graphics.renderSize();
        const int renderWidth = renderSize.x, renderHeight = renderSize.y;
        renderer.resize(renderWidth, renderHeight);
        if (zombies && !sleepPlan.state().asleep(1)) renderer.updateZombies(*zombies);
        std::vector<viewer::DebugVertex> overlay;
        if (app.collisionDebug) {
            if (!debug) debug = std::make_unique<viewer::CollisionDebug>(options.shaders);
            overlay = collisionOverlay(app);
        }
        intervalPreparationSeconds += glfwGetTime() - preparationStart;
        if (frameProfile) frameProfile->beginGpu();
        gpuTimer.begin();
        const double renderStart = glfwGetTime();
        renderer.updateFloorplans(floorplans);
        if (sleepPlan.state().asleep(1)) renderer.renderBlack();
        else renderer.render(viewCamera, app.displayedAnimationSeconds, [&] {
            if (birdRenderer)
                birdRenderer->draw(viewCamera, float(renderWidth) / renderHeight,
                    app.displayedAnimationSeconds, birds->birds(), dayNightTint);
            if (!app.driver.occupied() && !app.weaponSwitch.pending() && !app.handling() && app.weapons.slot() == viewer::punchSlot)
                bodyRenderer.drawPose(presentedBody, viewCamera.projection(float(renderWidth) / renderHeight) *
                                       viewCamera.view(), renderWidth, renderHeight,
                                       (1.f - presentationAlpha) * viewer::PhysicsWorld::fixedStepSeconds);
        }, simulationClock.seconds());
        if (app.collisionDebug && !sleepPlan.state().asleep(1))
            debug->draw(viewCamera, overlay, renderWidth, renderHeight);
        const auto& heldWeapon = app.weapons.equipped();
        {
            viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::overlay);
            if (!sleepPlan.state().asleep(1) && !app.driver.occupied() && !app.weaponSwitch.pending() && heldWeapon.owned && app.weapons.slot() != viewer::handlingSlot &&
                app.rig.camera().viewMode() == viewer::Camera::ViewMode::original &&
                viewer::weaponViewVisible(heldWeapon.config, heldWeapon.loaded, simulationClock.seconds() < heldWeapon.readyAt))
                weaponOverlay.draw(app.weapons.slot(), renderWidth, renderHeight, flash,
                                   viewer::directionalMeleeSlot(app.weapons.slot()) && !app.temporaryHandling() ?
                                       viewer::swordMotion(app.weapons.directionalMelee().pose(simulationClock.seconds())) :
                                       app.weaponMotion.pose(app.weapons.slot(), simulationClock.seconds(), app.temporaryHandling()));
            const auto overlayViewport = renderer.beginOverlay(app.width, app.height);
            const float sigma = viewer::hipFireInaccuracy(heldWeapon.config,
                glm::length(glm::vec2(app.playerVelocity.x, app.playerVelocity.z)),
                app.rig.player().movementState().posture, app.rig.player().config(),
                viewCamera.viewMode() != viewer::Camera::ViewMode::original);
            const float projectionY = viewCamera.projection(float(overlayViewport.width) / overlayViewport.height)[1][1];
            if (!sleepPlan.state().asleep(1)) reticle.draw(app.rig.camera().viewMode(), overlayViewport.width, overlayViewport.height,
                viewer::inaccuracyCrosshairRadius(sigma, projectionY));
        }
        viewer::HudState hudState;
        hudState.seconds = simulationClock.seconds();
        hudState.gameSeconds = simulationClock.seconds();
        hudState.dayNight = options.dayNight;
        hudState.localPlayer = 1;
        hudState.sleepPlan = sleepPlan.state();
        hudState.health = app.vitals.health();
        hudState.stamina = app.rig.player().exertion().stamina;
        hudState.strain = app.rig.player().exertion().strain;
        hudState.weariness = app.vitals.weariness();
        if (app.hudScreen == viewer::HudScreen::Health) hudState.medical = app.vitals.healthState();
        hudState.lastDamageSeconds = app.lastDamageSeconds;
        hudState.screen = app.hudScreen;
        hudState.feet = app.driver.occupied() ? app.driver.eyePosition(cars) - glm::vec3(0, 1.65f, 0) : app.rig.walking() ? app.rig.player().feet() : app.rig.camera().position() - glm::vec3(0, 1.65f, 0);
        hudState.forward = app.rig.camera().direction();
        hudState.position = app.driver.occupied() ? app.driver.eyePosition(cars) : app.rig.camera().position();
        hudState.yawDegrees = viewCamera.yawDegrees();
        hudState.movement = app.driver.occupied() ? (app.driver.driving() ? "DRIVING" : "PASSENGER") :
            !app.rig.walking() ? "FREE FLY" : app.rig.player().stance() == viewer::PlayerStance::Prone ? "PRONE" :
            app.rig.player().stance() == viewer::PlayerStance::Crouched ? "CROUCH" : "STAND";
        hudState.mode = app.handling() ? "OBJECT HANDLING" : app.weapons.slot() >= viewer::punchSlot ? "SKILL" : "ITEM";
        const auto velocity = app.driver.car() ? cars.velocity(*app.driver.car()) : app.playerVelocity;
        hudState.speedMetersPerSecond = !app.driver.occupied() && !app.rig.walking() && dt > 0 ?
            glm::length(presentedCamera.position() - previousCameraPosition) / dt : glm::length(glm::vec2(velocity.x, velocity.z));
        hudState.equipment = heldWeapon.owned ? heldWeapon.config.name : "EMPTY HANDS";
        hudState.slot = heldWeapon.owned ? static_cast<int>(app.weapons.slot()) : -1;
        if (heldWeapon.owned && app.weapons.slot() < viewer::weaponIds.size()) {
            hudState.loaded = heldWeapon.loaded;
            hudState.reserve = heldWeapon.reserve;
        }
        hudState.mouseCaptured = app.capture;
        hudState.objectHandling = app.handling() && !app.driver.occupied();
        hudState.handInventoryActive = app.handInventory.active();
        hudState.handAccessRemaining = app.handInventory.remaining();
        hudState.handStoreRejections = app.handInventory.rejectedStores();
        hudState.selectionSlot = app.actionSelection.selected();
        hudState.weaponSwitchRemaining = app.weaponSwitch.remaining(simulationClock.seconds());
        if (const auto* timing = app.weaponSwitch.timing()) hudState.weaponSwitchDuration = timing->total();
        hudState.reloadRemaining = app.weaponReload.remaining(simulationClock.seconds());
        hudState.reloadDuration = app.weaponReload.duration();
        if (app.driver.occupied()) hudState.prompt = "Enter: exit vehicle";
        else if (app.interaction.charging()) hudState.prompt = "Release right mouse: throw / push";
        else if (app.interaction.grabbing()) hudState.prompt = "Release left: drop; hold right: throw";
        else if (app.handling()) hudState.prompt = "Hold left: drag; right click / hold: push";
        else if (birds && app.rig.walking() && app.vitals.health() > 0) {
            for (const auto& flock : birds->flocks()) {
                if (flock.mode == viewer::BirdFlockMode::gathering || flock.mode == viewer::BirdFlockMode::swooping) {
                    hudState.prompt = "Birds diving! Move aside, duck or reach cover";
                    break;
                }
            }
        }
        {
            viewer::frame_stage_profile::Timer timer(viewer::frame_stage_profile::Stage::hud);
            hud.draw(hudState, app.width, app.height);
        }
        if (const auto action = hud.takeMissionMenuAction()) {
            if (*action == viewer::MissionMenuAction::Abandon) glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
            else { app.hudScreen = viewer::HudScreen::Gameplay; captureMouse(window.get(), app.capture); }
        }
        if (const auto requested = hud.takeScreenRequest()) app.hudScreen = *requested;
        if (const auto action = hud.takeSleepAction()) sleepPlan.apply(1, *action, simulationClock.seconds());
        if (sleepPlan.state().ready() && !sleepPlan.state().sleeping && !app.rig.walking() && !app.driver.occupied())
            app.rig.enterWalking(app.rig.camera().position());
        if ((app.rig.walking() || app.driver.occupied()) && sleepPlan.begin(simulationClock.seconds(), options.dayNight)) {
            sleepDamageEvents = app.vitals.damageEvents(); app.keys.fill(false); app.proneToggleQueued = false;
            app.handInventory.release(app.interaction, groundItems, app.weapons);
            app.interaction.cancel(); app.weapons.cancelTrigger();
        }
        for (const auto& action : hud.takeInventoryActions()) {
            switch (action.kind) {
            case viewer::InventoryDragActionKind::ApplyBandage:
                if (!hud.inventoryDragging() && !app.handInventory.active() && !sleepPlan.state().inSleepSession(1))
                    viewer::applyBandage(app.vitals.healthState(), app.inventory, action.item, action.injury);
                break;
            case viewer::InventoryDragActionKind::HandPickUp:
                if (app.handling() && !app.weaponSwitch.pending() && !app.weaponReload.pending())
                    app.handInventory.begin(action.item);
                break;
            case viewer::InventoryDragActionKind::HandStore:
                app.handInventory.store(app.interaction, groundItems, app.weapons); break;
            case viewer::InventoryDragActionKind::HandRelease:
                app.handInventory.release(app.interaction, groundItems, app.weapons); break;
            default: break; // Full inventory page already applies its operations locally.
            }
        }
        intervalRenderSeconds += glfwGetTime() - renderStart;
        gpuTimer.end();
        if (frameProfile) frameProfile->endGpu();
        const double renderEnd=glfwGetTime();
        // Presentation can wait for VSync, so keep it outside both render timers.
        if (!options.hidden) {
            renderer.present(app.width, app.height);
            glfwSwapBuffers(window.get());
        } else glFlush();
        if (frameCount == 0) viewer::Renderer::checkErrors("first rendered frame");
#ifndef NDEBUG
        viewer::Renderer::checkErrors("render loop");
#endif
        ++frameCount;
        ++intervalFrames;
        const double completed = glfwGetTime();
        if (frameProfile) frameProfile->record({static_cast<std::uint64_t>(frameCount),
            (renderEnd-renderStart)*1000,(preparationStart-simulationStart)*1000,
            (renderStart-preparationStart)*1000,(simulationStart-now)*1000,(completed-renderEnd)*1000});
        const double intervalSeconds = completed - titleTime;
        if (intervalSeconds >= 1.0 && (!options.hidden || options.profile || frameProfile)) {
            const auto fps = static_cast<int>(static_cast<double>(intervalFrames) / intervalSeconds);
            const auto milliseconds = [](double value) {
                std::ostringstream text;
                text << std::fixed << std::setprecision(2) << value << " ms";
                return text.str();
            };
            const auto gpuMilliseconds = gpuTimer.takeAverageMilliseconds();
            const auto gpuTime = gpuMilliseconds ? milliseconds(*gpuMilliseconds)
                : gpuTimer.supported() ? std::string("pending") : std::string("unavailable");
            const double frameScale = 1000.0 / static_cast<double>(intervalFrames);
            const auto cpuTime = milliseconds(intervalRenderSeconds * frameScale);
            const auto simulationTime = milliseconds(intervalSimulationSeconds * frameScale);
            const auto status = app.rig.walking() ? (app.rig.player().grounded() ? " | grounded" : " | airborne") : "";
            const auto& treeStats = renderer.treeStats();
            const auto& sceneStats = renderer.sceneStats();
            const auto treeStatus = trees ? " | Trees " + std::to_string(treeStats.visible) + "/" + std::to_string(treeStats.total) : "";
            const auto townStatus = " | Town " + std::to_string(sceneStats.visible) + "/" + std::to_string(sceneStats.total)
                + " (" + std::to_string(sceneStats.drawCalls) + " draws)";
            const auto zombieStatus = zombies ? " | Zombies " + std::to_string(renderer.zombieVisibleCount()) + "/" +
                std::to_string(renderer.zombieCount()) +
                " (" + std::to_string(zombies->ragdollCount()) + " ragdolls)" : "";
            const auto dogStatus = dogs.instances.empty() ? "" : " | Dogs " +
                std::to_string(renderer.dogVisibleCount()) + "/" + std::to_string(dogs.instances.size());
            std::ostringstream clockStatus;
            if (!options.night) {
                const int minutes = static_cast<int>(displayedHour * 60.0);
                clockStatus << " | " << std::setfill('0') << std::setw(2) << minutes / 60
                            << ':' << std::setw(2) << minutes % 60;
            }
            std::ostringstream speedStatus;
            const float speed = app.driver.car() ? glm::length(cars.velocity(*app.driver.car())) : app.rig.speed(
                app.keys[GLFW_KEY_LEFT_SHIFT] || app.keys[GLFW_KEY_RIGHT_SHIFT]);
            speedStatus << std::fixed << std::setprecision(2) << speed;
            const auto entry = !app.driver.occupied() && app.rig.walking() ?
                viewer::vehicleEntry(cars, app.rig.camera().position(), collision) : std::optional<viewer::VehicleSeatRef>{};
            const auto caption = title + " | " + (app.driver.occupied() ? (app.driver.driving() ? "Driving" : "Passenger | Mouse look, Enter exit") : app.rig.modeName()) + " | " + app.weapons.equipped().config.name
                + " | " + app.weapons.status()
                + (app.interaction.grabbing() ? " (holding)" : "") + status + " | " + std::to_string(fps)
                + " FPS | GPU " + gpuTime + " | CPU render " + cpuTime + " | CPU sim " + simulationTime + townStatus + treeStatus + zombieStatus + dogStatus + clockStatus.str() + " | " + speedStatus.str() + " m/s"
                + (entry ? " | " + viewer::vehicleEntryLabel(cars, *entry) : "");
            glfwSetWindowTitle(window.get(), caption.c_str());
            if (frameProfile && !options.profile) std::cout << caption << '\n';
            if (options.profile) {
                std::cout << caption << '\n'
                          << "CPU stages: behavior " << milliseconds(intervalBehaviorSeconds * frameScale)
                          << "; preparation " << milliseconds(intervalPreparationSeconds * frameScale);
                if (zombies) {
                    const auto& stats = zombies->behaviorStats();
                    std::cout << "; behavior pool entries " << zombies->behaviors.entries().size()
                              << "; last step " << stats.perceptionChecks << " perception checks, "
                              << stats.sightRays << " sight rays";
                    const auto& crowdStats = zombies->crowdStats();
                    std::cout << "; crowd " << crowdStats.neighborChecks << " neighbor checks, "
                              << crowdStats.steeringChecks << " steering checks, "
                              << crowdStats.budgetStops << " budget stops";
                    const auto& updates = renderer.zombieUpdateStats();
                    std::cout << "; render preparation " << updates.instances << " instances, " << updates.bounds
                              << " bounds" << (updates.full ? " (full refresh)" : " (incremental)");
                }
                std::cout << '\n';
                if (!dogs.instances.empty()) {
                    const auto hunting = std::count_if(dogs.behaviors.begin(), dogs.behaviors.end(),
                        [](const auto& behavior) { return behavior.state == viewer::DogState::hunting; });
                    std::cout << "Dogs: " << renderer.dogVisibleCount() << '/' << dogs.instances.size()
                              << " visible; " << hunting << " hunting; " << dogs.scent().activeCells()
                              << " scent cells; player bleeding " << app.vitals.bleeding() << '\n';
                }
                if (zombies) {
                    const auto& visibility = renderer.zombieVisibilityStats();
                    std::cout << "Zombie visibility: " << visibility.blocks << " blocks; visited "
                              << visibility.visitedGroups << '/' << visibility.groups << " groups; refitted "
                              << visibility.refittedGroups << " groups, " << visibility.refittedMembers
                              << " members, " << visibility.refittedBlocks << " blocks; camera/sun/flashlight group tests "
                              << visibility.cameraGroupTests << '/' << visibility.shadowGroupTests << '/'
                              << visibility.flashlightGroupTests << '\n';
                }
                frameStages.print(std::cout, intervalFrames);
                frameStages = {};
                zombieProfile.print(std::cout);
                zombieProfile = {};
            }
            titleTime = completed;
            intervalFrames = 0;
            intervalRenderSeconds = 0.0;
            intervalSimulationSeconds = intervalBehaviorSeconds = intervalPreparationSeconds = 0.0;
        }
        if (options.frames > 0 && frameCount >= options.frames) break;
    }
    if (frameProfile) {
        glFlush();
        const double deadline=glfwGetTime()+5;
        while (frameProfile->pending() && glfwGetTime()<deadline) {
            frameProfile->collect();
            if (frameProfile->pending()) glfwWaitEventsTimeout(.0005);
        }
    }
    if (options.profile) {
        frameStages.print(std::cout, intervalFrames);
        zombieProfile.print(std::cout);
    }
    app.handInventory.release(app.interaction, groundItems, app.weapons);
    if (doodads) {
        doodads->captureChanges();
        if (savedDoodadRevision != doodadState->revision()) doodadState->save(doodadSave);
        std::cout << "Last doodad frame: " << renderer.doodadVisibleCount() << '/'
                  << doodads->instances().size() << " visible; " << doodads->cellCount()
                  << " cached cells, " << doodads->activeBodyCount() << " physical props; "
                  << doodadState->changes().size() << " saved poses\n";
    }
    if (!options.screenshot.empty()) renderer.writeScreenshot(options.screenshot);
    if (trees) {
        const auto& stats = renderer.treeStats();
        std::cout << "Last tree frame: " << stats.visible << '/' << stats.total << " visible, "
                  << stats.drawCalls << " instanced draw calls\n";
    }
    if (forests) {
        const auto& stats = renderer.forestStats();
        std::cout << "Last forest frame: " << stats.volumes << " visible volumes, "
                  << stats.drawCalls << " material batches, "
                  << stats.detailedPatches << '/' << stats.patches << " patches using individual trees\n";
    }
    const auto& sceneStats = renderer.sceneStats();
    std::cout << "Last town frame: " << sceneStats.visible << '/' << sceneStats.total << " visible, "
              << sceneStats.drawCalls << " draw calls\n";
    if (!dogs.instances.empty()) std::cout << "Last dog frame: " << renderer.dogVisibleCount() << '/'
                                          << dogs.instances.size() << " visible instances\n";
    if (zombies) std::cout << "Last zombie frame: " << renderer.zombieVisibleCount() << '/' << renderer.zombieCount()
                          << " visible instances, "
                          << zombies->ragdollCount() << " ragdolls, " << renderer.zombieDrawCalls()
                          << " instanced draw calls\n";
    if (zombies) {
        const auto& stats = renderer.zombieRenderStats();
        std::cout << "Zombie detail: camera LOD0/1/2/3 " << stats.cameraLevels[0] << '/' << stats.cameraLevels[1]
                  << '/' << stats.cameraLevels[2] << '/' << stats.cameraLevels[3]
                  << "; " << stats.cameraTriangles << " camera triangles, " << stats.shadowTriangles
                  << " sun shadow triangles, " << stats.flashlightTriangles << " flashlight triangles; " << stats.distanceCulled
                  << " distance culled after frustum; " << renderer.zombieVisibilityStats().cameraRegionRejectedGroups
                  << " groups rejected by camera distance\n";
    }
    viewer::Renderer::checkErrors("shutdown");
    std::cout << "Rendered " << frameCount << " frames successfully\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    try {
        const auto settingsFile = viewer::settingsPath();
        auto options = parseOptions(argc, argv, viewer::readSettings(settingsFile));
        options.settingsFile = settingsFile;
        if (options.help) { printHelp(); return 0; }
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << "godollo_viewer: " << error.what() << '\n';
        return 1;
    }
}
